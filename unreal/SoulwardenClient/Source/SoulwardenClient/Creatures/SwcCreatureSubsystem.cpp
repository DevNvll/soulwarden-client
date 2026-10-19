#include "Creatures/SwcCreatureSubsystem.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "CanvasItem.h"
#include "Creatures/SwcCreature.h"
#include "Creatures/SwcDamageMessage.h"
#include "Creatures/SwcEffectActor.h"
#include "Debug/DebugDrawService.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
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
	bLootProbe = FParse::Param(FCommandLine::Get(), TEXT("SwcLootProbe"));
	bLootModel = FParse::Param(FCommandLine::Get(), TEXT("SwcLootModel"));
	bDrawLockPoints = FParse::Param(FCommandLine::Get(), TEXT("SwcLockPoints"));
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
		UE_LOG(LogSwcCreatures, Log, TEXT("creatures: %s: %d creature(s) made (%d placeholder(s)), %d removed, %d action(s), %d death(s), %d kind(s), %d damage number(s), %d shot(s), %d area(s), %d loot bag(s); the largest spawn work %.1f ms"),
			Cause, Spawned, Placeholders, Removed, Actions, Deaths, Kinds.Num(), Damages, Shots, Areas, LootBags, SpawnWorkMax * 1000.0);
	}
	for (const TPair<uint32, TWeakObjectPtr<ASwcCreature>>& Pair : Actors)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->Destroy();
		}
	}
	Actors.Reset();
	for (const TPair<uint32, TWeakObjectPtr<ASwcEffectActor>>& Pair : Effects)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->Destroy();
		}
	}
	Effects.Reset();
	for (const TPair<uint32, TWeakObjectPtr<AActor>>& Pair : Bags)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->Destroy();
		}
	}
	Bags.Reset();
	Shots = Areas = LootBags = 0;
	DamageNumbers.Reset();
	Damages = 0;
	Table.clear();
	++TableRevision;
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
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Feet = SwcEngineToUnreal(Entity.samples.back().position);
	ASwcCreature* Creature = World->SpawnActor<ASwcCreature>(Feet, FRotator::ZeroRotator, Parameters);
	if (Creature == nullptr)
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	// The animation sets of the slice and of its animated weapons: read on a worker thread when the library does not have them; then
	// the async load of the assets (ASwcCreature::Setup).
	TArray<FString> Keys;
	if (const FSwcCreatureSlice* Slice = Library.FindSlice(UTF8_TO_TCHAR(Entity.slice_key().c_str())))
	{
		Keys.Add(Slice->AnimationKey);
		for (const FSwcCreatureWeapon& Weapon : Slice->Weapons)
		{
			if (!Weapon.SkeletonMeshPath.IsEmpty())
			{
				Keys.AddUnique(Weapon.AnimationKey);
			}
		}
	}
	Keys.RemoveAll([this](const FString& Key) { return Key.IsEmpty() || Library.HasAnimations(Key); });
	if (Keys.IsEmpty())
	{
		Creature->Setup(Library, Entity, Streamer);
	}
	else
	{
		Creature->BeginLoading(Entity.serial);
		const uint32 Interest = Entity.interest;
		const TWeakObjectPtr<ASwcCreature> Weak(Creature);
		RequestAnimations(Keys, [this, Weak, Interest]()
		{
			const swc::creatures::Entity* Now = Table.find(Interest);
			if (Weak.IsValid() && Now != nullptr && Now->serial == Weak->GetSerial())
			{
				Weak->Setup(Library, *Now, Streamer);
			}
		});
	}
	const double Work = FPlatformTime::Seconds() - Start;
	SpawnWorkMax = FMath::Max(SpawnWorkMax, Work);
	Actors.Add(Entity.interest, Creature);
	++Spawned;
	UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: spawn work %.1f ms on the game thread (%s)"), Entity.interest, Work * 1000.0,
		Creature->IsLoading() ? TEXT("the assets load async") : TEXT("the assets were in memory"));
	SpawnWatchUntil = FPlatformTime::Seconds() + 3.0;
	++SpawnWatchCount;
	double Seconds = 0.0;
	if (Creature->TakeReady(Seconds))
	{
		Ready(Entity, Creature, Seconds);
	}
}

void USwcCreatureSubsystem::RequestAnimations(const TArray<FString>& Keys, TFunction<void()> Done)
{
	const TSharedRef<int32> Remaining = MakeShared<int32>(Keys.Num());
	const TSharedRef<TFunction<void()>> Finish = MakeShared<TFunction<void()>>(MoveTemp(Done));
	for (const FString& Key : Keys)
	{
		TArray<TFunction<void()>>* Waiters = AnimationWaiters.Find(Key);
		const bool bStart = Waiters == nullptr;
		if (bStart)
		{
			Waiters = &AnimationWaiters.Add(Key);
		}
		Waiters->Add([Remaining, Finish]()
		{
			if (--(*Remaining) == 0)
			{
				(*Finish)();
			}
		});
		if (bStart)
		{
			const TWeakObjectPtr<USwcCreatureSubsystem> Weak(this);
			AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Weak, Key]()
			{
				TSharedPtr<FSwcCreatureAnimations> Set = FSwcCreatureLibrary::ParseAnimations(Key);
				AsyncTask(ENamedThreads::GameThread, [Weak, Key, Set]()
				{
					if (USwcCreatureSubsystem* Subsystem = Weak.Get())
					{
						Subsystem->OnAnimationsParsed(Key, Set);
					}
				});
			});
		}
	}
}

