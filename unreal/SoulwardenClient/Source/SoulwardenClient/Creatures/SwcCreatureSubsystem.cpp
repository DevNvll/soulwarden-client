#include "Creatures/SwcCreatureSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Creatures/SwcCreature.h"
#include "Creatures/SwcDamageMessage.h"
#include "Debug/DebugDrawService.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/SwcSpace.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "SwcBackendSubsystem.h"

DEFINE_LOG_CATEGORY(LogSwcCreatures);

namespace
{
/** The plate of a development picture (-SwcPlates=all): each creature within this distance (m). */
constexpr double AllPlatesRange = 40.0;
/** The crosshair rule of the marker (m_nearAngle at the near range, down to m_farAngle at m_angleShrinkDistance): a plate
 *  farther than this shows only by the crosshair (an inference: the far limit of the marker is not read). */
constexpr double CrosshairRange = 50.0;
/** The plate of a dead creature goes away some seconds after the dead mark (protocol notes 4.14; the time is an inference). */
constexpr double DeadPlateSeconds = 3.0;
/** The body of a dead creature: the client removes it about 5 minutes later (the delayed event of 300 s of the slice). */
constexpr double DeadBodySeconds = 300.0;
/** A creature is a candidate for a contact when its bounds are this near to the weapon shape (cm). */
constexpr double ContactMargin = 50.0;
/** The wall line: from the feet of the attacker at this height (cm; a value of this client) to the point of the contact; a hit
 *  nearer than the point by more than the slack is a wall. */
constexpr double WallLineHeight = 100.0;
constexpr double WallSlack = 5.0;
/** The range of the development contact -SwcFarContact (m): beyond the reach of the server (3.5 m + 1.5 m). */
constexpr double FarContactMin = 6.0;
constexpr double FarContactMax = 10.0;
/** The damage number (a simple form; its times and its rise are not from the data): it rises this far (cm) in its time (s),
 *  and it fades in the last part of its time. */
constexpr double DamageRise = 70.0;
constexpr double DamageSeconds = 1.2;
constexpr double DamageFadeSeconds = 0.4;
/** The names of the damage types of the wire (DamageTypes of the data: the order of the backend, world/combat.h). */
const TCHAR* DamageTypeName(uint8 Type)
{
	switch (Type)
	{
	case 4:
		return TEXT("slash");
	case 5:
		return TEXT("thrust");
	case 6:
		return TEXT("strike");
	default:
		return TEXT("type");
	}
}
const FLinearColor PlateBack(0.02f, 0.02f, 0.02f, 0.7f);
const FLinearColor HealthFill(0.62f, 0.07f, 0.05f, 1.0f);
const FLinearColor ImmuneFill(0.55f, 0.55f, 0.55f, 1.0f);
} // namespace

void USwcCreatureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Plates;
	bAllPlates = FParse::Value(FCommandLine::Get(), TEXT("SwcPlates="), Plates) && Plates == TEXT("all");
	// -SwcFarContact, or -SwcFarContact=SECONDS: from that time of the world.
	bFarContacts = FParse::Param(FCommandLine::Get(), TEXT("SwcFarContact")) || FParse::Value(FCommandLine::Get(), TEXT("SwcFarContact="), FarContactFrom);
	bDrawVolumes = FParse::Param(FCommandLine::Get(), TEXT("SwcHitVolumes"));
	bVolumeProbe = FParse::Param(FCommandLine::Get(), TEXT("SwcVolumeProbe"));
	DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &USwcCreatureSubsystem::DrawPlates));
}

void USwcCreatureSubsystem::Deinitialize()
{
	UDebugDrawService::Unregister(DrawHandle);
	Clear(TEXT("the world ends"));
	Super::Deinitialize();
}

bool USwcCreatureSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcCreatureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcCreatureSubsystem, STATGROUP_Tickables);
}

