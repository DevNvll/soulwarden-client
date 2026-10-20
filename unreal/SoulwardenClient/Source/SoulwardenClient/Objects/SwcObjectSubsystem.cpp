#include "Objects/SwcObjectSubsystem.h"

#include "CanvasItem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Creatures/SwcCreatureData.h"
#include "Creatures/SwcCreatureSubsystem.h"
#include "Dom/JsonObject.h"
#include "Debug/DebugDrawService.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/SwcSpace.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshResources.h"
#include "SwcBackendSubsystem.h"

namespace
{
/** The time between two updates of the states and of the visuals (s). */
constexpr double UpdateSeconds = 0.2;
/** The radius of the light of the loot of a chest (m) and its place above the chest (m). The light is a stand-in for the Idle_VFX of
 *  the variation row (Loot.Crate_small01 and others; the particles are not converted): a choice of the look, not of the feel. */
constexpr double ChestLightMeters = 60.0;
constexpr double ChestLightHeight = 0.4;
/** The depletion effect of an elite chest (a stand-in for the DepletedFX of its variation row, cFX_Loot.Chest_Big_Celebration*: the
 *  particles are not converted): a light that fades in BurstSeconds. The loot notice shows a line for LootLineSeconds. */
constexpr double BurstSeconds = 1.5;
constexpr float BurstCandelas = 4000.0f;
constexpr double LootLineSeconds = 4.0;
/** The fall of a tree (a look stand-in, not a value of the data): the angle grows with the square of the time to FallDegrees in
 *  FallSeconds; the trunk then lies FallHoldSeconds and goes. */
/** A depleted mesh that does not load (a level build or an import makes it again): the next try of the load (s). */
constexpr double MeshRetrySeconds = 5.0;
constexpr double FallSeconds = 2.0;
constexpr double FallDegrees = 85.0;
constexpr double FallHoldSeconds = 2.5;
/** The instance of a node mesh at the place of a node: the level places it at the place of the record (cells.py). */
constexpr double InstanceCellCm = 50.0;
constexpr double InstanceNearCm = 30.0;
constexpr double InstanceHeightCm = 150.0;
/** The instance index of the node meshes is made again after the character moves this far (cm). */
constexpr double IndexMoveCm = 2000.0;
constexpr double VisualMoveCm = 800.0;

const FName KindNode(TEXT("node"));
const FName KindNatural(TEXT("naturalNode"));
const FName KindChest(TEXT("chest"));
const FName KindGatherable(TEXT("gatherable"));
const FName KindBody(TEXT("body"));


/** @return The claim of a chest in a map of claims (G to the state), or no value. */
std::optional<uint32> ClaimOf(const TMap<uint64, uint32>& Claims, uint64 Group)
{
	const uint32* Claim = Claims.Find(Group);
	return Claim != nullptr ? std::optional<uint32>(*Claim) : std::nullopt;
}

uint64 HexNumber(const FString& Text)
{
	return FCString::Strtoui64(*Text, nullptr, 16);
}

swc::net::ActorId HexActor(const FString& Text)
{
	swc::net::ActorId Out{};
	for (int32 Index = 0; Index + 1 < Text.Len() && Index / 2 < static_cast<int32>(Out.size()); Index += 2)
	{
		Out[static_cast<size_t>(Index / 2)] = static_cast<uint8>(FCString::Strtoi(*Text.Mid(Index, 2), nullptr, 16));
	}
	return Out;
}

FVector3d ReadPlace(const TArray<TSharedPtr<FJsonValue>>& Values)
{
	return Values.Num() == 3 ? FVector3d(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber()) : FVector3d::ZeroVector;
}

FVector ToUnreal(const FVector3d& Engine)
{
	return FVector(Engine.X * 100.0, -Engine.Y * 100.0, Engine.Z * 100.0);
}

int32 FloorDiv(int32 Value, int32 By)
{
	return Value >= 0 ? Value / By : -((-Value + By - 1) / By);
}

FString RegionName(int32 X, int32 Y)
{
	return FString::Printf(TEXT("r_%+03d_%+03d"), X, Y);
}

const TCHAR* StateName(uint8 State)
{
	return State == swc::objects::kNodeDepleted ? TEXT("Depleted") : State == swc::objects::kNodeHealthy ? TEXT("Healthy") : TEXT("no record");
}

bool SameInstance(const FSwcMeshInstanceRef& A, const FSwcMeshInstanceRef& B)
{
	return A.Component == B.Component && A.Index == B.Index;
}

void AddInstance(TArray<FSwcMeshInstanceRef>& List, const FSwcMeshInstanceRef& Instance)
{
	if (!List.ContainsByPredicate([&Instance](const FSwcMeshInstanceRef& Item) { return SameInstance(Item, Instance); }))
	{
		List.Add(Instance);
	}
}

FString Hex64(uint64 Value)
{
	return FString::Printf(TEXT("%016llx"), Value);
}
} // namespace

void USwcObjectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The return to the menu that keeps the world (AST-203): the actors of the session go at its end.
	if (UGameInstance* Instance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (USwcBackendSubsystem* Backend = Instance->GetSubsystem<USwcBackendSubsystem>())
		{
			SessionEnd = Backend->OnWorldSessionEnd.AddUObject(this, &USwcObjectSubsystem::OnWorldSessionEnd);
		}
	}
	FString ProbeMode;
	bProbe = FParse::Param(FCommandLine::Get(), TEXT("SwcObjectProbe")) || FParse::Value(FCommandLine::Get(), TEXT("SwcObjectProbe="), ProbeMode);
	bProbeDraw = !ProbeMode.Equals(TEXT("log"), ESearchCase::IgnoreCase);
	bSightSearch = FParse::Param(FCommandLine::Get(), TEXT("SwcSightSearch")) || FParse::Value(FCommandLine::Get(), TEXT("SwcSightSearch="), SightSearchAt);
	FString Drop;
	if (FParse::Value(FCommandLine::Get(), TEXT("SwcDrop="), Drop))
	{
		FString Seconds;
		if (Drop.Split(TEXT(":"), &Seconds, &DropItem))
		{
			DropAt = FCString::Atod(*Seconds);
		}
	}
	DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &USwcObjectSubsystem::DrawLoot));
}

void USwcObjectSubsystem::Deinitialize()
{
	if (UGameInstance* Instance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (USwcBackendSubsystem* Backend = Instance->GetSubsystem<USwcBackendSubsystem>())
		{
			Backend->OnWorldSessionEnd.Remove(SessionEnd);
		}
	}
	UDebugDrawService::Unregister(DrawHandle);
	ClearAll(TEXT("the world ends"));
	Super::Deinitialize();
}

void USwcObjectSubsystem::OnWorldSessionEnd()
{
	ClearAll(TEXT("the session ends"));
}

bool USwcObjectSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcObjectSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcObjectSubsystem, STATGROUP_Tickables);
}

void USwcObjectSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	UpdateFalls(Now);
	for (int32 Index = Bursts.Num() - 1; Index >= 0; --Index)
	{
		const double Part = (Now - Bursts[Index].Start) / BurstSeconds;
		if (Part >= 1.0 || !Bursts[Index].Light.IsValid())
		{
			if (Bursts[Index].Light.IsValid())
			{
				Bursts[Index].Light->DestroyComponent();
			}
			Bursts.RemoveAt(Index);
			continue;
		}
		Bursts[Index].Light->SetIntensity(BurstCandelas * static_cast<float>(1.0 - Part));
	}
	if (Now < NextUpdate)
	{
		return;
	}
	NextUpdate = Now + UpdateSeconds;
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	swc::net::RepClient::OwnEntity Own;
	if (Backend == nullptr || !Backend->GetLocalPlayer(Own))
	{
		if (bConnected)
		{
			bConnected = false;
			ClearAll(TEXT("the client left the world"));
		}
		return;
	}
	bConnected = true;
	if (!bTablesLoaded && !bTablesFailed)
	{
		FString Report;
		bTablesLoaded = LoadTables(Report);
		bTablesFailed = !bTablesLoaded;
		UE_LOG(LogSwcObjects, Log, TEXT("%s"), *Report);
	}
	if (!bTablesLoaded)
	{
		return;
	}
	// The states: the records of the objects, the LootTracker claims, the Achievement state.
	ReadStates();
	if (Own.lootRevision != SeenLootRevision)
	{
		SeenLootRevision = Own.lootRevision;
		TMap<uint64, uint32> Claims;
		for (const swc::net::LootClaim& Claim : Own.lootClaims)
		{
			Claims.Add(swc::net::group_of(Claim.actor), Claim.state);
		}
		if (!Claims.OrderIndependentCompareEqual(ChestClaims))
		{
			ChestClaims = MoveTemp(Claims);
			++StateRevision;
		}
		if (++ClaimMaps == 1)
		{
			LastClaims = ChestClaims;   // the claims of before the entry: no depletion effect, no reset line
		}
	}
	if (Own.achievementRevision != SeenAchievementRevision)
	{
		SeenAchievementRevision = Own.achievementRevision;
		int32 Bits = 0;
		for (const uint8 Byte : Own.achievements)
		{
			Bits += FMath::CountBits(Byte);
		}
		UE_LOG(LogSwcObjects, Log, TEXT("objects: the Achievement state: %d byte(s), %d bit(s) (revision %u)"), static_cast<int32>(Own.achievements.size()), Bits,
			Own.achievementRevision);
		Achievements = Own.achievements;
		++StateRevision;
	}
	// The tools of the paperdoll (the item index of each tool slot).
	Equipment.Reset();
	for (uint32 Slot = 33; Slot <= 36; ++Slot)
	{
		const auto Found = Own.equipment.find(Slot);
		if (Found != Own.equipment.end() && Found->second != 0)
		{
			Equipment.Add(Slot, Found->second);
		}
	}
	// The place of the character: the pawn (the prediction of the client), else the place of the server.
	const APlayerController* Controller = World->GetFirstPlayerController();
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	// Before the world entry and during a travel the pawn is not at the place of the server yet.
	const FVector Server = SwcEngineToUnreal(Own.position);
	const FVector Center = Pawn != nullptr && FVector::Dist(Pawn->GetActorLocation(), Server) < 10000.0 ? Pawn->GetActorLocation() : Server;
	UpdateRegions(SwcUnrealToEngine(Center));
	UpdateVisuals(Center);
	UpdateChestLights(Center);
	UpdateLoot(Own, Now);
	if (bProbe)
	{
		DevelopmentAids();
	}
	if (bSightSearch && !bSightSearched && !Regions.IsEmpty() && FPlatformTime::Seconds() - WorldSince > SightSearchAt)
	{
		bSightSearched = true;
		SightSearch(Center);
	}
}

