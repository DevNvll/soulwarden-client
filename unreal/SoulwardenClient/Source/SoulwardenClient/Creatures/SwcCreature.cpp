#include "Creatures/SwcCreature.h"

#include "Character/SwcSmooth.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Creatures/SwcCreatureAnimInstance.h"
#include "Creatures/SwcCreatureData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
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
	BodyBlock = CreateDefaultSubobject<UCapsuleComponent>(TEXT("BodyBlock"));
	BodyBlock->SetupAttachment(Root);
	BodyBlock->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyBlock->SetCanEverAffectNavigation(false);
	BodyBlock->SetHiddenInGame(true);
}

void ASwcCreature::SetupBodyBlock()
{
	// The body capsule of the slice blocks the movement of the player: in the retail game the creatures block the players (an
	// inference of the orchestrator, docs/CREATURES.md). The rule of the backend for its place (world/projectiles.cpp,
	// capsule_of): the segment of the length `height` on the axis in the frame of the creature (x right, y front, z up), the
	// radius around it, the offset in the same frame, and the lowest point at the feet. No scale (the backend uses none).
	if (Slice == nullptr || !Slice->bHasCapsule || Slice->CapsuleRadius <= 0.0f)
	{
		return;
	}
	// The frame of the creature in the frame of the actor (X front, Y right, Z up).
	const auto ToActor = [](const FVector& Engine) { return FVector(Engine.Y, Engine.X, Engine.Z); };
	const FVector Axis = ToActor(Slice->CapsuleAxis).GetSafeNormal();
	const double Half = Slice->CapsuleHeight * 0.5;
	const double Lift = FMath::Abs(Axis.Z) * Half + Slice->CapsuleRadius;
	BodyBlock->SetAbsolute(false, false, true);
	BodyBlock->SetWorldScale3D(FVector::OneVector);
	BodyBlock->SetCapsuleSize(Slice->CapsuleRadius * 100.0f, static_cast<float>((Axis.IsZero() ? 0.0 : Half) + Slice->CapsuleRadius) * 100.0f);
	BodyBlock->SetRelativeLocation((ToActor(Slice->CapsuleOffset) + FVector(0.0, 0.0, Lift)) * 100.0);
	BodyBlock->SetRelativeRotation(Axis.IsZero() ? FQuat::Identity : FRotationMatrix::MakeFromZ(Axis).ToQuat());
	// Only the movement of the player: the object type Pawn, a block for the channel Pawn, and no other channel (the camera, the
	// ground traces, and the wall line of the melee contacts do not see it). The player cannot stand on it.
	BodyBlock->SetCollisionObjectType(ECC_Pawn);
	BodyBlock->SetCollisionResponseToAllChannels(ECR_Ignore);
	BodyBlock->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BodyBlock->CanCharacterStepUpOn = ECB_No;
	BodyBlock->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.0f));
	BodyBlock->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void ASwcCreature::Setup(FSwcCreatureLibrary& Library, const swc::creatures::Entity& Entity, FStreamableManager& InStreamer)
{
	Serial = Entity.serial;
	const FString Key = UTF8_TO_TCHAR(Entity.slice_key().c_str());
	Slice = Library.FindSlice(Key);
	SlicePath = Slice != nullptr ? Slice->Path : Key;
	PendingLibrary = &Library;
	Streamer = &InStreamer;
	TArray<FSoftObjectPath> Paths;
	CollectAssets(Paths, true);
	Paths.RemoveAll([](const FSoftObjectPath& Path) { return Path.ResolveObject() != nullptr; });
	if (Paths.IsEmpty())
	{
		bLoading = false;
		LoadSeconds = LoadStart > 0.0 ? FPlatformTime::Seconds() - LoadStart : 0.0;
		FinishSetup();
		bReady = true;
		LoadRest();
		return;
	}
	if (!bLoading)
	{
		bLoading = true;
		LoadStart = FPlatformTime::Seconds();
	}
	SetActorHiddenInGame(true);
	LoadHandle = Streamer->RequestAsyncLoad(Paths, FStreamableDelegate::CreateUObject(this, &ASwcCreature::OnAssetsLoaded),
		FStreamableManager::AsyncLoadHighPriority);
}

