#include "Character/SwcPlayerActions.h"

#include "Character/SwcActions.h"
#include "Character/SwcCharacterMovement.h"
#include "Character/SwcPlayerAnimInstance.h"
#include "Character/SwcPlayerCharacter.h"
#include "Combat/SwcAttackGraph.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SwcBackendSubsystem.h"
#include "SwcCameraManager.h"

#include <chrono>

namespace
{
/** The off-hand slots 27 to 29 go with the weapon slots 24 to 26. */
uint32 WeaponSlotOf(const FSwcGearPart& Part)
{
	return Part.PaperdollSlot >= 27 ? Part.PaperdollSlot - 3 : Part.PaperdollSlot;
}

int64 SteadyNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/** Turns a yaw to a target yaw: exponential (the share of the remaining angle in each step of 1/30 s, C 0x2A919A0) or
 *  linear (degrees in each second). */
double TurnYaw(double Yaw, double Target, const FSwcFacingRule& Rule, float DeltaTime)
{
	const double Delta = FMath::FindDeltaAngleDegrees(Yaw, Target);
	if (Rule.bExponential)
	{
		const float Alpha = Rule.TurnRate >= 1.0f ? 1.0f : 1.0f - FMath::Pow(1.0f - Rule.TurnRate, 30.0f * DeltaTime);
		return Yaw + Delta * Alpha;
	}
	const double Step = Rule.TurnRate * DeltaTime;
	return Yaw + FMath::Clamp(Delta, -Step, Step);
}
} // namespace

USwcPlayerActions::USwcPlayerActions()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bDrawStrikes = FParse::Param(FCommandLine::Get(), TEXT("SwcDrawStrikes"));
	bContactProbe = FParse::Param(FCommandLine::Get(), TEXT("SwcContactProbe"));
}

std::vector<swc::net::MeleeContactItem> USwcPlayerActions::TakeContacts()
{
	std::vector<swc::net::MeleeContactItem> Out;
	Out.swap(Outgoing);
	return Out;
}

bool USwcPlayerActions::ShapeOf(const FSwcStrikeShape& Clip, FSwcMeleeShape& Out) const
{
	const ASwcPlayerCharacter* Owner = Character();
	const USkeletalMeshComponent* Mesh = Owner->GetMesh();
	// The frame of the joint: a bone of the player at its shown pose, or the weapon in the hand (its model frame, then a
	// joint of its skeleton).
	FTransform Joint;
	if (!Clip.Joint.IsNone() && Mesh->GetBoneIndex(Clip.Joint) != INDEX_NONE)
	{
		Joint = Mesh->GetSocketTransform(Clip.Joint, RTS_World);
	}
	else
	{
		const TArray<FSwcGearPart>& Parts = Owner->GetGearParts();
		const uint32 Slot = Clip.bOffhand ? VisualSlot + 3 : VisualSlot;
		const int32 Index = VisualSlot == 0 ? INDEX_NONE
			: Parts.IndexOfByPredicate([Slot](const FSwcGearPart& Part) { return Part.PaperdollSlot == Slot && !Part.bAccessory; });
		if (Index == INDEX_NONE || Owner->GetGearComponent(Index) == nullptr)
		{
			return false;
		}
		// The weapon in the hand: its place on the hand bone at the pose of this frame (the attached component follows the
		// bone one step later).
		const FSwcAttachPlace& Hand = Parts[Index].Hand;
		Joint = Hand.Offset * Mesh->GetSocketTransform(Hand.Bone, RTS_World);
		if (const FTransform* InModel = !Clip.Joint.IsNone() ? Data->WeaponJoint(Parts[Index].Model, Clip.Joint) : nullptr)
		{
			Joint = *InModel * Joint;
		}
	}
	// The offset and the rotation of the clip in the frame of the joint, through the space map of stage 1.1 (x, -y, z; the
	// rotation about x, then y, then z, as the offsets of the equip database in docs/GEAR.md).
	const FQuat Game = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Clip.Rotation.Z))
		* FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Clip.Rotation.Y)) * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Clip.Rotation.X));
	const FQuat Local = FQuat(-Game.X, Game.Y, -Game.Z, Game.W).GetNormalized();
	Out.Center = Joint.TransformPosition(FVector(Clip.Offset.X, -Clip.Offset.Y, Clip.Offset.Z) * 100.0);
	Out.Rotation = (Joint.GetRotation() * Local).GetNormalized();
	Out.Kind = Clip.Kind == FSwcStrikeShape::EKind::Sphere ? FSwcMeleeShape::EKind::Sphere
		: Clip.Kind == FSwcStrikeShape::EKind::Box          ? FSwcMeleeShape::EKind::Box
															 : FSwcMeleeShape::EKind::Capsule;
	Out.Radius = Clip.Radius * 100.0f;
	Out.HalfLength = Clip.Kind == FSwcStrikeShape::EKind::Capsule ? Clip.HalfHeight * 100.0f : 0.0f;
	Out.HalfExtents = Clip.Box * 50.0;
	Out.Attacker = Owner->GetFeetLocation();
	Out.Direction = Owner->GetActorForwardVector();
	Out.bNoSight = Clip.bNoSight;
	return true;
}