void USwcCreatureSubsystem::Clear(const TCHAR* Cause)
{
	if (Spawned > 0 || !Actors.IsEmpty())
	{
		UE_LOG(LogSwcCreatures, Log, TEXT("creatures: %s: %d creature(s) made (%d placeholder(s)), %d removed, %d action(s), %d death(s), %d kind(s), %d damage number(s)"),
			Cause, Spawned, Placeholders, Removed, Actions, Deaths, Kinds.Num(), Damages);
	}
	for (const TPair<uint32, TWeakObjectPtr<ASwcCreature>>& Pair : Actors)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->Destroy();
		}
	}
	Actors.Reset();
	DamageNumbers.Reset();
	Damages = 0;
	Table.clear();
	Table.take_changes();
	Spawned = Removed = Actions = Deaths = Placeholders = 0;
	Kinds.Reset();
}

void USwcCreatureSubsystem::Spawn(const swc::creatures::Entity& Entity)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	if (!Library.IsLoaded())
	{
		FString Report;
		Library.Load(Report);
		UE_LOG(LogSwcCreatures, Log, TEXT("%s"), *Report);
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Feet = SwcEngineToUnreal(Entity.samples.back().position);
	ASwcCreature* Creature = World->SpawnActor<ASwcCreature>(Feet, FRotator::ZeroRotator, Parameters);
	if (Creature == nullptr)
	{
		return;
	}
	Creature->Setup(Library, Entity);
	Actors.Add(Entity.interest, Creature);
	++Spawned;
	const FString Key = UTF8_TO_TCHAR(Entity.slice_key().c_str());
	if (!Creature->HasModel())
	{
		++Placeholders;
		if (Library.AddWanted(Key))
		{
			UE_LOG(LogSwcCreatures, Warning, TEXT("creature: the slice %s (%s) has no imported model: it is in the wanted list (tools/convert-creatures.sh wanted, tools/import-creatures.sh)"),
				*Key, *Creature->GetSlicePath());
		}
	}
	const auto& Position = Entity.samples.back().position;
	UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s, level %u, health %.0f/%.0f%s, slice %s, %s, at %.2f %.2f %.2f"), Entity.interest,
		Entity.hasVitals ? *Library.Name(Entity.categoryId, Entity.vitalsId) : TEXT("(no vitals yet)"), Entity.level, Entity.health, Entity.healthMax,
		Entity.dead ? TEXT(", dead") : TEXT(""), *Creature->GetSlicePath(), Creature->HasModel() ? TEXT("body") : TEXT("placeholder"), Position.x, Position.y,
		Position.z);
}

void USwcCreatureSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UGameInstance* GameInstance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
	USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	std::vector<swc::net::RepClient::OtherUpdate> Updates;
	if (Backend == nullptr || !Backend->TakeOtherUpdates(Updates))
	{
		if (bConnected)
		{
			bConnected = false;
			Clear(TEXT("the connection ended"));
		}
		return;
	}
	bConnected = true;
	for (const swc::net::RepClient::OtherUpdate& Update : Updates)
	{
		if (Update.messageType == swc::net::kDamageDealt.index)
		{
			OnDamage(Update.message);
		}
		else if (Update.stop)
		{
			Table.stop(Update.interest);
		}
		else
		{
			Table.apply(Update.record);
		}
	}
	for (const swc::creatures::Change& Change : Table.take_changes())
	{
		const swc::creatures::Entity* Entity = Table.find(Change.interest);
		TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Change.interest);
		const bool bStale = Actor != nullptr && (!Actor->IsValid() || (*Actor)->GetSerial() != Change.serial);
		if (Change.kind == swc::creatures::Change::Kind::removed || bStale)
		{
			if (Actor != nullptr && Actor->IsValid() && (Change.kind == swc::creatures::Change::Kind::removed ? (*Actor)->GetSerial() == Change.serial : true))
			{
				UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: removed (%s)"), Change.interest, *(*Actor)->GetDisplayName());
				(*Actor)->Destroy();
				Actors.Remove(Change.interest);
				++Removed;
			}
			continue;
		}
		if (Entity == nullptr || Actor == nullptr || !Actor->IsValid())
		{
			continue;
		}
		if (Change.kind == swc::creatures::Change::Kind::health)
		{
			UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s health %.1f/%.1f"), Change.interest, *(*Actor)->GetDisplayName(), Entity->health, Entity->healthMax);
		}
		else if (Change.kind == swc::creatures::Change::Kind::death)
		{
			++Deaths;
			UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s is dead"), Change.interest, *(*Actor)->GetDisplayName());
		}
	}
	const uint64 ServerNowUs = Backend->GetServerTimeMicroseconds();
	if (ServerNowUs == 0)
	{
		return;
	}
	const uint64 RenderTimeUs = ServerNowUs - static_cast<uint64>(InterpolationDelaySeconds * 1.0e6);
	for (const auto& [Interest, Entity] : Table.entities())
	{
		TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Interest);
		if (Actor == nullptr || !Actor->IsValid())
		{
			if (Entity.hasMetadata && Entity.hasPosition())
			{
				Spawn(Entity);
				Actor = Actors.Find(Interest);
			}
			if (Actor == nullptr || !Actor->IsValid())
			{
				continue;
			}
		}
		ASwcCreature* Creature = Actor->Get();
		const FString Action = Creature->UpdateFromEntity(Library, Entity, RenderTimeUs, DeltaTime);
		if (!Action.IsEmpty())
		{
			++Actions;
			Kinds.Add(Creature->GetDisplayName());
			UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s: %s"), Interest, *Creature->GetDisplayName(), *Action);
		}
		// The body of a dead creature goes away after the delayed event of its slice (the server keeps the entity).
		const double DeadSeconds = Creature->GetDeadSeconds();
		Creature->SetActorHiddenInGame(DeadSeconds > DeadBodySeconds);
	}
	if (bDrawVolumes || bVolumeProbe)
	{
		DevelopmentAids();
	}
}

void USwcCreatureSubsystem::DevelopmentAids()
{
	UWorld* World = GetWorld();
	APlayerController* Player = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = Player != nullptr ? Player->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		return;
	}
	const FVector Own = Pawn->GetActorLocation();
	if (bDrawVolumes)
	{
		TArray<FSwcCreatureHitShape> Shapes;
		for (const TPair<uint32, TWeakObjectPtr<ASwcCreature>>& Pair : Actors)
		{
			const ASwcCreature* Creature = Pair.Value.Get();
			if (Creature == nullptr || FVector::Dist(Creature->GetActorLocation(), Own) > 2000.0)
			{
				continue;
			}
			Shapes.Reset();
			Creature->GetHitShapes(Shapes);
			for (const FSwcCreatureHitShape& Hit : Shapes)
			{
				const FColor Color = Hit.Volume->bHeadshot ? FColor::Red : Hit.Volume->bLegshot ? FColor::Cyan : FColor::Yellow;
				const FSwcHitShape& Shape = Hit.Shape;
				if (Shape.Core == FSwcHitShape::ECore::Box)
				{
					DrawDebugBox(World, Shape.Center, Shape.HalfSize, Shape.Rotation, Color, false, -1.0f, 0, 0.5f);
				}
				else
				{
					const FVector Axis = Shape.End - Shape.Start;
					const FQuat Rotation = Axis.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromZ(Axis).ToQuat();
					DrawDebugCapsule(World, Shape.Middle(), static_cast<float>(Axis.Size() * 0.5) + Shape.Radius, Shape.Radius, Rotation, Color, false, -1.0f, 0, 0.5f);
				}
			}
		}
	}
	const double Now = FPlatformTime::Seconds();
	if (bVolumeProbe && Now >= NextProbe)
	{
		NextProbe = Now + 0.5;
		// A test capsule: 0.6 m to 2.2 m in front of the character (the yaw of the camera) at the height of its chest, radius 0.3 m.
		const FVector Forward = FRotator(0.0, Player->GetControlRotation().Yaw, 0.0).Vector();
		FSwcMeleeShape Probe;
		Probe.Kind = FSwcMeleeShape::EKind::Capsule;
		Probe.Center = Own + FVector(0.0, 0.0, 40.0) + Forward * 140.0;
		Probe.Rotation = FRotationMatrix::MakeFromZ(Forward).ToQuat();
		Probe.Radius = 30.0f;
		Probe.HalfLength = 80.0f;
		Probe.Attacker = Own - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
		Probe.Direction = Forward;
		TArray<FSwcMeleeContact> Contacts;
		FindMeleeContacts(Probe, Contacts);
		DrawDebugCapsule(World, Probe.Center, Probe.HalfLength + Probe.Radius, Probe.Radius, Probe.Rotation, Contacts.IsEmpty() ? FColor::White : FColor::Green,
			false, 0.5f, 0, 0.5f);
		for (const FSwcMeleeContact& Contact : Contacts)
		{
			DrawDebugPoint(World, Contact.Point, 12.0f, FColor::Green, false, 0.5f);
			UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: probe contact: volume %s at %.2f m, point %.2f %.2f %.2f, target %016llx%s"), Contact.Interest,
				*Contact.Volume, Contact.Distance, Contact.Item.point.x, Contact.Item.point.y, Contact.Item.point.z,
				static_cast<unsigned long long>(Contact.Item.facet), Contact.bDevelopment ? TEXT(" (development contact)") : TEXT(""));
		}
	}
}