void USwcCreatureSubsystem::OnAnimationsParsed(const FString& Key, const TSharedPtr<FSwcCreatureAnimations>& Set)
{
	Library.AddAnimations(Key, Set);
	TArray<TFunction<void()>> Waiters;
	AnimationWaiters.RemoveAndCopyValue(Key, Waiters);
	for (const TFunction<void()>& Waiter : Waiters)
	{
		Waiter();
	}
}

void USwcCreatureSubsystem::Ready(const swc::creatures::Entity& Entity, ASwcCreature* Creature, double LoadSeconds)
{
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
	UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s, level %u, health %.0f/%.0f%s, slice %s, %s, at %.2f %.2f %.2f (the assets after %.2f s)"), Entity.interest,
		Entity.hasVitals ? *Library.Name(Entity.categoryId, Entity.vitalsId) : TEXT("(no vitals yet)"), Entity.level, Entity.health, Entity.healthMax,
		Entity.dead ? TEXT(", dead") : TEXT(""), *Creature->GetSlicePath(), Creature->HasModel() ? TEXT("body") : TEXT("placeholder"), Position.x, Position.y,
		Position.z, LoadSeconds);
}

void USwcCreatureSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// The frames after a spawn (the work of a spawn must stay small: no load that waits for the async loads).
	if (SpawnWatchCount > 0)
	{
		SpawnWatchMax = FMath::Max(SpawnWatchMax, DeltaTime);
		if (FPlatformTime::Seconds() > SpawnWatchUntil)
		{
			UE_LOG(LogSwcCreatures, Log, TEXT("creatures: the longest frame in the 3 s after %d spawn(s): %.1f ms"), SpawnWatchCount, SpawnWatchMax * 1000.0f);
			SpawnWatchCount = 0;
			SpawnWatchMax = 0.0f;
		}
	}
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
	if (!bConnected)
	{
		ConnectedAt = FPlatformTime::Seconds();
	}
	bConnected = true;
	// The data of the creatures, the effects, and the loot bags: before the first record (a bag or a shot can come before a creature).
	if (!Library.IsLoaded())
	{
		FString Report;
		Library.Load(Report);
		UE_LOG(LogSwcCreatures, Log, TEXT("%s"), *Report);
		TArray<FSoftObjectPath> Common = {FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")),
			FSoftObjectPath(TEXT("/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent"))};
		for (const TPair<FString, FSwcLootContainer>& Container : Library.GetLootContainers())
		{
			if (!Container.Value.MeshPath.IsEmpty())
			{
				Common.AddUnique(FSoftObjectPath(Container.Value.MeshPath));
			}
		}
		bCommonReady = false;
		CommonHandle = Streamer.RequestAsyncLoad(Common, FStreamableDelegate::CreateWeakLambda(this, [this]() { bCommonReady = true; }));
	}
	// The server time of the records: the states of the delayed events of a spell count from it.
	const uint64 ApplyTimeUs = Backend->GetServerTimeMicroseconds();
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
			Table.apply(Update.record, ApplyTimeUs);
		}
	}
	if (!Updates.empty())
	{
		++TableRevision;
	}
	for (const swc::creatures::Change& Change : Table.take_changes())
	{
		// A loot bag: the stop removes it (the server ends the bag at its lifetime, or after its last item).
		if (Change.kind == swc::creatures::Change::Kind::removed && Bags.Contains(Change.interest))
		{
			if (Bags[Change.interest].IsValid())
			{
				Bags[Change.interest]->Destroy();
			}
			Bags.Remove(Change.interest);
			UE_LOG(LogSwcCreatures, Log, TEXT("loot bag %u: removed (the stop of its record)"), Change.interest);
			continue;
		}
		// A shot or an area: the stop removes its actor.
		if (TWeakObjectPtr<ASwcEffectActor>* Effect = Effects.Find(Change.interest))
		{
			const bool bStaleEffect = !Effect->IsValid() || (*Effect)->GetSerial() != Change.serial;
			if (Change.kind == swc::creatures::Change::Kind::removed || bStaleEffect)
			{
				if (Effect->IsValid() && (Change.kind != swc::creatures::Change::Kind::removed || (*Effect)->GetSerial() == Change.serial))
				{
					UE_LOG(LogSwcCreatures, Log, TEXT("effect %u: removed (the stop of its record)"), Change.interest);
					(*Effect)->Destroy();
					Effects.Remove(Change.interest);
				}
			}
			if (Change.kind == swc::creatures::Change::Kind::removed)
			{
				continue;
			}
		}
		if (Change.kind == swc::creatures::Change::Kind::effect)
		{
			continue;
		}
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
		else if (Change.kind == swc::creatures::Change::Kind::status)
		{
			FString Names;
			for (const swc::creatures::Entity::StatusEntry& Status : Entity->statuses)
			{
				const FSwcStatusRow* Row = Library.FindStatusRow(Status.index);
				Names += FString::Printf(TEXT("%s%s (%u) x%u"), Names.IsEmpty() ? TEXT("") : TEXT(", "), Row != nullptr ? *Row->Id : TEXT("?"), Status.index, Status.stacks);
			}
			UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s statuses: %s"), Change.interest, *(*Actor)->GetDisplayName(), Names.IsEmpty() ? TEXT("none") : *Names);
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
		// A loot bag: the slice of a loot container of the data.
		if (Entity.hasMetadata && Entity.hasLootDrop)
		{
			if (const FSwcLootContainer* Container = Library.FindLootContainer(UTF8_TO_TCHAR(Entity.slice_key().c_str())))
			{
				UpdateBag(Interest, Entity, *Container);
				continue;
			}
		}
		// A shot, or a spell: a Spell state, a delayed event, or a spell slice (PositionInTheWorld alone is a state of many objects).
		if (Entity.hasProjectile || (Entity.hasSpellParts && (Entity.spell.hasSpell || !Entity.spell.events.empty()
			|| (Entity.hasMetadata && Library.FindSpell(UTF8_TO_TCHAR(Entity.slice_key().c_str())) != nullptr))))
		{
			UpdateEffect(Interest, Entity, RenderTimeUs);
			continue;
		}
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
		double LoadSeconds = 0.0;
		if (Creature->TakeReady(LoadSeconds))
		{
			Ready(Entity, Creature, LoadSeconds);
		}
		const FString Action = Creature->UpdateFromEntity(Library, Entity, RenderTimeUs, DeltaTime);
		if (!Action.IsEmpty())
		{
			++Actions;
			Kinds.Add(Creature->GetDisplayName());
			UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s: %s"), Interest, *Creature->GetDisplayName(), *Action);
		}
		// The body of a dead creature goes away after the delayed event of its slice (the server keeps the entity).
		const double DeadSeconds = Creature->GetDeadSeconds();
		Creature->SetActorHiddenInGame(Creature->IsLoading() || DeadSeconds > DeadBodySeconds);
	}
	if (bDrawVolumes || bVolumeProbe || bLootProbe || bLootModel || bDrawLockPoints)
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
			// The body capsule that blocks the player: white.
			if (const UCapsuleComponent* Block = Creature->GetBodyBlock(); Block != nullptr && Block->IsCollisionEnabled())
			{
				DrawDebugCapsule(World, Block->GetComponentLocation(), Block->GetScaledCapsuleHalfHeight(), Block->GetScaledCapsuleRadius(),
					Block->GetComponentQuat(), FColor::White, false, -1.0f, 0, 0.5f);
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
	// The character is at its place in the world (not at the origin of the level before its spawn).
	if (bLootModel && !bLootModelShown && !Own.IsNearlyZero(1000.0) && FPlatformTime::Seconds() - ConnectedAt > 10.0)
	{
		// The model of the loot container 2 m in front of the character, one time: a look check of the model alone.
		bLootModelShown = true;
		for (const FString& Key : {FString(TEXT("9844F2DE-C3B1-5BD3-98FA-8768B5445906:2"))})
		{
			const FSwcLootContainer* Container = Library.FindLootContainer(Key);
			UStaticMesh* Mesh = Container != nullptr && !Container->MeshPath.IsEmpty() ? LoadObject<UStaticMesh>(nullptr, *Container->MeshPath) : nullptr;
			const FVector Forward = FRotator(0.0, Player->GetControlRotation().Yaw, 0.0).Vector();
			const FVector Place = Own - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight()) + Forward * 200.0;
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AStaticMeshActor* Model = Mesh != nullptr ? World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Place), Parameters) : nullptr)
			{
				Model->SetMobility(EComponentMobility::Movable);
				Model->GetStaticMeshComponent()->SetStaticMesh(Mesh);
				FVector Center;
				FVector Extent;
				Model->GetActorBounds(false, Center, Extent);
				UE_LOG(LogSwcCreatures, Log, TEXT("loot model: %s at %s, bounds extent %s (cm), %d material(s), the first one %s"), *Container->MeshPath, *Place.ToString(),
					*Extent.ToString(), Mesh->GetStaticMaterials().Num(), Mesh->GetStaticMaterials().IsEmpty() ? TEXT("-") : *GetNameSafe(Mesh->GetMaterial(0)));
			}
			else
			{
				UE_LOG(LogSwcCreatures, Warning, TEXT("loot model: no model (%s)"), Container != nullptr ? *Container->MeshPath : TEXT("no loot container"));
			}
		}
	}
	if (bLootProbe)
	{
		// The bounds of each bag actor (green): the place and the size of its model.
		for (const TPair<uint32, TWeakObjectPtr<AActor>>& Pair : Bags)
		{
			if (const AActor* Bag = Pair.Value.Get())
			{
				FVector Center;
				FVector Extent;
				Bag->GetActorBounds(false, Center, Extent);
				DrawDebugBox(World, Center, Extent, FColor::Green, false, -1.0f, 0, 1.0f);
			}
		}
	}
	if (bDrawLockPoints)
	{
		// The lock point of each creature (magenta): ASwcCreature::GetLockPoint.
		for (const TPair<uint32, TWeakObjectPtr<ASwcCreature>>& Pair : Actors)
		{
			if (const ASwcCreature* Creature = Pair.Value.Get(); Creature != nullptr && !Creature->IsHidden())
			{
				DrawDebugSphere(World, Creature->GetLockPoint(), 12.0f, 8, FColor::Magenta, false, -1.0f, 0, 1.0f);
			}
		}
	}
	if (bLootProbe && Now >= NextLootProbe)
	{
		NextLootProbe = Now + 1.0;
		// The place of the player (its feet: the place of the input report of the character).
		const FVector Feet = Own - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
		FSwcLootBag Bag;
		TArray<FSwcLootBag> All;
		GetLootBags(Feet, All);
		const FString Text = FindNearestLootBag(Feet, Bag)
			? FString::Printf(TEXT("the bag %u in range: %.2f m, target %016llx, %d option(s), rarity %u"), Bag.Interest, Bag.Distance,
				static_cast<unsigned long long>(Bag.Target), Bag.Options.Num(), Bag.Rarity)
			: FString::Printf(TEXT("no bag in range (%d bag(s)%s)"), All.Num(),
				All.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", the nearest one %.2f m"), All[0].Distance));
		if (Text != LastLootProbe)
		{
			LastLootProbe = Text;
			UE_LOG(LogSwcCreatures, Log, TEXT("loot probe: %s"), *Text);
		}
	}
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