void USwcPlayerActions::DrawStrikes(const FSwcWeaponActions& Weapon, int32 GraphFragment, float Clock) const
{
	if (!Weapon.Strikes.IsValidIndex(GraphFragment))
	{
		return;
	}
	for (const FSwcStrike& Strike : Weapon.Strikes[GraphFragment])
	{
		if (Clock < Strike.Start - swc::combat::kClockEpsilon || Clock > Strike.End + swc::combat::kClockEpsilon)
		{
			continue;
		}
		for (const FSwcStrikeShape& Clip : Strike.Shapes)
		{
			FSwcMeleeShape Shape;
			if (!ShapeOf(Clip, Shape))
			{
				continue;
			}
			// One frame: the shape at the pose of this frame.
			switch (Shape.Kind)
			{
			case FSwcMeleeShape::EKind::Sphere:
				DrawDebugSphere(GetWorld(), Shape.Center, Shape.Radius, 12, FColor::Red, false, -1.0f);
				break;
			case FSwcMeleeShape::EKind::Capsule:
				DrawDebugCapsule(GetWorld(), Shape.Center, Shape.HalfLength + Shape.Radius, Shape.Radius, Shape.Rotation, FColor::Red, false, -1.0f);
				break;
			case FSwcMeleeShape::EKind::Box:
				DrawDebugBox(GetWorld(), Shape.Center, Shape.HalfExtents, Shape.Rotation, FColor::Red, false, -1.0f);
				break;
			}
		}
	}
}

void USwcPlayerActions::QueryStrikes(const FSwcWeaponActions& Weapon, int32 GraphFragment, uint32 VisitNumber, float From, float To)
{
	if (!Weapon.Strikes.IsValidIndex(GraphFragment))
	{
		return;
	}
	const TArray<FSwcStrike>& Strikes = Weapon.Strikes[GraphFragment];
	for (int32 StrikeIndex = 0; StrikeIndex < Strikes.Num(); ++StrikeIndex)
	{
		const FSwcStrike& Strike = Strikes[StrikeIndex];
		// The window of the strike has a clock of this frame: (From, To], To the clock of the next input step.
		if (To < Strike.Start - swc::combat::kClockEpsilon || From > Strike.End + swc::combat::kClockEpsilon)
		{
			continue;
		}
		if (!StrikesSeen.Contains(StrikeIndex))
		{
			StrikesSeen.Add(StrikeIndex);
			++StrikeCount;
		}
		for (int32 ShapeIndex = 0; ShapeIndex < Strike.Shapes.Num(); ++ShapeIndex)
		{
			FSwcMeleeShape Shape;
			if (!ShapeOf(Strike.Shapes[ShapeIndex], Shape))
			{
				continue;
			}
			Shape.Visit = VisitNumber;
			Shape.Strike = StrikeIndex;
			Shape.Key = Strike.Key;
			// The cast direction: the move of the shape since the step before.
			const int32 ShapeKey = StrikeIndex * 64 + ShapeIndex;
			if (const FVector* Before = LastCenters.Find(ShapeKey); Before != nullptr && !(Shape.Center - *Before).IsNearlyZero(0.1))
			{
				Shape.Direction = (Shape.Center - *Before).GetSafeNormal();
			}
			LastCenters.Add(ShapeKey, Shape.Center);
			++ShapeQueries;
			std::vector<swc::net::MeleeContactItem> Found;
			if (ContactQuery)
			{
				ContactQuery(Shape, Found);
			}
			else if (!bQueryNoted)
			{
				bQueryNoted = true;
				UE_LOG(LogSwcBackend, Log, TEXT("attack: no melee contact query of the creatures: the strikes give no contact%s"),
					bContactProbe ? TEXT(" (the probe sends made-up contacts)") : TEXT(""));
			}
			if (bContactProbe && ShapeIndex == 0 && !Contacted.Contains(FString::Printf(TEXT("%u|%d|probe"), VisitNumber, StrikeIndex)))
			{
				// A receiver that does not exist (actor id 0, facet 0): the backend refuses the claim (a check of list 3).
				Contacted.Add(FString::Printf(TEXT("%u|%d|probe"), VisitNumber, StrikeIndex));
				swc::net::MeleeContactItem Probe;
				const FVector Point = Shape.Center / 100.0;
				Probe.point = {static_cast<float>(Point.X), static_cast<float>(-Point.Y), static_cast<float>(Point.Z)};
				Probe.normal = {0.0F, 0.0F, 1.0F};
				Probe.direction = {static_cast<float>(Shape.Direction.X), static_cast<float>(-Shape.Direction.Y), static_cast<float>(Shape.Direction.Z)};
				Found.push_back(Probe);
			}
			for (const swc::net::MeleeContactItem& Contact : Found)
			{
				// One contact for each target for each strike (the damage key of the fragment visit).
				const FString Key = FString::Printf(TEXT("%u|%d|%016llx|%016llx"), VisitNumber, StrikeIndex, static_cast<unsigned long long>(Contact.entity),
					static_cast<unsigned long long>(Contact.facet));
				if (Contacted.Contains(Key))
				{
					continue;
				}
				Contacted.Add(Key);
				Outgoing.push_back(Contact);
				++ContactCount;
				UE_LOG(LogSwcBackend, Log, TEXT("attack: contact of visit %u strike %s (%s): entity %016llx facet %016llx at %.2f %.2f %.2f m"), VisitNumber,
					*Strike.Key, UTF8_TO_TCHAR(Weapon.Graph.fragments[GraphFragment].name.c_str()),
					static_cast<unsigned long long>(Contact.entity), static_cast<unsigned long long>(Contact.facet), Contact.point.x, Contact.point.y, Contact.point.z);
			}
		}
	}
}