void USwcCreatureSubsystem::OnDamage(const swc::net::Bytes& Body)
{
	const std::optional<swc::creatures::DamageMessage> Damage = swc::creatures::read_damage_message(Body);
	if (!Damage.has_value())
	{
		UE_LOG(LogSwcCreatures, Warning, TEXT("creatures: a damage number message (%d bytes) does not have the form"), static_cast<int32>(Body.size()));
		return;
	}
	++Damages;
	// The creature of the message: the entity with its actor id.
	uint32 Interest = 0;
	for (const auto& [Number, Entity] : Table.entities())
	{
		if (Entity.hasMetadata && Entity.actor == Damage->actor)
		{
			Interest = Number;
			break;
		}
	}
	const TWeakObjectPtr<ASwcCreature>* Actor = Interest != 0 ? Actors.Find(Interest) : nullptr;
	const ASwcCreature* Creature = Actor != nullptr ? Actor->Get() : nullptr;
	const bool bKill = (Damage->flags & swc::creatures::DamageMessage::kCausedDeath) != 0;
	const bool bCrit = (Damage->flags & swc::creatures::DamageMessage::kCrit) != 0;
	FString Parts;
	for (const swc::creatures::DamageEntry& Entry : Damage->entries)
	{
		Parts += FString::Printf(TEXT("%s%.1f %s"), Parts.IsEmpty() ? TEXT("") : TEXT(" + "), Entry.amount, DamageTypeName(Entry.type));
	}
	UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s: damage number %s, flags 0x%04x%s%s"), Interest, Creature != nullptr ? *Creature->GetDisplayName() : TEXT("(not in the table)"),
		*Parts, Damage->flags, bKill ? TEXT(", the kill") : TEXT(""), bCrit ? TEXT(", critical hit") : TEXT(""));
	if (Creature == nullptr)
	{
		return;
	}
	// The whole part of the damage (no rounding up: 64.9 shows 64).
	FDamageNumber& Number = DamageNumbers.AddDefaulted_GetRef();
	const FBoxSphereBounds Bounds = Creature->GetBodyBounds();
	// The numbers of one creature in a short time go side by side.
	int32 Recent = 0;
	for (const FDamageNumber& Other : DamageNumbers)
	{
		Recent += FVector::Dist2D(Other.Point, Bounds.Origin) < 200.0 && FPlatformTime::Seconds() - Other.Start < 0.5 ? 1 : 0;
	}
	Number.Point = Bounds.Origin + FVector(0.0, 0.0, Bounds.BoxExtent.Z * 0.6) + FVector(0.0, 0.0, 15.0 * (Recent - 1));
	Number.Text = FString::Printf(TEXT("%d"), FMath::FloorToInt(Damage->total()));
	Number.Start = FPlatformTime::Seconds();
	Number.bCrit = bCrit;
	Number.bKill = bKill;
}