bool USwcObjectSubsystem::LoadTables(FString& OutReport)
{
	const FString Path = FPaths::Combine(FSwcCreatureLibrary::ContentFolder(), TEXT("world-objects/objects.json"));
	FString Text;
	TSharedPtr<FJsonObject> Document;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document) || !Document.IsValid()
		|| Document->GetIntegerField(TEXT("format")) != 1)
	{
		OutReport = FString::Printf(TEXT("objects: no %s of format 1 (tools/convert-objects.sh write): no objects of the level data"), *Path);
		return false;
	}
	Options.Reset();
	for (const TSharedPtr<FJsonValue>& Value : Document->GetArrayField(TEXT("options")))
	{
		// [name, id, action, committed, delay, hold]
		const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
		FOptionRow Option;
		if (Row.Num() == 6)
		{
			Option.Option.name = TCHAR_TO_UTF8(*Row[0]->AsString());
			Option.Option.id = static_cast<uint32>(Row[1]->AsNumber());
			Option.Option.action = static_cast<uint8>(Row[2]->AsNumber());
			Option.Option.committed = Row[3]->AsBool();
			Option.Option.delay = static_cast<float>(Row[4]->AsNumber());
			Option.Option.hold = static_cast<float>(Row[5]->AsNumber());
		}
		Options.Add(Option);
	}
	const int32 GatherOption = static_cast<int32>(Document->GetIntegerField(TEXT("gatherOption")));
	Visuals.Reset();
	StartFolders.Reset();
	DepletedFolders.Reset();
	for (const TSharedPtr<FJsonValue>& Value : Document->GetArrayField(TEXT("visuals")))
	{
		// [start folder, depleted folder, depleted mesh object path]
		const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
		FVisual Entry;
		if (Row.Num() == 3)
		{
			Entry.StartFolder = Row[0]->AsString();
			Entry.DepletedFolder = Row[1]->AsString();
			Entry.DepletedMesh = Row[2]->AsString();
		}
		const int32 Index = Visuals.Add(Entry);
		StartFolders.FindOrAdd(Entry.StartFolder).Add(Index);
		if (!Entry.DepletedFolder.IsEmpty())
		{
			DepletedFolders.FindOrAdd(Entry.DepletedFolder).Add(Index);
		}
	}
	Templates.Reset();
	for (const TSharedPtr<FJsonValue>& Value : Document->GetArrayField(TEXT("templates")))
	{
		const TSharedPtr<FJsonObject> Info = Value->AsObject();
		const TSharedPtr<FJsonObject> Bound = Info->GetObjectField(TEXT("bound"));
		FTemplate Template;
		Template.Root = HexNumber(Info->GetStringField(TEXT("root")));
		Template.BoundEntity = HexNumber(Bound->GetStringField(TEXT("entity")));
		Template.Component = HexNumber(Bound->GetStringField(TEXT("component")));
		Template.OptionEntity = HexNumber(Bound->GetStringField(TEXT("option")));
		Template.Slot = static_cast<uint32>(Bound->GetIntegerField(TEXT("slot")));
		Template.RangeSq = static_cast<float>(Bound->GetNumberField(TEXT("rangeSq")));
		Template.bLineOfSight = Bound->GetBoolField(TEXT("lineOfSight"));
		Template.Translation = ReadPlace(Bound->GetArrayField(TEXT("translation")));
		ReadShape(Info, Template.Shape);
		MaxRangeMeters = FMath::Max(MaxRangeMeters, FMath::Sqrt(static_cast<double>(Template.RangeSq)));
		for (const TSharedPtr<FJsonValue>& Index : Info->GetArrayField(TEXT("visuals")))
		{
			Template.Visuals.Add(static_cast<int32>(Index->AsNumber()));
		}
		const TArray<TSharedPtr<FJsonValue>>* SliceVisuals = nullptr;
		if (Info->TryGetArrayField(TEXT("sliceVisuals"), SliceVisuals))
		{
			for (const TSharedPtr<FJsonValue>& Index : *SliceVisuals)
			{
				Template.SliceVisuals.Add(static_cast<int32>(Index->AsNumber()));
			}
		}
		Templates.Add(Template);
	}
	RegionTypes.Reset();
	int32 Nodes = 0;
	int32 Objects = 0;
	for (const auto& Pair : Document->GetObjectField(TEXT("regions"))->Values)
	{
		const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
		TArray<FNodeType>& Types = RegionTypes.Add(FString(Pair.Key));
		for (const TSharedPtr<FJsonValue>& Value : Info->GetArrayField(TEXT("types")))
		{
			const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
			FNodeType Type;
			if (Row.Num() >= 3)
			{
				Type.Template = static_cast<int32>(Row[0]->AsNumber());
				Type.Row = static_cast<int32>(Row[1]->AsNumber());
				Type.Visual = static_cast<int32>(Row[2]->AsNumber());
				Type.Radius = Row.Num() >= 4 ? static_cast<float>(Row[3]->AsNumber()) : -1.0f;
			}
			Types.Add(Type);
		}
		Nodes += static_cast<int32>(Info->GetIntegerField(TEXT("nodes")));
		Objects += static_cast<int32>(Info->GetIntegerField(TEXT("objects")));
	}
	// The gathering (stage P5.2).
	Rows.Reset();
	Tools.Reset();
	Bodies.Reset();
	Texts.Reset();
	const TSharedPtr<FJsonObject>* Gathering = nullptr;
	if (Document->TryGetObjectField(TEXT("gathering"), Gathering))
	{
		for (const TSharedPtr<FJsonValue>& Value : (*Gathering)->GetArrayField(TEXT("rows")))
		{
			// [id, type, action, restriction, tradeskill, level, time, name, unsupported]
			const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
			FGatherRow& Entry = Rows.AddDefaulted_GetRef();
			if (Row.Num() >= 8)
			{
				Entry.Id = Row[0]->AsString();
				Entry.Type = Row[1]->AsString();
				Entry.Action = Row[2]->AsString();
				Entry.Restriction = Row[3]->AsString();
				Entry.Tradeskill = Row[4]->AsString();
				Entry.Level = static_cast<int32>(Row[5]->AsNumber());
				Entry.Time = static_cast<float>(Row[6]->AsNumber());
				Entry.Name = Row[7]->AsString();
			}
		}
		for (const auto& Pair : (*Gathering)->GetObjectField(TEXT("tools"))->Values)
		{
			// item index -> [id, weapon tags, gathering types]
			const TArray<TSharedPtr<FJsonValue>>& Row = Pair.Value->AsArray();
			if (Row.Num() == 3)
			{
				FToolItem& Tool = Tools.Add(static_cast<uint32>(FCString::Atoi(*FString(Pair.Key))));
				Tool.Id = Row[0]->AsString();
				Tool.Tags = Row[1]->AsString();
				for (const TSharedPtr<FJsonValue>& Type : Row[2]->AsArray())
				{
					Tool.Types.Add(Type->AsString());
				}
			}
		}
		for (const auto& Pair : (*Gathering)->GetObjectField(TEXT("bodies"))->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>& Row = Pair.Value->AsArray();
			if (Row.Num() == 2)
			{
				Bodies.Add(FString(Pair.Key), TPair<int32, int32>(static_cast<int32>(Row[0]->AsNumber()), static_cast<int32>(Row[1]->AsNumber())));
			}
		}
		for (const auto& Pair : (*Gathering)->GetObjectField(TEXT("texts"))->Values)
		{
			Texts.Add(FString(Pair.Key), Pair.Value->AsString());
		}
	}
	if (!Options.IsValidIndex(GatherOption))
	{
		OutReport = FString::Printf(TEXT("objects: %s has no GatherOption"), *Path);
		return false;
	}
	GatherOptionIndex = GatherOption;
	OutReport = FString::Printf(TEXT("objects: %d options, %d gathering templates, %d node visuals, %d regions (%d objects of the interaction data, %d natural nodes), %d gatherable rows, %d tools, %d skinnable slices from %s"),
		Options.Num(), Templates.Num(), Visuals.Num(), RegionTypes.Num(), Objects, Nodes, Rows.Num(), Tools.Num(), Bodies.Num(), *Path);
	return true;
}

bool USwcObjectSubsystem::LoadRegion(int32 X, int32 Y, FRegion& Out)
{
	Out.Name = RegionName(X, Y);
	Out.X = X;
	Out.Y = Y;
	const FString Folder = FPaths::Combine(FSwcCreatureLibrary::ContentFolder(), TEXT("world-objects"));
	if (const TArray<FNodeType>* Types = RegionTypes.Find(Out.Name))
	{
		Out.Types = *Types;
	}
	TArray<uint8> Bytes;
	if (FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Folder, TEXT("nodes"), Out.Name + TEXT(".bin")), FILEREAD_Silent))
	{
		if (!Out.Nodes.read(std::span<const uint8>(Bytes.GetData(), static_cast<size_t>(Bytes.Num())))
			|| Out.Nodes.regionX() != X || Out.Nodes.regionY() != Y)
		{
			UE_LOG(LogSwcObjects, Warning, TEXT("objects: the node file of %s does not have its form (tools/convert-objects.sh write)"), *Out.Name);
			Out.Nodes = swc::objects::NodeFile();
		}
	}
	FString Text;
	TSharedPtr<FJsonObject> Document;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Folder, TEXT("regions"), Out.Name + TEXT(".json")), FFileHelper::EHashOptions::None, FILEREAD_Silent)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document) || !Document.IsValid())
	{
		return Out.Nodes.size() > 0;
	}
	int32 Mismatches = 0;
	for (const TSharedPtr<FJsonValue>& Value : Document->GetArrayField(TEXT("objects")))
	{
		const TSharedPtr<FJsonObject> Info = Value->AsObject();
		FDataObject Object;
		Object.Key = Info->GetStringField(TEXT("key"));
		Object.Kind = FName(*Info->GetStringField(TEXT("kind")));
		Object.Actor = HexActor(Info->GetStringField(TEXT("actor")));
		Object.Group = swc::net::group_of(Object.Actor);
		Object.EnginePlace = ReadPlace(Info->GetArrayField(TEXT("position")));
		// The actor of the data must be the actor of the rule of the client (the UUID of the placement).
		const auto Uuid = swc::objects::parse_uuid(TCHAR_TO_UTF8(*Object.Key));
		if (!Uuid || swc::objects::placement_actor(*Uuid) != Object.Actor)
		{
			++Mismatches;
		}
		const TArray<TSharedPtr<FJsonValue>>* Chest = nullptr;
		if (Info->TryGetArrayField(TEXT("chest"), Chest) && Chest->Num() >= 5)
		{
			Object.ChestName = (*Chest)[3]->AsString();
			Object.bElite = (*Chest)[4]->AsBool();
		}
		const TArray<TSharedPtr<FJsonValue>>* Gather = nullptr;
		if (Info->TryGetArrayField(TEXT("gather"), Gather) && Gather->Num() == 2)
		{
			Object.Template = static_cast<int32>((*Gather)[0]->AsNumber());
			Object.Row = static_cast<int32>((*Gather)[1]->AsNumber());
		}
		for (const TSharedPtr<FJsonValue>& BoundValue : Info->GetArrayField(TEXT("bounds")))
		{
			const TSharedPtr<FJsonObject> BoundInfo = BoundValue->AsObject();
			FBound Bound;
			Bound.Slot = static_cast<uint32>(BoundInfo->GetIntegerField(TEXT("slot")));
			Bound.Target = HexNumber(BoundInfo->GetStringField(TEXT("target")));
			Bound.EnginePlace = ReadPlace(BoundInfo->GetArrayField(TEXT("position")));
			Bound.RangeSq = static_cast<float>(BoundInfo->GetNumberField(TEXT("rangeSq")));
			Bound.bLineOfSight = BoundInfo->GetBoolField(TEXT("lineOfSight"));
			ReadShape(BoundInfo, Bound.Shape);
			MaxRangeMeters = FMath::Max(MaxRangeMeters, FMath::Sqrt(static_cast<double>(Bound.RangeSq)));
			Bound.LoreBit = static_cast<int32>(BoundInfo->GetIntegerField(TEXT("lore")));
			for (const TSharedPtr<FJsonValue>& OptionValue : BoundInfo->GetArrayField(TEXT("options")))
			{
				const TArray<TSharedPtr<FJsonValue>>& Pair = OptionValue->AsArray();
				if (Pair.Num() == 2 && Options.IsValidIndex(static_cast<int32>(Pair[0]->AsNumber())))
				{
					Bound.Options.Emplace(static_cast<int32>(Pair[0]->AsNumber()), HexNumber(Pair[1]->AsString()));
				}
			}
			Object.Bounds.Add(Bound);
		}
		const int32 Index = Out.Objects.Add(MoveTemp(Object));
		for (const FBound& Bound : Out.Objects[Index].Bounds)
		{
			TArray<int32>& Cell = Out.Grid.FindOrAdd(FIntPoint(swc::objects::grid_cell(Bound.EnginePlace.X), swc::objects::grid_cell(Bound.EnginePlace.Y)));
			Cell.AddUnique(Index);
		}
	}
	if (Mismatches > 0)
	{
		UE_LOG(LogSwcObjects, Warning, TEXT("objects: %s: %d actor id(s) of the data are not the actor of the UUID rule"), *Out.Name, Mismatches);
	}
	return true;
}