ASwcPlayerCharacter* USwcPlayerActions::Character() const
{
	return Cast<ASwcPlayerCharacter>(GetOwner());
}

USwcPlayerAnimInstance* USwcPlayerActions::Animation() const
{
	const ASwcPlayerCharacter* Owner = Character();
	return Owner != nullptr && Owner->GetMesh() != nullptr ? Cast<USwcPlayerAnimInstance>(Owner->GetMesh()->GetAnimInstance()) : nullptr;
}

void USwcPlayerActions::Setup(const TSharedPtr<FSwcActions>& InData, bool bInFemale, const std::shared_ptr<swc::combat::Driver>& InDriver)
{
	Data = InData;
	bFemale = bInFemale;
	Driver = InDriver;
	if (USwcPlayerAnimInstance* Anim = Animation())
	{
		Anim->SetActions(Data, bFemale);
	}
	// The movement ticks after this component: it gets the velocity of the action of this frame.
	if (ASwcPlayerCharacter* Owner = Character())
	{
		Owner->GetSwcMovement()->AddTickPrerequisiteComponent(this);
		// The shapes of the strikes at the final pose of each frame.
		if (USkeletalMeshComponent* Mesh = BonesMesh.Get())
		{
			Mesh->UnregisterOnBoneTransformsFinalizedDelegate(BonesHandle);
		}
		BonesMesh = Owner->GetMesh();
		BonesHandle = Owner->GetMesh()->RegisterOnBoneTransformsFinalizedDelegate(
			FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &USwcPlayerActions::OnBonesFinal));
	}
}

const FSwcWeaponActions* USwcPlayerActions::WeaponOf(uint32 Slot) const
{
	const ASwcPlayerCharacter* Owner = Character();
	if (Owner == nullptr || !Data.IsValid())
	{
		return nullptr;
	}
	for (const FSwcGearPart& Part : Owner->GetGearParts())
	{
		if (Part.PaperdollSlot == Slot && !Part.bAccessory)
		{
			return Data->Weapon(Part.Tags);
		}
	}
	return nullptr;
}

void USwcPlayerActions::OnGearChanged()
{
	ASwcPlayerCharacter* Owner = Character();
	if (Owner == nullptr)
	{
		return;
	}
	// The pose follows the weapon state of the paperdoll (SetWeaponState placed the parts at once).
	Upper.Reset();
	VisualSlot = Owner->IsWeaponDrawn() ? Owner->GetActiveWeaponSlot() : 0;
	bVisualOffhand = VisualSlot != 0 && Owner->HasWeapon(VisualSlot + 3);
	const FSwcWeaponActions* First = WeaponOf(24);
	const FSwcWeaponActions* Second = WeaponOf(25);
	const swc::combat::Graph* Graph1 = First != nullptr && First->HasGraph() ? &First->Graph : nullptr;
	const swc::combat::Graph* Graph2 = Second != nullptr && Second->HasGraph() ? &Second->Graph : nullptr;
	const uint32 Held = Owner->GetActiveWeaponSlot() != 0 ? Owner->GetActiveWeaponSlot() : 24;
	if (Driver && (DriverGraphs[0] != Graph1 || DriverGraphs[1] != Graph2 || DriverHeld != Held))
	{
		DriverGraphs[0] = Graph1;
		DriverGraphs[1] = Graph2;
		DriverHeld = Held;
		Driver->set_weapons(Graph1, Graph2, Held);
		UE_LOG(LogSwcBackend, Log, TEXT("actions: attack graphs: slot 24 %s, slot 25 %s; the weapon in the hand: slot %u"),
			First != nullptr ? *FString::Join(First->Tags, TEXT(",")) : TEXT("none"), Second != nullptr ? *FString::Join(Second->Tags, TEXT(",")) : TEXT("none"),
			Held);
	}
}