void USwcCreatureSubsystem::SetLootBagHidden(uint32 Interest, bool bHidden)
{
	if (const TWeakObjectPtr<AActor>* Found = Bags.Find(Interest); Found != nullptr && Found->IsValid())
	{
		(*Found)->SetActorHiddenInGame(bHidden);
	}
}

void USwcCreatureSubsystem::UpdateBag(uint32 Interest, const swc::creatures::Entity& Entity, const FSwcLootContainer& Container)
{
	// The model of a bag comes from the common assets in memory (no load that waits for the async loads).
	if (!Entity.hasWorldPlace || !bCommonReady)
	{
		return;
	}
	const FVector Place = SwcEngineToUnreal(Entity.worldPlace);
	const FQuat Rotation = SwcEngineToUnreal(Entity.worldPlaceRotation);
	TWeakObjectPtr<AActor>* Found = Bags.Find(Interest);
	if (Found != nullptr && Found->IsValid())
	{
		(*Found)->SetActorLocationAndRotation(Place, Rotation);
		return;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Bag = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Place), Parameters);
	if (Bag == nullptr)
	{
		return;
	}
	Bag->SetMobility(EComponentMobility::Movable);
	// The model of the slice (the sack of the personal loot container; its origin is at its bottom). No collision: the player track
	// finds the bag through the query, not through a trace.
	if (UStaticMesh* Mesh = Container.MeshPath.IsEmpty() ? nullptr : FindObject<UStaticMesh>(nullptr, *Container.MeshPath))
	{
		Bag->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	}
	Bag->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bags.Add(Interest, Bag);
	++LootBags;
	FVector Center;
	FVector Extent;
	Bag->GetActorBounds(false, Center, Extent);
	UE_LOG(LogSwcCreatures, Log, TEXT("loot bag %u: the bounds of the model: center %s, extent %s (cm)"), Interest, *Center.ToString(), *Extent.ToString());
	FSwcLootBag Info;
	MakeLootBag(Interest, Entity, Place, Info);
	UE_LOG(LogSwcCreatures, Log, TEXT("loot bag %u: %s at %.2f %.2f %.2f, rarity %u, the Interact bound at slot %u (target %016llx, range %.1f m)%s"), Interest,
		*Container.Name, Entity.worldPlace.x, Entity.worldPlace.y, Entity.worldPlace.z, Entity.lootRarity, Info.InteractSlot,
		static_cast<unsigned long long>(Info.Target), FMath::Sqrt(Info.RangeSq), Bag->GetStaticMeshComponent()->GetStaticMesh() != nullptr ? TEXT("") : TEXT(" (no model)"));
}

