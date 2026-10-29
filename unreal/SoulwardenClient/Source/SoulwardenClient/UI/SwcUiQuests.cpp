#include "UI/SwcUiQuests.h"

#include "SwcBackendSubsystem.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SwcUi/Runtime.h"
#include "Net/Codec/SwcRepMessages.h"

#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogSwcUiQuests, Log, All);

void FSwcUiQuests::Reset()
{
	for (FPoi& Poi : Pois)
	{
		Poi.bInSphere = false;
		Poi.bInShape = false;
	}
	bEnabled = false;
	bRestored = false;
	SentRevision = 0;
	SentReadyRevision = 0;
	bSentAny = false;
}

void FSwcUiQuests::LoadEncounterTrees(const FString& ContentDir)
{
	// The task tree of an encounter objective (protocol notes 4.62, the collector 0x26F1FD0 and the factories of 0x44650F0): the stages of
	// the manager in their order (stageOrder) and the child stages of each one, and in each stage with m_userWave each objective
	// (objective, then more) that makes a task: InvasionWaveObjective (target 1), SliceDestroyedObjective and SliceSpawnedObjective (target
	// m_requiredCount), EntityActiveStateChangedObjective (target 1, only with a description); WaitForParentObjective and the other types
	// make no task. Task m has the leaf 2m+1 and its wrapper 2m+2; the outer container is 2T+1 (swc/quests.lua builds the nodes).
	bEncounterTreesLoaded = true;
	// The head title of each recipe (E2b; static read work/ut567/qgplan/E2b): the name key of the territory definition of the "Territory
	// Id" of the TerritoryDataProviderComponent of the root (ui/gamedata/encounters.json, tools/convert-ui.sh encounters). No id or no row:
	// an empty title (retail has no fallback, not the stage title).
	TMap<FString, FString> Heads;
	{
		FString HeadText;
		TSharedPtr<FJsonObject> HeadRoot;
		const TSharedPtr<FJsonObject>* Titles = nullptr;
		const FString HeadPath = FPaths::Combine(ContentDir, TEXT("ui/gamedata/encounters.json"));
		if (FFileHelper::LoadFileToString(HeadText, *HeadPath) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(HeadText), HeadRoot)
			&& HeadRoot.IsValid() && HeadRoot->TryGetObjectField(TEXT("titles"), Titles))
		{
			for (const auto& Entry : (*Titles)->Values)
			{
				const TSharedPtr<FJsonObject> Head = Entry.Value->AsObject();
				if (Head.IsValid())
				{
					Heads.Add(FString(Entry.Key).ToUpper(), Head->GetStringField(TEXT("title")));
				}
			}
		}
		else
		{
			UE_LOG(LogSwcUiQuests, Warning, TEXT("ui: quests: no encounter titles (%s; tools/convert-ui.sh encounters): the encounter rows have no head"), *HeadPath);
		}
	}
	FString Text;
	const FString Path = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_GAMEDATA_DIR")), TEXT("encounter-data.json"));
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogSwcUiQuests, Warning, TEXT("ui: quests: no encounter data (%s): an encounter row has no task text"), *Path);
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Recipes = nullptr;
	if (!Root->TryGetArrayField(TEXT("recipes"), Recipes))
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Recipes)
	{
		const TSharedPtr<FJsonObject> Recipe = Value->AsObject();
		FString Asset;
		const TArray<TSharedPtr<FJsonValue>>* Stages = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Order = nullptr;
		if (!Recipe.IsValid() || !Recipe->TryGetStringField(TEXT("asset"), Asset) || EncounterTrees.Contains(Asset.ToUpper())
			|| !Recipe->TryGetArrayField(TEXT("stages"), Stages) || !Recipe->TryGetArrayField(TEXT("stageOrder"), Order))
		{
			continue;
		}
		const FString* Head = Heads.Find(Asset.ToUpper());
		const FString Title = Head != nullptr ? *Head : FString();
		TArray<FString> Tasks;
		TSet<int32> Seen;
		TFunction<void(int32)> Visit = [&](int32 Index) {
			if (!Stages->IsValidIndex(Index) || Seen.Contains(Index))
			{
				return;
			}
			Seen.Add(Index);
			const TSharedPtr<FJsonObject> Stage = (*Stages)[Index]->AsObject();
			bool bUserWave = false;
			const TSharedPtr<FJsonObject>* Objective = nullptr;
			if (Stage.IsValid() && Stage->TryGetBoolField(TEXT("userWave"), bUserWave) && bUserWave && Stage->TryGetObjectField(TEXT("objective"), Objective))
			{
				TArray<TSharedPtr<FJsonObject>> Objectives{*Objective};
				const TArray<TSharedPtr<FJsonValue>>* More = nullptr;
				if ((*Objective)->TryGetArrayField(TEXT("more"), More))
				{
					for (const TSharedPtr<FJsonValue>& Extra : *More)
					{
						Objectives.Add(Extra->AsObject());
					}
				}
				for (const TSharedPtr<FJsonObject>& Each : Objectives)
				{
					if (!Each.IsValid())
					{
						continue;
					}
					const FString Type = Each->GetStringField(TEXT("type"));
					const FString Description = Each->GetStringField(TEXT("description"));
					int32 Target = 0;
					if (Type == TEXT("InvasionWaveObjective") || (Type == TEXT("EntityActiveStateChangedObjective") && !Description.IsEmpty()))
					{
						Target = 1;
					}
					else if (Type == TEXT("SliceDestroyedObjective") || Type == TEXT("SliceSpawnedObjective"))
					{
						double Required = 1.0;
						Each->TryGetNumberField(TEXT("requiredCount"), Required);
						Target = FMath::Max(1, static_cast<int32>(Required));
					}
					if (Target == 0)
					{
						continue;
					}
					double TimeLimit = 0.0;
					Each->TryGetNumberField(TEXT("timeLimit"), TimeLimit);
					bool bCountDown = false;
					Each->TryGetBoolField(TEXT("countDown"), bCountDown);
					const FString StageTitle = Each->GetStringField(TEXT("title"));
					// The fifth field: the title of the stage objective, the text of the wrapper line (retail: "Stellar Research" under the
					// head line; ut567 2026-10-29 retail stills).
					Tasks.Add(FString::Printf(TEXT("%s|%d|%g|%d|%s"), *Description, Target, TimeLimit, bCountDown ? 1 : 0, *StageTitle));
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (Stage.IsValid() && Stage->TryGetArrayField(TEXT("children"), Children))
			{
				for (const TSharedPtr<FJsonValue>& Child : *Children)
				{
					Visit(static_cast<int32>(Child->AsNumber()));
				}
			}
		};
		for (const TSharedPtr<FJsonValue>& Entry : *Order)
		{
			Visit(static_cast<int32>(Entry->AsNumber()));
		}
		// A recipe with no user wave (Legion Attack: all stages userWave false) has no row in the tracker: no tree (ut567 2026-10-29, the
		// client drew a lone POI diamond with no title).
		if (Tasks.Num() > 0)
		{
			EncounterTrees.Add(Asset.ToUpper(), Title + TEXT("\t") + FString::Join(Tasks, TEXT("\t")));
		}
	}
	UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: the task trees of %d encounter slice(s) (%s)"), EncounterTrees.Num(), *Path);
}

void FSwcUiQuests::Feed(swc::ui::Runtime& Runtime, const swc::net::RepClient::OwnEntity& Own, USwcBackendSubsystem& Backend, const FString& ContentDir)
{
	using FArg = swc::ui::Runtime::Arg;
	if (!Own.hasMetadata)
	{
		return;
	}
	if (Own.group != Group)
	{
		// A new character or a new world session: the calls and the state start again.
		Reset();
		Group = Own.group;
		Backend.SetQuestTags(TSet<FString>());
	}
	if (!bEnabled)
	{
		// The auto-pin flags of 2709 (the options Hud.LocalPlayer.Options.Misc.EnableAutoPinningObjectives.*; the retail default: each type
		// on, CommunityGoal off: live check L1 of stage 1.8a).
		std::vector<bool> Flags(swc::net::kAutoPinFlags, true);
		Flags[swc::net::kCommunityGoalType] = false;
		bEnabled = Backend.EnableObjectives(Flags);
	}
	// The encounter roots of the links (QG5; protocol notes 4.62): a root shows when the own Objectives field 5 has its G and its record
	// is known (a link can come before the root, and the root before the link).
	TArray<TPair<uint32, swc::net::RepClient::OtherEntity>> Roots;
	Backend.GetEncounterRoots(Roots);
	std::vector<const swc::net::RepClient::OtherEntity*> Linked;
	uint64 Encounters = 1469598103934665603ULL;
	const auto Mix = [&Encounters](uint64 Value) { Encounters = (Encounters ^ Value) * 1099511628211ULL; };
	for (const std::uint64_t Link : Own.encounterLinks)
	{
		Mix(Link);
		for (const TPair<uint32, swc::net::RepClient::OtherEntity>& Root : Roots)
		{
			if (swc::net::group_of(Root.Value.actor) == Link && Root.Value.hasRootObjectives && !Root.Value.rootActive.empty())
			{
				Linked.push_back(&Root.Value);
				Mix(Root.Key);
				Mix(Root.Value.encounterRevision);
				break;
			}
		}
	}
	if (Own.questRevision != SentRevision || Encounters != SentEncounters)
	{
		SentRevision = Own.questRevision;
		SentEncounters = Encounters;
		std::string Lines;
		// "E INSTANCE ASSET": the objective of a linked root and the slice asset of its recipe (the GUID in the order of its text); its task
		// elements and timer starts in the T and S lines; it is tracked first (the retail HUD shows it at the top).
		for (const swc::net::RepClient::OtherEntity* Root : Linked)
		{
			char Asset[33];
			for (int Index = 0; Index < 16; ++Index)
			{
				std::snprintf(Asset + Index * 2, 3, "%02X", Root->sliceAsset[Index]);
			}
			const std::uint64_t Instance = Root->rootActive.front().instance;
			if (!bEncounterTreesLoaded)
			{
				LoadEncounterTrees(ContentDir);
			}
			const FString* Tree = EncounterTrees.Find(UTF8_TO_TCHAR(Asset));
			if (Tree == nullptr)
			{
				continue;
			}
			Lines += "E " + std::to_string(Instance) + " " + Asset + "\t" + std::string(TCHAR_TO_UTF8(**Tree)) + "\n";
			for (const auto& Task : Root->rootTasks)
			{
				Lines += "T " + std::to_string(Task.instance) + " " + std::to_string(Task.index) + " " + std::to_string(Task.task) + " "
					+ std::to_string(Task.progress) + " " + std::to_string(Task.state) + "\n";
			}
			for (const auto& Start : Root->rootStarts)
			{
				Lines += "S " + std::to_string(Start.instance) + " " + std::to_string(Start.index) + " " + std::to_string(Start.startNs) + "\n";
			}
			Lines += "K " + std::to_string(Instance) + "\n";
		}
		for (const auto& Active : Own.questActive)
		{
			Lines += "A " + std::to_string(Active.instance) + " " + std::to_string(Active.objective) + " " + std::to_string(Active.originTerritory) + " "
				+ std::to_string(Active.parent) + "\n";
		}
		for (const auto& Task : Own.questTasks)
		{
			Lines += "T " + std::to_string(Task.instance) + " " + std::to_string(Task.index) + " " + std::to_string(Task.task) + " "
				+ std::to_string(Task.progress) + " " + std::to_string(Task.state) + "\n";
		}
		for (const std::uint64_t Instance : Own.questTracked)
		{
			Lines += "K " + std::to_string(Instance) + "\n";
		}
		for (const auto& Start : Own.questStarts)
		{
			Lines += "S " + std::to_string(Start.instance) + " " + std::to_string(Start.index) + " " + std::to_string(Start.startNs) + "\n";
		}
		for (const std::uint64_t Instance : Own.questCompleted)
		{
			Lines += "C " + std::to_string(Instance) + "\n";
		}
		Runtime.call("HostQuests", {FArg::of(Lines)});
		bSentAny = true;
		// The InteractTags of the Active tasks: the focus of the quest objects (AST-241).
		std::string Tags;
		Runtime.call("QuestActiveTags", {}, &Tags);
		TArray<FString> Items;
		FString(UTF8_TO_TCHAR(Tags.c_str())).ParseIntoArray(Items, TEXT(","));
		Backend.SetQuestTags(TSet<FString>(Items));
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: %d active, %d task element(s), %d tracked, %d linked encounter(s) of %d link(s)"),
			static_cast<int32>(Own.questActive.size()), static_cast<int32>(Own.questTasks.size()), static_cast<int32>(Own.questTracked.size()),
			static_cast<int32>(Linked.size()), static_cast<int32>(Own.encounterLinks.size()));
	}
	if (Own.pinReadyRevision != SentReadyRevision)
	{
		SentReadyRevision = Own.pinReadyRevision;
		Runtime.call("HostQuestPinReady", {FArg::of(Own.pinReady)});
	}
	// The UI states that opened (2617; swc/manager.lua): sent in the world, after 2709 (the backend counts TaskOpenUiScreen with them).
	for (int32 Guard = 0; Guard < 16; ++Guard)
	{
		std::string Text;
		Runtime.call("TakeQuestScreen", {}, &Text);
		const uint32 Screen = static_cast<uint32>(std::strtoull(Text.c_str(), nullptr, 10));
		if (Screen == 0)
		{
			break;
		}
		if (bEnabled)
		{
			Backend.NotifyOpenUiScreen(Screen);
		}
	}
	if (!bRestored && Own.pinReady && bSentAny)
	{
		// The pinned list of the state goes back to the backend once after the readiness.
		bRestored = Backend.UpdateTrackedObjectives(std::vector<uint64>(Own.questTracked.begin(), Own.questTracked.end()));
	}
}