void USwcObjectSubsystem::UpdateRegions(const swc::net::WorldPosition& Center)
{
	const auto RegionOf = [](double Meters) { return static_cast<int32>(FMath::FloorToDouble(Meters / swc::objects::kRegionMeters)); };
	// Load the regions within RegionMeters; remove the regions beyond RegionMeters + 200 m.
	for (int32 Y = RegionOf(Center.y - RegionMeters); Y <= RegionOf(Center.y + RegionMeters); ++Y)
	{
		for (int32 X = RegionOf(Center.x - RegionMeters); X <= RegionOf(Center.x + RegionMeters); ++X)
		{
			if (Regions.Contains(FIntPoint(X, Y)))
			{
				continue;
			}
			const double Start = FPlatformTime::Seconds();
			FRegion& Region = Regions.Add(FIntPoint(X, Y));
			LoadRegion(X, Y, Region);
			UE_LOG(LogSwcObjects, Log, TEXT("objects: region %s: %d object(s) of the interaction data, %d natural node(s) (%.0f ms)"), *Region.Name,
				Region.Objects.Num(), static_cast<int32>(Region.Nodes.size()), (FPlatformTime::Seconds() - Start) * 1000.0);
		}
	}
	const double Keep = RegionMeters + 200.0;
	for (auto It = Regions.CreateIterator(); It; ++It)
	{
		const double MinX = It.Key().X * swc::objects::kRegionMeters;
		const double MinY = It.Key().Y * swc::objects::kRegionMeters;
		const double DX = FMath::Max3(MinX - Center.x, 0.0, Center.x - (MinX + swc::objects::kRegionMeters));
		const double DY = FMath::Max3(MinY - Center.y, 0.0, Center.y - (MinY + swc::objects::kRegionMeters));
		if (DX * DX + DY * DY > Keep * Keep)
		{
			UE_LOG(LogSwcObjects, Log, TEXT("objects: region %s: removed (the character is far)"), *It.Value().Name);
			It.RemoveCurrent();
		}
	}
}

void USwcObjectSubsystem::ReadStates()
{
	const USwcCreatureSubsystem* Creatures = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcCreatureSubsystem>() : nullptr;
	if (Creatures == nullptr || Creatures->GetTableRevision() == SeenTableRevision)
	{
		return;
	}
	SeenTableRevision = Creatures->GetTableRevision();
	TMap<uint64, uint8> States;
	TMap<uint64, TMap<uint32, bool>> Enabled;
	for (const auto& [Interest, Entity] : Creatures->GetEntityTable().entities())
	{
		if (!Entity.hasMetadata)
		{
			continue;
		}
		const uint64 Group = swc::net::group_of(Entity.actor);
		if (Entity.hasGatherable)
		{
			States.Add(Group, Entity.gatherableState);
		}
		for (const auto& [Slot, State] : Entity.interact)
		{
			Enabled.FindOrAdd(Group).Add(Slot, State.enabled);
		}
	}
	// The log of each change of a node state (a record that stops is no record: the data state).
	for (const TPair<uint64, uint8>& Pair : States)
	{
		const uint8* Before = NodeStates.Find(Pair.Key);
		if (Before == nullptr || *Before != Pair.Value)
		{
			UE_LOG(LogSwcObjects, Log, TEXT("objects: the record of %s: node state %s"), *Hex64(Pair.Key), StateName(Pair.Value));
		}
	}
	for (const TPair<uint64, uint8>& Pair : NodeStates)
	{
		if (!States.Contains(Pair.Key))
		{
			UE_LOG(LogSwcObjects, Log, TEXT("objects: the record of %s stopped (the data state)"), *Hex64(Pair.Key));
		}
	}
	if (!States.OrderIndependentCompareEqual(NodeStates))
	{
		NodeStates = MoveTemp(States);
		++StateRevision;
	}
	bool bSame = Enabled.Num() == BoundEnabled.Num();
	for (const TPair<uint64, TMap<uint32, bool>>& Pair : Enabled)
	{
		const TMap<uint32, bool>* Before = BoundEnabled.Find(Pair.Key);
		bSame = bSame && Before != nullptr && Before->OrderIndependentCompareEqual(Pair.Value);
	}
	if (!bSame)
	{
		BoundEnabled = MoveTemp(Enabled);
		++StateRevision;
	}
}

ESwcWorldObjectState USwcObjectSubsystem::StateOf(FName Kind, uint64 Group) const
{
	if (Kind == KindNode || Kind == KindNatural || Kind == KindBody)
	{
		const uint8* State = NodeStates.Find(Group);
		return State != nullptr && *State == swc::objects::kNodeDepleted ? ESwcWorldObjectState::Depleted : ESwcWorldObjectState::Ready;
	}
	if (Kind == KindChest)
	{
		switch (swc::objects::chest_state(ClaimOf(ChestClaims, Group)))
		{
		case swc::objects::ChestState::looted:
			return ESwcWorldObjectState::Looted;
		case swc::objects::ChestState::locked:
			return ESwcWorldObjectState::Locked;
		case swc::objects::ChestState::ready:
			break;
		}
	}
	return ESwcWorldObjectState::Ready;
}

void USwcObjectSubsystem::MakeTarget(const FString& Key, FName Kind, const swc::net::ActorId& Actor, uint64 Group, const FBound& Bound, const FVector3d& From,
	FSwcWorldObjectTarget& Out) const
{
	Out.Key = Key;
	Out.Kind = Kind;
	Out.Actor = Actor;
	Out.Group = Group;
	Out.Slot = Bound.Slot;
	Out.Target = Bound.Target;
	Out.EnginePlace = swc::net::WorldPosition{static_cast<float>(Bound.EnginePlace.X), static_cast<float>(Bound.EnginePlace.Y), static_cast<float>(Bound.EnginePlace.Z)};
	Out.Place = ToUnreal(Bound.EnginePlace);
	Out.RangeSq = Bound.RangeSq;
	Out.bLineOfSight = Bound.bLineOfSight;
	Out.Shape = Bound.Shape;
	Out.Options.Reset();
	for (const TPair<int32, uint64>& Pair : Bound.Options)
	{
		FSwcWorldObjectOption& Option = Out.Options.AddDefaulted_GetRef();
		Option.Index = Pair.Key;
		Option.Option = Options[Pair.Key].Option;
		Option.Option.entity = Pair.Value;
		// The request of the block (the rule of swc::interact::request_of with the ids of the data).
		Option.Request.reference.actor = Actor;
		Option.Request.reference.group = Group;
		Option.Request.reference.target = Bound.Target;
		Option.Request.action = Option.Option.action;
		Option.Request.baseAction = Option.Option.committed ? Option.Option.action : 0;
		Option.Request.optionEntity = Pair.Value;
		Option.Request.committed = Option.Option.committed;
		Option.Request.holdTime = Option.Option.delay;
		Option.Request.optionId = Option.Option.id;
	}
	Out.State = StateOf(Kind, Group);
	const TMap<uint32, bool>* Enabled = BoundEnabled.Find(Group);
	const bool* Mark = Enabled != nullptr ? Enabled->Find(Bound.Slot) : nullptr;
	Out.bEnabled = Mark == nullptr || *Mark;
	Out.LoreBit = Bound.LoreBit;
	Out.bRead = Bound.LoreBit >= 0 && swc::net::has_bit(Achievements, static_cast<uint32>(Bound.LoreBit));
	Out.Distance = static_cast<float>(FVector3d::Dist(From, Bound.EnginePlace));
}