bool USwcCreatureSubsystem::MakeLootBag(uint32 Interest, const swc::creatures::Entity& Entity, const FVector& From, FSwcLootBag& Out) const
{
	const FSwcLootContainer* Container = Entity.hasMetadata ? Library.FindLootContainer(UTF8_TO_TCHAR(Entity.slice_key().c_str())) : nullptr;
	if (Container == nullptr || !Entity.hasWorldPlace)
	{
		return false;
	}
	Out = FSwcLootBag();
	Out.Interest = Interest;
	Out.Actor = Entity.actor;
	Out.Group = swc::net::group_of(Entity.actor);
	Out.EnginePlace = Entity.worldPlace;
	Out.Place = SwcEngineToUnreal(Entity.worldPlace);
	Out.Rarity = Entity.lootRarity;
	Out.Distance = static_cast<float>(FVector::Dist(From, Out.Place) / 100.0);
	if (!Container->Bounds.IsEmpty())
	{
		// The ids of a server child come from G of its actor (the XOR rule of protocol notes 4.2; backend Interactions::spawn).
		const FSwcLootContainer::FBound& Bound = Container->Bounds[0];
		Out.InteractSlot = Bound.Slot;
		Out.Target = swc::creatures::facet_target(Out.Group, Bound.Entity ^ Container->Root, Bound.Component);
		Out.RangeSq = Bound.RangeSq;
		const auto State = Entity.interact.find(Bound.Slot);
		Out.bEnabled = State == Entity.interact.end() || State->second.enabled;
		for (const TPair<uint32, uint64>& Option : Bound.Options)
		{
			Out.Options.Add({Option.Key, Option.Value ^ Container->Root ^ Out.Group});
		}
	}
	return true;
}

