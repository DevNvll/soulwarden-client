#pragma once

#include "CoreMinimal.h"
#include "Creatures/SwcCreatureData.h"
#include "Creatures/SwcEntityTable.h"
#include "Combat/SwcMeleeShape.h"
#include "Creatures/SwcHitShapes.h"
#include "Creatures/SwcMeleeContact.h"
#include "Engine/StreamableManager.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcCreatureSubsystem.generated.h"

class APlayerController;
class ASwcCreature;
class ASwcEffectActor;
class UCanvas;

DECLARE_LOG_CATEGORY_EXTERN(LogSwcCreatures, Log, All);

/** A creature for the target lock of the camera: its interest number, its feet (Unreal cm), the dead mark, and its lock point
 *  (ASwcCreature::GetLockPoint: the CameraLockTargetComponent of the slice). */
struct FSwcCreatureTarget
{
	uint32 Interest = 0;
	FVector Position = FVector::ZeroVector;
	bool bDead = false;
	FVector LockPoint = FVector::ZeroVector;
	/** The homing segment of an attack (stage P3.G, research p3g-collision): the axis of the body capsule of the slice, the offset
	 *  +- the segment length x the axis in the frame of the creature (Unreal world space; the wolf: 0.8 m behind to 1.0 m in front of
	 *  its center); with no capsule both are the position. */
	FVector AxisA = FVector::ZeroVector;
	FVector AxisB = FVector::ZeroVector;
};

/** The lock point of a creature for the camera (stage P3.5): the point (Unreal world space), and the pitch limits of the observer of
 *  the client facet of its CameraLockTargetComponent (degrees; m_limitObserverPitch, m_observerPitchMin, m_observerPitchMax). */
struct FSwcLockPoint
{
	FVector Point = FVector::ZeroVector;
	bool bLimitPitch = false;
	float PitchMin = 0.0f;
	float PitchMax = 0.0f;
};

/**
 * A loot bag of the local character (stage P2.10; protocol notes 4.42; Soulwarden docs/systems/loot-bags.md): the personal bag of a
 * kill or of a discard. The server sends the record of a bag only to its eligible characters: each bag of the table is a bag of the
 * local character.
 */
struct FSwcLootBag
{
	uint32 Interest = 0;
	/** The reference of the interaction request: the actor id of the bag and its G. */
	swc::net::ActorId Actor{};
	uint64 Group = 0;
	/** The place of the bag (Unreal world space, cm; engine space, m) and the rarity of its LootDrop state. */
	FVector Place = FVector::ZeroVector;
	swc::net::WorldPosition EnginePlace;
	uint32 Rarity = 0;
	/** The Interact bound of the bag (the first one of its slice): the slot of its Interact state, its facet target (the target of
	 *  the interaction request: (entity XOR root XOR G) + component), m_interactRangeSq (m^2), the Enabled mark of its state, and
	 *  its options: the index of the option in the options table of interaction-data.json (80, 30; not a CRC-32 id) and the
	 *  instance id of the option entity (entity XOR root XOR G). */
	uint32 InteractSlot = 0;
	uint64 Target = 0;
	float RangeSq = 0.0f;
	bool bEnabled = true;
	TArray<TPair<uint32, uint64>> Options;
	/** The 3D distance from the point of the query (m). */
	float Distance = 0.0f;
};

/** One status of a creature at its name plate: the id of the status row and the stacks. */
struct FSwcPlateStatus
{
	FString Id;
	uint32 Stacks = 0;
};

/**
 * The values of the name plate of one creature for the UI (the AiMarker plate of lyshineui/markers/markermanager.dynamicuicanvas,
 * the UI track; stage WF7): GetPlates gives each creature with vitals and a marker within the range of the plates.
 */
struct FSwcCreaturePlate
{
	uint32 Interest = 0;
	/** One more for each new entity of the table (a new record of the same interest number has a new serial). */
	uint64 Serial = 0;
	/** The feet (the place of the entity) and the point of the simple plate (above the bounds of the body), Unreal world space. */
	FVector Feet = FVector::ZeroVector;
	FVector PlatePoint = FVector::ZeroVector;
	/** The display name, the marker type of the slice ("Wolf"; empty: none), m_creatureTypeOverride, and the creature type of the
	 *  category or vitals row (names.json: "Solo", "Critter", "Elite", "Named_Solo+", ...). */
	FString Name;
	FString MarkerType;
	FString CreatureTypeOverride;
	FString CreatureType;
	uint32 Level = 0;
	bool bHidesLevel = false;
	float Health = 0.0f;
	float HealthMax = 0.0f;
	/** One more at each health change that the plate shows (bit 0 of healthChangeFlags). */
	uint32 HealthRevision = 0;
	bool bDead = false;
	/** The time since the dead mark (s), or a negative value. */
	double DeadSeconds = -1.0;
	bool bImmune = false;
	/** The marker of the slice: m_neverShowHealthBar, m_forceDisplayHealthBar, m_verticalOffset (m). */
	bool bNeverShowHealthBar = false;
	bool bForceDisplayHealthBar = false;
	float VerticalOffset = 0.0f;
	/** The distance from the player (m), and the show rule of the simple plate (the near range, or the crosshair within the angle of
	 *  the marker of the slice; -SwcPlates=all). */
	float Distance = 0.0f;
	bool bShown = false;
	TArray<FSwcPlateStatus> Statuses;
};