void USwcObjectSubsystem::AddTargets(const FVector3d& From, double RadiusMeters, bool bUsableOnly, TArray<FSwcWorldObjectTarget>& Out) const
{
	const int32 MinX = swc::objects::grid_cell(From.X - RadiusMeters);
	const int32 MaxX = swc::objects::grid_cell(From.X + RadiusMeters);
	const int32 MinY = swc::objects::grid_cell(From.Y - RadiusMeters);
	const int32 MaxY = swc::objects::grid_cell(From.Y + RadiusMeters);
	const int32 PerRegion = static_cast<int32>(swc::objects::kRegionMeters / swc::objects::kCellMeters);
	// A gatherable that is not a chest or a node (mostly master_quest_instanced; the backend refuses it) has no prompt.
	const auto Accept = [&](const FSwcWorldObjectTarget& Target) {
		return !bUsableOnly
			|| (!Target.Options.IsEmpty() && Target.bEnabled && Target.State == ESwcWorldObjectState::Ready && Target.Kind != KindGatherable
				&& Target.bInSight);
	};
	AddBodies(From, RadiusMeters, bUsableOnly, Out);
	for (int32 GY = MinY; GY <= MaxY; ++GY)
	{
		for (int32 GX = MinX; GX <= MaxX; ++GX)
		{
			const FRegion* Region = Regions.Find(FIntPoint(FloorDiv(GX, PerRegion), FloorDiv(GY, PerRegion)));
			if (Region == nullptr)
			{
				continue;
			}
			if (const TArray<int32>* List = Region->Grid.Find(FIntPoint(GX, GY)))
			{
				for (const int32 Index : *List)
				{
					const FDataObject& Object = Region->Objects[Index];
					for (const FBound& Bound : Object.Bounds)
					{
						if (swc::objects::grid_cell(Bound.EnginePlace.X) != GX || swc::objects::grid_cell(Bound.EnginePlace.Y) != GY)
						{
							continue;   // the cell of another bound of the object
						}
						// The prompt: the candidate shape of the bound has the point of the character (AST-198), not its range.
						if (bUsableOnly ? !InShape(Bound, swc::net::Quaternion{0.0F, 0.0F, 0.0F, 1.0F}, From)
										: FVector3d::DistSquared(From, Bound.EnginePlace) > RadiusMeters * RadiusMeters)
						{
							continue;
						}
						FSwcWorldObjectTarget Target;
						MakeTarget(Object.Key, Object.Kind, Object.Actor, Object.Group, Bound, From, Target);
						if (Object.Kind == KindNode || Object.Kind == KindChest)
						{
							// A capital node: the tool of its row; a chest: the default Gather fragment (its action LootContainer has none).
							FillGather(Target, Object.Kind == KindNode ? Object.Row : -1);
						}
						if (Object.Kind == KindChest)
						{
							Target.Name = Text(Object.ChestName);
							Target.bElite = Object.bElite;
						}
						if (bUsableOnly && Bound.bLineOfSight)
						{
							Target.bInSight = HasSight(From, Bound.EnginePlace);
						}
						if (Accept(Target))
						{
							Out.Add(MoveTemp(Target));
						}
					}
				}
			}
			// The natural nodes of the cell: the ids of the offline id of the record and of the template of its type.
			for (const swc::objects::NodeRecord& Node : Region->Nodes.cell(GX - Region->X * PerRegion, GY - Region->Y * PerRegion))
			{
				if (!Region->Types.IsValidIndex(Node.type) || !Templates.IsValidIndex(Region->Types[Node.type].Template))
				{
					continue;
				}
				const FTemplate& Template = Templates[Region->Types[Node.type].Template];
				FBound Bound;
				Bound.EnginePlace = FVector3d(Node.x, Node.y, Node.z) + Template.Translation;
				Bound.RangeSq = Template.RangeSq;
				// The shape of the slice with the sphere radius of the variant (the small iron vein: 3 m, not the 2 m of its slice).
				Bound.Shape = Template.Shape;
				if (Region->Types[Node.type].Radius > 0.0f && Bound.Shape.type == swc::interact::Shape::Type::sphere)
				{
					Bound.Shape.radius = Region->Types[Node.type].Radius;
				}
				if (bUsableOnly ? !InShape(Bound, swc::net::Quaternion{0.0F, 0.0F, 0.0F, 1.0F}, From)
								: FVector3d::DistSquared(From, Bound.EnginePlace) > RadiusMeters * RadiusMeters)
				{
					continue;
				}
				const uint64 Group = swc::objects::natural_group(Region->X, Region->Y, Node.record + 1);
				const swc::objects::BoundIds Ids = swc::objects::bound_ids(Template.Root, Group, Template.BoundEntity, Template.Component, Template.OptionEntity);
				Bound.Slot = Template.Slot;
				Bound.Target = Ids.target;
				Bound.bLineOfSight = Template.bLineOfSight;
				Bound.Options.Emplace(GatherOptionIndex, Ids.optionEntity);
				FSwcWorldObjectTarget Target;
				MakeTarget(FString::Printf(TEXT("%s#%u"), *Region->Name, Node.record), KindNatural, swc::objects::actor_of_group(Group), Group, Bound, From, Target);
				FillGather(Target, Region->Types[Node.type].Row);
				if (bUsableOnly && Bound.bLineOfSight)
				{
					Target.bInSight = HasSight(From, Bound.EnginePlace);
				}
				if (Accept(Target))
				{
					Out.Add(MoveTemp(Target));
				}
			}
		}
	}
}

bool USwcObjectSubsystem::HasSight(const FVector3d& Feet, const FVector3d& Place) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return true;
	}
	const FVector Eye = ToUnreal(Feet + FVector3d(0.0, 0.0, swc::objects::kSightEyeMeters));
	const FVector Target = ToUnreal(Place + FVector3d(0.0, 0.0, swc::objects::kSightAimMeters));
	const FVector Base = ToUnreal(Place);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SwcObjectSight), false);
	if (const APlayerController* Controller = World->GetFirstPlayerController(); Controller != nullptr && Controller->GetPawn() != nullptr)
	{
		Params.AddIgnoredActor(Controller->GetPawn());
	}
	const FVector Direction = (Target - Eye).GetSafeNormal();
	FVector Start = Eye;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, Target, ECC_Visibility, Params) || FVector::Dist(Hit.ImpactPoint, Target) <= swc::objects::kSightOwnMeters * 100.0)
		{
			return true;
		}
		// The meshes of the object itself do not block its own prompt (the level places them at the place of the object; the rule of
		// swc::objects::own_mesh). The line goes on behind the box of that mesh.
		const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Hit.GetComponent());
		FTransform Transform;
		if (Instances == nullptr || Instances->GetStaticMesh() == nullptr || !Instances->GetInstanceTransform(Hit.Item, Transform, true))
		{
			return false;
		}
		const FBox Local = Instances->GetStaticMesh()->GetBoundingBox().ExpandBy(swc::objects::kSightOwnMarginMeters * 100.0);
		const bool bHolds = Local.IsInside(Transform.InverseTransformPosition(Base)) || Local.IsInside(Transform.InverseTransformPosition(Target));
		if (!swc::objects::own_mesh((Local.GetSize() * Transform.GetScale3D().GetAbs()).GetMax() / 100.0, FVector::Dist2D(Transform.GetLocation(), Base) / 100.0,
				FMath::Abs(Transform.GetLocation().Z - Base.Z) / 100.0, bHolds))
		{
			return false;
		}
		// The exit of the line from the box in the frame of the instance (the same line parameter: cm along the direction).
		const FVector From = Transform.InverseTransformPosition(Hit.ImpactPoint);
		const FVector LocalStep = Transform.InverseTransformVector(Direction);
		const std::optional<double> Exit = swc::objects::box_exit({Local.Min.X, Local.Min.Y, Local.Min.Z}, {Local.Max.X, Local.Max.Y, Local.Max.Z},
			{From.X, From.Y, From.Z}, {LocalStep.X, LocalStep.Y, LocalStep.Z});
		if (!Exit.has_value() || FVector::DotProduct(Target - Hit.ImpactPoint, Direction) <= *Exit)
		{
			return true;   // the aim point is in the box of the own mesh
		}
		Start = Hit.ImpactPoint + Direction * (FMath::Max(*Exit, 0.0) + 2.0);
	}
	return false;
}

FString USwcObjectSubsystem::Text(const FString& Key) const
{
	// A text key of the data ("@BushSmall_DisplayName", "ui_notoolchopping"); the key itself when the texts do not have it.
	const FString Bare = Key.StartsWith(TEXT("@")) ? Key.Mid(1) : Key;
	const FString* Found = Texts.Find(Bare.ToLower());
	return Found != nullptr ? *Found : Bare;
}

void USwcObjectSubsystem::FillGather(FSwcWorldObjectTarget& Out, int32 Row) const
{
	Out.Row = Row;
	Out.UseKind = TEXT("gather|None|");
	if (!Rows.IsValidIndex(Row))
	{
		return;
	}
	const FGatherRow& Entry = Rows[Row];
	Out.GatheringType = Entry.Type;
	Out.GatherAction = Entry.Action;
	Out.Name = Text(Entry.Name);
	// The tool rule of the client and the Gather fragment (Objects/SwcObjectRules.h).
	const uint32 SlotItem = Equipment.FindRef(swc::objects::tool_slot(TCHAR_TO_UTF8(*Entry.Type)));
	const FToolItem* Tool = SlotItem != 0 ? Tools.Find(SlotItem) : nullptr;
	const swc::objects::ToolCheck Check = swc::objects::check_tool(TCHAR_TO_UTF8(*Entry.Type), TCHAR_TO_UTF8(*Entry.Restriction),
		Tool != nullptr && Tool->Types.Contains(Entry.Type));
	Out.ToolSlot = Check.slot;
	Out.ToolItem = Check.hasTool ? SlotItem : 0;
	Out.ToolTags = Check.hasTool ? Tool->Tags : FString();
	Out.bToolMissing = Check.missing;
	Out.RefusalText = Check.missing ? Text(TEXT("ui_notool") + Entry.Type.ToLower()) : FString();
	Out.UseKind = UTF8_TO_TCHAR(swc::objects::gather_use_kind(TCHAR_TO_UTF8(*Entry.Action), TCHAR_TO_UTF8(*Out.ToolTags), Check.hasTool).c_str());
}

void USwcObjectSubsystem::AddBodies(const FVector3d& From, double RadiusMeters, bool bUsableOnly, TArray<FSwcWorldObjectTarget>& Out) const
{
	const USwcCreatureSubsystem* Creatures = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcCreatureSubsystem>() : nullptr;
	if (Creatures == nullptr || Bodies.IsEmpty())
	{
		return;
	}
	for (const auto& [Interest, Entity] : Creatures->GetEntityTable().entities())
	{
		// A dead creature whose slice has a skinning row: the server sets its GatherableController Healthy at the death (gathering.md).
		if (!Entity.dead || !Entity.hasGatherable || !Entity.hasMetadata || !Entity.hasPosition())
		{
			continue;
		}
		const TPair<int32, int32>* Body = Bodies.Find(UTF8_TO_TCHAR(Entity.slice_key().c_str()));
		if (Body == nullptr || !Templates.IsValidIndex(Body->Key))
		{
			continue;
		}
		const FTemplate& Template = Templates[Body->Key];
		const swc::net::WorldPosition& Feet = Entity.samples.back().position;
		const swc::net::Quaternion& Turn = Entity.samples.back().rotation;
		FBound Bound;
		// The bound in the frame of the creature (the body of the wolf: the box 2.5 m, 1 m above the feet; AST-198).
		const float Offset[3]{static_cast<float>(Template.Translation.X), static_cast<float>(Template.Translation.Y), static_cast<float>(Template.Translation.Z)};
		const swc::net::WorldPosition Place = swc::interact::transform_point(Feet, Turn, Offset);
		Bound.EnginePlace = FVector3d(Place.x, Place.y, Place.z);
		Bound.RangeSq = Template.RangeSq;
		Bound.Shape = Template.Shape;
		if (bUsableOnly ? !InShape(Bound, Turn, From) : FVector3d::DistSquared(From, Bound.EnginePlace) > RadiusMeters * RadiusMeters)
		{
			continue;
		}
		// The ids of a body: G of the creature entity, and the XOR rule with the template of its slice.
		const uint64 Group = swc::net::group_of(Entity.actor);
		const swc::objects::BoundIds Ids = swc::objects::bound_ids(Template.Root, Group, Template.BoundEntity, Template.Component, Template.OptionEntity);
		Bound.Slot = Template.Slot;
		Bound.Target = Ids.target;
		Bound.bLineOfSight = Template.bLineOfSight;
		Bound.Options.Emplace(GatherOptionIndex, Ids.optionEntity);
		FSwcWorldObjectTarget Target;
		MakeTarget(FString::Printf(TEXT("body#%u"), Interest), KindBody, Entity.actor, Group, Bound, From, Target);
		Target.Interest = Interest;
		FillGather(Target, Body->Value);
		// The prompt of a body has the name of the creature ("E CHALKY RABBIT": protocol notes 4.44, stage run S1).
		const FString Creature = Creatures->CreatureDisplayName(Group);
		if (!Creature.IsEmpty())
		{
			Target.Name = Creature;
		}
		// The prompt of a body shows only with the knife in slot 35 (protocol notes 4.44): no refusal text, no prompt.
		if (bUsableOnly && (Target.bToolMissing || Target.State != ESwcWorldObjectState::Ready || !Target.bEnabled))
		{
			continue;
		}
		Out.Add(MoveTemp(Target));
	}
}