void USwcPlayerActions::OnWeaponKey(uint32 Slot)
{
	ASwcPlayerCharacter* Owner = Character();
	if (Owner == nullptr || !Owner->HasWeapon(Slot))
	{
		return;
	}
	const uint32 Held = Driver ? Driver->snapshot().held : Owner->GetActiveWeaponSlot();
	if (Slot == Held || !Driver)
	{
		// The key of the weapon in the hand draws it (the server has no draw and asks for nothing, protocol notes 4.11).
		if (Owner->GetActiveWeaponSlot() != Slot || !Owner->IsWeaponDrawn())
		{
			UE_LOG(LogSwcBackend, Log, TEXT("weapon: key for slot %u: draw"), Slot);
			Request(Slot, true);
		}
		return;
	}
	// The key of the other weapon: the switch at the step of the driver (at rest at once; in an attack at its switch mark).
	UE_LOG(LogSwcBackend, Log, TEXT("weapon: key for slot %u: the switch waits for its step"), Slot);
}

void USwcPlayerActions::OnSheatheKey()
{
	ASwcPlayerCharacter* Owner = Character();
	if (Owner != nullptr && Owner->GetActiveWeaponSlot() != 0)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("weapon: sheathe key: slot %u %s"), Owner->GetActiveWeaponSlot(),
			Owner->IsWeaponDrawn() ? TEXT("sheathe") : TEXT("draw"));
		Request(Owner->GetActiveWeaponSlot(), !Owner->IsWeaponDrawn());
	}
}

void USwcPlayerActions::OnDodgeKey()
{
	// The bit goes for each press; the stamina of the driver decides the dodge at its step, as the server does.
	if (Driver)
	{
		Driver->dodge_press();
	}
}

void USwcPlayerActions::OnAttackButton(bool bDown)
{
	if (bDown == bButtonDown)
	{
		return;
	}
	bButtonDown = bDown;
	if (Driver)
	{
		Driver->button(bDown);
	}
}

void USwcPlayerActions::StartUpper(uint32 Slot, bool bDraw, double Start)
{
	const FSwcWeaponActions* Weapon = WeaponOf(Slot);
	FUpperAction Action;
	Action.Slot = Slot;
	Action.bDraw = bDraw;
	Action.Start = Start;
	Action.Serial = ++UpperSerial;
	Action.Fragment = Weapon != nullptr ? (bDraw ? Weapon->Draw : Weapon->Sheathe) : INDEX_NONE;
	if (Action.Fragment != INDEX_NONE)
	{
		const FSwcActionFragment& Fragment = Data->Fragments[Action.Fragment];
		Action.Length = Fragment.Length > 0.0f ? Fragment.Length : 1.0f;
		Action.bOffhandMark = Fragment.MarkTime(TEXT("Unsheathe_Offhand"), -1.0f) >= 0.0f;
	}
	UE_LOG(LogSwcBackend, Log, TEXT("weapon: slot %u %s: %s in %.3f s"), Slot, bDraw ? TEXT("draw") : TEXT("sheathe"),
		Action.Fragment != INDEX_NONE ? *Data->Fragments[Action.Fragment].Key : TEXT("(no fragment: the marks of the gear)"),
		Start - GetWorld()->GetTimeSeconds());
	Upper.Add(Action);
}

void USwcPlayerActions::Request(uint32 Slot, bool bDrawn)
{
	ASwcPlayerCharacter* Owner = Character();
	if (Owner == nullptr)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const uint32 OldSlot = Owner->GetActiveWeaponSlot();
	const bool bOldDrawn = Owner->IsWeaponDrawn();
	// A new request replaces the draw and the sheathe that play: the parts stay where they are until the new marks.
	Upper.Reset();
	float Delay = 0.0f;
	if (bOldDrawn && OldSlot != 0 && (!bDrawn || OldSlot != Slot))
	{
		StartUpper(OldSlot, false, Now);
		// The sheathe mark of the weapon that leaves the hand delays the draw of the new one (gear.md).
		const FUpperAction& Sheathe = Upper.Last();
		float Mark = 0.0f;
		for (const FSwcGearPart& Part : Owner->GetGearParts())
		{
			Mark = WeaponSlotOf(Part) == OldSlot ? FMath::Max(Mark, Part.SheatheTime) : Mark;
		}
		Delay = Sheathe.Fragment != INDEX_NONE ? Data->Fragments[Sheathe.Fragment].MarkTime(TEXT("sheathe_weapon"), Mark) : Mark;
	}
	if (bDrawn && (Slot != OldSlot || !bOldDrawn))
	{
		StartUpper(Slot, true, Now + Delay);
	}
	Owner->SetLogicalWeaponState(Slot, bDrawn);
}