void USwcCreatureSubsystem::DrawDamageNumbers(UCanvas* Canvas)
{
	const double Now = FPlatformTime::Seconds();
	DamageNumbers.RemoveAll([Now](const FDamageNumber& Number) { return Now - Number.Start > DamageSeconds; });
	UFont* Font = GEngine != nullptr ? GEngine->GetMediumFont() : nullptr;
	UFont* LargeFont = GEngine != nullptr ? GEngine->GetLargeFont() : nullptr;
	for (const FDamageNumber& Number : DamageNumbers)
	{
		const double Age = Now - Number.Start;
		const FVector Screen = Canvas->Project(Number.Point + FVector(0.0, 0.0, DamageRise * Age / DamageSeconds));
		UFont* Used = Number.bCrit && LargeFont != nullptr ? LargeFont : Font;
		if (Screen.Z <= 0.0 || Used == nullptr)
		{
			continue;
		}
		const float Alpha = static_cast<float>(FMath::Clamp((DamageSeconds - Age) / DamageFadeSeconds, 0.0, 1.0));
		float Width = 0.0f;
		float Height = 0.0f;
		Canvas->StrLen(Used, Number.Text, Width, Height);
		const FLinearColor Color = Number.bCrit ? FLinearColor(1.0f, 0.85f, 0.2f, Alpha) : FLinearColor(1.0f, 1.0f, 1.0f, Alpha);
		FCanvasTextItem Text(FVector2D(Screen.X - Width * 0.5f, Screen.Y - Height * 0.5f), FText::FromString(Number.Text), Used, Color);
		Text.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, Alpha));
		Text.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Text);
	}
}

void USwcCreatureSubsystem::GetTargets(TArray<FSwcCreatureTarget>& Out) const
{
	Out.Reset();
	for (const auto& [Interest, Entity] : Table.entities())
	{
		const TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Interest);
		if (Actor != nullptr && Actor->IsValid())
		{
			Out.Add({Interest, (*Actor)->GetActorLocation(), Entity.dead});
		}
	}
}