void USwcCreatureSubsystem::GetLootBags(const FVector& From, TArray<FSwcLootBag>& Out) const
{
	Out.Reset();
	for (const TPair<uint32, TWeakObjectPtr<AActor>>& Pair : Bags)
	{
		const swc::creatures::Entity* Entity = Table.find(Pair.Key);
		FSwcLootBag Bag;
		if (Entity != nullptr && MakeLootBag(Pair.Key, *Entity, From, Bag))
		{
			Out.Add(MoveTemp(Bag));
		}
	}
}

bool USwcCreatureSubsystem::FindNearestLootBag(const FVector& From, FSwcLootBag& Out) const
{
	TArray<FSwcLootBag> All;
	GetLootBags(From, All);
	const FSwcLootBag* Best = nullptr;
	for (const FSwcLootBag& Bag : All)
	{
		// The check of the server (client code 0x35C2E60): the squared 3D distance is not more than m_interactRangeSq.
		if (Bag.bEnabled && Bag.InteractSlot != 0 && Bag.Distance * Bag.Distance <= Bag.RangeSq && (Best == nullptr || Bag.Distance < Best->Distance))
		{
			Best = &Bag;
		}
	}
	if (Best == nullptr)
	{
		return false;
	}
	Out = *Best;
	return true;
}

bool USwcCreatureSubsystem::IsCreature(uint64 Group) const
{
	for (const TPair<uint32, TWeakObjectPtr<ASwcCreature>>& Pair : Actors)
	{
		const swc::creatures::Entity* Entity = Table.find(Pair.Key);
		if (Entity != nullptr && Entity->hasMetadata && swc::net::group_of(Entity->actor) == Group)
		{
			return true;
		}
	}
	return false;
}

FString USwcCreatureSubsystem::CreatureDisplayName(uint64 Group) const
{
	for (const auto& [Interest, Entity] : Table.entities())
	{
		if (Entity.hasMetadata && swc::net::group_of(Entity.actor) == Group)
		{
			const TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Interest);
			return Actor != nullptr && Actor->IsValid() ? (*Actor)->GetDisplayName() : FString();
		}
	}
	return FString();
}

FString USwcCreatureSubsystem::CreatureName(uint64 Group) const
{
	for (const auto& [Interest, Entity] : Table.entities())
	{
		if (Entity.hasMetadata && swc::net::group_of(Entity.actor) == Group)
		{
			const TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Interest);
			return FString::Printf(TEXT("creature %u %s"), Interest, Actor != nullptr && Actor->IsValid() ? *(*Actor)->GetDisplayName() : TEXT(""));
		}
	}
	return FString::Printf(TEXT("entity %016llx"), static_cast<unsigned long long>(Group));
}