void USwcPlayerActions::PlaceSlot(uint32 Slot, bool bInHand, bool bMain, bool bOffhand)
{
	ASwcPlayerCharacter* Owner = Character();
	const TArray<FSwcGearPart>& Parts = Owner->GetGearParts();
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		const FSwcGearPart& Part = Parts[Index];
		const bool bOffhandPart = Part.PaperdollSlot >= 27;
		if (Part.bAccessory || WeaponSlotOf(Part) != Slot || (bOffhandPart ? !bOffhand : !bMain))
		{
			continue;
		}
		Owner->PlaceGear(Index, bInHand);
	}
	if (bMain)
	{
		VisualSlot = bInHand ? Slot : (VisualSlot == Slot ? 0 : VisualSlot);
	}
	if (bOffhand && bInHand)
	{
		bVisualOffhand = Owner->HasWeapon(Slot + 3);
	}
	else if (bOffhand && (Slot == VisualSlot || VisualSlot == 0))
	{
		bVisualOffhand = false;
	}
}

void USwcPlayerActions::DrawAtOnce(uint32 Slot)
{
	ASwcPlayerCharacter* Owner = Character();
	Upper.Reset();
	for (uint32 Other = 24; Other <= 26; ++Other)
	{
		PlaceSlot(Other, Other == Slot, true, true);
	}
	Owner->SetLogicalWeaponState(Slot, true);
}

void USwcPlayerActions::UpdateUpper(double Now)
{
	USwcPlayerAnimInstance* Anim = Animation();
	const FUpperAction* Shown = nullptr;
	float ShownClock = 0.0f;
	for (int32 Index = 0; Index < Upper.Num();)
	{
		FUpperAction& Action = Upper[Index];
		if (Now < Action.Start)
		{
			++Index;
			continue;
		}
		const float Clock = static_cast<float>(Now - Action.Start);
		if (Action.Fragment != INDEX_NONE)
		{
			// The marks of this frame, in the order of their times (each one time).
			for (const swc::combat::Mark* Mark : swc::combat::marks_between(Data->Fragments[Action.Fragment].Marks, Action.Last, Clock))
			{
				const FString Name = UTF8_TO_TCHAR(Mark->name.c_str());
				if (Action.bDraw && Name.Equals(TEXT("unsheathe_weapon"), ESearchCase::IgnoreCase))
				{
					PlaceSlot(Action.Slot, true, true, !Action.bOffhandMark);
				}
				else if (Action.bDraw && Name.Equals(TEXT("Unsheathe_Offhand"), ESearchCase::IgnoreCase))
				{
					PlaceSlot(Action.Slot, true, false, true);
				}
				else if (!Action.bDraw && Name.Equals(TEXT("sheathe_weapon"), ESearchCase::IgnoreCase))
				{
					PlaceSlot(Action.Slot, false, true, true);
				}
			}
		}
		else
		{
			// No fragment for the tags: the marker times of the gear (weapons.json timings).
			float Mark = 0.0f;
			for (const FSwcGearPart& Part : Character()->GetGearParts())
			{
				Mark = WeaponSlotOf(Part) == Action.Slot ? FMath::Max(Mark, Action.bDraw ? Part.DrawTime : Part.SheatheTime) : Mark;
			}
			if (Action.Last < Mark && Clock >= Mark)
			{
				PlaceSlot(Action.Slot, Action.bDraw, true, true);
			}
			Action.Length = Mark + 0.01f;
		}
		Action.Last = Clock;
		if (Clock >= Action.Length)
		{
			Upper.RemoveAt(Index);
			continue;
		}
		if (Action.Fragment != INDEX_NONE)
		{
			Shown = &Action;
			ShownClock = Clock;
		}
		++Index;
	}
	if (Anim != nullptr)
	{
		Anim->SetAction(1, Shown != nullptr ? Shown->Fragment : INDEX_NONE, Shown != nullptr ? Shown->Serial : 0, ShownClock);
	}
}

void USwcPlayerActions::FillFacts(const FSwcActionFragment& Fragment, const FSwcWeaponActions& Weapon, float Time, FSwcConditionFacts& Facts) const
{
	const ASwcPlayerCharacter* Owner = Character();
	Facts.Marks = &Fragment.Marks;
	Facts.Time = Time;
	for (const FString& Tag : Weapon.Tags)
	{
		Facts.Tags.Add(Tag.ToLower());
	}
	if (Weapon.Tags.IsEmpty())
	{
		Facts.Tags.Add(TEXT("unarmed"));
	}
	const APlayerController* Controller = Cast<APlayerController>(Owner->GetController());
	const ASwcCameraManager* Camera = Controller != nullptr ? Cast<ASwcCameraManager>(Controller->PlayerCameraManager) : nullptr;
	Facts.bCameraLock = Camera != nullptr && Camera->IsLocked();
	if (Driver)
	{
		const swc::combat::Bits Bits = Driver->snapshot().bits;
		Facts.bAttackPress = Bits.press;
		Facts.bAttackHold = Bits.hold || Bits.press;
	}
	const FVector Velocity = Owner->GetVelocity();
	if (Velocity.Size2D() > 10.0)
	{
		const FVector Forward = Owner->GetActorForwardVector();
		Facts.FacingVelocityAngle = static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(Forward.GetSafeNormal2D(), Velocity.GetSafeNormal2D()), -1.0, 1.0))));
	}
}

