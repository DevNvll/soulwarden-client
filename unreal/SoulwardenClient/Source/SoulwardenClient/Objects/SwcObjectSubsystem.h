#pragma once

#include <vector>

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Interact/SwcInteraction.h"
#include "Net/Codec/SwcRepClient.h"
#include "Objects/SwcLevelInstances.h"
#include "Objects/SwcObjectRules.h"
#include "SwcObjectSubsystem.generated.h"

class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** The state of a world object for the local character (FSwcWorldObjectTarget::State). */
enum class ESwcWorldObjectState : uint8
{
	/** The data state: the object can be used. */
	Ready,
	/** A gathering node with the node state Depleted (no prompt, the node hides). */
	Depleted,
	/** A chest with the LootTracker claim Looted (no prompt; the light of the loot goes off). */
	Looted,
	/** A chest with the LootTracker claim Locked or NotOnRequiredQuest (no prompt). */
	Locked,
};

/** One option of a bound of a world object, ready for the request of the player track (swc::interact::Use). */
struct FSwcWorldObjectOption
{
	/** The index of the option in the options table of interaction-data.json. */
	int32 Index = 0;
	/** The option of the data (its name, CRC-32 id, action, committed mark, Delay Time, hold time). Its `entity` is the instance id of
	 *  the option entity (the value of the block), not the entity in the frame of the slice. */
	swc::interact::Option Option;
	/** The request of the block for this option (the reference of the bound, the action, the option entity, the option id). */
	swc::net::InteractionRequest Request;
};

/** One Interact bound of a world object near the character (USwcObjectSubsystem::FindNearestObject, GetObjectsNear). */
struct FSwcWorldObjectTarget
{
	/** The key of the object: the UUID of its placement, or REGION#RECORD of a natural node. */
	FString Key;
	/** The kind of the data: lorePage, chest, node (a capital node), naturalNode, gatherable, camp, craftingStation, storage,
	 *  tradingPost, npc, shrine, door, restFurniture, other. */
	FName Kind;
	/** The actor id of the object and its entity group G (the reference of the request). */
	swc::net::ActorId Actor{};
	uint64 Group = 0;
	/** The bound: the slot of its Interact state, its facet target, its place (Unreal cm; engine m), m_interactRangeSq (m^2), and
	 *  the line of sight mark of the data. */
	uint32 Slot = 0;
	uint64 Target = 0;
	FVector Place = FVector::ZeroVector;
	swc::net::WorldPosition EnginePlace;
	float RangeSq = 0.0f;
	bool bLineOfSight = false;
	TArray<FSwcWorldObjectOption> Options;
	/** The server state, and the Enabled mark of the Interact state of a record of the object (true with no record). */
	ESwcWorldObjectState State = ESwcWorldObjectState::Ready;
	bool bEnabled = true;
	/** A lore page: the bit of its achievement, and true when the Achievement state of the character has it. */
	int32 LoreBit = -1;
	bool bRead = false;
	/** The 3D distance from the point of the query (m). */
	float Distance = 0.0f;

	/** The gathering (stage P5.2; a node, a chest, a body): the gatherable row (-1: none), its GatheringType (the tool family:
	 *  Chopping, Cutting, Dressing, Mining, None) and GatheringAction (the tag of the Gather fragment), and the name of the row. */
	int32 Row = -1;
	FString GatheringType;
	FString GatherAction;
	FString Name;
	/** The tool: its paperdoll slot (33 axe, 34 sickle, 35 knife, 36 pick; 0: none), the item index in that slot when its gathering
	 *  types have the type of the row, and its weapon tags. With the restriction ToolRequired and no tool the client sends no request
	 *  and the prompt has RefusalText ("No equipped Axe"; Soulwarden protocol notes 4.44, live check L6). */
	uint32 ToolSlot = 0;
	uint32 ToolItem = 0;
	FString ToolTags;
	bool bToolMissing = false;
	FString RefusalText;
	/** The use fragment of the player for the work ("gather|ACTION|TOOLTAGS": the use fragments of actions.json), empty for a use
	 *  that is not a gather. */
	FString UseKind;
	/** A body (the skinning of a dead creature): the interest number of the creature. */
	uint32 Interest = 0;
};