void ASwcCreature::CollectAssets(TArray<FSoftObjectPath>& Out, bool bFirst) const
{
	const auto Add = [&Out](const FString& Path)
	{
		if (!Path.IsEmpty())
		{
			Out.AddUnique(FSoftObjectPath(Path));
		}
	};
	const auto AddClip = [&Add](const FSwcCreatureAnimations& Set, int32 Clip)
	{
		if (Set.Clips.IsValidIndex(Clip))
		{
			Add(Set.Clips[Clip].AssetPath);
		}
	};
	const auto AddSpace = [&AddClip](const FSwcCreatureAnimations& Set, int32 Space)
	{
		for (const int32 Clip : Set.BlendSpaces.IsValidIndex(Space) ? Set.BlendSpaces[Space].ExampleClips : TArray<int32>())
		{
			AddClip(Set, Clip);
		}
	};
	const auto AddItems = [&AddClip, &AddSpace](const FSwcCreatureAnimations& Set, const TArray<FSwcFragmentItem>& Items)
	{
		for (const FSwcFragmentItem& Item : Items)
		{
			if (Item.Kind == FSwcFragmentItem::EKind::Clip)
			{
				AddClip(Set, Item.Index);
			}
			else if (Item.Kind == FSwcFragmentItem::EKind::BlendSpace)
			{
				AddSpace(Set, Item.Index);
			}
			else if (Item.Kind == FSwcFragmentItem::EKind::Comb && Set.Combs.IsValidIndex(Item.Index))
			{
				for (const int32 Space : Set.Combs[Item.Index].Spaces)
				{
					AddSpace(Set, Space);
				}
			}
		}
	};
	// The first clips: the fragments Idle and Move (the body shows with them); else each clip of the set.
	const auto AddClips = [&](const TSharedPtr<FSwcCreatureAnimations>& Set)
	{
		if (!Set.IsValid())
		{
			return;
		}
		if (!bFirst)
		{
			for (const FSwcClip& Clip : Set->Clips)
			{
				Add(Clip.AssetPath);
			}
			return;
		}
		for (const int32 Fragment : {Set->IdleFragment, Set->MoveFragment})
		{
			if (const FSwcCreatureFragment* Found = Set->Fragments.Find(Fragment))
			{
				AddItems(*Set, Found->Items);
				for (const TArray<FSwcFragmentItem>& Layer : Found->ExtraLayers)
				{
					AddItems(*Set, Layer);
				}
			}
		}
	};
	if (bFirst)
	{
		Add(PlaceholderMesh);
	}
	if (Slice == nullptr || PendingLibrary == nullptr)
	{
		return;
	}
	if (bFirst)
	{
		Add(Slice->SkeletonMeshPath);
		Add(Slice->MeshPath);
		for (const TPair<uint32, FString>& Variant : Slice->VariantMeshes)
		{
			Add(Variant.Value);
		}
	}
	AddClips(PendingLibrary->Animations(Slice->AnimationKey));
	for (const FSwcCreatureWeapon& Weapon : Slice->Weapons)
	{
		if (bFirst)
		{
			Add(Weapon.SkeletonMeshPath);
			Add(Weapon.BodyMeshPath);
			Add(Weapon.MeshPath);
		}
		if (!Weapon.SkeletonMeshPath.IsEmpty())
		{
			AddClips(PendingLibrary->Animations(Weapon.AnimationKey));
		}
	}
}

void ASwcCreature::CollectAlternates(const TArray<FSoftObjectPath>& Paths, TArray<FSoftObjectPath>& Out)
{
	for (const FSoftObjectPath& Path : Paths)
	{
		if (Path.ResolveObject() != nullptr)
		{
			continue;
		}
		const FString Other = USwcCreatureAnimInstance::AlternateClipPath(Path.ToString());
		if (!Other.IsEmpty() && FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Other)))
		{
			Out.AddUnique(FSoftObjectPath(Other));
		}
	}
}

void ASwcCreature::OnAssetsLoaded()
{
	// The second form of a clip that Interchange named STEM + "Null" + NAME: one more async request.
	if (!AlternateHandle.IsValid() && Streamer != nullptr)
	{
		TArray<FSoftObjectPath> Paths;
		CollectAssets(Paths, true);
		TArray<FSoftObjectPath> Alternates;
		CollectAlternates(Paths, Alternates);
		if (!Alternates.IsEmpty())
		{
			AlternateHandle = Streamer->RequestAsyncLoad(Alternates, FStreamableDelegate::CreateUObject(this, &ASwcCreature::OnAssetsLoaded),
				FStreamableManager::AsyncLoadHighPriority);
			return;
		}
	}
	bLoading = false;
	LoadSeconds = FPlatformTime::Seconds() - LoadStart;
	FinishSetup();
	bReady = true;
	LoadRest();
}

