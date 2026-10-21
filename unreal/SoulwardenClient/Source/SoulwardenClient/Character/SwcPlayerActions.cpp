#include "Character/SwcPlayerActions.h"

#include "Character/SwcActions.h"
#include "Character/SwcCharacterMovement.h"
#include "Character/SwcPlayerAnimInstance.h"
#include "Character/SwcPlayerCharacter.h"
#include "Combat/SwcAttackGraph.h"
#include "Creatures/SwcCreatureSubsystem.h"
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

/** The time that a Nav_Stop fragment plays (AST-199): its mark NavStop_Exit, else the end of the item of layer 0 (its clip, or the
 *  longest example of its blend space: IfFragmentDone of the action Nav_Stop) [D playeractions_nav]. */
float NavStopLength(const FSwcActions& Data, const FSwcActionFragment& Fragment)
{
	if (Fragment.Layers.IsEmpty() || Fragment.Layers[0].IsEmpty())
	{
		return 0.0f;
	}
	const FSwcActionItem& Item = Fragment.Layers[0].Last();
	float Duration = 0.0f;
	if (Data.Clips.IsValidIndex(Item.Clip))
	{
		Duration = FMath::Max(Data.Clips[Item.Clip].Duration - Item.Start, 0.0f);
	}
	else if (Data.BlendSpaces.IsValidIndex(Item.BlendSpace))
	{
		const FSwcBlendSpace& Space = Data.BlendSpaces[Item.BlendSpace];
		for (int32 Example = 0; Example < Space.ExampleClips.Num(); ++Example)
		{
			if (Data.Clips.IsValidIndex(Space.ExampleClips[Example]))
			{
				Duration = FMath::Max(Duration, Data.Clips[Space.ExampleClips[Example]].Duration / FMath::Max(Space.ExampleScales[Example], 0.01f));
			}
		}
	}
	return Fragment.MarkTime(TEXT("NavStop_Exit"), Item.At + Duration / FMath::Max(Item.Speed, 0.01f));
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
		// The rows for the learned nodes (stage P3.5; Combat row_for of the backend): a strike with no such row is no strike of the
		// character (Maelstrom has two strikes in each window, one with the node Gravity and one without it), and only the shapes
		// of those rows query.
		TSet<int32> RowShapes;
		for (const FSwcStrike::FRow& Row : Strike.Rows)
		{
			const bool bNeeds = std::all_of(Row.Needs.begin(), Row.Needs.end(), [this](uint32 Node) { return LearnedNodes.Contains(Node); });
			const bool bForbids = std::none_of(Row.Forbids.begin(), Row.Forbids.end(), [this](uint32 Node) { return LearnedNodes.Contains(Node); });
			if (bNeeds && bForbids)
			{
				RowShapes.Add(Row.Shape);
			}
		}
		if (!Strike.Rows.IsEmpty() && RowShapes.IsEmpty())
		{
			if (!StrikesSeen.Contains(StrikeIndex))
			{
				StrikesSeen.Add(StrikeIndex);
				++SkippedStrikes;
			}
			continue;
		}
		if (!StrikesSeen.Contains(StrikeIndex))
		{
			StrikesSeen.Add(StrikeIndex);
			++StrikeCount;
		}
		for (int32 ShapeIndex = 0; ShapeIndex < Strike.Shapes.Num(); ++ShapeIndex)
		{
			if (!Strike.Rows.IsEmpty() && !RowShapes.Contains(Strike.Shapes[ShapeIndex].DataIndex))
			{
				continue;
			}
			FSwcMeleeShape Shape;
			if (!ShapeOf(Strike.Shapes[ShapeIndex], Shape))
			{
				continue;
			}
			Shape.Visit = VisitNumber;
			Shape.Strike = StrikeIndex;
			Shape.Key = Strike.Key;
			for (const FSwcStrike::FRow& Row : Strike.Rows)
			{
				if (Row.Shape == Strike.Shapes[ShapeIndex].DataIndex && RowShapes.Contains(Row.Shape))
				{
					Shape.DamageRow = Row.Damage;
					break;
				}
			}
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
			if (bContactProbe && ShapeIndex == 0)
			{
				// A receiver that does not exist (actor id 0, facet 0): the backend refuses the claim (a check of list 3).
				swc::net::MeleeContactItem Probe;
				const FVector Point = Shape.Center / 100.0;
				Probe.point = {static_cast<float>(Point.X), static_cast<float>(-Point.Y), static_cast<float>(Point.Z)};
				Probe.normal = {0.0F, 0.0F, 1.0F};
				Probe.direction = {static_cast<float>(Shape.Direction.X), static_cast<float>(-Shape.Direction.Y), static_cast<float>(Shape.Direction.Z)};
				Found.push_back(Probe);
			}
			for (const swc::net::MeleeContactItem& Contact : Found)
			{
				// One contact for each target for each strike of the fragment visit: the target group, not its hit volume.
				if (!Contacted.take(VisitNumber, StrikeIndex, Contact.group))
				{
					continue;
				}
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
	// A consumable use stays (stage MVP-E: the server takes the item and the paperdoll changes; a weapon swap keeps Use).
	Upper.RemoveAll([](const FUpperAction& Action) { return !Action.bUse; });
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
	// The ranged programs of the bow and the staffs (stage P3.6); the driver keeps its player when they are the same.
	if (Driver && Data.IsValid())
	{
		Driver->set_ranged(First != nullptr ? First->Ranged.Get() : nullptr, Second != nullptr ? Second->Ranged.Get() : nullptr, &Data->GetRangedShared());
	}
}

const swc::ranged::Program* USwcPlayerActions::GetRangedProgram(uint32 Slot) const
{
	const FSwcWeaponActions* Weapon = WeaponOf(Slot);
	return Weapon != nullptr ? Weapon->Ranged.Get() : nullptr;
}

void USwcPlayerActions::SetSelfTarget(bool bDown)
{
	if (Driver)
	{
		Driver->set_self_target(bDown);
	}
}

void USwcPlayerActions::OnWeaponKey(uint32 Slot)
{
	ASwcPlayerCharacter* Owner = Character();
	if (Owner == nullptr || !Owner->HasWeapon(Slot) || IsActionLocked())
	{
		return;
	}
	const uint32 Held = Driver ? Driver->snapshot().held : Owner->GetActiveWeaponSlot();
	if (Slot == Held || !Driver)
	{
		// The key of the weapon in the hand draws it (the server has no draw and asks for nothing, protocol notes 4.11).
		if (Owner->GetActiveWeaponSlot() != Slot || !Owner->IsWeaponDrawn())
		{
			// A weapon draw from prone (not a rifle) rises to crouch [D playeractions_sheathe: Unsheathe_MainHand_1_UB].
			if (Owner->GetSwcMovement()->GetPosture() == swc::move::Posture::prone)
			{
				Owner->GetSwcMovement()->ForcePosture(swc::move::Posture::crouch);
			}
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
	if (Owner != nullptr && Owner->GetActiveWeaponSlot() != 0 && !IsActionLocked())
	{
		UE_LOG(LogSwcBackend, Log, TEXT("weapon: sheathe key: slot %u %s"), Owner->GetActiveWeaponSlot(),
			Owner->IsWeaponDrawn() ? TEXT("sheathe") : TEXT("draw"));
		Request(Owner->GetActiveWeaponSlot(), !Owner->IsWeaponDrawn());
	}
}

void USwcPlayerActions::SetSwimming(bool bSwim)
{
	ASwcPlayerCharacter* Owner = Character();
	if (bSwim == bSwimming || Owner == nullptr)
	{
		return;
	}
	if (bSwim)
	{
		if (bButtonDown)
		{
			OnAttackButton(false);
		}
		bSwimUnsheathe = Owner->IsWeaponDrawn() && Owner->GetActiveWeaponSlot() != 0;
		if (bSwimUnsheathe)
		{
			// Both hands sheathe at once, with no clip (the tags Unarmed and AutoUnsheathe of playeractions_swim).
			Upper.Reset();
			for (uint32 Other = 24; Other <= 26; ++Other)
			{
				PlaceSlot(Other, false, true, true);
			}
			Owner->SetLogicalWeaponState(Owner->GetActiveWeaponSlot(), false);
			UE_LOG(LogSwcBackend, Log, TEXT("weapon: the swim sheathes slot %u at once"), Owner->GetActiveWeaponSlot());
		}
		bSwimming = true;
		return;
	}
	bSwimming = false;
	if (bSwimUnsheathe)
	{
		bSwimUnsheathe = false;
		UE_LOG(LogSwcBackend, Log, TEXT("weapon: the end of the swim draws slot %u (AutoUnsheathe)"), Owner->GetActiveWeaponSlot());
		Request(Owner->GetActiveWeaponSlot(), true);
	}
}

void USwcPlayerActions::OnDodgeKey()
{
	// The bit goes for each press; the stamina of the driver decides the dodge at its step, as the server does. No dodge in a wade or
	// a swim (Dodge, Dodge_Stunlock, CanDodgeCancel need IfWaterDepth LessThan Shallow; research wf5-swim/water-uses.json).
	const ASwcPlayerCharacter* Owner = Character();
	const USwcCharacterMovement* Movement = Owner != nullptr ? Cast<USwcCharacterMovement>(Owner->GetCharacterMovement()) : nullptr;
	if (Movement != nullptr && !Movement->CanDodgeInWater())
	{
		UE_LOG(LogSwcBackend, Log, TEXT("dodge: no dodge in the water"));
		return;
	}
	if (Driver && !IsActionLocked() && !GateByUse(3, 0))
	{
		Driver->dodge_press();
	}
}

void USwcPlayerActions::SetLearnedNodes(uint32 Revision, const std::vector<std::uint32_t>& Nodes)
{
	if (Revision != NodesRevision)
	{
		NodesRevision = Revision;
		LearnedNodes.Reset();
		for (const std::uint32_t Node : Nodes)
		{
			LearnedNodes.Add(Node);
		}
	}
}

void USwcPlayerActions::OnAbilityKey(uint32 Place, bool bDown)
{
	if (!bDown && UsePendingKind == 2 && UsePendingPlace == Place)
	{
		bUsePendingRelease = true;
	}
	if (Place >= 3 || (bDown && IsActionLocked()) || bAbilityDown[Place] == bDown)
	{
		return;
	}
	if (bDown && GateByUse(2, Place))
	{
		return;
	}
	bAbilityDown[Place] = bDown;
	if (Driver)
	{
		Driver->ability_key(Place, bDown);
	}
}

void USwcPlayerActions::OnAttackButton(bool bDown)
{
	if (bDown && IsActionLocked())
	{
		return;
	}
	if (!bDown && UsePendingKind == 1)
	{
		bUsePendingRelease = true;
	}
	if (bDown && GateByUse(1, 0))
	{
		return;
	}
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

uint32 USwcPlayerActions::GetConsumingSlot() const
{
	const double Now = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	for (const FUpperAction& Action : Upper)
	{
		if (Action.bUse && Now >= Action.Start)
		{
			return Action.Slot;
		}
	}
	return 0;
}

bool USwcPlayerActions::GateByUse(uint8 Kind, uint32 Place)
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FUpperAction& Action : Upper)
	{
		if (!Action.bUse || Now < Action.Start)
		{
			continue;
		}
		const FSwcActionFragment& Fragment = Data->Fragments[Action.Fragment];
		const float Clock = static_cast<float>(Now - Action.Start);
		const float Buffer = Fragment.MarkTime(TEXT("CanBuffer_UpperBody"), Action.Length);
		if (Clock >= Buffer)
		{
			UsePendingKind = Kind;
			UsePendingPlace = Place;
			bUsePendingRelease = false;
		}
		UE_LOG(LogSwcBackend, Log, TEXT("use: input %u at %.2f s of the use: %s"), Kind, Clock,
			Clock >= Buffer ? TEXT("waits for CanTransition_UpperBody") : TEXT("nothing (before CanBuffer_UpperBody)"));
		return true;
	}
	return false;
}

bool USwcPlayerActions::StartConsumableUse(uint32 PaperdollSlot)
{
	ASwcPlayerCharacter* Owner = Character();
	USwcBackendSubsystem* Backend = Owner != nullptr && Owner->GetGameInstance() != nullptr ? Owner->GetGameInstance()->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	const FSwcWeaponActions* Unarmed = Data.IsValid() ? Data->Weapon(FString()) : nullptr;
	if (Owner == nullptr || Backend == nullptr || Unarmed == nullptr || IsActionLocked())
	{
		return false;
	}
	// The activation conditions of Use_QuickSlot_N: an item in the slot, not prone, not in deep water (the swim), not mounted. The
	// consumable cooldown timer of the condition is not checked: the retail client plays the drink also in a cooldown of the server
	// (protocol notes 4.23, live proof I7 and I12).
	const USwcCharacterMovement* Movement = Owner->GetSwcMovement();
	const swc::move::Posture Posture = Movement->GetPosture();
	if (Posture == swc::move::Posture::prone || Movement->IsSwimming())
	{
		UE_LOG(LogSwcBackend, Log, TEXT("use: quick slot %u: no fragment (prone or swim)"), PaperdollSlot);
		return false;
	}
	swc::net::RepClient::OwnEntity Own;
	const FSwcPlayerLooks* Looks = Backend->GetPlayerLooks();
	if (!Backend->GetLocalPlayer(Own) || Looks == nullptr)
	{
		return false;
	}
	const auto Found = Own.paperdollItems.find(PaperdollSlot);
	if (Found == Own.paperdollItems.end())
	{
		UE_LOG(LogSwcBackend, Log, TEXT("use: quick slot %u is empty: no fragment"), PaperdollSlot);
		return false;
	}
	const FString ItemId = Looks->ItemId(Found->second.index);
	const FString Tag = Data->ConsumableTags.FindRef(ItemId.ToLower());
	const TCHAR* PostureTag = Posture == swc::move::Posture::crouch ? TEXT("Crouch") : TEXT("");
	// The fragment Use of the tag and the posture; a tag with no variant (Boost) takes the variant with no tag (unarmed_drink_ub).
	int32 Fragment = INDEX_NONE;
	for (const FString& Key : {FString::Printf(TEXT("use|%s|%s"), PostureTag, *Tag), FString::Printf(TEXT("use|%s|"), PostureTag),
			 FString::Printf(TEXT("use||%s"), *Tag), FString(TEXT("use||"))})
	{
		if (const int32* Index = Unarmed->Use.Find(Key))
		{
			Fragment = *Index;
			break;
		}
	}
	if (Fragment == INDEX_NONE)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("use: quick slot %u (%s, tag %s): no fragment Use (tools/convert-player.sh actions)"), PaperdollSlot, *ItemId, *Tag);
		return false;
	}
	FUpperAction Action;
	Action.Slot = PaperdollSlot;
	Action.bUse = true;
	Action.Fragment = Fragment;
	Action.Start = GetWorld()->GetTimeSeconds();
	Action.Serial = ++UpperSerial;
	Action.Length = Data->Fragments[Fragment].Length > 0.0f ? Data->Fragments[Fragment].Length : 1.0f;
	Upper.Add(Action);
	UE_LOG(LogSwcBackend, Log, TEXT("use: quick slot %u (%s, tag %s): %s, %.2f s"), PaperdollSlot, *ItemId, Tag.IsEmpty() ? TEXT("none") : *Tag,
		*Data->Fragments[Fragment].Key, Action.Length);
	return true;
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
	// A consumable use stays (stage MVP-E: the server takes the item and the paperdoll changes; a weapon swap keeps Use).
	Upper.RemoveAll([](const FUpperAction& Action) { return !Action.bUse; });
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
	// A consumable use stays (stage MVP-E: the server takes the item and the paperdoll changes; a weapon swap keeps Use).
	Upper.RemoveAll([](const FUpperAction& Action) { return !Action.bUse; });
	for (uint32 Other = 24; Other <= 26; ++Other)
	{
		PlaceSlot(Other, Other == Slot, true, true);
	}
	Owner->SetLogicalWeaponState(Slot, true);
}

void USwcPlayerActions::UpdateBlock(double Now)
{
	ASwcPlayerCharacter* Owner = Character();
	USwcCharacterMovement* Movement = Owner != nullptr ? Owner->GetSwcMovement() : nullptr;
	if (Movement == nullptr || !Data.IsValid())
	{
		return;
	}
	// The weapons of the block: the tags 1H_Melee, 2H_Melee, MagicStaff, Ice of Block_UpperBody (a void gauntlet blocks only with
	// TEMP_WeaponMaster: no block here), with the fragments of the block [D playeractions_block].
	const auto CanBlockWith = [](const FSwcWeaponActions* Weapon)
	{
		return Weapon != nullptr && Weapon->BlockRaise != INDEX_NONE && Weapon->Tags.ContainsByPredicate([](const FString& Tag)
		{
			return Tag.Equals(TEXT("1H_Melee"), ESearchCase::IgnoreCase) || Tag.Equals(TEXT("2H_Melee"), ESearchCase::IgnoreCase)
				|| Tag.Equals(TEXT("MagicStaff"), ESearchCase::IgnoreCase) || Tag.Equals(TEXT("Ice"), ESearchCase::IgnoreCase);
		});
	};
	const swc::combat::Driver::Snapshot Snapshot = Driver ? Driver->snapshot() : swc::combat::Driver::Snapshot();
	const bool bDodging = Snapshot.dodge && Snapshot.dodge->number != DodgeReleased;
	// The conditions of Block_UpperBody: block_active_virtual (the right button held), no prone, the stamina above 0 and no
	// Exhausted, alive; an attack, a dodge, or a draw on the UpperBody channel holds the block back (the client keeps one action).
	const bool bFree = !IsActionLocked() && !bPlaying && !bDodging && !IsUsingConsumable() && Movement->GetPosture() != swc::move::Posture::prone
		&& Snapshot.stamina > 0.0f && !Snapshot.exhausted;
	if (bBlocking)
	{
		// The end: the button up after Block_ForceBlock_Timer (0.3 s, the status of the data); also the end of the conditions.
		const FSwcWeaponActions* Weapon = VisualSlot == BlockSlot ? WeaponOf(BlockSlot) : nullptr;
		const bool bRelease = !bAimHeld && Now >= BlockForceUntil;
		if (bRelease || !bFree || !CanBlockWith(Weapon))
		{
			bBlocking = false;
			UE_LOG(LogSwcBackend, Log, TEXT("actions: block off after %.3f s (%s)"), Now - BlockStart,
				bRelease ? TEXT("the button up") : !bFree ? TEXT("an action or a state ends it") : TEXT("no weapon of the block"));
		}
	}
	else if (bAimHeld && bFree && Upper.IsEmpty())
	{
		uint32 Slot = 0;
		if (Owner->IsWeaponDrawn() && CanBlockWith(WeaponOf(VisualSlot)))
		{
			Slot = VisualSlot;
		}
		else if (!Owner->IsWeaponDrawn() && CanBlockWith(WeaponOf(Owner->GetActiveWeaponSlot())))
		{
			// Trigger_From_Sheathed: the block of a sheathed melee weapon draws it at once (SheatheWeapon UnSheathe of Block_UpperBody).
			Slot = Owner->GetActiveWeaponSlot();
			DrawAtOnce(Slot);
		}
		if (Slot != 0)
		{
			bBlocking = true;
			BlockSlot = Slot;
			BlockStart = Now;
			// Block_ForceBlock_Timer: 0.3 s (the status table of the backend copy, ability-data.json).
			BlockForceUntil = Now + 0.3;
			++BlockSerial;
			UE_LOG(LogSwcBackend, Log, TEXT("actions: block on, slot %u: %s, then %s"), Slot, *Data->Fragments[WeaponOf(Slot)->BlockRaise].Key,
				WeaponOf(Slot)->BlockLoop != INDEX_NONE ? *Data->Fragments[WeaponOf(Slot)->BlockLoop].Key : TEXT("(no hold fragment)"));
		}
	}
	Movement->SetBlocking(bBlocking);
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
				if (Action.bUse)
				{
					// The deactivation of Use_QuickSlot_N: IfFragmentDone or IfMannequinMarker Exit_Use.
					if (Name.Equals(TEXT("Exit_Use"), ESearchCase::IgnoreCase))
					{
						Action.Length = FMath::Min(Action.Length, Clock);
					}
				}
				else if (Action.bDraw && Name.Equals(TEXT("unsheathe_weapon"), ESearchCase::IgnoreCase))
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
		if (Action.bUse && UsePendingKind != 0 && Clock >= Data->Fragments[Action.Fragment].MarkTime(TEXT("CanTransition_UpperBody"), Action.Length))
		{
			// The waiting input replaces the use at CanTransition_UpperBody.
			Action.Length = Clock;
		}
		if (Clock >= Action.Length)
		{
			const bool bUseEnded = Action.bUse;
			Upper.RemoveAt(Index);
			if (bUseEnded && UsePendingKind != 0)
			{
				const uint8 Kind = UsePendingKind;
				UsePendingKind = 0;
				UE_LOG(LogSwcBackend, Log, TEXT("use: the waiting input %u starts at %.2f s of the use"), Kind, Clock);
				if (Kind == 1)
				{
					OnAttackButton(true);
					if (bUsePendingRelease)
					{
						OnAttackButton(false);
					}
				}
				else if (Kind == 2)
				{
					OnAbilityKey(UsePendingPlace, true);
					if (bUsePendingRelease)
					{
						OnAbilityKey(UsePendingPlace, false);
					}
				}
				else
				{
					OnDodgeKey();
				}
			}
			continue;
		}
		if (Action.Fragment != INDEX_NONE)
		{
			Shown = &Action;
			ShownClock = Clock;
		}
		++Index;
	}
	if (Anim != nullptr && Shown == nullptr && bBlocking)
	{
		// The block on the UpperBody channel: Block_UpperBody_Raise to its end, then Block_UpperBody (Block_Continue_UpperBody)
		// while the block lasts. Serials of their own (the high bits): never the serial of a draw.
		const FSwcWeaponActions* Weapon = WeaponOf(BlockSlot);
		const float Clock = static_cast<float>(Now - BlockStart);
		const float RaiseLength = Weapon != nullptr ? Data->Fragments[Weapon->BlockRaise].Length : 0.0f;
		const bool bRaise = Weapon != nullptr && (Clock < RaiseLength || Weapon->BlockLoop == INDEX_NONE);
		const int32 Fragment = Weapon == nullptr ? INDEX_NONE : bRaise ? Weapon->BlockRaise : Weapon->BlockLoop;
		Anim->SetAction(1, Fragment, 0xC0000000u | (BlockSerial << 1) | (bRaise ? 0u : 1u), bRaise ? Clock : Clock - RaiseLength);
	}
	else if (Anim != nullptr)
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
	if (bStart)
	{
		HomingFragment = nullptr;
	}
	// The Homing clip of the fragment (stage P3.G, research p3g-collision): while it runs and has a target, the root motion stops and
	// the character moves to the target in MoveToDuration.
	if (MoveHoming(Fragment, To, DeltaTime))
	{
		return;
	}
	FVector Root = FVector::ZeroVector;
	Data->RootDelta(Fragment, From, To, Root);
	if (Fragment.Name.StartsWith(TEXT("Dodge_Roll")))
	{
		// The dodge has ApplyMoveSpeedMultiplier SprintSpeedMod with ClampMax 1.0: a slow shortens it (stage P3.3).
		Root *= Movement->GetLimits().dodge_multiplier();
	}
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

bool USwcPlayerActions::MoveHoming(const FSwcActionFragment& Fragment, float Clock, float DeltaTime)
{
	ASwcPlayerCharacter* Owner = Character();
	USwcCharacterMovement* Movement = Owner->GetSwcMovement();
	int32 ClipIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Fragment.Homing.Num(); ++Index)
	{
		const FSwcHoming& Clip = Fragment.Homing[Index];
		const float End = Clip.End >= 0.0f ? Clip.End : (Fragment.Length > 0.0f ? Fragment.Length : TNumericLimits<float>::Max());
		if (Clip.bMoveToTarget && Clock >= Clip.Start && Clock < End)
		{
			ClipIndex = Index;
		}
	}
	if (ClipIndex == INDEX_NONE)
	{
		return false;
	}
	const FSwcHoming& Clip = Fragment.Homing[ClipIndex];
	const FVector Feet = Owner->GetActorLocation();
	if (HomingFragment != &Fragment || HomingClip != ClipIndex)
	{
		// The start: the target in Radius, MaxAngle of the facing, and Height (the score: the distance / Radius + the angle / MaxAngle, the
		// weights 1 of the data; an inference of the research), its point T on the homing segment of the creature, the goal 1 m before T
		// (MoveToDistance), at most MaxMoveDistance from the start.
		HomingFragment = &Fragment;
		HomingClip = ClipIndex;
		bHomingTarget = false;
		TArray<FSwcCreatureTarget> Targets;
		if (const USwcCreatureSubsystem* Creatures = GetWorld()->GetSubsystem<USwcCreatureSubsystem>())
		{
			Creatures->GetTargets(Targets);
		}
		const FVector Ahead = Owner->GetActorForwardVector().GetSafeNormal2D();
		float Best = TNumericLimits<float>::Max();
		for (const FSwcCreatureTarget& Target : Targets)
		{
			const FVector Point = FMath::ClosestPointOnSegment(Feet, Target.AxisA, Target.AxisB);
			const FVector To2D = (Point - Feet) * FVector(1.0, 1.0, 0.0);
			const float Distance = static_cast<float>(To2D.Size() / 100.0);
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Ahead, To2D.GetSafeNormal()), -1.0, 1.0)));
			if (Target.bDead || Distance > Clip.Radius || Angle > Clip.MaxAngle || FMath::Abs(Point.Z - Feet.Z) / 100.0 > Clip.Height)
			{
				continue;
			}
			const float Score = Distance / FMath::Max(Clip.Radius, 0.01f) + Angle / FMath::Max(Clip.MaxAngle, 0.01f);
			if (Score < Best)
			{
				Best = Score;
				bHomingTarget = true;
				HomingPoint = Point;
			}
		}
		HomingStart = Feet;
		if (bHomingTarget)
		{
			const FVector Toward = ((HomingPoint - Feet) * FVector(1.0, 1.0, 0.0)).GetSafeNormal();
			const FVector Goal = HomingPoint - Toward * Clip.MoveToDistance * 100.0;
			HomingGoal = Feet + ((Goal - Feet) * FVector(1.0, 1.0, 0.0)).GetClampedToMaxSize(Clip.MaxMoveDistance * 100.0);
			UE_LOG(LogSwcBackend, Log, TEXT("attack: homing of %s at %.3f s: a target %.2f m away, the goal %.2f m from it"), *Fragment.Name, Clock,
				FVector::Dist2D(Feet, HomingPoint) / 100.0, FVector::Dist2D(HomingGoal, HomingPoint) / 100.0);
		}
	}
	if (!bHomingTarget)
	{
		return false;
	}
	// The smoothstep from the start to the goal in MoveToDuration (no upward motion: horizontal only), then no motion to the end of the
	// clip; the turn to the target point (TurnRate, exponential: the fraction for each 1/30 s).
	const float X = FMath::Clamp((Clock - Clip.Start) / FMath::Max(Clip.MoveToDuration, 0.001f), 0.0f, 1.0f);
	const FVector Wanted = HomingStart + (HomingGoal - HomingStart) * (X * X * (3.0f - 2.0f * X));
	const FVector Step = (Wanted - Feet) * FVector(1.0, 1.0, 0.0);
	Movement->SetActionMotion(true, DeltaTime > 0.0f ? Step / DeltaTime : FVector::ZeroVector);
	const double TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(HomingPoint.Y - Feet.Y, HomingPoint.X - Feet.X));
	const float Alpha = Clip.TurnRate >= 1.0f ? 1.0f : 1.0f - FMath::Pow(1.0f - Clip.TurnRate, 30.0f * DeltaTime);
	const double Yaw = Owner->GetActorRotation().Yaw;
	Owner->SetActorRotation(FRotator(0.0, Yaw + FMath::FindDeltaAngleDegrees(Yaw, TargetYaw) * Alpha, 0.0));
	return true;
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
	// The keys of the bar (stage P3.5): the verdict of the client for each press, and the shots of the casts.
	if (Snapshot.abilityPresses != SeenAbilityPresses && Snapshot.lastPress)
	{
		SeenAbilityPresses = Snapshot.abilityPresses;
		static const TCHAR* const Keys[] = {TEXT("Q"), TEXT("R"), TEXT("F")};
		const swc::combat::Driver::Snapshot::AbilityPress& Press = *Snapshot.lastPress;
		const swc::combat::Ability* Ability = Snapshot.graph != nullptr ? Snapshot.graph->ability(Press.ability) : nullptr;
		const auto Running = Snapshot.cooldowns.find(Press.ability);
		if (Snapshot.ranged.program && Snapshot.graph == nullptr)
		{
			// A ranged weapon (stage P3.6): the ranged player plays the key from the action lists of the data (the "ranged:" lines).
			UE_LOG(LogSwcBackend, Log, TEXT("ability: key %s (place %u) %08x at input %u: to the ranged program%s"), Keys[FMath::Min(Press.place, 2u)], Press.place,
				Press.ability, static_cast<unsigned>(Press.input),
				Running != Snapshot.cooldowns.end() && Running->second > Snapshot.inputNs
					? *FString::Printf(TEXT(", own cooldown %.1f s left"), static_cast<double>(Running->second - Snapshot.inputNs) / 1e9)
					: TEXT(""));
		}
		else
		{
			UE_LOG(LogSwcBackend, Log, TEXT("ability: key %s (place %u) %s %08x at input %u: %s%s"), Keys[FMath::Min(Press.place, 2u)], Press.place,
				Ability != nullptr ? UTF8_TO_TCHAR(Ability->name.c_str()) : TEXT("(none)"), Press.ability, static_cast<unsigned>(Press.input),
				UTF8_TO_TCHAR(swc::combat::press_verdict_text(Press.verdict)),
				Press.verdict == swc::combat::PressVerdict::played
					? *FString::Printf(TEXT(", %s at input %u, cast %llu, own cooldown %.1f s"),
						Press.fragment >= 0 ? UTF8_TO_TCHAR(Snapshot.graph->fragments[static_cast<std::size_t>(Press.fragment)].name.c_str()) : TEXT("?"),
						static_cast<unsigned>(Press.at), static_cast<unsigned long long>(Press.cast),
						Running != Snapshot.cooldowns.end() ? static_cast<double>(Running->second - Snapshot.inputNs) / 1e9 : 0.0)
					: Running != Snapshot.cooldowns.end() ? *FString::Printf(TEXT(", %.1f s left"), static_cast<double>(Running->second - Snapshot.inputNs) / 1e9)
														  : TEXT(""));
		}
	}
	if (Snapshot.shots != SeenShots)
	{
		SeenShots = Snapshot.shots;
		UE_LOG(LogSwcBackend, Log, TEXT("ability: the shot of the cast at input %u (list 2 of the input entry)"), static_cast<unsigned>(Snapshot.input));
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
	bAttackBeforeTransition = false;
	// The collision of the character in a fragment (character_collided: the uppercut of Charge at a creature).
	if (const uint32 Blocks = Movement->GetCreatureBlocks(); Blocks != SeenBlocks)
	{
		SeenBlocks = Blocks;
		if (bPlays && Driver)
		{
			Driver->collided();
			if (CollidedVisit != Newest->number)
			{
				CollidedVisit = Newest->number;
				UE_LOG(LogSwcBackend, Log, TEXT("attack: the character collided with a creature in visit %u %s at input %u"), Newest->number,
					UTF8_TO_TCHAR(Weapon->Graph.fragments[Newest->fragment].name.c_str()), static_cast<unsigned>(Snapshot.input));
			}
		}
	}
	const bool bStopNow = bDodgeStop;
	bDodgeStop = false;
	if (!bPlays)
	{
		if (bPlaying)
		{
			bPlaying = false;
			Movement->EndActionMotion(CarryLimit);
			if (bStopNow)
			{
				// The end of a dodge with no move key: the character stops at once (the retail dodges of set6-combat/
				// player-dodge-stamina-2.json: 0.297 m each step to CanTransition, then no motion).
				Movement->ClearMotion();
			}
		}
		// The fragment of the ranged scope (stage P3.6) on the FullBody channel: its clips are upper-body clips, the legs keep the
		// movement; no root motion. A start ends autorun and draws the weapon of the program (an attack with a sheathed weapon).
		const swc::combat::Driver::Snapshot::Ranged& Ranged = Snapshot.ranged;
		const int32 RangedFragment = Ranged.program && !Ranged.record.empty() ? Data->FragmentOfRecord(UTF8_TO_TCHAR(Ranged.record.c_str())) : INDEX_NONE;
		if (RangedFragment != INDEX_NONE)
		{
			if (Ranged.serial != RangedSerial)
			{
				RangedSerial = Ranged.serial;
				UE_LOG(LogSwcBackend, Log, TEXT("ranged: %s (action %s) at input %u"), UTF8_TO_TCHAR(Ranged.fragment.c_str()), UTF8_TO_TCHAR(Ranged.action.c_str()),
					static_cast<unsigned>(Ranged.start));
				Movement->CancelAutoRun(TEXT("an attack"));
				if (VisualSlot != Snapshot.held || !Owner->IsWeaponDrawn() || Owner->GetActiveWeaponSlot() != Snapshot.held)
				{
					DrawAtOnce(Snapshot.held);
				}
			}
			if (Anim != nullptr)
			{
				// A serial of its own (the high bit) so that a ranged fragment never takes the serial of an attack visit.
				Anim->SetAction(0, RangedFragment, 0x80000000U | Ranged.serial, Ranged.clock + Since);
			}
			// The turn to the camera of a CameraRelativeFacing of the action (AlignToCamera; an exponential rate of the data).
			if (Ranged.facingRate > 0.0f)
			{
				FSwcFacingRule Rule;
				Rule.TurnRate = Ranged.facingRate;
				Rule.bExponential = true;
				Owner->SetActorRotation(FRotator(0.0, TurnYaw(Owner->GetActorRotation().Yaw, Movement->GetCameraYaw(), Rule, DeltaTime), 0.0));
			}
			return;
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
		// An attack ends autorun (AttackAction) and stands the character up from crouch or prone (the attack actions set Stand;
		// stage P3.3).
		Movement->CancelAutoRun(TEXT("an attack"));
		Movement->ForcePosture(swc::move::Posture::stand);
		LastClock = 0.0f;
		StrikeClock = -1.0f;
		StrikesSeen.Reset();
		LastCenters.Reset();
		Contacted.reset();
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
	bAttackBeforeTransition = !swc::combat::mark_active(Data->Fragments[Fragment].Marks, "CanTransition", Clock);
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
	// The end: the mark CanTransition (the action Dodge ends at IfFragmentDone and CanTransition; the retail dodges end at 0.5 s with
	// or without a move key: set6-combat/player-dodge-stamina-2.json, the root track x FastRoll_Translation 1.15 to 0.5 s, then the
	// next state), or the end of the animation. With no move key the character stops at once; with a key the movement goes on
	// (SkipSprintStartTimer: the sprint at once).
	const float Transition = Fragment.MarkTime(TEXT("CanTransition"), 0.5f);
	if ((Fragment.Length > 0.0f && Clock >= Fragment.Length) || Clock >= Transition)
	{
		DodgeReleased = Dodge.number;
		bDodgeStop = Movement->GetMoveInput().IsNearlyZero();
		Movement->SkipSprintStart();
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

void USwcPlayerActions::SetLife(bool bDead, bool bLoading)
{
	if (bLoading != LifeState.loading())
	{
		UE_LOG(LogSwcBackend, Log, TEXT("life: the loading screen %s"), bLoading ? TEXT("shows (the input is locked)") : TEXT("ends"));
	}
	switch (LifeState.set(bDead, bLoading))
	{
	case swc::combat::Life::Event::death:
		// The action Death: the attack and the aim end; the weapons go to their sheaths (SheatheWeapon of the main and the off
		// hand, the tag Unarmed); DisableInteractor, DisableCameraLock.
		SheatheForAction();
		StartLifeFragment(&FSwcWeaponActions::Life, TEXT("death"));
		break;
	case swc::combat::Life::Event::waiting:
		UE_LOG(LogSwcBackend, Log, TEXT("life: alive again; the pose of the death holds until the loading screen ends"));
		break;
	case swc::combat::Life::Event::respawn:
		// A respawn has the full stamina (Stamina::refill: a revival, a respawn).
		if (Driver)
		{
			Driver->refill();
		}
		StartLifeFragment(&FSwcWeaponActions::Life, TEXT("respawn"));
		break;
	default:
		break;
	}
}

void USwcPlayerActions::SheatheForAction()
{
	ASwcPlayerCharacter* Owner = Character();
	bButtonDown = false;
	bAimHeld = false;
	if (Driver)
	{
		Driver->button(false);
		Driver->set_aim(false);
		Driver->set_weapons(static_cast<const swc::combat::Graph*>(DriverGraphs[0]), static_cast<const swc::combat::Graph*>(DriverGraphs[1]), DriverHeld);
	}
	Upper.Reset();
	if (Owner != nullptr && Owner->IsWeaponDrawn())
	{
		Owner->SetWeaponState(Owner->GetActiveWeaponSlot(), false);
		OnGearChanged();
	}
	if (USwcPlayerAnimInstance* Anim = Animation())
	{
		Anim->SetAction(1, INDEX_NONE, 0, 0.0f);
	}
	bPlaying = false;
}

void USwcPlayerActions::SetTimedUse(bool bOn, const FString& Kind, uint32 ToolSlot, const FVector* FacePlace)
{
	// InteractionAlignment (playeractions_interact DelayedInteraction, playeractions_gather Gather): AlignFacingBlendTime 0.2 s. The
	// character turns to the object in that time (a linear turn; the position part, AlignPositionSpeed, is not in the client: the
	// object is in range already).
	const auto Align = [this, FacePlace]()
	{
		ASwcPlayerCharacter* Owner = Character();
		if (FacePlace == nullptr || Owner == nullptr)
		{
			return;
		}
		constexpr float AlignFacingBlendTime = 0.2f;
		const FVector To = *FacePlace - Owner->GetActorLocation();
		if (To.SizeSquared2D() < 1.0)
		{
			return;
		}
		AlignYaw = To.Rotation().Yaw;
		AlignRate = static_cast<float>(FMath::Max(FMath::Abs(FMath::FindDeltaAngleDegrees(Owner->GetActorRotation().Yaw, AlignYaw)) / AlignFacingBlendTime, 1.0));
		bAlignFacing = true;
	};
	// A gather fragment that the data does not have: the default gather fragment, else the kneel.
	const FSwcWeaponActions* Unarmed = Data.IsValid() ? Data->Weapon(FString()) : nullptr;
	const FString Played = Unarmed == nullptr || Unarmed->Use.Contains(Kind) ? Kind
		: Kind.StartsWith(TEXT("gather|")) && Unarmed->Use.Contains(TEXT("gather|None|")) ? FString(TEXT("gather|None|")) : FString(TEXT("delayed"));
	if (!LifeState.set_timed_use(bOn))
	{
		// A new use while one plays (a gather after a gather): its fragment and its tool.
		if (bOn && !TimedUseKind.IsEmpty() && Played != TimedUseKind)
		{
			TimedUseKind = Played;
			StartLifeFragment(&FSwcWeaponActions::Use, *Played);
			if (ASwcPlayerCharacter* Owner = Character())
			{
				Owner->SetToolInHand(ToolSlot);
			}
			Align();
		}
		return;
	}
	if (bOn)
	{
		// The action DelayedInteraction: "Instant Sheathe-All", then Delayed_Interact (the kneel) while the request lasts. The action
		// Gather: the weapons go to their sheaths, the tool of the row goes into the hand, and its Gather fragment plays.
		SheatheForAction();
		TimedUseKind = Played;
		StartLifeFragment(&FSwcWeaponActions::Use, *Played);
		if (ASwcPlayerCharacter* Owner = Character())
		{
			Owner->SetToolInHand(ToolSlot);
		}
		Align();
		return;
	}
	TimedUseKind.Reset();
	bAlignFacing = false;
	if (ASwcPlayerCharacter* Owner = Character())
	{
		Owner->GetSwcMovement()->EndActionMotion(0.0f);
		Owner->SetToolInHand(0);
	}
	if (USwcPlayerAnimInstance* Anim = Animation())
	{
		Anim->SetAction(0, INDEX_NONE, 0, 0.0f);
	}
	UE_LOG(LogSwcBackend, Log, TEXT("actions: the timed use ends"));
}

void USwcPlayerActions::StartLifeFragment(const TMap<FString, int32> FSwcWeaponActions::* Map, const TCHAR* Kind)
{
	++LifeSerial;
	const FSwcWeaponActions* Unarmed = Data.IsValid() ? Data->Weapon(FString()) : nullptr;
	LifeFragment = Unarmed != nullptr ? (Unarmed->*Map).FindRef(Kind, INDEX_NONE) : INDEX_NONE;
	UE_LOG(LogSwcBackend, Log, TEXT("life: %s, fragment %s"), Kind,
		LifeFragment != INDEX_NONE ? *Data->Fragments[LifeFragment].Key : TEXT("none (tools/convert-player.sh actions)"));
}

bool USwcPlayerActions::UpdateLife(float DeltaTime)
{
	if (!LifeState.owns_body())
	{
		return false;
	}
	ASwcPlayerCharacter* Owner = Character();
	USwcCharacterMovement* Movement = Owner->GetSwcMovement();
	const FSwcActionFragment* Fragment = LifeFragment != INDEX_NONE ? &Data->Fragments[LifeFragment] : nullptr;
	const float Length = Fragment != nullptr && Fragment->Length > 0.0f ? Fragment->Length : 0.0f;
	const float Transition = Fragment != nullptr ? Fragment->MarkTime(TEXT("CanTransition"), Length) : 0.0f;
	const swc::combat::Life::Event Event = LifeState.advance(DeltaTime, Transition);
	if (Event == swc::combat::Life::Event::free)
	{
		Movement->EndActionMotion(0.0f);
		if (USwcPlayerAnimInstance* Anim = Animation())
		{
			Anim->SetAction(0, INDEX_NONE, 0, 0.0f);
		}
		UE_LOG(LogSwcBackend, Log, TEXT("life: the respawn reached CanTransition (%.2f s): the input is free"), LifeState.clock());
		return false;
	}
	// The clips play in place (ReactionMotion has no root track in this client): the character does not move on the ground.
	Movement->SetActionMotion(true, FVector::ZeroVector);
	// The turn of InteractionAlignment at the start of a timed use (SetTimedUse).
	if (bAlignFacing && LifeState.timed_use())
	{
		FSwcFacingRule Rule;
		Rule.TurnRate = AlignRate;
		const double Yaw = TurnYaw(Owner->GetActorRotation().Yaw, AlignYaw, Rule, DeltaTime);
		Owner->SetActorRotation(FRotator(0.0, Yaw, 0.0));
		bAlignFacing = FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, AlignYaw)) > 0.5;
	}
	if (USwcPlayerAnimInstance* Anim = Animation())
	{
		// A clip that ends holds its last pose (the death lies on the ground until the respawn); the kneel of a timed use loops. A gather
		// plays its Gather fragment (one swing in the data) again while the work lasts (stage P5.2; the repeat is an inference).
		const float Clock = LifeState.clock();
		const bool bGather = LifeState.timed_use() && TimedUseKind.StartsWith(TEXT("gather|")) && Length > 0.0f;
		Anim->SetAction(0, LifeFragment, 0x80000000u | LifeSerial,
			bGather ? FMath::Fmod(Clock, Length) : Length > 0.0f && !LifeState.timed_use() ? FMath::Min(Clock, Length) : Clock);
	}
	return true;
}

void USwcPlayerActions::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Data.IsValid() || Character() == nullptr)
	{
		return;
	}
	if (UpdateLife(DeltaTime))
	{
		// The death or the respawn owns the body: no attack, no aim, no combat rates, no lock.
		bAiming = false;
		bLockAllowed = false;
		bBlocking = false;
		Character()->GetSwcMovement()->SetBlocking(false);
		Character()->GetSwcMovement()->SetCombat(false, false);
		Character()->GetSwcMovement()->SetWeaponTags({});
		if (Driver)
		{
			Driver->set_aim(false);
		}
		return;
	}
	UpdateAttack(DeltaTime);
	UpdateBlock(GetWorld()->GetTimeSeconds());
	UpdateUpper(GetWorld()->GetTimeSeconds());
	// The weapon in the hand: the armed rates of the movement and the aim of a ranged weapon (stage P2.6, the tags of the
	// conditions Stand+Armed, Combat_Nav_Stand, CombatNav+Melee, CombatNav+Magic of the action lists).
	const FSwcWeaponActions* Drawn = VisualSlot != 0 ? WeaponOf(VisualSlot) : nullptr;
	const auto Has = [Drawn](std::initializer_list<const TCHAR*> Names)
	{
		for (const TCHAR* Name : Names)
		{
			if (Drawn != nullptr && Drawn->Tags.ContainsByPredicate([Name](const FString& Tag) { return Tag.Equals(Name, ESearchCase::IgnoreCase); }))
			{
				return true;
			}
		}
		return false;
	};
	// The weapon of the active slot, drawn or sheathed: the combat stance of the native Nav (research p3g-move 4: the item of the
	// active weapon slot, not the drawn state).
	const uint32 ActiveSlot = Character()->GetActiveWeaponSlot();
	const FSwcWeaponActions* Active = ActiveSlot != 0 && Character()->HasWeapon(ActiveSlot) ? WeaponOf(ActiveSlot) : nullptr;
	const bool bArmedNav = Active != nullptr && Active->Tags.ContainsByPredicate([](const FString& Tag)
	{
		for (const TCHAR* Name : {TEXT("1H_Melee"), TEXT("2H_Melee"), TEXT("1H_Ranged"), TEXT("2H_Ranged"), TEXT("Bow"), TEXT("MagicGauntlet"), TEXT("MagicStaff")})
		{
			if (Tag.Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	});
	const bool bWasAiming = bAiming;
	bAiming = bAimHeld && !bPlaying && Has({TEXT("1H_Ranged"), TEXT("2H_Ranged"), TEXT("Bow")});
	// The ranged actions (stage P3.6): the aim is the tag AimDownSights of the actions of the data (the bow aims from the ready fragment
	// to the lower fragment), and a cast walks in its mark ForceWalk.
	const swc::combat::Driver::Snapshot::Ranged Ranged = Driver ? Driver->snapshot().ranged : swc::combat::Driver::Snapshot::Ranged();
	if (Ranged.program)
	{
		bAiming = Ranged.aimDownSights;
	}
	if (bAiming != bWasAiming)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("actions: aim %s"), bAiming ? TEXT("on (AimDownSights)") : TEXT("off"));
	}
	Character()->GetSwcMovement()->SetCombat(bArmedNav, bAiming || (Ranged.program && Ranged.forceWalk));
	// The tags of the weapon in the hand for the conditions of the movement data (stage P3.3; unarmed: none), and of the weapon of
	// the active slot for the combat stance.
	Character()->GetSwcMovement()->SetWeaponTags(Drawn != nullptr ? Drawn->Tags : TArray<FString>());
	Character()->GetSwcMovement()->SetStanceTags(Active != nullptr ? Active->Tags : TArray<FString>());
	// The end of Sprint_Start: the CAGE-TransitionInstant of the stance fragment of the weapon in the hand (1.3667 s for a 1H ranged
	// weapon, 1.4 s for the others; research p3g-move adb-sprint-start.txt), else the unarmed fragment.
	const int32* SprintStart = Drawn != nullptr ? Drawn->Stance.Find(TEXT("Sprint_Start")) : nullptr;
	Character()->GetSwcMovement()->SetSprintStartTransition(SprintStart != nullptr ? Data->Fragments[*SprintStart].MarkTime(TEXT("TransitionInstant"), 0.0f) : 0.0f);
	// The lengths of the Nav_Stop fragments of the weapon in the hand (AST-199; the NavType tags Walk, Run, Sprint), else the unarmed ones.
	float StopLengths[3] = {0.0f, 0.0f, 0.0f};
	const TCHAR* const NavTypes[3] = {TEXT("Nav_Stop+Walk"), TEXT("Nav_Stop+Run"), TEXT("Nav_Stop+Sprint")};
	for (int32 Type = 0; Type < 3; ++Type)
	{
		const int32* Stop = Drawn != nullptr ? Drawn->Stance.Find(NavTypes[Type]) : nullptr;
		StopLengths[Type] = Stop != nullptr ? NavStopLength(*Data, Data->Fragments[*Stop]) : 0.0f;
		// AST-204: the marks PreventNav and ResumeNav of the fragment (a move input between them does not start the Nav).
		Character()->GetSwcMovement()->SetNavStopHold(Type, Stop != nullptr ? Data->Fragments[*Stop].MarkTime(TEXT("PreventNav"), 0.0f) : 0.0f,
			Stop != nullptr ? Data->Fragments[*Stop].MarkTime(TEXT("ResumeNav"), 0.0f) : 0.0f);
	}
	Character()->GetSwcMovement()->SetNavStopLengths(StopLengths[0], StopLengths[1], StopLengths[2]);
	// Prone sheathes the weapons, but a rifle (the musket): the transitions to prone set Unarmed and AutoUnsheathe [D idleposetransition].
	const bool bProne = Character()->GetSwcMovement()->GetPosture() == swc::move::Posture::prone;
	if (bProne && Drawn != nullptr && !Has({TEXT("Rifle")}) && Character()->IsWeaponDrawn() && !bProneSheathe)
	{
		bProneSheathe = true;
		UE_LOG(LogSwcBackend, Log, TEXT("weapon: prone sheathes slot %u"), Character()->GetActiveWeaponSlot());
		Request(Character()->GetActiveWeaponSlot(), false);
	}
	bProneSheathe = bProne && bProneSheathe;
	if (Driver)
	{
		// The right button of a ranged program goes to its player as it is (aim_draw, block_active_virtual of the data).
		Driver->set_aim(Ranged.program ? bAimHeld && !IsActionLocked() : bAiming);
		// The bits of the right button (each weapon, as the retail client: the aim of the bow and the block of the rapier send the
		// same bits, set6-combat/player-bow.json and player-bitten-block.json).
		Driver->set_right_button(bAimHeld && !IsInputLocked());
	}
	bLockAllowed = !bAiming && !bAttackBeforeTransition && !IsInputLocked();
	if (USwcPlayerAnimInstance* Anim = Animation())
	{
		Anim->SetStance(VisualSlot != 0 ? WeaponOf(VisualSlot) : nullptr, bVisualOffhand);
		// The weapon scope (AST-202): the weapon of the stance plays the weapon records of the fragments.
		Character()->SetAnimatedWeapon(VisualSlot, Data, Anim);
		// The aim stance of the bow (the tags Aim and ChargedAim of the ranged player: player_bow.adb Idle and Strafe Bow+Aim,
		// Bow+ChargedAim).
		Anim->SetStanceVariant(Ranged.program && Ranged.chargedAim ? TEXT("+ChargedAim") : Ranged.program && Ranged.aim ? TEXT("+Aim") : TEXT(""));
	}
}

int32 USwcPlayerActions::GetGatherImpacts() const
{
	// The Gather fragment plays again while the work lasts (UpdateLife): one moment of impact in each play, at the start of its mark
	// GatherMomentOfImpact (player_axe.adb Axe+Chopping 0.467 s of 1.167 s, player_pick.adb 2H_Melee+Pick+Mining 0.867 s of 1.7 s).
	if (!LifeState.timed_use() || !TimedUseKind.StartsWith(TEXT("gather|")) || LifeFragment == INDEX_NONE)
	{
		return 0;
	}
	const FSwcActionFragment& Fragment = Data->Fragments[LifeFragment];
	const float Impact = Fragment.MarkTime(TEXT("GatherMomentOfImpact"), -1.0f);
	const float Clock = LifeState.clock();
	if (Fragment.Length <= 0.0f || Impact < 0.0f || Clock < Impact)
	{
		return 0;
	}
	return 1 + FMath::FloorToInt32((Clock - Impact) / Fragment.Length);
}