void USwcCreatureSubsystem::UpdateEffect(uint32 Interest, const swc::creatures::Entity& Entity, uint64 RenderTimeUs)
{
	TWeakObjectPtr<ASwcEffectActor>* Found = Effects.Find(Interest);
	ASwcEffectActor* Effect = Found != nullptr ? Found->Get() : nullptr;
	if (Effect == nullptr)
	{
		// The slice of the metadata state gives the data of the effect; the Spell state gives the spell row (its key). The shape and
		// its material are common assets in memory.
		if (!Entity.hasMetadata || !bCommonReady)
		{
			return;
		}
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Effect = GetWorld()->SpawnActor<ASwcEffectActor>(ASwcEffectActor::StaticClass(), FTransform::Identity, Parameters);
		if (Effect == nullptr)
		{
			return;
		}
		Effect->SetSerial(Entity.serial);
		const FString Key = UTF8_TO_TCHAR(Entity.slice_key().c_str());
		if (Entity.hasProjectile)
		{
			const FSwcProjectileSlice* Slice = Library.FindProjectile(Key);
			Effect->SetupProjectile(Slice);
			++Shots;
			UE_LOG(LogSwcCreatures, Log, TEXT("effect %u: a shot of %s (%s), launch %.2f %.2f %.2f, velocity %.1f %.1f %.1f m/s"), Interest,
				*CreatureName(Entity.projectile.owner), Slice != nullptr ? *Slice->Path : *Key, Entity.projectile.anchor.x, Entity.projectile.anchor.y,
				Entity.projectile.anchor.z, Entity.projectile.velocity[0], Entity.projectile.velocity[1], Entity.projectile.velocity[2]);
		}
		else
		{
			const FSwcSpellSlice* Slice = Library.FindSpell(Key);
			const FSwcSpellRow* Row = Entity.spell.hasSpell ? Library.FindSpellRow(Entity.spell.key) : nullptr;
			// A spell of a character, not of a creature: its caster is no creature of the table; a spell with no Spell state (a trap,
			// a status area) by the folder of its slice (the slices of the weapons of the players).
			const bool bCreatureCaster = Entity.spell.hasSpell && IsCreature(Entity.spell.caster);
			const bool bFriendly = Entity.spell.hasSpell ? !bCreatureCaster : (Slice != nullptr && Slice->Path.StartsWith(TEXT("slices/items/weapons/")));
			Effect->SetupArea(Slice, Row, bFriendly, Entity.hasVitals);
			++Areas;
			UE_LOG(LogSwcCreatures, Log, TEXT("effect %u: %s %s of %s (%s), radius %.2f m%s%s"), Interest, bFriendly ? TEXT("a spell") : TEXT("an area"),
				Row != nullptr ? *Row->Id : TEXT("(no Spell state)"), Entity.spell.hasSpell ? *CreatureName(Entity.spell.caster) : TEXT("-"),
				Slice != nullptr ? *Slice->Path : *Key, Effect->GetRadius(), Entity.hasVitals ? TEXT(", with health (a summon)") : TEXT(""),
				Entity.spell.attached ? *FString::Printf(TEXT(", it follows %s"), *CreatureName(Entity.spell.attachedGroup)) : TEXT(""));
		}
		Effects.Add(Interest, Effect);
	}
	// A spell that follows an entity (the Beacon): the shown place of a creature, or of the local player.
	FVector FollowPlace;
	const FVector* Follow = nullptr;
	if (Entity.hasSpellParts && Entity.spell.attached)
	{
		for (const TPair<uint32, TWeakObjectPtr<ASwcCreature>>& Pair : Actors)
		{
			const swc::creatures::Entity* Other = Table.find(Pair.Key);
			if (Other != nullptr && Other->hasMetadata && swc::net::group_of(Other->actor) == Entity.spell.attachedGroup && Pair.Value.IsValid())
			{
				FollowPlace = Pair.Value->GetActorLocation();
				Follow = &FollowPlace;
			}
		}
		UGameInstance* GameInstance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
		const USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
		swc::net::RepClient::OwnEntity Own;
		const APlayerController* Player = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (Follow == nullptr && Backend != nullptr && Backend->GetLocalPlayer(Own) && Own.group == Entity.spell.attachedGroup && Player != nullptr
			&& Player->GetPawn() != nullptr)
		{
			FollowPlace = Player->GetPawn()->GetActorLocation() - FVector(0.0, 0.0, Player->GetPawn()->GetSimpleCollisionHalfHeight());
			Follow = &FollowPlace;
		}
	}
	const FString Text = Effect->UpdateFromEntity(Entity, RenderTimeUs, Follow);
	if (!Text.IsEmpty())
	{
		UE_LOG(LogSwcCreatures, Log, TEXT("effect %u: %s"), Interest, *Text);
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
	const FBoxSphereBounds Bounds = Creature->GetBodyBounds();
	// The event for the UI (stage WF7): the newest 32 stay.
	FSwcDamageEvent& Event = DamageEvents.AddDefaulted_GetRef();
	Event.Serial = ++DamageSerial;
	Event.Interest = Interest;
	Event.Point = Bounds.Origin + FVector(0.0, 0.0, Bounds.BoxExtent.Z * 0.6);
	Event.Flags = Damage->flags;
	Event.Time = FPlatformTime::Seconds();
	for (const swc::creatures::DamageEntry& Entry : Damage->entries)
	{
		Event.Entries.Emplace(Entry.type, Entry.amount);
	}
	if (DamageEvents.Num() > 32)
	{
		DamageEvents.RemoveAt(0, DamageEvents.Num() - 32);
	}
	// The whole part of the damage (no rounding up: 64.9 shows 64).
	FDamageNumber& Number = DamageNumbers.AddDefaulted_GetRef();
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
	UFont* Font = GEngine != nullptr ? GEngine->GetLargeFont() : nullptr;
	if (Font == nullptr)
	{
		return;
	}
	for (const FDamageNumber& Number : DamageNumbers)
	{
		const double Age = Now - Number.Start;
		const FVector Screen = Canvas->Project(Number.Point + FVector(0.0, 0.0, DamageRise * Age / DamageSeconds));
		if (Screen.Z <= 0.0)
		{
			continue;
		}
		// A critical hit: 1.4 times larger.
		const float Scale = Number.bCrit ? 1.4f : 1.0f;
		const float Alpha = static_cast<float>(FMath::Clamp((DamageSeconds - Age) / DamageFadeSeconds, 0.0, 1.0));
		float Width = 0.0f;
		float Height = 0.0f;
		Canvas->StrLen(Font, Number.Text, Width, Height);
		Width *= Scale;
		Height *= Scale;
		const FLinearColor Color = Number.bCrit ? FLinearColor(1.0f, 0.85f, 0.2f, Alpha) : FLinearColor(1.0f, 1.0f, 1.0f, Alpha);
		FCanvasTextItem Text(FVector2D(Screen.X - Width * 0.5f, Screen.Y - Height * 0.5f), FText::FromString(Number.Text), Font, Color);
		Text.Scale = FVector2D(Scale, Scale);
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
			const ASwcCreature& Creature = **Actor;
			FSwcCreatureTarget& Target = Out.Add_GetRef({Interest, Creature.GetActorLocation(), Entity.dead, Creature.GetLockPoint()});
			Target.AxisA = Target.AxisB = Target.Position;
			if (const FSwcCreatureSlice* Slice = Creature.GetSlice(); Slice != nullptr && Slice->bHasCapsule)
			{
				// The frame of the creature (x right, y front, z up) in the frame of the actor (X front, Y right, Z up), m to cm.
				const auto ToActor = [](const FVector& Engine) { return FVector(Engine.Y, Engine.X, Engine.Z) * 100.0; };
				const FTransform Frame(FRotator(0.0, Creature.GetActorRotation().Yaw, 0.0), Target.Position);
				const FVector Offset = ToActor(Slice->CapsuleOffset);
				const FVector Axis = ToActor(Slice->CapsuleAxis).GetSafeNormal() * Slice->CapsuleHeight * 100.0;
				Target.AxisA = Frame.TransformPosition(Offset - Axis);
				Target.AxisB = Frame.TransformPosition(Offset + Axis);
			}
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

bool USwcCreatureSubsystem::GetLockPoint(uint32 Interest, FSwcLockPoint& Out) const
{
	const TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Interest);
	if (Actor == nullptr || !Actor->IsValid())
	{
		return false;
	}
	Out = FSwcLockPoint();
	Out.Point = (*Actor)->GetLockPoint();
	if (const FSwcCreatureSlice* Slice = (*Actor)->GetSlice(); Slice != nullptr && Slice->bHasLock)
	{
		Out.bLimitPitch = Slice->bLimitPitch;
		Out.PitchMin = Slice->PitchMin;
		Out.PitchMax = Slice->PitchMax;
	}
	return true;
}

bool USwcCreatureSubsystem::ShowsPlate(const ASwcCreature& Creature, const FVector& Eye, const FVector& Look, const FVector& Own, double& OutDistance) const
{
	const double Distance = FVector::Dist(Own, Creature.GetActorLocation()) / 100.0;
	OutDistance = Distance;
	// The rule of the marker of the slice (MarkerComponent client facet): the near range, or the crosshair within an angle
	// that shrinks from the near angle at the near range to the far angle at the shrink distance.
	const FSwcCreatureSlice* Slice = Creature.GetSlice();
	const double NearRange = Slice != nullptr ? Slice->NearRange : 6.0;
	const double NearAngle = Slice != nullptr ? Slice->NearAngle : 0.1309;
	const double FarAngle = Slice != nullptr ? Slice->FarAngle : 0.0174533;
	const double Shrink = Slice != nullptr ? FMath::Max(Slice->AngleShrinkDistance, NearRange + 0.1) : 20.0;
	const FVector Center = Creature.GetActorLocation() + FVector(0.0, 0.0, 80.0 * Creature.GetActorScale3D().Z);
	const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Look, (Center - Eye).GetSafeNormal()), -1.0, 1.0));
	const double Allowed = FMath::Lerp(NearAngle, FarAngle, FMath::Clamp((Distance - NearRange) / (Shrink - NearRange), 0.0, 1.0));
	return Distance <= NearRange || (Distance <= CrosshairRange && Angle <= Allowed) || (bAllPlates && Distance <= AllPlatesRange);
}