bool FSwcUiQuests::HandleRequest(const FString& Request, USwcBackendSubsystem& Backend)
{
	if (!Request.StartsWith(TEXT("quest ")))
	{
		return false;
	}
	TArray<FString> Parts;
	Request.ParseIntoArray(Parts, TEXT(" "));
	const auto Number = [](const FString& Text) { return static_cast<uint64>(FCString::Strtoui64(*Text, nullptr, 10)); };
	if (Parts.Num() >= 2 && Parts[1] == TEXT("pins"))
	{
		std::vector<uint64> Instances;
		if (Parts.Num() >= 3)
		{
			TArray<FString> Items;
			Parts[2].ParseIntoArray(Items, TEXT(","));
			for (const FString& Item : Items)
			{
				Instances.push_back(Number(Item));
			}
		}
		Backend.UpdateTrackedObjectives(Instances);
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: the pinned list (1458): %d instance(s)"), static_cast<int32>(Instances.size()));
	}
	else if (Parts.Num() == 3 && Parts[1] == TEXT("screen"))
	{
		Backend.NotifyOpenUiScreen(static_cast<uint32>(Number(Parts[2])));
	}
	else if (Parts.Num() == 3 && Parts[1] == TEXT("abandon"))
	{
		Backend.AbandonObjective(Number(Parts[2]));
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: abandon %s (3836)"), *Parts[2]);
	}
	else if (Parts.Num() == 3 && Parts[1] == TEXT("reset"))
	{
		Backend.ResetObjective(Number(Parts[2]));
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: reset %s (5600)"), *Parts[2]);
	}
	// The offer and the hand-in of the conversation screen (AST-239): the objective CRC.
	else if (Parts.Num() == 3 && Parts[1] == TEXT("accept"))
	{
		Backend.AcceptObjective(static_cast<uint32>(Number(Parts[2])));
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: accept %08x (3772)"), static_cast<uint32>(Number(Parts[2])));
	}
	else if (Parts.Num() == 3 && Parts[1] == TEXT("complete"))
	{
		Backend.CompleteObjective(static_cast<uint32>(Number(Parts[2])));
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: complete %08x (794)"), static_cast<uint32>(Number(Parts[2])));
	}
	else
	{
		UE_LOG(LogSwcUiQuests, Warning, TEXT("ui: a quest request with no form: %s"), *Request);
	}
	return true;
}