void ASwcCreature::LoadRest()
{
	if (Streamer == nullptr)
	{
		return;
	}
	TArray<FSoftObjectPath> Paths;
	CollectAssets(Paths, false);
	Paths.RemoveAll([](const FSoftObjectPath& Path) { return Path.ResolveObject() != nullptr; });
	if (!Paths.IsEmpty())
	{
		RestHandle = Streamer->RequestAsyncLoad(Paths, FStreamableDelegate::CreateUObject(this, &ASwcCreature::OnRestLoaded));
	}
	else
	{
		OnRestLoaded();
	}
}

void ASwcCreature::OnRestLoaded()
{
	if (!RestAlternateHandle.IsValid() && Streamer != nullptr)
	{
		TArray<FSoftObjectPath> Paths;
		CollectAssets(Paths, false);
		TArray<FSoftObjectPath> Alternates;
		CollectAlternates(Paths, Alternates);
		if (!Alternates.IsEmpty())
		{
			RestAlternateHandle = Streamer->RequestAsyncLoad(Alternates, FStreamableDelegate::CreateUObject(this, &ASwcCreature::OnRestLoaded));
			return;
		}
	}
	// Each animation instance finds the clips that came now.
	TArray<USkeletalMeshComponent*> Meshes;
	GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (USwcCreatureAnimInstance* Animation = Cast<USwcCreatureAnimInstance>(Mesh->GetAnimInstance()))
		{
			Animation->RefreshClips();
		}
	}
}

void ASwcCreature::BeginLoading(uint64 InSerial)
{
	Serial = InSerial;
	bLoading = true;
	LoadStart = FPlatformTime::Seconds();
	SetActorHiddenInGame(true);
}

bool ASwcCreature::TakeReady(double& Seconds)
{
	Seconds = LoadSeconds;
	const bool bWas = bReady;
	bReady = false;
	return bWas;
}

namespace
{
/** @return An asset of a path that is in memory (the async load of ASwcCreature::Setup made it), else a load. */
template <typename T>
T* InMemory(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return nullptr;
	}
	if (T* Found = FindObject<T>(nullptr, *Path))
	{
		return Found;
	}
	return LoadObject<T>(nullptr, *Path);
}
} // namespace