/** One damage number of the player (the message "on damage dealt", 2071) for the UI: the creature, its point, the entries, and the
 *  flags (Creatures/SwcDamageMessage.h). */
struct FSwcDamageEvent
{
	/** One more for each event (the first is 1). */
	uint32 Serial = 0;
	uint32 Interest = 0;
	/** The point of the simple damage number at the creature (Unreal world space), with no side offset. */
	FVector Point = FVector::ZeroVector;
	uint16 Flags = 0;
	/** The entries: the number of the damage type on the wire (DamageTypes of combat-tables.json) and the amount. */
	TArray<TPair<uint8, float>> Entries;
	double Time = 0.0;
};

/** A melee contact: the weapon shape of the player touches a hit volume of a creature (FindMeleeContacts). */
struct FSwcMeleeContact
{
	uint32 Interest = 0;
	/** The hit volume (Head, Leg_F_L, ...) and its marks. */
	FString Volume;
	bool bHeadshot = false;
	bool bLegshot = false;
	/** The point on the surface of the volume and its normal (Unreal world space). */
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
	/** The item of list 3 of the input entry (engine space; swc::net::write_melee_contact writes its 96 bytes). */
	swc::net::MeleeContactItem Item;
	/** The distance from the feet of the attacker to the feet of the creature (m), for the log. */
	float Distance = 0.0f;
	/** A contact of the development switch -SwcFarContact: a creature out of the reach (the server refuses it: "too far"). */
	bool bDevelopment = false;
};

