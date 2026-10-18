#include "Creatures/SwcCreature.h"

#include "Character/SwcSmooth.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Creatures/SwcCreatureAnimInstance.h"
#include "Creatures/SwcCreatureData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "Net/SwcSpace.h"

namespace
{
/** The imported meshes face engine +Y, Unreal -Y in their asset (Net/SwcSpace.h): the yaw of the player mesh (stage 1.6). */
constexpr double MeshYaw = 90.0;
/** The motion parameter smoothing of the creature slices (MotionParameterSmoothingComponent: TravelSpeedConvergeTime 0.1,
 *  TravelAngleConvergeTime 0.1, TurnSpeedConvergeTime 0.75, MovementSpeedEpsilon 0.02; research feel-values.md R3.3.1). */
constexpr float SpeedConvergeTime = 0.1f;
constexpr float AngleConvergeTime = 0.1f;
constexpr float TurnConvergeTime = 0.75f;
constexpr float MovementSpeedEpsilon = 0.02f;
/** A step after a pause: the backend sends a movement state for each step of 200 ms of a way (protocol notes 4.17); a creature
 *  that stood sends none. The step after a longer gap starts 200 ms before its state, not at the old state. */
constexpr int64 StepUs = 200000;
constexpr int64 PauseUs = 400000;
/** The one-shot rule of the client (0x2A2BC20, the test at 0x2A2C28D, the constant -0.5 at 0x7F4016C): the same record starts
 *  again only when its time is more than half the fragment length away from the time of the client. */
constexpr float RestartShare = 0.5f;
/** The placeholder of a slice that is not converted: an engine cylinder of 0.6 m x 1.8 m. */
const TCHAR* PlaceholderMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
} // namespace

ASwcCreature::ASwcCreature()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Leader = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Leader"));
	Leader->SetupAttachment(Root);
	Leader->SetRelativeRotation(FRotator(0.0, MeshYaw, 0.0));
	Leader->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The leader computes its pose each frame also when it is hidden (the follower and the weapons use it).
	Leader->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Leader);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetVisibility(false);
	Placeholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Placeholder"));
	Placeholder->SetupAttachment(Root);
	Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Placeholder->SetVisibility(false);
}

void ASwcCreature::Setup(FSwcCreatureLibrary& Library, const swc::creatures::Entity& Entity)
{
	Serial = Entity.serial;
	const FString Key = UTF8_TO_TCHAR(Entity.slice_key().c_str());
	Slice = Library.FindSlice(Key);
	SlicePath = Slice != nullptr ? Slice->Path : Key;
	USkeletalMesh* SkeletonMesh = Slice != nullptr && !Slice->SkeletonMeshPath.IsEmpty() ? LoadObject<USkeletalMesh>(nullptr, *Slice->SkeletonMeshPath) : nullptr;
	USkeletalMesh* Mesh = Slice != nullptr && !Slice->MeshPath.IsEmpty() ? LoadObject<USkeletalMesh>(nullptr, *Slice->MeshPath) : nullptr;
	if (SkeletonMesh != nullptr && Mesh != nullptr)
	{
		Leader->SetSkeletalMeshAsset(SkeletonMesh);
		Animations = Library.Animations(Slice->AnimationKey);
		if (Animations.IsValid())
		{
			Leader->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Leader->SetAnimInstanceClass(USwcCreatureAnimInstance::StaticClass());
			if (USwcCreatureAnimInstance* Animation = Cast<USwcCreatureAnimInstance>(Leader->GetAnimInstance()))
			{
				Animation->SetAnimations(Animations);
			}
		}
		ShowBody(Mesh);
		// The default weapons on their sockets: the bone of the CA_BONE attachment of the body and its relative transform
		// (engine space: the map of Net/SwcSpace.h for a position and a rotation).
		for (const FSwcCreatureWeapon& Weapon : Slice->Weapons)
		{
			UStaticMesh* Model = Weapon.MeshPath.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *Weapon.MeshPath);
			if (Model == nullptr || Leader->GetBoneIndex(Weapon.Bone) == INDEX_NONE)
			{
				continue;
			}
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
			Component->SetStaticMesh(Model);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetupAttachment(Leader, Weapon.Bone);
			const swc::net::WorldPosition Offset{static_cast<float>(Weapon.EnginePosition.X), static_cast<float>(Weapon.EnginePosition.Y),
				static_cast<float>(Weapon.EnginePosition.Z)};
			const swc::net::Quaternion Turn{static_cast<float>(Weapon.EngineRotation.X), static_cast<float>(Weapon.EngineRotation.Y),
				static_cast<float>(Weapon.EngineRotation.Z), static_cast<float>(Weapon.EngineRotation.W)};
			Component->SetRelativeTransform(FTransform(SwcEngineToUnreal(Turn), SwcEngineToUnreal(Offset)));
			Component->RegisterComponent();
			Weapons.Add(Component);
		}
		bHasModel = true;
	}
	else if (UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, PlaceholderMesh))
	{
		// The engine cylinder is 1 m high and 1 m wide, with its origin at its center.
		Placeholder->SetStaticMesh(Cylinder);
		Placeholder->SetRelativeScale3D(FVector(0.6, 0.6, 1.8));
		Placeholder->SetRelativeLocation(FVector(0.0, 0.0, 90.0));
		Placeholder->SetVisibility(true);
	}
}