void ASwcCreature::FinishSetup()
{
	FSwcCreatureLibrary& Library = *PendingLibrary;
	USkeletalMesh* SkeletonMesh = Slice != nullptr ? InMemory<USkeletalMesh>(Slice->SkeletonMeshPath) : nullptr;
	USkeletalMesh* Mesh = Slice != nullptr ? InMemory<USkeletalMesh>(Slice->MeshPath) : nullptr;
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
			if (Leader->GetBoneIndex(Weapon.Bone) == INDEX_NONE)
			{
				continue;
			}
			const swc::net::WorldPosition Offset{static_cast<float>(Weapon.EnginePosition.X), static_cast<float>(Weapon.EnginePosition.Y),
				static_cast<float>(Weapon.EnginePosition.Z)};
			const swc::net::Quaternion Turn{static_cast<float>(Weapon.EngineRotation.X), static_cast<float>(Weapon.EngineRotation.Y),
				static_cast<float>(Weapon.EngineRotation.Z), static_cast<float>(Weapon.EngineRotation.W)};
			const FTransform Socket(SwcEngineToUnreal(Turn), SwcEngineToUnreal(Offset));
			// An animated weapon (the weapon scope): the skeletal mesh of its skeleton file with its animation set, and its body.
			USkeletalMesh* WeaponSkeleton = InMemory<USkeletalMesh>(Weapon.SkeletonMeshPath);
			TSharedPtr<FSwcCreatureAnimations> WeaponAnimations = WeaponSkeleton != nullptr ? Library.Animations(Weapon.AnimationKey) : nullptr;
			if (WeaponSkeleton != nullptr && WeaponAnimations.IsValid())
			{
				USkeletalMeshComponent* WeaponLeader = NewObject<USkeletalMeshComponent>(this);
				WeaponLeader->SetSkeletalMeshAsset(WeaponSkeleton);
				WeaponLeader->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				WeaponLeader->SetupAttachment(Leader, Weapon.Bone);
				WeaponLeader->SetRelativeTransform(Socket);
				WeaponLeader->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				WeaponLeader->SetAnimationMode(EAnimationMode::AnimationBlueprint);
				WeaponLeader->SetAnimInstanceClass(USwcCreatureAnimInstance::StaticClass());
				WeaponLeader->RegisterComponent();
				if (USwcCreatureAnimInstance* WeaponAnimation = Cast<USwcCreatureAnimInstance>(WeaponLeader->GetAnimInstance()))
				{
					WeaponAnimation->SetAnimations(WeaponAnimations);
				}
				WeaponMeshes.Add(WeaponLeader);
				USkeletalMesh* WeaponBody = InMemory<USkeletalMesh>(Weapon.BodyMeshPath);
				if (WeaponBody != nullptr && WeaponBody != WeaponSkeleton)
				{
					// A different body on the same weapon skeleton follows the pose of the skeleton mesh (Leader Pose).
					USkeletalMeshComponent* Follower = NewObject<USkeletalMeshComponent>(this);
					Follower->SetSkeletalMeshAsset(WeaponBody);
					Follower->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Follower->SetupAttachment(WeaponLeader);
					Follower->RegisterComponent();
					Follower->SetLeaderPoseComponent(WeaponLeader);
					WeaponLeader->SetVisibility(false);
					WeaponMeshes.Add(Follower);
				}
				AnimatedWeapons.Add({WeaponLeader, WeaponAnimations});
				continue;
			}
			UStaticMesh* Model = InMemory<UStaticMesh>(Weapon.MeshPath);
			if (Model == nullptr)
			{
				continue;
			}
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
			Component->SetStaticMesh(Model);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetupAttachment(Leader, Weapon.Bone);
			Component->SetRelativeTransform(Socket);
			Component->RegisterComponent();
			Weapons.Add(Component);
		}
		bHasModel = true;
		SetupBodyBlock();
	}
	else if (UStaticMesh* Cylinder = InMemory<UStaticMesh>(PlaceholderMesh))
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

void ASwcCreature::GetHitShapes(TArray<FSwcCreatureHitShape>& Out) const
{
	if (!bHasModel || Slice == nullptr)
	{
		return;
	}
	// The bone transforms have the scale of the creature: the sizes of the volumes get it too. A volume is in the space of its
	// bone in engine space (m): the map of Net/SwcSpace.h (the weapons on their sockets use the same one).
	const double Scale = GetActorScale3D().GetMax() * 100.0;
	const auto ToUnreal = [](const FVector& Engine) { return FVector(Engine.X, -Engine.Y, Engine.Z); };
	for (const FSwcHitVolume& Volume : Slice->HitVolumes)
	{
		const int32 Bone = Leader->GetBoneIndex(Volume.Bone);
		if (Bone == INDEX_NONE)
		{
			continue;
		}
		const FTransform BoneWorld = Leader->GetBoneTransform(Bone);
		const FVector Center = BoneWorld.TransformPosition(ToUnreal(Volume.EngineCenter) * 100.0);
		FSwcCreatureHitShape& Hit = Out.AddDefaulted_GetRef();
		Hit.Volume = &Volume;
		switch (Volume.Shape)
		{
		case FSwcHitVolume::EShape::Sphere:
			Hit.Shape = FSwcHitShape::Sphere(Center, static_cast<float>(Volume.Radius * Scale));
			break;
		case FSwcHitVolume::EShape::Box:
			Hit.Shape = FSwcHitShape::Box(Center, BoneWorld.GetRotation(), Volume.EngineHalfSize.GetAbs() * Scale);
			break;
		case FSwcHitVolume::EShape::Capsule:
		{
			// The height of a capsule is the length with its two ends.
			const FVector Axis = BoneWorld.TransformVectorNoScale(ToUnreal(Volume.EngineAxis));
			const double Half = FMath::Max(0.0, Volume.Height * 0.5 - Volume.Radius) * Scale;
			Hit.Shape = FSwcHitShape::Capsule(Center - Axis * Half, Center + Axis * Half, static_cast<float>(Volume.Radius * Scale));
			break;
		}
		}
	}
}