/**
 * The creatures of the backend in the world of the game (docs/CREATURES.md): the entity table of the other entities (the
 * records and the stops of USwcBackendSubsystem::TakeOtherUpdates), one ASwcCreature for each entity with a slice and a
 * position, the movement states shown with a delay of InterpolationDelay on the server clock, and the name plates (a simple
 * plate drawn on the canvas of the game viewport; the original is the AiMarker template of
 * lyshineui/markers/markermanager.dynamicuicanvas, for the UI track).
 *
 * The loot bags of the local character (stage P2.10): the model of the bag slice at its place, and the query of the bag in range
 * for the player track (FindNearestLootBag).
 *
 * The shots and the danger areas of the creatures (stage P2.9): the entities with a Projectile state or the states of a spell
 * (ASwcEffectActor; Creatures/SwcEffects.h).
 *
 * The damage numbers of the player (the message "on damage dealt", Creatures/SwcDamageMessage.h): a simple number at the
 * creature that rises and fades. The original is the damage number canvas of the game (for the UI runtime later).
 *
 * Command line: -SwcPlates=all (development pictures) draws the plate of each creature within 40 m. -SwcHitVolumes draws the
 * hit volumes of the creatures within 20 m and their body capsules (white). -SwcVolumeProbe tests a capsule in front of the player each 0.5 s with
 * FindMeleeContacts and writes a log line for each contact (it sends nothing). -SwcLootProbe writes the result of
 * FindNearestLootBag at the place of the player when it changes (each 1 s), and draws the bounds of each bag (green).
 * -SwcLootModel puts the model of the loot container 2 m in front of the player one time (a look check of the model).
 * -SwcLockPoints draws the lock point of each creature (magenta).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcCreatureSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	/** The end of a world session on a return to the menu that keeps the world (USwcBackendSubsystem::OnWorldSessionEnd). */
	void OnWorldSessionEnd();
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The read path of the camera (the target lock): the creatures of the table with a position. */
	void GetTargets(TArray<FSwcCreatureTarget>& Out) const;
	/** @return False when the creature of an interest number has no actor. */
	bool GetLockPoint(uint32 Interest, FSwcLockPoint& Out) const;
	const swc::creatures::EntityTable& GetTable() const { return Table; }

	/**
	 * The melee contacts of one damage shape of a strike at one input step (the player track: FSwcMeleeShape): the hit volumes
	 * of the creatures at the pose that they show (protocol notes 4.15). One contact for each creature (the touched volume
	 * nearest to the center of the shape); none for a dead creature, for a creature with the mark "invulnerable" (the server
	 * refuses it), for a placeholder, or behind a wall (a line from the chest of the attacker to the point hits the static
	 * world; not with bNoSight). The caller keeps one contact for each creature for each strike.
	 *
	 * Command line: -SwcFarContact[=SECONDS] (development runs) adds, when the shape touches no creature, one contact with the
	 * nearest living creature 6 to 10 m from the attacker (from SECONDS of the world time): the server refuses it ("too far",
	 * reach 3.5 m + 1.5 m).
	 * @return The number of contacts in Out (Out is reset).
	 */
	int32 FindMeleeContacts(const FSwcMeleeShape& Shape, TArray<FSwcMeleeContact>& Out) const;
	/** The loot bags of the local character (with a place and the data of their slice), and their distance from a point (Unreal world
	 *  space, cm). */
	void GetLootBags(const FVector& From, TArray<FSwcLootBag>& Out) const;
	/**
	 * The nearest loot bag that the local character can use from a point (the place of the character in its input report, Unreal
	 * world space): its Interact bound is enabled, and the 3D distance is not more than the range of the bound (the check of the
	 * server: the squared distance against m_interactRangeSq). The player track does the interaction, the take, and the panel.
	 * @return False when no bag is in range.
	 */
	bool FindNearestLootBag(const FVector& From, FSwcLootBag& Out) const;
	/** The player track hides the model of a bag of a Looted claim (HideMeshDelay of the slice, 1.5 s after the claim; stage P2.10). */
	void SetLootBagHidden(uint32 Interest, bool bHidden);
	/** The entity table of the other entities: the object layer (Objects/SwcObjectSubsystem.h) reads the records of the objects of
	 *  the level data from it (the Interact and GatherableController states by the actor of the object). */
	const swc::creatures::EntityTable& GetEntityTable() const { return Table; }
	/** @return The display name of the creature of an entity id (G), or empty: the prompt of a body (the skinning of stage P5.2). */
	FString CreatureDisplayName(uint64 Group) const;
	/** One more at each Tick that applied records (the object layer reads the table again then). */
	uint32 GetTableRevision() const { return TableRevision; }

	/** The name plates for the UI (stage WF7): each creature with vitals and a marker within the range of the plates (the crosshair
	 *  range; with -SwcPlates=all also the range of the development plates), with the show rule of the simple plate in bShown. */
	void GetPlates(TArray<FSwcCreaturePlate>& Out) const;
	/** The damage numbers for the UI (stage WF7): the events after the serial After (the newest 32 stay). @return The newest serial. */
	uint32 GetDamageEvents(uint32 After, TArray<FSwcDamageEvent>& Out) const;
	/** The UI draws the original plates and damage numbers: the simple plates and damage numbers of this subsystem go off (default
	 *  on). */
	void SetUiPlates(bool bUi) { bUiPlates = bUi; }

	/** The contact query of the player actions (USwcPlayerActions::SetContactQuery): the items of FindMeleeContacts. */
	void QueryMeleeContacts(const FSwcMeleeShape& Shape, std::vector<swc::net::MeleeContactItem>& Out) const;

	/** The delay of the shown movement states on the server clock: the backend sends a state for each step of 200 ms. */
	static constexpr double InterpolationDelaySeconds = 0.25;