/**
 * The objects of the level data near the character (stage P4.1 part 2; docs/OBJECTS.md): the lore pages, the chests, the gathering
 * nodes (natural and capital), the stations, the persons, the doors, the shrines, the furniture. The client makes these objects
 * from its data; the server sends a record of one only when its state is not the state of the data.
 *
 * - The data: SWC_CONTENT/world-objects (tools/convert-objects.sh), loaded for the regions within 300 m of the character. The ids
 *   follow the rules of the backend (Objects/SwcObjectRules.h): the actor from the UUID of a placement or the offline id of a
 *   natural node, the facet target and the option entity from the template.
 * - The states: the records of the objects in the entity table of the creature view (the GatherableController state: Healthy,
 *   Depleted; the Interact state: Enabled), the LootTracker claims of the character (a chest), and its Achievement state (a lore
 *   page that it read).
 * - The visuals, only within 100 m of the character (the interest radius of the server): a depleted node hides its healthy mesh and
 *   shows its depleted mesh (the level instance at its place, or an own mesh); a healthy node hides a depleted level instance at
 *   its place (the level before the final pass of the world track places both). A chest that the character can loot has the light
 *   of the loot (a point light; the Idle_VFX of the game is not converted). The meshes stay in the level of the world track: the
 *   layer hides an instance only through USwcLevelInstances.
 * - The query of the player track (Interact/SwcInteractor): FindNearestObject, as FindNearestLootBag of the creature view. The
 *   cost: the grid cells of 64 m near the point, never a scan of the world.
 *
 * Development aid: -SwcObjectProbe logs the nearest object each 1 s and draws the bounds near the character (green: usable, red:
 * not usable).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcObjectSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/**
	 * The nearest bound that the local character can use from a point (the place of the character in its input report, Unreal
	 * world space): its 3D squared distance is not more than its range (equal passes; the backend check), it has an option, its
	 * Interact state is Enabled, and its object state is Ready (a lore page that the character read is Ready: it can read it again).
	 * @return False when no bound is in range.
	 */
	bool FindNearestObject(const FVector& From, FSwcWorldObjectTarget& Out) const;
	/** The bounds within a radius (m) of a point, in each state (for a marker or a check). */
	void GetObjectsNear(const FVector& From, double RadiusMeters, TArray<FSwcWorldObjectTarget>& Out) const;

	/** The radius of the visuals (m): the interest radius of the server (a record of an object starts at 100 m). */
	static constexpr double VisualMeters = 100.0;
	/** The radius of the loaded regions (m). */
	static constexpr double RegionMeters = 300.0;