void ASwcCreature::ShowBody(USkeletalMesh* Mesh)
{
	if (Mesh == Leader->GetSkeletalMeshAsset())
	{
		Leader->SetVisibility(true);
		Body->SetVisibility(false);
	}
	else
	{
		Body->SetSkeletalMeshAsset(Mesh);
		Body->SetLeaderPoseComponent(Leader);
		Body->SetVisibility(true);
		Leader->SetVisibility(false);
	}
	BodyTop = static_cast<float>(Mesh->GetImportedBounds().GetBox().Max.Z);
}

FVector ASwcCreature::GetPlatePoint() const
{
	const double Offset = Slice != nullptr ? Slice->VerticalOffset * 100.0 : 0.0;
	return GetActorLocation() + FVector(0.0, 0.0, (BodyTop + 30.0) * GetActorScale3D().Z + Offset);
}

double ASwcCreature::GetDeadSeconds() const
{
	return bDead ? FPlatformTime::Seconds() - DeadAt : -1.0;
}

void ASwcCreature::SampleMotion(const swc::creatures::Entity& Entity, uint64 RenderTimeUs, FVector& OutPosition, FQuat& OutRotation) const
{
	const auto& Samples = Entity.samples;
	const auto Rotation = [](const swc::creatures::MovementSample& Sample) { return SwcEngineToUnreal(Sample.rotation); };
	if (Samples.size() == 1 || static_cast<int64>(RenderTimeUs) <= static_cast<int64>(Samples.front().timeUs))
	{
		OutPosition = SwcEngineToUnreal(Samples.front().position);
		OutRotation = Rotation(Samples.front());
		if (Samples.size() == 1)
		{
			return;
		}
	}
	if (RenderTimeUs >= Samples.back().timeUs)
	{
		OutPosition = SwcEngineToUnreal(Samples.back().position);
		OutRotation = Rotation(Samples.back());
		return;
	}
	for (std::size_t Index = 1; Index < Samples.size(); ++Index)
	{
		const swc::creatures::MovementSample& A = Samples[Index - 1];
		const swc::creatures::MovementSample& B = Samples[Index];
		if (RenderTimeUs > B.timeUs)
		{
			continue;
		}
		// After a pause the step starts one step before its state.
		const int64 Start = static_cast<int64>(B.timeUs) - static_cast<int64>(A.timeUs) > PauseUs ? static_cast<int64>(B.timeUs) - StepUs
			: static_cast<int64>(A.timeUs);
		const double Alpha = FMath::Clamp(static_cast<double>(static_cast<int64>(RenderTimeUs) - Start) / static_cast<double>(static_cast<int64>(B.timeUs) - Start),
			0.0, 1.0);
		OutPosition = FMath::Lerp(SwcEngineToUnreal(A.position), SwcEngineToUnreal(B.position), Alpha);
		OutRotation = FQuat::Slerp(Rotation(A), Rotation(B), Alpha);
		return;
	}
}