private:
	FDelegateHandle SessionEnd;
	void Clear(const TCHAR* Cause);
	void Spawn(const swc::creatures::Entity& Entity);
	/** The creature has its body (its assets are in memory): the log line and the wanted list of a slice with no model. */
	void Ready(const swc::creatures::Entity& Entity, ASwcCreature* Creature, double LoadSeconds);
	/** Reads the animation sets of keys on a worker thread, then calls Done on the game thread (each key one time). */
	void RequestAnimations(const TArray<FString>& Keys, TFunction<void()> Done);
	void OnAnimationsParsed(const FString& Key, const TSharedPtr<FSwcCreatureAnimations>& Set);
	TMap<FString, TArray<TFunction<void()>>> AnimationWaiters;
	void DrawPlates(UCanvas* Canvas, APlayerController* Controller);
	void OnDamage(const swc::net::Bytes& Body);
	/** AST-230 (docs/EFFECTS.md "The hits"): plays the hit effect of the newest contact of the character on a creature (within 1 s of the
	 *  damage message, with the critical flag of the message). */
	void PlayImpact(uint32 Interest, const class ASwcCreature& Creature, bool bCrit);
	void DrawDamageNumbers(UCanvas* Canvas);

	/** A damage number on the screen: its point at the creature, its text, its start, and its marks. */
	struct FDamageNumber
	{
		FVector Point = FVector::ZeroVector;
		FString Text;
		double Start = 0.0;
		bool bCrit = false;
		bool bKill = false;
	};
	TArray<FDamageNumber> DamageNumbers;
	/** The damage events for the UI, the last serial, and the switch of the simple plates and numbers. */
	TArray<FSwcDamageEvent> DamageEvents;
	uint32 DamageSerial = 0;
	bool bUiPlates = false;
	/** The show rule of the simple plate (the marker of the slice): Eye and Look of the camera, Own the place of the player. */
	bool ShowsPlate(const ASwcCreature& Creature, const FVector& Eye, const FVector& Look, const FVector& Own, double& OutDistance) const;

	swc::creatures::EntityTable Table;
	uint32 TableRevision = 0;
	/** The async loads of the assets of the creatures (no load on the game thread waits for the World Partition streaming). */
	FStreamableManager Streamer;
	/** The common assets (the engine cylinder and the translucent material of the shots and the areas, the loot container meshes): one
	 *  async load at the load of the library; a bag or an effect actor waits for it. */
	TSharedPtr<FStreamableHandle> CommonHandle;
	bool bCommonReady = false;
	/** The work of the game thread of each spawn (s) and the largest one, for the log. */
	double SpawnWorkMax = 0.0;
	/** The frame watch after a spawn: the longest frame (s) of the next 3 s, for the log. */
	double SpawnWatchUntil = 0.0;
	float SpawnWatchMax = 0.0f;
	int32 SpawnWatchCount = 0;
	FSwcCreatureLibrary Library;
	/** Loads the creature library and the common assets (the first record, or the preload of a loading screen). */
	void EnsureLibrary();
	/** The preload of the kinds near an arrival (AST-216): the gate check "creature kinds". The slice keys of the spawn points of
	 *  each 128 m cell (content/creatures/spawn-cells.json); at a new place of a loading screen the first assets of the slices
	 *  within PreloadRadius load (ASwcCreature::CollectSliceAssets), and the screen ends when they are loaded. */
	bool PreloadKinds(const FVector& Place, FString& Why);
	TMap<FIntPoint, TArray<FString>> SpawnCells;
	bool bSpawnCellsRead = false;
	FVector PreloadPlace = FVector(TNumericLimits<double>::Max());
	TSharedPtr<FStreamableHandle> PreloadHandle;
	int32 PreloadSlices = 0;
	int32 PreloadAssets = 0;
	double PreloadStart = 0.0;
	bool bPreloadLogged = false;
	TMap<uint32, TWeakObjectPtr<ASwcCreature>> Actors;
	/** The loot bags (stage P2.10): the model of the slice at the place of each bag. */
	TMap<uint32, TWeakObjectPtr<AActor>> Bags;
	void UpdateBag(uint32 Interest, const swc::creatures::Entity& Entity, const FSwcLootContainer& Container);
	bool MakeLootBag(uint32 Interest, const swc::creatures::Entity& Entity, const FVector& From, FSwcLootBag& Out) const;
	int32 LootBags = 0;
	/** The shots and the danger areas (stage P2.9): one actor for each entity with a Projectile state or the states of a spell. */
	TMap<uint32, TWeakObjectPtr<ASwcEffectActor>> Effects;
	void UpdateEffect(uint32 Interest, const swc::creatures::Entity& Entity, uint64 RenderTimeUs);
	/** @return The name of the creature of an entity id (G), for the log. */
	FString CreatureName(uint64 Group) const;
	/** @return True when an entity id (G) is a creature with an actor. */
	bool IsCreature(uint64 Group) const;
	int32 Shots = 0;
	int32 Areas = 0;
	FDelegateHandle DrawHandle;
	bool bAllPlates = false;
	bool bNoPlates = false;
	bool bFarContacts = false;
	float FarContactFrom = 0.0f;
	bool bDrawVolumes = false;
	bool bVolumeProbe = false;
	bool bMotionProbe = false;
	double NextMotionProbe = 0.0;
	bool bLootProbe = false;
	bool bLootModel = false;
	bool bDrawLockPoints = false;
	bool bLootModelShown = false;
	double ConnectedAt = 0.0;
	double NextLootProbe = 0.0;
	FString LastLootProbe;
	double NextProbe = 0.0;
	/** The strikes and creatures with a log line of a contact (visit|strike|interest). */
	mutable TSet<FString> LoggedContacts;
	/** AST-230: the contacts of the character that wait for their damage message (the hit effect). */
	struct FPendingHit
	{
		uint32 Interest = 0;
		FVector Point = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		FVector Direction = FVector::ForwardVector;
		FString DamageRow;
		FString Volume;
		double Time = 0.0;
	};
	mutable TArray<FPendingHit> PendingHits;
	TSharedPtr<class FSwcImpactEffects> Impacts;
	void DevelopmentAids();
	bool bConnected = false;
	/** The counts of a session for the log line at its end. */
	int32 Spawned = 0;
	int32 Removed = 0;
	int32 Actions = 0;
	int32 Deaths = 0;
	int32 Placeholders = 0;
	int32 Damages = 0;
	TSet<FString> Kinds;
};