void USwcPlayerActions::Move(const FSwcActionFragment& Fragment, const FSwcWeaponActions& Weapon, float From, float To, bool bStart, float DeltaTime)
{
	ASwcPlayerCharacter* Owner = Character();
	USwcCharacterMovement* Movement = Owner->GetSwcMovement();
	const FSwcActivity* Activity = Data->Activity(Fragment.Name);
	if (Activity == nullptr && Fragment.Name.StartsWith(TEXT("Dodge_Roll")))
	{
		Activity = Data->Activity(TEXT("Dodge_Roll")); // The action Dodge of playeractions_dodge changes its fragment by the load class.
	}
	FSwcConditionFacts Facts;
	FillFacts(Fragment, Weapon, To, Facts);
	const FSwcMotionRule Rule = Activity != nullptr ? Data->MotionRule(*Activity, Facts) : FSwcMotionRule();
	const FRotator Frame(0.0, Owner->GetActorRotation().Yaw, 0.0);
	const FVector Forward = Frame.Vector();
	const FVector Right = FRotationMatrix(Frame).GetUnitAxis(EAxis::Y);
	if (bStart)
	{
		// InheritVelocity: the horizontal velocity at the start, at most InheritVelocityMaxSpeed (m/s).
		const FVector Velocity = Owner->GetVelocity();
		Inherited = Rule.bInheritVelocity
			? FVector(FVector::DotProduct(Velocity, Right), FVector::DotProduct(Velocity, Forward), 0.0).GetClampedToMaxSize(Rule.InheritVelocityMaxSpeed * 100.0)
			: FVector::ZeroVector;
	}
	// Decel: the inherited velocity goes to 0 on each axis (m/s2; the unit is an inference).
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const double Step = Rule.Decel[Axis] * 100.0 * DeltaTime;
		Inherited[Axis] = Inherited[Axis] > 0.0 ? FMath::Max(Inherited[Axis] - Step, 0.0) : FMath::Min(Inherited[Axis] + Step, 0.0);
	}
	FVector Root = FVector::ZeroVector;
	Data->RootDelta(Fragment, From, To, Root);
	// The root track (m, x right, y forward) x MotionMultiplier, in the frame of the character now.
	const FVector Step = Root * 100.0 * Rule.MotionMultiplier;
	FVector Velocity = DeltaTime > 0.0f ? (Forward * Step.Y + Right * Step.X) / DeltaTime : FVector::ZeroVector;
	Velocity += Forward * Inherited.Y + Right * Inherited.X;
	Movement->SetActionMotion(true, Velocity);
	// The facing: CameraRelativeFacing first (the 1H_Melee attacks face the camera), else InputRelativeFacing with the move keys
	// (the priority of the two is an inference).
	if (Activity == nullptr)
	{
		return;
	}
	double Yaw = Owner->GetActorRotation().Yaw;
	if (const FSwcFacingRule* Camera = Data->FacingRule(Activity->CameraFacing, Activity->CameraDefault, Facts))
	{
		Yaw = TurnYaw(Yaw, Movement->GetCameraYaw(), *Camera, DeltaTime);
	}
	else if (const FSwcFacingRule* Input = Data->FacingRule(Activity->InputFacing, Activity->InputDefault, Facts);
			 Input != nullptr && !Movement->GetMoveInput().IsNearlyZero())
	{
		const FVector2D Keys = Movement->GetMoveInput();
		const double Target = Movement->GetCameraYaw() + FMath::RadiansToDegrees(FMath::Atan2(Keys.X, Keys.Y));
		Yaw = TurnYaw(Yaw, Target, *Input, DeltaTime);
	}
	Owner->SetActorRotation(FRotator(0.0, Yaw, 0.0));
}