bool USwcObjectSubsystem::FindNearestObject(const FVector& From, FSwcWorldObjectTarget& Out) const
{
	if (!bTablesLoaded)
	{
		return false;
	}
	const swc::net::WorldPosition Engine = SwcUnrealToEngine(From);
	// The cells within the largest range of the data (100 m: rangeSq 10000 of a few bounds).
	TArray<FSwcWorldObjectTarget> Found;
	AddTargets(FVector3d(Engine.x, Engine.y, Engine.z), MaxRangeMeters, true, Found);
	const FSwcWorldObjectTarget* Best = nullptr;
	for (const FSwcWorldObjectTarget& Target : Found)
	{
		if (Best == nullptr || Target.Distance < Best->Distance)
		{
			Best = &Target;
		}
	}
	if (Best == nullptr)
	{
		return false;
	}
	Out = *Best;
	return true;
}

bool USwcObjectSubsystem::FindFocusObject(const FVector& From, const float Camera[3], const float Forward[3], FSwcWorldObjectTarget& Out,
	float& OutScore) const
{
	if (!bTablesLoaded)
	{
		return false;
	}
	const swc::net::WorldPosition Engine = SwcUnrealToEngine(From);
	TArray<FSwcWorldObjectTarget> Found;
	AddTargets(FVector3d(Engine.x, Engine.y, Engine.z), MaxRangeMeters, true, Found);
	const FSwcWorldObjectTarget* Best = nullptr;
	for (const FSwcWorldObjectTarget& Target : Found)
	{
		// The retail score (0x490FED0): a strictly smaller score replaces the best one; an equal score keeps the first.
		const float Place[3]{Target.EnginePlace.x, Target.EnginePlace.y, Target.EnginePlace.z};
		float Score = 0.0f;
		if (swc::interact::line_score(Camera, Forward, Place, Score) && (Best == nullptr || Score < OutScore))
		{
			Best = &Target;
			OutScore = Score;
		}
	}
	if (Best == nullptr)
	{
		return false;
	}
	Out = *Best;
	return true;
}

void USwcObjectSubsystem::ReadShape(const TSharedPtr<FJsonObject>& Info, swc::interact::Shape& Out)
{
	Out = swc::interact::Shape();
	const TSharedPtr<FJsonObject>* Shape = nullptr;
	if (!Info.IsValid() || !Info->TryGetObjectField(TEXT("shape"), Shape) || !Shape->IsValid())
	{
		return;
	}
	const FString Type = (*Shape)->GetStringField(TEXT("type"));
	using EType = swc::interact::Shape::Type;
	Out.type = Type == TEXT("sphere") ? EType::sphere : Type == TEXT("box") ? EType::box : Type == TEXT("capsule") ? EType::capsule
		: Type == TEXT("cylinder") ? EType::cylinder : EType::none;
	double Number = 0.0;
	if ((*Shape)->TryGetNumberField(TEXT("radius"), Number))
	{
		Out.radius = static_cast<float>(Number);
	}
	if ((*Shape)->TryGetNumberField(TEXT("height"), Number))
	{
		Out.height = static_cast<float>(Number);
	}
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if ((*Shape)->TryGetArrayField(TEXT("dimensions"), Values) && Values->Num() == 3)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Out.dimensions[Axis] = static_cast<float>((*Values)[Axis]->AsNumber());
		}
	}
	if ((*Shape)->TryGetArrayField(TEXT("basis"), Values) && Values->Num() == 9)
	{
		for (int32 Index = 0; Index < 9; ++Index)
		{
			Out.basis[Index] = static_cast<float>((*Values)[Index]->AsNumber());
		}
	}
}

bool USwcObjectSubsystem::InShape(const FBound& Bound, const swc::net::Quaternion& Rotation, const FVector3d& Point)
{
	return swc::interact::contains(Bound.Shape, swc::net::WorldPosition{static_cast<float>(Bound.EnginePlace.X), static_cast<float>(Bound.EnginePlace.Y),
		static_cast<float>(Bound.EnginePlace.Z)}, Rotation, swc::net::WorldPosition{static_cast<float>(Point.X), static_cast<float>(Point.Y),
		static_cast<float>(Point.Z)}, Bound.RangeSq);
}

void USwcObjectSubsystem::GetObjectsNear(const FVector& From, double RadiusMeters, TArray<FSwcWorldObjectTarget>& Out) const
{
	Out.Reset();
	if (!bTablesLoaded)
	{
		return;
	}
	const swc::net::WorldPosition Engine = SwcUnrealToEngine(From);
	AddTargets(FVector3d(Engine.x, Engine.y, Engine.z), RadiusMeters, false, Out);
}

// ------------------------------------------------------------------------------------------------------------------ visuals

int32 USwcObjectSubsystem::MeshRole(const UStaticMesh* Mesh, const TArray<int32>& NodeVisuals) const
{
	if (Mesh == nullptr)
	{
		return 0;
	}
	FString* Folder = MeshFolders.Find(Mesh);
	if (Folder == nullptr)
	{
		// /SwcGameContent/Meshes/STEM_HASH/NAME/StaticMeshes/ASSET.ASSET: the folder of the manifest is the first three parts.
		TArray<FString> Parts;
		Mesh->GetPathName().ParseIntoArray(Parts, TEXT("/"));
		Folder = &MeshFolders.Add(Mesh, Parts.Num() >= 3 ? FString::Printf(TEXT("/%s/%s/%s"), *Parts[0], *Parts[1], *Parts[2]) : FString());
	}
	for (const int32 Index : NodeVisuals)
	{
		if (Visuals.IsValidIndex(Index) && Visuals[Index].StartFolder == *Folder)
		{
			return 1;
		}
	}
	for (const int32 Index : NodeVisuals)
	{
		if (Visuals.IsValidIndex(Index) && !Visuals[Index].DepletedFolder.IsEmpty() && Visuals[Index].DepletedFolder == *Folder)
		{
			return 2;
		}
	}
	return 0;
}

void USwcObjectSubsystem::RebuildInstanceIndex(const FVector& Center)
{
	USwcLevelInstances* Level = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcLevelInstances>() : nullptr;
	InstanceIndex.Reset();
	IndexCenter = Center;
	if (Level == nullptr)
	{
		return;
	}
	IndexRevision = Level->GetRevision();
	TArray<FSwcMeshInstanceRef> All;
	Level->FindInstancesAt(Center, (VisualMeters * 100.0) + IndexMoveCm + 1000.0, All);
	int32 Kept = 0;
	TArray<int32> AnyVisual;
	for (const FSwcMeshInstanceRef& Instance : All)
	{
		const UStaticMesh* Mesh = Instance.Mesh.Get();
		if (Mesh == nullptr)
		{
			continue;
		}
		FString* Folder = MeshFolders.Find(Mesh);
		if (Folder == nullptr)
		{
			MeshRole(Mesh, AnyVisual);   // fills MeshFolders
			Folder = MeshFolders.Find(Mesh);
		}
		if (Folder == nullptr || (!StartFolders.Contains(*Folder) && !DepletedFolders.Contains(*Folder)))
		{
			continue;
		}
		InstanceIndex.FindOrAdd(FIntPoint(FMath::FloorToInt32(Instance.Origin.X / InstanceCellCm), FMath::FloorToInt32(Instance.Origin.Y / InstanceCellCm))).Add(Instance);
		++Kept;
	}
	UE_LOG(LogSwcObjects, Verbose, TEXT("objects: the instance index: %d node mesh instance(s) of %d (level revision %u)"), Kept, All.Num(), IndexRevision);
}

void USwcObjectSubsystem::FindAt(const FVector& Place, TArray<FSwcMeshInstanceRef>& Out) const
{
	Out.Reset();
	const int32 CX = FMath::FloorToInt32(Place.X / InstanceCellCm);
	const int32 CY = FMath::FloorToInt32(Place.Y / InstanceCellCm);
	for (int32 DY = -1; DY <= 1; ++DY)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			if (const TArray<FSwcMeshInstanceRef>* List = InstanceIndex.Find(FIntPoint(CX + DX, CY + DY)))
			{
				for (const FSwcMeshInstanceRef& Instance : *List)
				{
					if (FVector2D::DistSquared(FVector2D(Instance.Origin), FVector2D(Place)) <= InstanceNearCm * InstanceNearCm
						&& FMath::Abs(Instance.Origin.Z - Place.Z) <= InstanceHeightCm)
					{
						Out.Add(Instance);
					}
				}
			}
		}
	}
}

AActor* USwcObjectSubsystem::VisualActor()
{
	if (!Visual.IsValid() && GetWorld() != nullptr)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
		if (Actor != nullptr)
		{
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
			Root->SetMobility(EComponentMobility::Movable);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
		}
		Visual = Actor;
	}
	return Visual.Get();
}

bool USwcObjectSubsystem::MeshReady(const UStaticMesh* Mesh, const FString& Name)
{
	// A null mesh, a mesh that still loads or compiles, or a mesh with no render data cannot go on a component now (SetStaticMesh read
	// null render data in one crash while a level build ran).
	bool bReady = Mesh != nullptr && !Mesh->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_BeginDestroyed)
		&& !Mesh->HasAnyInternalFlags(EInternalObjectFlags_AsyncLoading) && !Mesh->IsCompiling();
	if (bReady)
	{
		const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		bReady = RenderData != nullptr && RenderData->IsInitialized();
	}
	if (!bReady && !NotReadyLogged.Contains(Name))
	{
		NotReadyLogged.Add(Name);
		UE_LOG(LogSwcObjects, Warning, TEXT("objects: the mesh %s is %s: no swap now, the next update tries again (logged one time)"), *Name,
			Mesh == nullptr ? TEXT("not loaded") : TEXT("not ready (loading, compiling, or no render data)"));
	}
	return bReady;
}