void ASwcCreature::ApplyScope(const swc::creatures::Entity& Entity)
{
	// One new state of the scope fields: the record of scope 0 (or none) with its start on the server clock. The time of the
	// record is the time of the action at the time anchor of its state (protocol notes 4.14).
	FPending Next;
	const swc::creatures::ScopeRecord* Record = Entity.scope(0);
	Next.bRecord = Record != nullptr && Record->fragment != swc::creatures::EntityTable::kNone;
	if (Next.bRecord)
	{
		Next.Record = *Record;
		Next.StartUs = static_cast<int64>(Entity.scopeTimeUs) - static_cast<int64>(static_cast<double>(Record->time) * 1.0e6);
	}
	else
	{
		Next.StartUs = static_cast<int64>(Entity.scopeTimeUs);
	}
	// The same record again (a timed action in each state, user test 2 UT2-3): it starts again only when its time is far from
	// the time of the action that plays.
	const FPending* Last = Pending.IsEmpty() ? nullptr : &Pending.Last();
	const bool bSameAsLast = Last != nullptr ? Last->bRecord == Next.bRecord && (!Next.bRecord || Last->Record.startCount == Next.Record.startCount)
		: bPlaying == Next.bRecord && (!Next.bRecord || Playing.startCount == Next.Record.startCount);
	if (bSameAsLast)
	{
		if (!Next.bRecord || Last != nullptr)
		{
			return;
		}
		const FSwcCreatureFragment* Fragment = Animations.IsValid() ? Animations->Fragment(static_cast<int32>(Next.Record.fragment)) : nullptr;
		const float Length = Fragment != nullptr ? Fragment->Length : -1.0f;
		if (Length <= 0.0f || FMath::Abs(static_cast<double>(Next.StartUs - PlayingStartUs)) <= RestartShare * Length * 1.0e6)
		{
			return;
		}
	}
	Pending.Add(Next);
}

FString ASwcCreature::StartPending(uint64 RenderTimeUs, bool bIsDead)
{
	FString Text;
	USwcCreatureAnimInstance* Animation = Cast<USwcCreatureAnimInstance>(Leader->GetAnimInstance());
	while (!Pending.IsEmpty() && Pending[0].StartUs <= static_cast<int64>(RenderTimeUs))
	{
		const FPending Next = Pending[0];
		Pending.RemoveAt(0);
		// A later record that is already due replaces this one.
		if (!Pending.IsEmpty() && Pending[0].StartUs <= static_cast<int64>(RenderTimeUs))
		{
			continue;
		}
		const float FragmentTime = static_cast<float>(static_cast<double>(static_cast<int64>(RenderTimeUs) - Next.StartUs) / 1.0e6);
		bPlaying = Next.bRecord;
		Playing = Next.Record;
		PlayingStartUs = Next.StartUs;
		int32 Fragment = INDEX_NONE;
		if (Next.bRecord)
		{
			Fragment = static_cast<int32>(Next.Record.fragment);
		}
		else if (!bIsDead && Animations.IsValid())
		{
			// The fields with no record: the client plays its own idle (protocol notes 4.14). A dead creature keeps its pose.
			Fragment = Animations->IdleFragment;
		}
		if (Fragment == INDEX_NONE || Animation == nullptr)
		{
			continue;
		}
		const bool bStarted = Animation->PlayFragment(Fragment, FragmentTime, Next.bRecord ? Next.Record.speed : 1.0f);
		PlayingFragment = Fragment;
		Text = FString::Printf(TEXT("%s %s (fragment %d%s), start count %u, at %.2f s of the action"), Next.bRecord ? TEXT("action") : TEXT("idle of the client"),
			Next.bRecord ? *FString::Printf(TEXT("%u %s"), Next.Record.action,
				Slice != nullptr && Slice->Actions.IsValidIndex(static_cast<int32>(Next.Record.action)) ? *Slice->Actions[static_cast<int32>(Next.Record.action)] : TEXT(""))
				: TEXT("-"), Fragment,
			Animations.IsValid() ? *(TEXT(" ") + Animations->FragmentName(Fragment)) : TEXT(""), Next.Record.startCount, FragmentTime);
		if (!bStarted)
		{
			Text += TEXT(": no variant of the fragment for the tags of the slice");
		}
	}
	return Text;
}