void USwcPlayerActions::UpdateAttack(float DeltaTime)
{
	ASwcPlayerCharacter* Owner = Character();
	USwcPlayerAnimInstance* Anim = Animation();
	USwcCharacterMovement* Movement = Owner->GetSwcMovement();
	const swc::combat::Driver::Snapshot Snapshot = Driver ? Driver->snapshot() : swc::combat::Driver::Snapshot();
	if (Snapshot.switches != SeenSwitches)
	{
		// The switch of a weapon key at its step: the old weapon to its sheath, the new one to the hand (the marks).
		SeenSwitches = Snapshot.switches;
		UE_LOG(LogSwcBackend, Log, TEXT("weapon: switch to slot %u at input %u"), Snapshot.switchSlot, static_cast<unsigned>(Snapshot.switchInput));
		Request(Snapshot.switchSlot, true);
	}
	if (Snapshot.lostKeys != SeenLost)
	{
		SeenLost = Snapshot.lostKeys;
		UE_LOG(LogSwcBackend, Log, TEXT("weapon: a key is lost: no switch window of the attack in its %u steps"), swc::combat::kKeySteps);
	}
	const FSwcWeaponActions* Weapon = Data.IsValid() ? Data->WeaponOfGraph(Snapshot.graph) : nullptr;
	const swc::combat::Visit* Newest = Snapshot.visits.empty() ? nullptr : &Snapshot.visits.back();
	const int32 Fragment = Newest != nullptr && Weapon != nullptr && Weapon->GraphFragments.IsValidIndex(Newest->fragment)
		? Weapon->GraphFragments[Newest->fragment] : INDEX_NONE;
	const bool bPlays = Newest != nullptr && !Newest->ended && Fragment != INDEX_NONE;
	// The time since the last input step (at most one step).
	const float Since = FMath::Clamp(static_cast<float>(SteadyNs() - Snapshot.inputNs) / 1e9f, 0.0f, 1.0f / swc::combat::kStepsInSecond);
	// The dodge (stage P2.5): the newer of the dodge and the attack visit plays on the FullBody channel.
	if (Snapshot.dodge && Snapshot.dodge->number != DodgeReleased)
	{
		const double AttackStart = bPlays ? Snapshot.time - static_cast<double>(static_cast<std::uint16_t>(Snapshot.input - Newest->start)) / swc::combat::kStepsInSecond : -1.0;
		if (!bPlays || Snapshot.dodge->start >= AttackStart - 1e-6)
		{
			if (PlayDodge(Snapshot, Since, DeltaTime))
			{
				return;
			}
		}
	}
	if (!bPlays)
	{
		if (bPlaying)
		{
			bPlaying = false;
			Movement->EndActionMotion(CarryLimit);
		}
		if (Anim != nullptr)
		{
			Anim->SetAction(0, INDEX_NONE, 0, 0.0f);
		}
		return;
	}
	// The clock of the visit at the last input step, and the time since that step.
	float Clock = swc::combat::clock_of(Newest->start, Snapshot.input) + Since;
	const bool bStart = Newest->number != Visit || bDodgePlayed;
	bDodgePlayed = false;
	if (bStart)
	{
		Visit = Newest->number;
		++Visits;
		LastClock = 0.0f;
		StrikeClock = -1.0f;
		StrikesSeen.Reset();
		LastCenters.Reset();
		Contacted.Reset();
		const FSwcActionFragment& Data0 = Data->Fragments[Fragment];
		UE_LOG(LogSwcBackend, Log, TEXT("attack: visit %u %s (%s) at input %u, clock %.3f s, length %.3f s"), Newest->number,
			UTF8_TO_TCHAR(Weapon->Graph.fragments[Newest->fragment].name.c_str()), *Data0.Key, static_cast<unsigned>(Newest->start), Clock,
			Weapon->Graph.fragments[Newest->fragment].length);
		// An attack holds the weapon of its graph in the hand (an attack with a sheathed weapon, or in the draw of a switch).
		if (VisualSlot != Snapshot.held || !Owner->IsWeaponDrawn() || Owner->GetActiveWeaponSlot() != Snapshot.held)
		{
			DrawAtOnce(Snapshot.held);
		}
	}
	Clock = FMath::Max(Clock, LastClock);
	bPlaying = true;
	CarryLimit = 0.0f;
	if (Anim != nullptr)
	{
		Anim->SetAction(0, Fragment, Newest->number, Clock);
	}
	Move(Data->Fragments[Fragment], *Weapon, LastClock, Clock, bStart, DeltaTime);
	LastClock = Clock;
	// The strikes: the clock of the next input step (the contacts of this frame go into that entry). The shapes need the
	// pose of this frame: they run when the bone transforms of the frame are final (OnBonesFinal).
	const float NextStep = swc::combat::clock_of(Newest->start, static_cast<std::uint16_t>(Snapshot.input + 1));
	Work.Weapon = Weapon;
	Work.GraphFragment = Newest->fragment;
	Work.Visit = Newest->number;
	Work.Clock = Clock;
	Work.bQuery = NextStep > StrikeClock;
	Work.From = StrikeClock;
	Work.To = NextStep;
	StrikeClock = FMath::Max(StrikeClock, NextStep);
}