private:
	struct FOptionRow
	{
		swc::interact::Option Option;
	};
	struct FTemplate
	{
		uint64 Root = 0;
		uint64 BoundEntity = 0;
		uint64 Component = 0;
		uint64 OptionEntity = 0;
		uint32 Slot = 0;
		float RangeSq = 0.0f;
		bool bLineOfSight = false;
		FVector3d Translation = FVector3d::ZeroVector;
		/** The visuals of the variation table of the template (a capital node), and the visuals of the meshes of its slice: the level
		 *  places these default meshes at each node of the slice, not the meshes of the variant of the record. */
		TArray<int32> Visuals;
		TArray<int32> SliceVisuals;
	};
	struct FVisual
	{
		FString StartFolder;
		FString DepletedFolder;
		FString DepletedMesh;
	};
	struct FBound
	{
		uint32 Slot = 0;
		uint64 Target = 0;
		FVector3d EnginePlace = FVector3d::ZeroVector;
		float RangeSq = 0.0f;
		bool bLineOfSight = false;
		TArray<TPair<int32, uint64>> Options;
		int32 LoreBit = -1;
	};
	struct FDataObject
	{
		FString Key;
		FName Kind;
		swc::net::ActorId Actor{};
		uint64 Group = 0;
		FVector3d EnginePlace = FVector3d::ZeroVector;
		TArray<FBound> Bounds;
		/** A capital node: its gathering template and its gatherable row. */
		int32 Template = -1;
		int32 Row = -1;
	};
	/** A gatherable row of the gathering data (stage P5.2). */
	struct FGatherRow
	{
		FString Id;
		FString Type;
		FString Action;
		FString Restriction;
		FString Tradeskill;
		int32 Level = 0;
		float Time = 0.0f;
		FString Name;
	};
	/** A gathering tool by its item index: its id, its weapon tags, and its gathering types. */
	struct FToolItem
	{
		FString Id;
		FString Tags;
		TArray<FString> Types;
	};
	struct FNodeType
	{
		int32 Template = -1;
		int32 Row = -1;
		int32 Visual = -1;
	};
	struct FRegion
	{
		FString Name;
		int32 X = 0;
		int32 Y = 0;
		TArray<FDataObject> Objects;
		/** The data objects by their cell of 64 m (global cells: floor(x / 64), floor(y / 64)). */
		TMap<FIntPoint, TArray<int32>> Grid;
		swc::objects::NodeFile Nodes;
		TArray<FNodeType> Types;
	};
	/** A gathering node near the character (natural or capital) and its visual. */
	struct FNodeView
	{
		FString Key;
		FVector Place = FVector::ZeroVector;
		TArray<int32> Visuals;
		uint8 AppliedState = 0;
		uint32 AppliedRevision = 0;
		TArray<FSwcMeshInstanceRef> HiddenHealthy;
		TArray<FSwcMeshInstanceRef> HiddenDepleted;
		TWeakObjectPtr<UStaticMeshComponent> Own;
		bool bSeen = false;
		/** The gatherable row of the node (a tree falls: GatheringType Chopping). */
		int32 Row = -1;
	};
	/** A tree that falls at its depletion (a stand-in for the cut trunk of the client: the healthy mesh turns around its base). */
	struct FFall
	{
		/** A copy of each healthy instance of the node (a tree has several meshes) and its level transform. */
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Components;
		TArray<FTransform> Bases;
		/** The base of the tree (the place of the node) and the axis of the fall. */
		FVector Pivot = FVector::ZeroVector;
		FVector Axis = FVector::ZeroVector;
		double Start = 0.0;
	};

	bool LoadTables(FString& OutReport);
	void UpdateRegions(const swc::net::WorldPosition& Center);
	bool LoadRegion(int32 X, int32 Y, FRegion& Out);
	void ReadStates();
	void UpdateVisuals(const FVector& Center);
	void RebuildInstanceIndex(const FVector& Center);
	/** bLive: the state changed while the character saw the node (a tree then falls). */
	void ApplyNode(uint64 Group, FNodeView& View, uint8 State, bool bLive = false);
	void UpdateFalls(double Now);
	void RestoreNode(FNodeView& View);
	void UpdateChestLights(const FVector& Center);
	void ClearAll(const TCHAR* Cause);
	AActor* VisualActor();
	/** @return 1 for a StartMesh folder of the visuals, 2 for a DepletedMesh folder, 0 for none. */
	int32 MeshRole(const UStaticMesh* Mesh, const TArray<int32>& Visuals) const;
	void FindAt(const FVector& Place, TArray<FSwcMeshInstanceRef>& Out) const;
	void AddTargets(const FVector3d& From, double RadiusMeters, bool bUsableOnly, TArray<FSwcWorldObjectTarget>& Out) const;
	void MakeTarget(const FString& Key, FName Kind, const swc::net::ActorId& Actor, uint64 Group, const FBound& Bound, const FVector3d& From,
		FSwcWorldObjectTarget& Out) const;
	ESwcWorldObjectState StateOf(FName Kind, uint64 Group) const;
	/** The gathering fields of a target from its row (the tool of the paperdoll, the refusal, the use fragment). */
	void FillGather(FSwcWorldObjectTarget& Out, int32 Row) const;
	/** The skinnable bodies of the creature view near a point (stage P5.2). */
	void AddBodies(const FVector3d& From, double RadiusMeters, bool bUsableOnly, TArray<FSwcWorldObjectTarget>& Out) const;
	FString Text(const FString& Key) const;
	void DevelopmentAids();

	bool bTablesLoaded = false;
	bool bTablesFailed = false;
	TArray<FOptionRow> Options;
	/** The index of GatherOption in the options (the option of each natural node). */
	int32 GatherOptionIndex = 0;
	/** The largest range of a bound of the loaded data (m): the radius of the cells of FindNearestObject. */
	double MaxRangeMeters = 0.0;
	TArray<FTemplate> Templates;
	TArray<FVisual> Visuals;
	TMap<FString, TArray<int32>> StartFolders;
	TMap<FString, TArray<int32>> DepletedFolders;
	TMap<FString, TArray<FNodeType>> RegionTypes;
	/** The gathering (stage P5.2): the rows, the tools by item index, the bodies (the slice key of a creature -> its template and row),
	 *  the texts of the prompts, and the item index of each tool slot of the paperdoll (33 to 36). */
	TArray<FGatherRow> Rows;
	TMap<uint32, FToolItem> Tools;
	TMap<FString, TPair<int32, int32>> Bodies;
	TMap<FString, FString> Texts;
	TMap<uint32, uint32> Equipment;
	TMap<FIntPoint, FRegion> Regions;

	/** The states of the records of the objects (by G): the node state, the Enabled marks of the Interact states by slot. */
	TMap<uint64, uint8> NodeStates;
	TMap<uint64, TMap<uint32, bool>> BoundEnabled;
	uint32 SeenTableRevision = 0;
	/** The LootTracker claims of the character by G of the actor, and its Achievement bytes. */
	TMap<uint64, uint32> ChestClaims;
	std::vector<uint8> Achievements;
	uint32 SeenLootRevision = 0;
	uint32 SeenAchievementRevision = 0;
	uint32 StateRevision = 0;

	/** The visuals: the node views within VisualMeters, the instance index of the node meshes near the character. */
	TMap<uint64, FNodeView> NodeViews;
	TMap<FIntPoint, TArray<FSwcMeshInstanceRef>> InstanceIndex;
	FVector IndexCenter = FVector(1.0e12);
	uint32 IndexRevision = 0;
	FVector VisualCenter = FVector(1.0e12);
	uint32 VisualStateRevision = 0;
	mutable TMap<const UStaticMesh*, FString> MeshFolders;
	TMap<FString, TObjectPtr<UStaticMesh>> DepletedMeshes;
	TWeakObjectPtr<AActor> Visual;
	TMap<uint64, TWeakObjectPtr<UPointLightComponent>> ChestLights;
	TArray<FFall> Falls;
	double NextUpdate = 0.0;
	double NextProbe = 0.0;
	bool bConnected = false;
	bool bProbe = false;
	/** The counts for the log. */
	int32 Depletions = 0;
	int32 OwnMeshes = 0;
};