void USwcObjectSubsystem::ApplyNode(uint64 Group, FNodeView& View, uint8 State, bool bLive)
{
	USwcLevelInstances* Level = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcLevelInstances>() : nullptr;
	if (Level == nullptr)
	{
		return;
	}
	TArray<FSwcMeshInstanceRef> Here;
	FindAt(View.Place, Here);
	TArray<FSwcMeshInstanceRef> Healthy;
	TArray<FSwcMeshInstanceRef> Depleted;
	for (const FSwcMeshInstanceRef& Instance : Here)
	{
		const int32 Role = MeshRole(Instance.Mesh.Get(), View.Visuals);
		if (Role == 1)
		{
			Healthy.Add(Instance);
		}
		else if (Role == 2)
		{
			Depleted.Add(Instance);
		}
	}
	const bool bChange = View.AppliedState != State;
	if (State == swc::objects::kNodeDepleted)
	{
		// The healthy mesh hides; the depleted mesh shows: the level instance at the place, else an own mesh with the transform of the
		// healthy instance (the rotation and the scale of the record).
		FTransform Transform(View.Place);
		int32 Visible = INDEX_NONE;
		TArray<TPair<UStaticMesh*, FTransform>> FallParts;
		for (const FSwcMeshInstanceRef& Instance : Healthy)
		{
			if (!Level->IsInstanceHidden(Instance) && Instance.Component.IsValid())
			{
				Instance.Component->GetInstanceTransform(Instance.Index, Transform, true);
				Visible = Instance.Index;
				if (Instance.Mesh.IsValid() && MeshReady(Instance.Mesh.Get(), Instance.Mesh->GetPathName()))
				{
					FallParts.Emplace(Instance.Mesh.Get(), Transform);   // a part whose mesh is not ready does not fall (a one-time visual)
				}
			}
			Level->SetInstanceHidden(Instance, true);
			AddInstance(View.HiddenHealthy, Instance);
		}
		for (const FSwcMeshInstanceRef& Instance : Depleted)
		{
			Level->SetInstanceHidden(Instance, false);
			View.HiddenDepleted.RemoveAll([&Instance](const FSwcMeshInstanceRef& Item) { return SameInstance(Item, Instance); });
		}
		// A tree that the character sees fall (the client: the cut trunk falls, the stump stays; gathering.md, protocol notes 4.44): a copy
		// of each healthy instance turns around the base of the tree, away from the character.
		const bool bTree = Rows.IsValidIndex(View.Row) && Rows[View.Row].Type == TEXT("Chopping");
		AActor* FallOwner = bLive && bTree && !FallParts.IsEmpty() ? VisualActor() : nullptr;
		if (FallOwner != nullptr)
		{
			const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
			const FVector Viewer = Controller != nullptr && Controller->GetPawn() != nullptr ? Controller->GetPawn()->GetActorLocation() : View.Place;
			FVector Away = View.Place - Viewer;
			Away.Z = 0.0;
			Away = Away.IsNearlyZero() ? FVector::ForwardVector : Away.GetSafeNormal();
			FFall& Fall = Falls.AddDefaulted_GetRef();
			Fall.Pivot = View.Place;
			Fall.Axis = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal();
			Fall.Start = FPlatformTime::Seconds();
			for (const TPair<UStaticMesh*, FTransform>& Part : FallParts)
			{
				UStaticMeshComponent* Trunk = NewObject<UStaticMeshComponent>(FallOwner);
				Trunk->SetMobility(EComponentMobility::Movable);
				Trunk->SetStaticMesh(Part.Key);
				Trunk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Trunk->SetupAttachment(FallOwner->GetRootComponent());
				Trunk->RegisterComponent();
				Trunk->SetWorldTransform(Part.Value);
				Fall.Components.Add(Trunk);
				Fall.Bases.Add(Part.Value);
			}
			UE_LOG(LogSwcObjects, Log, TEXT("objects: the tree %s falls (%d mesh(es)) away from the character"), *View.Key, FallParts.Num());
		}
		FString Shown = Depleted.IsEmpty() ? TEXT("none") : TEXT("the level instance");
		if (Depleted.IsEmpty() && !View.Own.IsValid())
		{
			// The visual of the node: the one of a natural node, or the one whose StartMesh the level placed (a capital node).
			const FVisual* Chosen = nullptr;
			for (const int32 Index : View.Visuals)
			{
				if (!Visuals.IsValidIndex(Index) || Visuals[Index].DepletedMesh.IsEmpty())
				{
					continue;
				}
				const bool bPlaced = !Healthy.IsEmpty() && MeshRole(Healthy[0].Mesh.Get(), TArray<int32>{Index}) == 1;
				if (Chosen == nullptr || bPlaced)
				{
					Chosen = &Visuals[Index];
				}
				if (bPlaced)
				{
					break;
				}
			}
			UStaticMesh* Mesh = nullptr;
			View.bOwnPending = false;
			if (Chosen != nullptr)
			{
				TObjectPtr<UStaticMesh>& Cached = DepletedMeshes.FindOrAdd(Chosen->DepletedMesh);
				const double Now = FPlatformTime::Seconds();
				double& NextLoad = NextMeshLoad.FindOrAdd(Chosen->DepletedMesh);
				if (Cached == nullptr && Now >= NextLoad)
				{
					NextLoad = Now + MeshRetrySeconds;   // a mesh that does not load: at most one try each MeshRetrySeconds
					Cached = LoadObject<UStaticMesh>(nullptr, *Chosen->DepletedMesh, nullptr, LOAD_NoWarn);
				}
				Mesh = Cached;
				if (!MeshReady(Mesh, Chosen->DepletedMesh))
				{
					Mesh = nullptr;
					View.bOwnPending = true;
				}
			}
			AActor* Owner = Mesh != nullptr ? VisualActor() : nullptr;
			if (Owner != nullptr)
			{
				UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner);
				Component->SetMobility(EComponentMobility::Movable);
				Component->SetStaticMesh(Mesh);
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Component->SetupAttachment(Owner->GetRootComponent());
				Component->RegisterComponent();
				Component->SetWorldTransform(Transform);
				View.Own = Component;
				++OwnMeshes;
				Shown = FString::Printf(TEXT("an own mesh %s"), *Mesh->GetName());
			}
		}
		else if (!Depleted.IsEmpty() && View.Own.IsValid())
		{
			View.Own->DestroyComponent();
			View.Own.Reset();
		}
		if (!Depleted.IsEmpty() || View.Own.IsValid())
		{
			View.bOwnPending = false;
		}
		if (bChange)
		{
			++Depletions;
			UE_LOG(LogSwcObjects, Log, TEXT("objects: node %s (%s) is Depleted: %d healthy instance(s) hidden%s, the depleted mesh: %s"), *View.Key, *Hex64(Group),
				Healthy.Num(), Visible == INDEX_NONE && !Healthy.IsEmpty() ? TEXT(" (hidden before)") : TEXT(""), *Shown);
		}
	}
	else
	{
		// Healthy (or no record): the healthy mesh shows again, a depleted level instance at the place hides, the own mesh goes away.
		View.bOwnPending = false;
		for (const FSwcMeshInstanceRef& Instance : View.HiddenHealthy)
		{
			Level->SetInstanceHidden(Instance, false);
		}
		View.HiddenHealthy.Reset();
		for (const FSwcMeshInstanceRef& Instance : Depleted)
		{
			Level->SetInstanceHidden(Instance, true);
			AddInstance(View.HiddenDepleted, Instance);
		}
		const bool bOwn = View.Own.IsValid();
		if (bOwn)
		{
			View.Own->DestroyComponent();
			View.Own.Reset();
		}
		if (bChange && View.AppliedState == swc::objects::kNodeDepleted)
		{
			UE_LOG(LogSwcObjects, Log, TEXT("objects: node %s (%s) is %s again: %d healthy instance(s) shown, %d depleted instance(s) hidden%s"), *View.Key, *Hex64(Group),
				StateName(State), Healthy.Num(), Depleted.Num(), bOwn ? TEXT(", the own mesh removed") : TEXT(""));
		}
	}
	View.AppliedState = State;
	View.AppliedRevision = IndexRevision;
}

void USwcObjectSubsystem::UpdateFalls(double Now)
{
	for (int32 Index = Falls.Num() - 1; Index >= 0; --Index)
	{
		FFall& Fall = Falls[Index];
		const double Time = Now - Fall.Start;
		if (Time > FallSeconds + FallHoldSeconds)
		{
			for (const TWeakObjectPtr<UStaticMeshComponent>& Component : Fall.Components)
			{
				if (Component.IsValid())
				{
					Component->DestroyComponent();
				}
			}
			Falls.RemoveAt(Index);
			continue;
		}
		// Each part turns around the base of the tree, away from the character.
		const double Part = FMath::Min(Time / FallSeconds, 1.0);
		const FQuat Turn(Fall.Axis, FMath::DegreesToRadians(FallDegrees * Part * Part));
		for (int32 Piece = 0; Piece < Fall.Components.Num(); ++Piece)
		{
			if (!Fall.Components[Piece].IsValid())
			{
				continue;
			}
			const FTransform& Base = Fall.Bases[Piece];
			FTransform Pose = Base;
			Pose.SetLocation(Fall.Pivot + Turn.RotateVector(Base.GetLocation() - Fall.Pivot));
			Pose.SetRotation(Turn * Base.GetRotation());
			Fall.Components[Piece]->SetWorldTransform(Pose);
		}
	}
}

void USwcObjectSubsystem::RestoreNode(FNodeView& View)
{
	if (USwcLevelInstances* Level = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcLevelInstances>() : nullptr)
	{
		for (const FSwcMeshInstanceRef& Instance : View.HiddenHealthy)
		{
			Level->SetInstanceHidden(Instance, false);
		}
		for (const FSwcMeshInstanceRef& Instance : View.HiddenDepleted)
		{
			Level->SetInstanceHidden(Instance, false);
		}
	}
	View.HiddenHealthy.Reset();
	View.HiddenDepleted.Reset();
	if (View.Own.IsValid())
	{
		View.Own->DestroyComponent();
	}
	View.Own.Reset();
}