bool USwcPlayerActions::PlayDodge(const swc::combat::Driver::Snapshot& Snapshot, float Since, float DeltaTime)
{
	ASwcPlayerCharacter* Owner = Character();
	USwcPlayerAnimInstance* Anim = Animation();
	USwcCharacterMovement* Movement = Owner->GetSwcMovement();
	const swc::combat::DodgeState& Dodge = *Snapshot.dodge;
	const bool bStart = Dodge.number != DodgeNumber;
	if (bStart)
	{
		// A new dodge: the fragment of its load class and of the direction of the move keys in the camera frame (no key: to the
		// rear, the trap of docs/systems/stamina-and-dodge.md), of the weapon in the hand or unarmed.
		DodgeNumber = Dodge.number;
		static const TCHAR* const Names[] = {TEXT("Dodge_Roll_Fast"), TEXT("Dodge_Roll"), TEXT("Dodge_Roll_Slow")};
		const FVector2D Keys = Movement->GetMoveInput();
		const float Angle = Keys.IsNearlyZero() ? 180.0f : FMath::RadiansToDegrees(FMath::Atan2(Keys.X, Keys.Y));
		const TCHAR* Variant = FMath::Abs(Angle) <= 45.0f ? TEXT("") : FMath::Abs(Angle) >= 135.0f ? TEXT("+BackNav") : Angle > 0.0f ? TEXT("+RightNav") : TEXT("+LeftNav");
		const FString Key = FString(Names[FMath::Min(static_cast<int32>(Dodge.load), 2)]) + Variant;
		const FSwcWeaponActions* Drawn = VisualSlot != 0 ? WeaponOf(VisualSlot) : nullptr;
		const FSwcWeaponActions* Unarmed = Data->Weapon(FString());
		DodgeWeapon = Drawn != nullptr && Drawn->Dodge.Contains(Key) ? Drawn : Unarmed;
		DodgeFragment = DodgeWeapon != nullptr ? DodgeWeapon->Dodge.FindRef(Key, INDEX_NONE) : INDEX_NONE;
		LastClock = 0.0f;
		// ApplyAlignRotation: the character faces the camera at the start of the dodge (the direction is in the camera frame).
		Owner->SetActorRotation(FRotator(0.0, Movement->GetCameraYaw(), 0.0));
		UE_LOG(LogSwcBackend, Log, TEXT("dodge: %u %s (%s), stamina %.1f of %.1f"), Dodge.number, *Key,
			DodgeFragment != INDEX_NONE ? *Data->Fragments[DodgeFragment].Key : TEXT("no fragment"), Snapshot.stamina, Snapshot.staminaMax);
	}
	if (DodgeFragment == INDEX_NONE || DodgeWeapon == nullptr)
	{
		return false;
	}
	const FSwcActionFragment& Fragment = Data->Fragments[DodgeFragment];
	const float Clock = FMath::Max(static_cast<float>(Snapshot.time - Dodge.start) + Since, LastClock);
	// The end: the end of the animation, or a move key after the mark CanTransition (the movement starts again).
	const float Transition = Fragment.MarkTime(TEXT("CanTransition"), 0.5f);
	if ((Fragment.Length > 0.0f && Clock >= Fragment.Length) || (Clock >= Transition && !Movement->GetMoveInput().IsNearlyZero()))
	{
		DodgeReleased = Dodge.number;
		return false;
	}
	bPlaying = true;
	bDodgePlayed = true;
	// The end of the roll: the movement starts from its velocity, at most InheritVelocityMaxSpeed of the action Dodge (6 m/s).
	const FSwcActivity* DodgeActivity = Data->Activity(TEXT("Dodge_Roll"));
	CarryLimit = DodgeActivity != nullptr && DodgeActivity->MotionDefault.InheritVelocityMaxSpeed > 0.0f ? DodgeActivity->MotionDefault.InheritVelocityMaxSpeed * 100.0f : 600.0f;
	if (Anim != nullptr)
	{
		Anim->SetAction(0, DodgeFragment, 0x80000000u | Dodge.number, Clock);
	}
	Move(Fragment, *DodgeWeapon, LastClock, Clock, bStart, DeltaTime);
	LastClock = Clock;
	return true;
}

void USwcPlayerActions::OnBonesFinal()
{
	if (Work.Weapon == nullptr || !Data.IsValid() || Character() == nullptr)
	{
		return;
	}
	if (Work.bQuery)
	{
		QueryStrikes(*Work.Weapon, Work.GraphFragment, Work.Visit, Work.From, Work.To);
	}
	if (bDrawStrikes)
	{
		DrawStrikes(*Work.Weapon, Work.GraphFragment, Work.Clock);
	}
	Work = FStrikeWork();
}

void USwcPlayerActions::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USkeletalMeshComponent* Mesh = BonesMesh.Get())
	{
		Mesh->UnregisterOnBoneTransformsFinalizedDelegate(BonesHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void USwcPlayerActions::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Data.IsValid() || Character() == nullptr)
	{
		return;
	}
	UpdateAttack(DeltaTime);
	UpdateUpper(GetWorld()->GetTimeSeconds());
	if (USwcPlayerAnimInstance* Anim = Animation())
	{
		Anim->SetStance(VisualSlot != 0 ? WeaponOf(VisualSlot) : nullptr, bVisualOffhand);
	}
}