FVector ASwcCreature::GetLockPoint() const
{
	const double Scale = GetActorScale3D().Z;
	if (Slice != nullptr && Slice->bHasLock && !Slice->LockOffset.IsNearlyZero())
	{
		// The frame of the creature: x right, y front, z up (engine m); the actor faces +X with +Y to its right.
		const FVector Local(Slice->LockOffset.Y, Slice->LockOffset.X, Slice->LockOffset.Z);
		return GetActorLocation() + GetActorQuat().RotateVector(Local * 100.0 * Scale);
	}
	if (Slice != nullptr && Slice->bHasLock && !Slice->LockJoint.IsNone() && bHasModel && Leader->GetBoneIndex(Slice->LockJoint) != INDEX_NONE)
	{
		return Leader->GetBoneLocation(Slice->LockJoint) + FVector(0.0, 0.0, Slice->LockJointOffset * 100.0 * Scale);
	}
	return GetActorLocation() + FVector(0.0, 0.0, 100.0 * Scale);
}

FBoxSphereBounds ASwcCreature::GetBodyBounds() const
{
	return Body->IsVisible() ? Body->Bounds : Leader->Bounds;
}

double ASwcCreature::GetDeadSeconds() const
{
	return bDead ? FPlatformTime::Seconds() - DeadAt : -1.0;
}

void ASwcCreature::SampleMotion(const swc::creatures::Entity& Entity, uint64 RenderTimeUs, FVector& OutPosition, FQuat& OutRotation) const
{
	const auto& Samples = Entity.samples;
	// The facing: the rotation of the newest state at once, with no turn between the states and no delay (stage P3.C: the retail
	// creature turns its body at each server state and faces the new way before its place follows; set 6 creature-wolf-turn, the
	// frame yaw of the retail video against the backend states). The place stays on the delayed time line below.
	OutRotation = SwcEngineToUnreal(Samples.back().rotation);
	if (Samples.size() == 1 || static_cast<int64>(RenderTimeUs) <= static_cast<int64>(Samples.front().timeUs))
	{
		OutPosition = SwcEngineToUnreal(Samples.front().position);
		if (Samples.size() == 1)
		{
			return;
		}
	}
	if (RenderTimeUs >= Samples.back().timeUs)
	{
		OutPosition = SwcEngineToUnreal(Samples.back().position);
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
		// After a pause the step starts one step before its state: the step of the motion after it (a forced motion of the server has
		// a state each 50 ms, a walk each 200 ms; backend forced_motion.cpp), at most 200 ms.
		int64 Step = StepUs;
		if (Index + 1 < Samples.size())
		{
			Step = FMath::Min<int64>(StepUs, static_cast<int64>(Samples[Index + 1].timeUs) - static_cast<int64>(B.timeUs));
		}
		const int64 Start = static_cast<int64>(B.timeUs) - static_cast<int64>(A.timeUs) > PauseUs ? static_cast<int64>(B.timeUs) - FMath::Max<int64>(Step, 1)
			: static_cast<int64>(A.timeUs);
		const double Alpha = FMath::Clamp(static_cast<double>(static_cast<int64>(RenderTimeUs) - Start) / static_cast<double>(static_cast<int64>(B.timeUs) - Start),
			0.0, 1.0);
		OutPosition = FMath::Lerp(SwcEngineToUnreal(A.position), SwcEngineToUnreal(B.position), Alpha);
		return;
	}
}

void ASwcCreature::ApplyScope(const swc::creatures::Entity& Entity)
{
	// One new state of the scope fields: the record of each scope with a bit in the mask, and the end of each scope that has a
	// record and no bit now (protocol notes 4.14).
	ApplyScopeRecord(0, Entity);
	for (uint32 Scope = 1; Scope < 32; ++Scope)
	{
		if ((Entity.scopeMask & (1U << Scope)) != 0 || Scopes.Contains(Scope))
		{
			ApplyScopeRecord(Scope, Entity);
		}
	}
}