void USwcObjectSubsystem::UpdateVisuals(const FVector& Center)
{
	const USwcLevelInstances* Level = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcLevelInstances>() : nullptr;
	if (Level == nullptr)
	{
		return;
	}
	const bool bIndex = Level->GetRevision() != IndexRevision || FVector::Dist(Center, IndexCenter) > IndexMoveCm;
	if (bIndex)
	{
		RebuildInstanceIndex(Center);
	}
	const bool bMoved = FVector::Dist(Center, VisualCenter) > VisualMoveCm;
	if (!bIndex && !bMoved && StateRevision == VisualStateRevision)
	{
		return;
	}
	VisualCenter = Center;
	VisualStateRevision = StateRevision;
	for (TPair<uint64, FNodeView>& Pair : NodeViews)
	{
		Pair.Value.bSeen = false;
	}
	const swc::net::WorldPosition Engine = SwcUnrealToEngine(Center);
	const FVector3d From(Engine.x, Engine.y, Engine.z);
	const double RadiusSq2D = VisualMeters * VisualMeters;
	const int32 PerRegion = static_cast<int32>(swc::objects::kRegionMeters / swc::objects::kCellMeters);
	const auto Visit = [&](uint64 Group, const FString& Key, const FVector3d& Place, const TArray<int32>& NodeVisuals, int32 Row) {
		if (FVector2d::DistSquared(FVector2d(Place.X, Place.Y), FVector2d(From.X, From.Y)) > RadiusSq2D || NodeVisuals.IsEmpty())
		{
			return;
		}
		bool bNew = false;
		FNodeView* View = NodeViews.Find(Group);
		if (View == nullptr)
		{
			View = &NodeViews.Add(Group);
			View->Key = Key;
			View->Place = ToUnreal(Place);
			View->Visuals = NodeVisuals;
			View->AppliedState = 0;
			View->Row = Row;
			bNew = true;
		}
		View->bSeen = true;
		const uint8* Found = NodeStates.Find(Group);
		const uint8 State = Found != nullptr ? *Found : 0;
		if (bNew || bIndex || View->AppliedState != State || View->bOwnPending)
		{
			ApplyNode(Group, *View, State, !bNew && View->AppliedState != State);
		}
	};
	for (int32 GY = swc::objects::grid_cell(From.Y - VisualMeters); GY <= swc::objects::grid_cell(From.Y + VisualMeters); ++GY)
	{
		for (int32 GX = swc::objects::grid_cell(From.X - VisualMeters); GX <= swc::objects::grid_cell(From.X + VisualMeters); ++GX)
		{
			const FRegion* Region = Regions.Find(FIntPoint(FloorDiv(GX, PerRegion), FloorDiv(GY, PerRegion)));
			if (Region == nullptr)
			{
				continue;
			}
			for (const swc::objects::NodeRecord& Node : Region->Nodes.cell(GX - Region->X * PerRegion, GY - Region->Y * PerRegion))
			{
				if (!Region->Types.IsValidIndex(Node.type))
				{
					continue;
				}
				// The visual of the variant of the record first (its depleted mesh is the own mesh), then the meshes of the slice.
				const FNodeType& Type = Region->Types[Node.type];
				TArray<int32> NodeVisuals;
				if (Type.Visual >= 0)
				{
					NodeVisuals.Add(Type.Visual);
				}
				if (Templates.IsValidIndex(Type.Template))
				{
					for (const int32 Index : Templates[Type.Template].SliceVisuals)
					{
						NodeVisuals.AddUnique(Index);
					}
				}
				Visit(swc::objects::natural_group(Region->X, Region->Y, Node.record + 1), FString::Printf(TEXT("%s#%u"), *Region->Name, Node.record),
					FVector3d(Node.x, Node.y, Node.z), NodeVisuals, Type.Row);
			}
			if (const TArray<int32>* List = Region->Grid.Find(FIntPoint(GX, GY)))
			{
				for (const int32 Index : *List)
				{
					const FDataObject& Object = Region->Objects[Index];
					if (Object.Kind == KindNode && Templates.IsValidIndex(Object.Template))
					{
						TArray<int32> NodeVisuals = Templates[Object.Template].Visuals;
						for (const int32 SliceVisual : Templates[Object.Template].SliceVisuals)
						{
							NodeVisuals.AddUnique(SliceVisual);
						}
						Visit(Object.Group, Object.Key, Object.EnginePlace, NodeVisuals, Object.Row);
					}
				}
			}
		}
	}
	// The nodes that are far now: the level state again.
	for (auto It = NodeViews.CreateIterator(); It; ++It)
	{
		if (!It.Value().bSeen)
		{
			if (It.Value().AppliedState == swc::objects::kNodeDepleted)
			{
				UE_LOG(LogSwcObjects, Log, TEXT("objects: node %s is far: the level meshes again"), *It.Value().Key);
			}
			RestoreNode(It.Value());
			It.RemoveCurrent();
		}
	}
}

void USwcObjectSubsystem::UpdateChestLights(const FVector& Center)
{
	const swc::net::WorldPosition Engine = SwcUnrealToEngine(Center);
	const FVector3d From(Engine.x, Engine.y, Engine.z);
	TSet<uint64> Seen;
	const int32 PerRegion = static_cast<int32>(swc::objects::kRegionMeters / swc::objects::kCellMeters);
	for (int32 GY = swc::objects::grid_cell(From.Y - ChestLightMeters); GY <= swc::objects::grid_cell(From.Y + ChestLightMeters); ++GY)
	{
		for (int32 GX = swc::objects::grid_cell(From.X - ChestLightMeters); GX <= swc::objects::grid_cell(From.X + ChestLightMeters); ++GX)
		{
			const FRegion* Region = Regions.Find(FIntPoint(FloorDiv(GX, PerRegion), FloorDiv(GY, PerRegion)));
			const TArray<int32>* List = Region != nullptr ? Region->Grid.Find(FIntPoint(GX, GY)) : nullptr;
			if (List == nullptr)
			{
				continue;
			}
			for (const int32 Index : *List)
			{
				const FDataObject& Object = Region->Objects[Index];
				if (Object.Kind != KindChest || FVector3d::Dist(From, Object.EnginePlace) > ChestLightMeters
					|| StateOf(Object.Kind, Object.Group) != ESwcWorldObjectState::Ready)
				{
					continue;
				}
				Seen.Add(Object.Group);
				if (TWeakObjectPtr<UPointLightComponent>* Light = ChestLights.Find(Object.Group); Light != nullptr && Light->IsValid())
				{
					continue;
				}
				AActor* Owner = VisualActor();
				if (Owner == nullptr)
				{
					continue;
				}
				UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner);
				Light->SetMobility(EComponentMobility::Movable);
				Light->SetIntensityUnits(ELightUnits::Candelas);
				Light->SetIntensity(40.0f);
				Light->SetLightColor(FLinearColor(1.0f, 0.75f, 0.35f));
				Light->SetAttenuationRadius(400.0f);
				Light->SetCastShadows(false);
				Light->SetupAttachment(Owner->GetRootComponent());
				Light->RegisterComponent();
				Light->SetWorldLocation(ToUnreal(Object.EnginePlace + FVector3d(0.0, 0.0, ChestLightHeight)));
				ChestLights.Add(Object.Group, Light);
				if (swc::objects::chest_change(ClaimOf(LastClaims, Object.Group), ClaimOf(ChestClaims, Object.Group)) == swc::objects::ChestChange::reset)
				{
					// The reset of the claim (the server sends the map with no entry: chests.md, live check L8).
					UE_LOG(LogSwcObjects, Log, TEXT("objects: chest %s (%s): the claim reset: the light of the loot comes back"), *Object.Key, *Text(Object.ChestName));
				}
			}
		}
	}
	// The depletion effect of an elite chest at its change to Looted (live check L9 of the backend: an elite chest shows its depletion
	// effect, a supply chest no change).
	for (const TPair<uint64, uint32>& Claim : ChestClaims)
	{
		if (swc::objects::chest_change(ClaimOf(LastClaims, Claim.Key), Claim.Value) != swc::objects::ChestChange::depleted)
		{
			continue;
		}
		const FDataObject* Elite = nullptr;
		for (int32 GY = swc::objects::grid_cell(From.Y - ChestLightMeters); GY <= swc::objects::grid_cell(From.Y + ChestLightMeters) && Elite == nullptr; ++GY)
		{
			for (int32 GX = swc::objects::grid_cell(From.X - ChestLightMeters); GX <= swc::objects::grid_cell(From.X + ChestLightMeters) && Elite == nullptr; ++GX)
			{
				const FRegion* Region = Regions.Find(FIntPoint(FloorDiv(GX, PerRegion), FloorDiv(GY, PerRegion)));
				const TArray<int32>* List = Region != nullptr ? Region->Grid.Find(FIntPoint(GX, GY)) : nullptr;
				for (const int32 Index : List != nullptr ? *List : TArray<int32>())
				{
					const FDataObject& Object = Region->Objects[Index];
					if (Object.Kind == KindChest && Object.Group == Claim.Key && Object.bElite)
					{
						Elite = &Object;
						break;
					}
				}
			}
		}
		AActor* Owner = Elite != nullptr ? VisualActor() : nullptr;
		if (Owner != nullptr)
		{
			UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner);
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetIntensityUnits(ELightUnits::Candelas);
			Light->SetIntensity(BurstCandelas);
			Light->SetLightColor(FLinearColor(1.0f, 0.85f, 0.5f));
			Light->SetAttenuationRadius(1000.0f);
			Light->SetCastShadows(false);
			Light->SetupAttachment(Owner->GetRootComponent());
			Light->RegisterComponent();
			Light->SetWorldLocation(ToUnreal(Elite->EnginePlace + FVector3d(0.0, 0.0, 1.0)));
			Bursts.Add({Light, FPlatformTime::Seconds()});
			UE_LOG(LogSwcObjects, Log, TEXT("objects: the elite chest %s (%s): the depletion effect (a stand-in for its DepletedFX)"), *Elite->Key, *Text(Elite->ChestName));
		}
	}
	LastClaims = ChestClaims;
	for (auto It = ChestLights.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			const uint32* Claim = ChestClaims.Find(It.Key());
			if (Claim != nullptr)
			{
				UE_LOG(LogSwcObjects, Log, TEXT("objects: chest %s: the light of the loot goes off (claim %u)"), *Hex64(It.Key()), *Claim);
			}
			if (It.Value().IsValid())
			{
				It.Value()->DestroyComponent();
			}
			It.RemoveCurrent();
		}
	}
}

const FString& USwcObjectSubsystem::ItemName(uint32 Index)
{
	if (!bItemsLoaded)
	{
		bItemsLoaded = true;
		// The ids of the item catalog of the backend copy (the item index of the wire is the position + 1).
		const FString Path = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_GAMEDATA_DIR")), TEXT("item-catalog.json"));
		FString Json;
		TSharedPtr<FJsonObject> Catalog;
		if (FFileHelper::LoadFileToString(Json, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Catalog) && Catalog.IsValid())
		{
			for (const TSharedPtr<FJsonValue>& Row : Catalog->GetArrayField(TEXT("items")))
			{
				const TArray<TSharedPtr<FJsonValue>>& Values = Row->AsArray();
				ItemIds.Add(Values.IsEmpty() ? FString() : Values[0]->AsString());
			}
		}
	}
	static const FString Unknown(TEXT("?"));
	return Index >= 1 && ItemIds.IsValidIndex(static_cast<int32>(Index) - 1) ? ItemIds[Index - 1] : Unknown;
}

void USwcObjectSubsystem::UpdateLoot(const swc::net::RepClient::OwnEntity& Own, double Now)
{
	if (WorldSince < 0.0)
	{
		WorldSince = Now;
	}
	// The loot notice: the items that the bag of the character gained (a chest, a node, a bag).
	if (Own.bagRevision != SeenBagRevision)
	{
		SeenBagRevision = Own.bagRevision;
		TMap<uint32, uint32> Counts;
		for (const soulwarden::backend::tests::WireItem& Item : Own.bag)
		{
			if (!Item.empty())
			{
				Counts.FindOrAdd(Item.index) += Item.quantity;
			}
		}
		if (bBagSeeded)
		{
			for (const TPair<uint32, uint32>& Pair : Counts)
			{
				const uint32 Before = BagCounts.FindRef(Pair.Key);
				if (Pair.Value > Before)
				{
					const FString Line = FString::Printf(TEXT("+%u %s"), Pair.Value - Before, *ItemName(Pair.Key));
					LootLines.Emplace(Line, Now);
					UE_LOG(LogSwcObjects, Log, TEXT("objects: the loot notice: %s"), *Line);
				}
			}
		}
		BagCounts = MoveTemp(Counts);
		bBagSeeded = true;
	}
	LootLines.RemoveAll([Now](const TPair<FString, double>& Line) { return Now - Line.Value > LootLineSeconds; });
	// The development drop: the stack of an item of the bag into a Discarded bag at the feet (1838; loot-bags.md, live check L7).
	if (DropAt >= 0.0 && Now - WorldSince >= DropAt)
	{
		DropAt = -1.0;
		for (uint32 Slot = 0; Slot < Own.bag.size(); ++Slot)
		{
			const soulwarden::backend::tests::WireItem& Item = Own.bag[Slot];
			if (!Item.empty() && ItemName(Item.index).Equals(DropItem, ESearchCase::IgnoreCase))
			{
				swc::net::DropItemEntry Entry;
				Entry.slot = Slot;
				std::copy(Item.instance.begin(), Item.instance.end(), Entry.instance.begin());
				Entry.quantity = Item.quantity;
				UGameInstance* GameInstance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
				USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
				const bool bSent = Backend != nullptr && Backend->DropItems({Entry});
				UE_LOG(LogSwcObjects, Log, TEXT("objects: the development drop of %u %s (bag slot %u): %s"), Item.quantity, *DropItem, Slot,
					bSent ? TEXT("DropInventoryItems 1838 sent") : TEXT("not sent"));
				return;
			}
		}
		UE_LOG(LogSwcObjects, Warning, TEXT("objects: the development drop: the bag has no %s"), *DropItem);
	}
}