int32 USwcCreatureSubsystem::FindMeleeContacts(const FSwcMeleeShape& Melee, TArray<FSwcMeleeContact>& Out) const
{
	Out.Reset();
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0;
	}
	FSwcHitShape Attack;
	switch (Melee.Kind)
	{
	case FSwcMeleeShape::EKind::Sphere:
		Attack = FSwcHitShape::Sphere(Melee.Center, Melee.Radius);
		break;
	case FSwcMeleeShape::EKind::Box:
		Attack = FSwcHitShape::Box(Melee.Center, Melee.Rotation, Melee.HalfExtents);
		break;
	case FSwcMeleeShape::EKind::Capsule:
	{
		// The axis of a capsule is its local Z.
		const FVector Axis = Melee.Rotation.GetAxisZ() * Melee.HalfLength;
		Attack = FSwcHitShape::Capsule(Melee.Center - Axis, Melee.Center + Axis, Melee.Radius);
		break;
	}
	}
	// The wall line starts at the chest of the attacker: its feet and the height of a chest (1 m; a value of this client).
	const FVector Origin = Melee.Attacker + FVector(0.0, 0.0, WallLineHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SwcMeleeWall), false);
	const FVector Middle = Attack.Middle();
	// The reach of the shape from its middle: the farthest point of its core and its radius.
	const double Reach = (Attack.Core == FSwcHitShape::ECore::Box ? Attack.HalfSize.Size() : FVector::Dist(Attack.Start, Attack.End) * 0.5)
		+ Attack.Radius;
	const auto MakeContact = [&](uint32 Interest, const swc::creatures::Entity& Entity, const FSwcHitVolume& Volume, const FVector& Point,
								 const FVector& Normal, const ASwcCreature& Creature)
	{
		FSwcMeleeContact Contact;
		Contact.Interest = Interest;
		Contact.Volume = Volume.Name;
		Contact.bHeadshot = Volume.bHeadshot;
		Contact.bLegshot = Volume.bLegshot;
		Contact.Point = Point;
		Contact.Normal = Normal;
		Contact.Distance = static_cast<float>(FVector::Dist(Melee.Attacker, Creature.GetActorLocation()) / 100.0);
		const FVector Direction = Melee.Direction.IsNearlyZero() ? (Point - Origin).GetSafeNormal() : Melee.Direction.GetSafeNormal();
		// A direction has the map of a position with no scale (Net/SwcSpace.h).
		const auto EngineDirection = [](const FVector& Unreal)
		{ return swc::net::WorldPosition{static_cast<float>(Unreal.X), static_cast<float>(-Unreal.Y), static_cast<float>(Unreal.Z)}; };
		Contact.Item = swc::creatures::melee_contact_item(Entity.actor,
			swc::creatures::facet_target(swc::net::group_of(Entity.actor), Volume.Local, Volume.Component), SwcUnrealToEngine(Point),
			EngineDirection(Normal), EngineDirection(Direction));
		return Contact;
	};
	TArray<FSwcCreatureHitShape> Shapes;
	for (const TPair<uint32, TWeakObjectPtr<ASwcCreature>>& Pair : Actors)
	{
		const ASwcCreature* Creature = Pair.Value.Get();
		const swc::creatures::Entity* Entity = Table.find(Pair.Key);
		if (Creature == nullptr || Entity == nullptr || !Entity->hasMetadata || Entity->dead || Entity->immune || !Creature->HasModel())
		{
			continue;
		}
		const FBoxSphereBounds Bounds = Creature->GetBodyBounds();
		if (FVector::Dist(Bounds.Origin, Middle) > Bounds.SphereRadius + Reach + ContactMargin)
		{
			continue;
		}
		Shapes.Reset();
		Creature->GetHitShapes(Shapes);
		const FSwcCreatureHitShape* Best = nullptr;
		FVector BestPoint = FVector::ZeroVector;
		FVector BestNormal = FVector::ZeroVector;
		double BestDistance = TNumericLimits<double>::Max();
		for (const FSwcCreatureHitShape& Shape : Shapes)
		{
			FVector Point;
			FVector Normal;
			if (SwcShapeContact(Shape.Shape, Attack, Origin - Shape.Shape.Middle(), Point, Normal))
			{
				const double Distance = FVector::DistSquared(Point, Middle);
				if (Distance < BestDistance)
				{
					Best = &Shape;
					BestPoint = Point;
					BestNormal = Normal;
					BestDistance = Distance;
				}
			}
		}
		if (Best == nullptr)
		{
			continue;
		}
		FHitResult Wall;
		if (!Melee.bNoSight && World->LineTraceSingleByObjectType(Wall, Origin, BestPoint, FCollisionObjectQueryParams(ECC_WorldStatic), Params)
			&& Wall.Distance < FVector::Dist(Origin, BestPoint) - WallSlack)
		{
			UE_LOG(LogSwcCreatures, Verbose, TEXT("creatures: no contact with interest %u: a wall (%s) at %.2f m"), Pair.Key,
				*GetNameSafe(Wall.GetActor()), Wall.Distance / 100.0);
			continue;
		}
		Out.Add(MakeContact(Pair.Key, *Entity, *Best->Volume, BestPoint, BestNormal, *Creature));
	}
	if (bFarContacts && Out.IsEmpty() && World->GetTimeSeconds() >= FarContactFrom)
	{
		// The development contact: the first volume of the nearest living creature out of the reach.
		const ASwcCreature* Nearest = nullptr;
		uint32 NearestInterest = 0;
		double NearestDistance = FarContactMax * 100.0;
		for (const TPair<uint32, TWeakObjectPtr<ASwcCreature>>& Pair : Actors)
		{
			const ASwcCreature* Creature = Pair.Value.Get();
			const swc::creatures::Entity* Entity = Table.find(Pair.Key);
			const double Distance = Creature != nullptr ? FVector::Dist(Melee.Attacker, Creature->GetActorLocation()) : 0.0;
			if (Creature != nullptr && Entity != nullptr && Entity->hasMetadata && !Entity->dead && !Entity->immune && Creature->HasModel()
				&& Distance >= FarContactMin * 100.0 && Distance <= NearestDistance)
			{
				Nearest = Creature;
				NearestInterest = Pair.Key;
				NearestDistance = Distance;
			}
		}
		Shapes.Reset();
		if (Nearest != nullptr)
		{
			Nearest->GetHitShapes(Shapes);
		}
		if (!Shapes.IsEmpty())
		{
			const FVector Point = Shapes[0].Shape.Middle();
			FSwcMeleeContact& Contact = Out.Add_GetRef(MakeContact(NearestInterest, *Table.find(NearestInterest), *Shapes[0].Volume, Point,
				(Origin - Point).GetSafeNormal(), *Nearest));
			Contact.bDevelopment = true;
		}
	}
	return Out.Num();
}