void USwcCreatureSubsystem::GetPlates(TArray<FSwcCreaturePlate>& Out) const
{
	Out.Reset();
	const APlayerController* Player = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Player == nullptr || Player->PlayerCameraManager == nullptr)
	{
		return;
	}
	const FVector Eye = Player->PlayerCameraManager->GetCameraLocation();
	const FVector Look = Player->PlayerCameraManager->GetCameraRotation().Vector();
	const FVector Own = Player->GetPawn() != nullptr ? Player->GetPawn()->GetActorLocation() : Eye;
	const double Range = bAllPlates ? FMath::Max(CrosshairRange, AllPlatesRange) : CrosshairRange;
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
		double Distance = 0.0;
		const bool bShown = ShowsPlate(*Creature, Eye, Look, Own, Distance);
		if (Distance > Range)
		{
			continue;
		}
		const FSwcCreatureSlice* Slice = Creature->GetSlice();
		FSwcCreaturePlate& Plate = Out.AddDefaulted_GetRef();
		Plate.Interest = Interest;
		Plate.Serial = Entity.serial;
		Plate.Feet = Creature->GetActorLocation();
		Plate.PlatePoint = Creature->GetPlatePoint();
		Plate.Name = Creature->GetDisplayName();
		Plate.CreatureType = Library.CreatureType(Entity.categoryId, Entity.vitalsId);
		Plate.Level = Entity.level;
		Plate.bHidesLevel = Creature->HidesLevel();
		Plate.Health = Entity.health;
		Plate.HealthMax = Entity.healthMax;
		Plate.HealthRevision = Entity.healthRevision;
		Plate.bDead = Entity.dead;
		Plate.DeadSeconds = DeadSeconds;
		Plate.bImmune = Entity.immune;
		if (Slice != nullptr)
		{
			Plate.MarkerType = Slice->MarkerType;
			Plate.CreatureTypeOverride = Slice->CreatureTypeOverride;
			Plate.bNeverShowHealthBar = Slice->bNeverShowHealthBar;
			Plate.bForceDisplayHealthBar = Slice->bForceDisplayHealthBar;
			Plate.VerticalOffset = Slice->VerticalOffset;
		}
		Plate.Distance = static_cast<float>(Distance);
		Plate.bShown = bShown;
		for (const swc::creatures::Entity::StatusEntry& Status : Entity.statuses)
		{
			const FSwcStatusRow* Row = Library.FindStatusRow(Status.index);
			Plate.Statuses.Add({Row != nullptr ? Row->Id : FString::Printf(TEXT("#%u"), Status.index), Status.stacks});
		}
	}
}