FString ASwcCreature::UpdateFromEntity(FSwcCreatureLibrary& Library, const swc::creatures::Entity& Entity, uint64 RenderTimeUs, float DeltaSeconds)
{
	if (!bLooksApplied && Entity.hasVitals)
	{
		// The look of the kind: the name of the category row, the creature type, the scale of the base vitals row, and the
		// material variant of the category row (research creature-visuals.md section 3).
		bLooksApplied = true;
		AppliedCategory = Entity.categoryId;
		DisplayName = Library.Name(Entity.categoryId, Entity.vitalsId);
		bHidesLevel = Library.HidesLevel(Entity.categoryId, Entity.vitalsId);
		SetActorScale3D(FVector(Library.Scale(Entity.vitalsId)));
		if (const FString* Variant = Slice != nullptr && bHasModel ? Slice->VariantMeshes.Find(Entity.categoryId) : nullptr)
		{
			if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, **Variant))
			{
				ShowBody(Mesh);
			}
		}
	}
	FVector Position = GetActorLocation();
	FQuat Rotation = GetActorQuat();
	if (Entity.hasPosition())
	{
		SampleMotion(Entity, RenderTimeUs, Position, Rotation);
	}
	// The facing: the rotation turns north (engine +Y; Unreal -Y) about the vertical axis; the actor faces its forward (+X).
	const FVector North = Rotation.RotateVector(FVector(0.0, -1.0, 0.0));
	const float Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(North.Y, North.X)));
	// The motion parameters of the blend spaces from the shown motion (the server sends none; research R3.3.1, open point O5).
	if (bHasLast && DeltaSeconds > 0.0f)
	{
		const FVector Velocity = (Position - LastPosition) / DeltaSeconds;
		const float Speed = static_cast<float>(Velocity.Size2D() / 100.0);
		SwcSmoothCD(MoveSpeed, MoveSpeedRate, Speed < MovementSpeedEpsilon ? 0.0f : Speed, SpeedConvergeTime, DeltaSeconds);
		if (Speed >= MovementSpeedEpsilon)
		{
			const FVector Forward = FRotator(0.0, Yaw, 0.0).Vector();
			const FVector Left = -FRotationMatrix(FRotator(0.0, Yaw, 0.0)).GetScaledAxis(EAxis::Y);
			const float Angle = static_cast<float>(FMath::Atan2(FVector::DotProduct(Velocity, Left), FVector::DotProduct(Velocity, Forward)));
			const float Target = TravelAngle + FMath::FindDeltaAngleRadians(TravelAngle, Angle);
			SwcSmoothCD(TravelAngle, TravelAngleRate, Target, AngleConvergeTime, DeltaSeconds);
			TravelAngle = FMath::UnwindRadians(TravelAngle);
		}
		// The Unreal yaw goes to the right (Y is south): a turn to the left is a smaller yaw.
		const float Turn = -FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(LastYaw, Yaw)) / DeltaSeconds;
		SwcSmoothCD(TurnSpeed, TurnSpeedRate, Turn, TurnConvergeTime, DeltaSeconds);
	}
	bHasLast = true;
	LastPosition = Position;
	LastYaw = Yaw;
	SetActorLocationAndRotation(Position, FRotator(0.0, Yaw, 0.0));
	if (Entity.dead && !bDead)
	{
		bDead = true;
		DeadAt = FPlatformTime::Seconds();
	}
	if (Entity.scopeRevision != ScopeRevision)
	{
		ScopeRevision = Entity.scopeRevision;
		ApplyScope(Entity);
	}
	FString Text = StartPending(RenderTimeUs, Entity.dead);
	if (USwcCreatureAnimInstance* Animation = Cast<USwcCreatureAnimInstance>(Leader->GetAnimInstance()))
	{
		Animation->SetMotion(MoveSpeed, TurnSpeed, TravelAngle);
		// The first idle: a creature with no action of the server (its first state has no record of scope 0).
		if (Animation->GetFragment() == INDEX_NONE && Pending.IsEmpty() && !bPlaying && Animations.IsValid() && !Entity.dead)
		{
			Animation->PlayFragment(Animations->IdleFragment, 0.0f, 1.0f);
		}
	}
	return Text;
}