namespace
{
uint64 HexU64(const FString& Text)
{
	return static_cast<uint64>(FCString::Strtoui64(*Text, nullptr, 16));
}

bool InPolygon(const TArray<FVector2D>& Polygon, const FVector2D& Point)
{
	bool bInside = false;
	for (int32 I = 0, J = Polygon.Num() - 1; I < Polygon.Num(); J = I++)
	{
		const FVector2D& A = Polygon[I];
		const FVector2D& B = Polygon[J];
		if ((A.Y > Point.Y) != (B.Y > Point.Y) && Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X)
		{
			bInside = !bInside;
		}
	}
	return bInside;
}
} // namespace

void FSwcUiQuests::FeedPlaces(const swc::net::WorldPosition& Feet, const swc::net::RepClient::OwnEntity& Own, USwcBackendSubsystem& Backend,
	const FString& ContentDir, double Now)
{
	if (!bPoisLoaded)
	{
		bPoisLoaded = true;
		const FString Path = FPaths::Combine(ContentDir, TEXT("ui/gamedata/poi-cues.json"));
		FString Text;
		TSharedPtr<FJsonObject> Document;
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document)
			|| !Document.IsValid() || !Document->TryGetArrayField(TEXT("pois"), List))
		{
			UE_LOG(LogSwcUiQuests, Warning, TEXT("ui: quests: no place cues (%s; tools/convert-ui.sh quests)"), *Path);
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TSharedPtr<FJsonObject> Info = Value->AsObject();
			FString Local;
			if (!Info->TryGetStringField(TEXT("local"), Local))
			{
				continue;   // a POI of a slice that is not the base POI slice: its local is not known (an open point)
			}
			FPoi Poi;
			Poi.Territory = static_cast<uint16>(Info->GetIntegerField(TEXT("id")));
			const FString Actor = Info->GetStringField(TEXT("actor"));
			for (int32 Index = 0; Index < 16 && Index * 2 + 1 < Actor.Len(); ++Index)
			{
				Poi.Actor[Index] = static_cast<uint8>(FCString::Strtoui64(*Actor.Mid(Index * 2, 2), nullptr, 16));
			}
			Poi.Group = HexU64(Info->GetStringField(TEXT("group")));
			Poi.Local = HexU64(Local);
			const TArray<TSharedPtr<FJsonValue>>& Center = Info->GetArrayField(TEXT("center"));
			Poi.Center = FVector(Center[0]->AsNumber(), Center[1]->AsNumber(), Center[2]->AsNumber());
			Poi.Radius = static_cast<float>(Info->GetNumberField(TEXT("radius")));
			Poi.Reach = Poi.Radius;
			for (const TSharedPtr<FJsonValue>& Point : Info->GetArrayField(TEXT("shape")))
			{
				const TArray<TSharedPtr<FJsonValue>>& XY = Point->AsArray();
				Poi.Shape.Emplace(XY[0]->AsNumber(), XY[1]->AsNumber());
				Poi.Reach = FMath::Max(Poi.Reach, static_cast<float>(FVector2D::Distance(Poi.Shape.Last(), FVector2D(Poi.Center))));
			}
			const TArray<TSharedPtr<FJsonValue>>& Discovered = Info->GetArrayField(TEXT("discovered"));
			const TArray<TSharedPtr<FJsonValue>>& Charted = Info->GetArrayField(TEXT("charted"));
			Poi.DiscoveredCrc = static_cast<uint32>(Discovered[0]->AsNumber());
			Poi.DiscoveredBit = static_cast<int32>(Discovered[1]->AsNumber());
			Poi.ChartedCrc = static_cast<uint32>(Charted[0]->AsNumber());
			Poi.ChartedBit = static_cast<int32>(Charted[1]->AsNumber());
			Pois.Add(MoveTemp(Poi));
		}
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: %d point(s) of interest with place cues (%s)"), Pois.Num(), *Path);
	}
	if (!bEnabled || Now < NextPlaceTest)
	{
		return;
	}
	NextPlaceTest = Now + 0.2;
	const FVector Point(Feet.x, Feet.y, Feet.z);
	const auto Locked = [&Own](int32 Bit) { return Bit < 0 || !swc::net::has_bit(Own.achievements, static_cast<uint32>(Bit)); };
	for (FPoi& Poi : Pois)
	{
		const bool bNear = FVector2D::Distance(FVector2D(Point), FVector2D(Poi.Center)) <= Poi.Reach + 5.0;
		const bool bInSphere = bNear && Poi.Radius > 0.0f && FVector::Distance(Point, Poi.Center) <= Poi.Radius;
		const bool bInShape = bNear && Poi.Shape.Num() >= 3 && FMath::Abs(Point.Z - Poi.Center.Z) <= 180.0 && InPolygon(Poi.Shape, FVector2D(Point));
		for (const auto& [bNow, bWas, Crc, Bit] : {std::make_tuple(bInSphere, &Poi.bInSphere, Poi.DiscoveredCrc, Poi.DiscoveredBit),
				 std::make_tuple(bInShape, &Poi.bInShape, Poi.ChartedCrc, Poi.ChartedBit)})
		{
			if (bNow == *bWas)
			{
				continue;
			}
			*bWas = bNow;
			if (bNow && Locked(Bit))
			{
				Backend.TerritoryReport(Poi.Actor, Poi.Group, Poi.Local, Poi.Territory, Crc);
			}
			else if (!bNow)
			{
				Backend.TerritoryReport(Poi.Actor, Poi.Group, Poi.Local, Poi.Territory, std::nullopt);
			}
		}
	}
}