void USwcCreatureSubsystem::QueryMeleeContacts(const FSwcMeleeShape& Shape, std::vector<swc::net::MeleeContactItem>& Out) const
{
	TArray<FSwcMeleeContact> Contacts;
	FindMeleeContacts(Shape, Contacts);
	for (const FSwcMeleeContact& Contact : Contacts)
	{
		// One log line for each creature in each strike (the query runs at each step of the window).
		if (LoggedContacts.Num() > 1000)
		{
			LoggedContacts.Reset();
		}
		bool bLogged = false;
		LoggedContacts.Add(FString::Printf(TEXT("%u|%d|%u"), Shape.Visit, Shape.Strike, Contact.Interest), &bLogged);
		if (!bLogged)
		{
			UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: melee contact: visit %u strike %d (%s), volume %s at %.2f m, point %.2f %.2f %.2f%s"), Contact.Interest,
				Shape.Visit, Shape.Strike, *Shape.Key, *Contact.Volume, Contact.Distance, Contact.Item.point.x, Contact.Item.point.y, Contact.Item.point.z,
				Contact.bDevelopment ? TEXT(" (the development contact out of the reach)") : TEXT(""));
		}
		Out.push_back(Contact.Item);
	}
}

void USwcCreatureSubsystem::DrawPlates(UCanvas* Canvas, APlayerController* Controller)
{
	if (Canvas == nullptr || GetWorld() == nullptr || Canvas->SceneView == nullptr || Canvas->SceneView->Family == nullptr
		|| Canvas->SceneView->Family->Scene == nullptr || Canvas->SceneView->Family->Scene->GetWorld() != GetWorld())
	{
		return;
	}
	APlayerController* Player = Controller != nullptr ? Controller : GetWorld()->GetFirstPlayerController();
	if (Player == nullptr || Player->PlayerCameraManager == nullptr)
	{
		return;
	}
	const FVector Eye = Player->PlayerCameraManager->GetCameraLocation();
	const FVector Look = Player->PlayerCameraManager->GetCameraRotation().Vector();
	const FVector Own = Player->GetPawn() != nullptr ? Player->GetPawn()->GetActorLocation() : Eye;
	UFont* Font = GEngine != nullptr ? GEngine->GetSmallFont() : nullptr;
	if (Font == nullptr)
	{
		return;
	}
	DrawDamageNumbers(Canvas);
	for (const auto& [Interest, Entity] : Table.entities())
	{
		const TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Interest);
		if (Actor == nullptr || !Actor->IsValid() || (*Actor)->IsHidden() || !Entity.hasVitals)
		{
			continue;
		}
		const ASwcCreature* Creature = Actor->Get();
		const double DeadSeconds = Creature->GetDeadSeconds();
		if (DeadSeconds > DeadPlateSeconds || (Entity.hasMarker && !Entity.markerEnabled))
		{
			continue;
		}
		const FVector Point = Creature->GetPlatePoint();
		const double Distance = FVector::Dist(Own, Creature->GetActorLocation()) / 100.0;
		// The rule of the marker of the slice (MarkerComponent client facet): the near range, or the crosshair within an angle
		// that shrinks from the near angle at the near range to the far angle at the shrink distance.
		const FSwcCreatureSlice* Slice = Creature->GetSlice();
		const double NearRange = Slice != nullptr ? Slice->NearRange : 6.0;
		const double NearAngle = Slice != nullptr ? Slice->NearAngle : 0.1309;
		const double FarAngle = Slice != nullptr ? Slice->FarAngle : 0.0174533;
		const double Shrink = Slice != nullptr ? FMath::Max(Slice->AngleShrinkDistance, NearRange + 0.1) : 20.0;
		const FVector Center = Creature->GetActorLocation() + FVector(0.0, 0.0, 80.0 * Creature->GetActorScale3D().Z);
		const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Look, (Center - Eye).GetSafeNormal()), -1.0, 1.0));
		const double Allowed = FMath::Lerp(NearAngle, FarAngle, FMath::Clamp((Distance - NearRange) / (Shrink - NearRange), 0.0, 1.0));
		const bool bShow = Distance <= NearRange || (Distance <= CrosshairRange && Angle <= Allowed) || (bAllPlates && Distance <= AllPlatesRange);
		if (!bShow)
		{
			continue;
		}
		const FVector Screen = Canvas->Project(Point);
		if (Screen.Z <= 0.0)
		{
			continue;
		}
		// The plate: the name with the level (no level mark for a Critter), and the health bar under it.
		const FString Title = Creature->HidesLevel() || Entity.level == 0 ? Creature->GetDisplayName()
			: FString::Printf(TEXT("%s  %u"), *Creature->GetDisplayName(), Entity.level);
		float TextWidth = 0.0f;
		float TextHeight = 0.0f;
		Canvas->StrLen(Font, Title, TextWidth, TextHeight);
		const float BarWidth = 120.0f;
		const float BarHeight = 7.0f;
		const float X = static_cast<float>(Screen.X);
		const float Y = static_cast<float>(Screen.Y);
		FCanvasTextItem Text(FVector2D(X - TextWidth * 0.5f, Y - TextHeight - BarHeight - 2.0f), FText::FromString(Title), Font, FLinearColor::White);
		Text.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Text);
		if (Slice == nullptr || !Slice->bNeverShowHealthBar)
		{
			const float Share = Entity.healthMax > 0.0f ? FMath::Clamp(Entity.health / Entity.healthMax, 0.0f, 1.0f) : 0.0f;
			FCanvasTileItem Back(FVector2D(X - BarWidth * 0.5f - 1.0f, Y - BarHeight - 1.0f), FVector2D(BarWidth + 2.0f, BarHeight + 2.0f), PlateBack);
			Back.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Back);
			FCanvasTileItem Fill(FVector2D(X - BarWidth * 0.5f, Y - BarHeight), FVector2D(BarWidth * Share, BarHeight), Entity.immune ? ImmuneFill : HealthFill);
			Canvas->DrawItem(Fill);
		}
	}
}