uint32 USwcCreatureSubsystem::GetDamageEvents(uint32 After, TArray<FSwcDamageEvent>& Out) const
{
	Out.Reset();
	for (const FSwcDamageEvent& Event : DamageEvents)
	{
		if (Event.Serial > After)
		{
			Out.Add(Event);
		}
	}
	return DamageSerial;
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
	if (bUiPlates)
	{
		// The UI draws the original plates and numbers (stage WF7).
		DamageNumbers.Reset();
		return;
	}
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
		const FSwcCreatureSlice* Slice = Creature->GetSlice();
		double Distance = 0.0;
		if (!ShowsPlate(*Creature, Eye, Look, Own, Distance))
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
		// The status effects of the compact map (the name plate of the creature): a simple line of their names (the original shows
		// the icons of the rows; the UI track).
		if (!Entity.statuses.empty())
		{
			FString Line;
			for (const swc::creatures::Entity::StatusEntry& Status : Entity.statuses)
			{
				const FSwcStatusRow* Row = Library.FindStatusRow(Status.index);
				FString Name = Row == nullptr ? FString::Printf(TEXT("#%u"), Status.index)
					: Row->bStunned ? FString(TEXT("Stunned"))
					: Row->bRoot ? FString(TEXT("Rooted"))
					: Row->MoveSpeedMod < 0.0f ? FString::Printf(TEXT("Slowed %d%%"), FMath::RoundToInt(-Row->MoveSpeedMod * 100.0f))
					: Row->Id;
				if (Status.stacks > 1)
				{
					Name += FString::Printf(TEXT(" x%u"), Status.stacks);
				}
				Line += (Line.IsEmpty() ? TEXT("") : TEXT("  ")) + Name;
			}
			float LineWidth = 0.0f;
			float LineHeight = 0.0f;
			Canvas->StrLen(Font, Line, LineWidth, LineHeight);
			FCanvasTextItem Statuses(FVector2D(X - LineWidth * 0.5f, Y + 3.0f), FText::FromString(Line), Font, FLinearColor(1.0f, 0.8f, 0.3f, 1.0f));
			Statuses.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(Statuses);
		}
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