void ASwcCreature::ApplyScopeRecord(uint32 Scope, const swc::creatures::Entity& Entity)
{
	// The record of the scope (or none) with its start on the server clock. The time of the record is the time of the action at
	// the time anchor of its state.
	FScopeState& State = Scopes.FindOrAdd(Scope);
	FPending Next;
	const swc::creatures::ScopeRecord* Record = Entity.scope(Scope);
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
	const FPending* Last = State.Pending.IsEmpty() ? nullptr : &State.Pending.Last();
	const bool bSameAsLast = Last != nullptr ? Last->bRecord == Next.bRecord && (!Next.bRecord || Last->Record.startCount == Next.Record.startCount)
		: State.bPlaying == Next.bRecord && (!Next.bRecord || State.Playing.startCount == Next.Record.startCount);
	if (bSameAsLast)
	{
		if (!Next.bRecord || Last != nullptr)
		{
			return;
		}
		const FSwcCreatureFragment* Fragment = Animations.IsValid() ? Animations->Fragment(static_cast<int32>(Next.Record.fragment)) : nullptr;
		const float Length = Fragment != nullptr ? Fragment->Length : -1.0f;
		if (Length <= 0.0f || FMath::Abs(static_cast<double>(Next.StartUs - State.PlayingStartUs)) <= RestartShare * Length * 1.0e6)
		{
			return;
		}
	}
	State.Pending.Add(Next);
}

FString ASwcCreature::StartPending(uint64 RenderTimeUs, bool bIsDead)
{
	FString Text;
	for (TPair<uint32, FScopeState>& Pair : Scopes)
	{
		const FString Started = StartScope(Pair.Key, Pair.Value, RenderTimeUs, bIsDead);
		if (!Started.IsEmpty())
		{
			Text += (Text.IsEmpty() ? TEXT("") : TEXT("; ")) + Started;
		}
	}
	return Text;
}

FString ASwcCreature::StartScope(uint32 Scope, FScopeState& State, uint64 RenderTimeUs, bool bIsDead)
{
	FString Text;
	USwcCreatureAnimInstance* Animation = Cast<USwcCreatureAnimInstance>(Leader->GetAnimInstance());
	while (!State.Pending.IsEmpty() && State.Pending[0].StartUs <= static_cast<int64>(RenderTimeUs))
	{
		const FPending Next = State.Pending[0];
		State.Pending.RemoveAt(0);
		// A later record that is already due replaces this one.
		if (!State.Pending.IsEmpty() && State.Pending[0].StartUs <= static_cast<int64>(RenderTimeUs))
		{
			continue;
		}
		const float FragmentTime = static_cast<float>(static_cast<double>(static_cast<int64>(RenderTimeUs) - Next.StartUs) / 1.0e6);
		const bool bWasPlaying = State.bPlaying;
		State.bPlaying = Next.bRecord;
		State.Playing = Next.Record;
		State.PlayingStartUs = Next.StartUs;
		int32 Fragment = INDEX_NONE;
		if (Next.bRecord)
		{
			Fragment = static_cast<int32>(Next.Record.fragment);
		}
		else if (Scope == 0 && !bIsDead && Animations.IsValid())
		{
			// The fields with no record: the client plays its own idle (protocol notes 4.14). A dead creature keeps its pose.
			Fragment = Animations->IdleFragment;
		}
		if (Animation == nullptr)
		{
			continue;
		}
		if (Scope != 0)
		{
			// An overlay: a record starts it, the end of the record blends it out.
			if (Fragment == INDEX_NONE && !bWasPlaying)
			{
				continue;
			}
			const bool bStarted = Animation->PlayOverlay(static_cast<int32>(Scope), Fragment, FragmentTime, Next.bRecord ? Next.Record.speed : 1.0f);
			State.PlayingFragment = Fragment;
			Text = Fragment == INDEX_NONE ? FString::Printf(TEXT("scope %u: end"), Scope)
				: FString::Printf(TEXT("scope %u: action %u %s (fragment %d %s), start count %u, at %.2f s%s"), Scope, Next.Record.action,
					Slice != nullptr && Slice->Actions.IsValidIndex(static_cast<int32>(Next.Record.action)) ? *Slice->Actions[static_cast<int32>(Next.Record.action)] : TEXT(""),
					Fragment, Animations.IsValid() ? *Animations->FragmentName(Fragment) : TEXT(""), Next.Record.startCount, FragmentTime,
					bStarted ? TEXT("") : TEXT(": no variant of the fragment for the tags of the slice"));
			continue;
		}
		if (Fragment == INDEX_NONE)
		{
			continue;
		}
		const bool bStarted = Animation->PlayFragment(Fragment, FragmentTime, Next.bRecord ? Next.Record.speed : 1.0f);
		const int32 WeaponScopes = PlayWeaponScope(Fragment, FragmentTime, Next.bRecord ? Next.Record.speed : 1.0f);
		State.PlayingFragment = Fragment;
		Text = FString::Printf(TEXT("%s %s (fragment %d%s), start count %u, at %.2f s of the action"), Next.bRecord ? TEXT("action") : TEXT("idle of the client"),
			Next.bRecord ? *FString::Printf(TEXT("%u %s"), Next.Record.action,
				Slice != nullptr && Slice->Actions.IsValidIndex(static_cast<int32>(Next.Record.action)) ? *Slice->Actions[static_cast<int32>(Next.Record.action)] : TEXT(""))
				: TEXT("-"), Fragment,
			Animations.IsValid() ? *(TEXT(" ") + Animations->FragmentName(Fragment)) : TEXT(""), Next.Record.startCount, FragmentTime);
		if (!bStarted)
		{
			Text += TEXT(": no variant of the fragment for the tags of the slice");
		}
		if (WeaponScopes > 0)
		{
			Text += FString::Printf(TEXT(", the weapon scope on %d weapon(s)"), WeaponScopes);
		}
	}
	return Text;
}