void USwcObjectSubsystem::DrawLoot(UCanvas* Canvas, APlayerController* Controller)
{
	UFont* Font = GEngine != nullptr ? GEngine->GetMediumFont() : nullptr;
	if (Canvas == nullptr || Font == nullptr || LootLines.IsEmpty() || bUiLootNotice)
	{
		return;
	}
	// A stand-in for the loot notice of the HUD (the UI track): the lines at the right of the screen.
	float Y = Canvas->ClipY * 0.55f;
	for (const TPair<FString, double>& Line : LootLines)
	{
		FCanvasTextItem Item(FVector2D(Canvas->ClipX * 0.72f, Y), FText::FromString(Line.Key), Font, FLinearColor(1.0f, 0.9f, 0.6f));
		Item.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Item);
		Y += 22.0f;
	}
}

void USwcObjectSubsystem::SightSearch(const FVector& Center)
{
	UWorld* World = GetWorld();
	TArray<FSwcWorldObjectTarget> Near;
	GetObjectsNear(Center, 250.0, Near);
	int32 Found = 0;
	for (const FSwcWorldObjectTarget& Target : Near)
	{
		if (Target.Kind != KindChest || !Target.bLineOfSight || World == nullptr)
		{
			continue;
		}
		const FVector3d Place(Target.EnginePlace.x, Target.EnginePlace.y, Target.EnginePlace.z);
		for (int32 Angle = 0; Angle < 360; Angle += 30)
		{
			for (const double Radius : {3.0, 5.0, 7.0, 9.0})
			{
				// The ground under the stand place: a line down from 6 m above the chest.
				const FVector3d Flat = Place + FVector3d(Radius * FMath::Cos(FMath::DegreesToRadians(Angle)), Radius * FMath::Sin(FMath::DegreesToRadians(Angle)), 0.0);
				FHitResult Ground;
				if (!World->LineTraceSingleByChannel(Ground, ToUnreal(Flat + FVector3d(0.0, 0.0, 6.0)), ToUnreal(Flat - FVector3d(0.0, 0.0, 12.0)), ECC_Visibility))
				{
					continue;
				}
				const swc::net::WorldPosition Feet = SwcUnrealToEngine(Ground.ImpactPoint);
				const FVector3d FeetPlace(Feet.x, Feet.y, Feet.z);
				if (FVector3d::DistSquared(FeetPlace, Place) > Target.RangeSq)
				{
					continue;
				}
				// The rule of the prompt (HasSight), and a line to 1.2 m above the object (over the mesh of a chest).
				const bool bVisibility = HasSight(FeetPlace, Place);
				FHitResult Hit;
				const FVector Eye = ToUnreal(FeetPlace + FVector3d(0.0, 0.0, swc::objects::kSightEyeMeters));
				const FVector Aim = ToUnreal(Place + FVector3d(0.0, 0.0, swc::objects::kSightAimMeters));
				const FVector High = ToUnreal(Place + FVector3d(0.0, 0.0, 1.2));
				FHitResult HighHit;
				const bool bHigh = !World->LineTraceSingleByChannel(HighHit, Eye, High, ECC_Visibility) || FVector::Dist(HighHit.ImpactPoint, High) <= swc::objects::kSightOwnMeters * 100.0;
				if (bVisibility)
				{
					continue;
				}
				World->LineTraceSingleByChannel(Hit, Eye, Aim, ECC_Visibility);
				const UInstancedStaticMeshComponent* HitInstances = Cast<UInstancedStaticMeshComponent>(Hit.GetComponent());
				FTransform HitTransform;
				FString MeshText = TEXT("-");
				if (HitInstances != nullptr && HitInstances->GetStaticMesh() != nullptr && HitInstances->GetInstanceTransform(Hit.Item, HitTransform, true))
				{
					const FVector Size = HitInstances->GetStaticMesh()->GetBoundingBox().GetSize() * HitTransform.GetScale3D().GetAbs() / 100.0;
					MeshText = FString::Printf(TEXT("%s (%.1f x %.1f x %.1f m, origin %.1f m from the object)"), *HitInstances->GetStaticMesh()->GetName(), Size.X, Size.Y, Size.Z,
						FVector::Dist(HitTransform.GetLocation(), ToUnreal(Place)) / 100.0);
				}
				++Found;
				UE_LOG(LogSwcObjects, Log, TEXT("objects sight search: chest %s (%s): from %.2f %.2f %.2f (%.1f m): blocked by %s %s item %d at %.2f m from the aim; the line to 1.2 m: %s"),
					*Target.Key, *Target.Name, Feet.x, Feet.y, Feet.z, FVector3d::Dist(FeetPlace, Place), Hit.GetComponent() != nullptr ? *Hit.GetComponent()->GetClass()->GetName() : TEXT("-"),
					*MeshText, Hit.Item, FVector::Dist(Hit.ImpactPoint, Aim) / 100.0, bHigh ? TEXT("in sight") : TEXT("blocked"));
			}
		}
	}
	UE_LOG(LogSwcObjects, Log, TEXT("objects sight search: %d place(s) with no line of sight near %d bound(s)"), Found, Near.Num());
}

void USwcObjectSubsystem::ClearAll(const TCHAR* Cause)
{
	for (TPair<uint64, FNodeView>& Pair : NodeViews)
	{
		RestoreNode(Pair.Value);
	}
	NodeViews.Reset();
	for (TPair<uint64, TWeakObjectPtr<UPointLightComponent>>& Pair : ChestLights)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->DestroyComponent();
		}
	}
	ChestLights.Reset();
	for (FFall& Fall : Falls)
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& Component : Fall.Components)
		{
			if (Component.IsValid())
			{
				Component->DestroyComponent();
			}
		}
	}
	Falls.Reset();
	for (FBurst& Burst : Bursts)
	{
		if (Burst.Light.IsValid())
		{
			Burst.Light->DestroyComponent();
		}
	}
	Bursts.Reset();
	LastClaims.Reset();
	ClaimMaps = 0;
	BagCounts.Reset();
	bBagSeeded = false;
	SeenBagRevision = 0;
	LootLines.Reset();
	WorldSince = -1.0;
	if (Visual.IsValid())
	{
		Visual->Destroy();
	}
	Visual.Reset();
	if (!Regions.IsEmpty() || Depletions > 0)
	{
		UE_LOG(LogSwcObjects, Log, TEXT("objects: %s: %d region(s), %d node depletion(s) shown, %d own depleted mesh(es)"), Cause, Regions.Num(), Depletions, OwnMeshes);
	}
	Regions.Reset();
	NodeStates.Reset();
	BoundEnabled.Reset();
	ChestClaims.Reset();
	Achievements.clear();
	InstanceIndex.Reset();
	IndexCenter = VisualCenter = FVector(1.0e12);
	SeenTableRevision = SeenLootRevision = SeenAchievementRevision = 0;
	++StateRevision;
	Depletions = OwnMeshes = 0;
}

void USwcObjectSubsystem::DevelopmentAids()
{
	const double Now = FPlatformTime::Seconds();
	if (Now < NextProbe)
	{
		return;
	}
	NextProbe = Now + 1.0;
	UWorld* World = GetWorld();
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		return;
	}
	// The feet of the pawn: the place of the character in its input report.
	const FVector Feet = Pawn->GetActorLocation() - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
	TArray<FSwcWorldObjectTarget> Near;
	GetObjectsNear(Feet, 15.0, Near);
	const swc::net::WorldPosition FeetEngine = SwcUnrealToEngine(Feet);
	for (const FSwcWorldObjectTarget& Target : Near)
	{
		const bool bInRange = Target.Distance * Target.Distance <= Target.RangeSq;
		bool bSight = true;
		if (Target.bLineOfSight && bInRange)
		{
			// The line of sight of a bound in range (the prompt needs it): a log line at each change.
			bSight = HasSight(FVector3d(FeetEngine.x, FeetEngine.y, FeetEngine.z),
				FVector3d(Target.EnginePlace.x, Target.EnginePlace.y, Target.EnginePlace.z));
			const bool* Before = ProbeSight.Find(Target.Key);
			if (Before == nullptr || *Before != bSight)
			{
				ProbeSight.Add(Target.Key, bSight);
				UE_LOG(LogSwcObjects, Log, TEXT("objects probe: %s %s (%s) in range at %.1f m: %s"), *Target.Kind.ToString(), *Target.Key, *Target.Name, Target.Distance,
					bSight ? TEXT("in sight") : TEXT("no line of sight: no prompt"));
			}
		}
		const bool bUsable = Target.State == ESwcWorldObjectState::Ready && Target.bEnabled && bInRange && bSight;
		if (bProbeDraw)
		{
			DrawDebugSphere(World, Target.Place, FMath::Min(FMath::Sqrt(Target.RangeSq), 3.0f) * 100.0f, 12, bUsable ? FColor::Green : FColor::Red, false, 1.0f);
		}
	}
	FSwcWorldObjectTarget Best;
	if (FindNearestObject(Feet, Best))
	{
		const FSwcWorldObjectOption* First = Best.Options.IsEmpty() ? nullptr : &Best.Options[0];
		if (Best.Row >= 0 || !Best.UseKind.IsEmpty())
		{
			UE_LOG(LogSwcObjects, Log, TEXT("objects probe: %s %s: row %d (%s, %s), tool slot %u item %u (%s)%s, use %s"), *Best.Kind.ToString(), *Best.Name, Best.Row,
				*Best.GatheringType, *Best.GatherAction, Best.ToolSlot, Best.ToolItem, *Best.ToolTags,
				Best.bToolMissing ? *FString::Printf(TEXT(": %s"), *Best.RefusalText) : TEXT(""), *Best.UseKind);
		}
		UE_LOG(LogSwcObjects, Log, TEXT("objects probe: nearest %s %s at %.1f m: actor %s, target %s, slot %u, %s (action %u, option entity %s)%s"), *Best.Kind.ToString(),
			*Best.Key, Best.Distance, UTF8_TO_TCHAR(swc::net::hex_of(std::span(Best.Actor).first(8)).c_str()), *Hex64(Best.Target), Best.Slot,
			First != nullptr ? UTF8_TO_TCHAR(First->Option.name.c_str()) : TEXT("no option"), First != nullptr ? First->Option.action : 0,
			First != nullptr ? *Hex64(First->Request.optionEntity) : TEXT("-"), Best.LoreBit >= 0 ? (Best.bRead ? TEXT(", read") : TEXT(", not read")) : TEXT(""));
	}
	UE_LOG(LogSwcObjects, Verbose, TEXT("objects probe: %d bound(s) within 15 m, %d node view(s), %d chest light(s)"), Near.Num(), NodeViews.Num(), ChestLights.Num());
}