int32 ASwcCreature::PlayWeaponScope(int32 Fragment, float FragmentTime, float PlaybackScale)
{
	int32 Playing = 0;
	for (const FAnimatedWeapon& Weapon : AnimatedWeapons)
	{
		USwcCreatureAnimInstance* WeaponAnimation = Weapon.Leader != nullptr ? Cast<USwcCreatureAnimInstance>(Weapon.Leader->GetAnimInstance()) : nullptr;
		if (WeaponAnimation == nullptr)
		{
			continue;
		}
		// The weapon database has a variant of the fragment: it plays with the time of the fragment of scope 0. Else the weapon
		// scope has no fragment, and the weapon shows its rest pose.
		if (Weapon.Animations->Fragment(Fragment) == nullptr || !WeaponAnimation->PlayFragment(Fragment, FragmentTime, PlaybackScale))
		{
			WeaponAnimation->Stop();
			continue;
		}
		++Playing;
	}
	return Playing;
}

FString ASwcCreature::ProbeText() const
{
	const swc::net::WorldPosition At = SwcUnrealToEngine(GetActorLocation());
	// The actor faces its +X; the Unreal yaw -90 is north (engine +Y): the engine yaw is the Unreal yaw + 90.
	const double EngineYaw = FRotator::ClampAxis(GetActorRotation().Yaw + 90.0);
	const USwcCreatureAnimInstance* Animation = Leader != nullptr ? Cast<USwcCreatureAnimInstance>(Leader->GetAnimInstance()) : nullptr;
	return FString::Printf(TEXT("at %.3f %.3f %.3f, yaw %.1f, move %.2f m/s, turn %.0f deg/s, travel %.0f deg%s; %s"), At.x, At.y, At.z, EngineYaw,
		MoveSpeed, FMath::RadiansToDegrees(TurnSpeed), FMath::RadiansToDegrees(TravelAngle), bDead ? TEXT(", dead") : TEXT(""),
		Animation != nullptr ? *Animation->DescribePose() : TEXT("no body"));
}

FString ASwcCreature::UpdateFromEntity(FSwcCreatureLibrary& Library, const swc::creatures::Entity& Entity, uint64 RenderTimeUs, float DeltaSeconds)
{
	if (!bLooksApplied && Entity.hasVitals && !bLoading)
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
			if (USkeletalMesh* Mesh = InMemory<USkeletalMesh>(*Variant))
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
		// A dead creature does not block.
		BodyBlock->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	// While the assets load: the place only (the actions of the server play from their states when the body is made).
	if (bLoading)
	{
		return FString();
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
		const FScopeState* Main = Scopes.Find(0);
		if (Animation->GetFragment() == INDEX_NONE && (Main == nullptr || (Main->Pending.IsEmpty() && !Main->bPlaying)) && Animations.IsValid()
			&& !Entity.dead)
		{
			Animation->PlayFragment(Animations->IdleFragment, 0.0f, 1.0f);
		}
	}
	return Text;
}
