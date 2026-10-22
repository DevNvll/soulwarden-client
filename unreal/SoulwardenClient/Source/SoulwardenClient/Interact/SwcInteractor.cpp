#include "Interact/SwcInteractor.h"

#include <algorithm>

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Creatures/SwcCreatureSubsystem.h"
#include "Net/SwcSpace.h"
#include "Objects/SwcObjectSubsystem.h"
#include "Persons/SwcPersonSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SwcBackendSubsystem.h"
#include "SwcDebugLines.h"
#include "Engine/Engine.h"

namespace
{
/** HideMeshDelay of the slice of the personal loot bag (interaction-data.json: lootContainers.personal, 1.5 s at looted). */
constexpr double BagHideDelay = 1.5;
/** GatherResource: the action of GatherOption (interaction-data.json), a timed use of the server (a node, a chest). */
constexpr uint8 GatherAction = 14;
/** OpenConversation: the action of the option of a person (interaction-data.json, option 17: action 63, committed, hold 0.5 s). */
constexpr uint8 ConversationAction = 63;
/** OpenCraftingOption and OpenCraftingOptionLevel60: the action of a station (interaction-data.json: action 20, committed, hold 0.5 s). */
constexpr uint8 StationAction = 20;
/** The prompt distance of a person with no npc object (m): the retail prompts of persons show at 2.07 to 2.98 m (set 5 of the
 *  Talk: the commander, the artificer, Giacomo); a client rule until the trigger area of a person is read. */
constexpr double PersonTalkRange = 3.0;

const TCHAR* GatherStepName(swc::interact::GatherWatch::Step Step)
{
	switch (Step)
	{
	case swc::interact::GatherWatch::Step::running:
		return TEXT("runs");
	case swc::interact::GatherWatch::Step::done:
		return TEXT("is Done");
	case swc::interact::GatherWatch::Step::failed:
		return TEXT("failed (Failure)");
	case swc::interact::GatherWatch::Step::late:
		return TEXT("has no end in its time");
	default:
		return TEXT("waits");
	}
}

bool IsLooted(const swc::net::RepClient::OwnEntity& Own, const swc::net::ActorId& Actor)
{
	for (const swc::net::LootClaim& Claim : Own.lootClaims)
	{
		if (Claim.actor == Actor)
		{
			return Claim.state == swc::net::kLootLooted;
		}
	}
	return false;
}

FString ContentFolder()
{
	const FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	if (!Content.IsEmpty())
	{
		return Content;
	}
	const FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
	return FPaths::Combine(Work.IsEmpty() ? FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden-client")) : Work,
		TEXT("content"));
}

uint64 HexNumber(const FString& Text)
{
	return FCString::Strtoui64(*Text, nullptr, 16);
}

swc::net::ActorId HexBytes(const FString& Text)
{
	swc::net::ActorId Out{};
	for (int32 Index = 0; Index + 1 < Text.Len() && Index / 2 < static_cast<int32>(Out.size()); Index += 2)
	{
		Out[static_cast<size_t>(Index / 2)] = static_cast<uint8>(FCString::Strtoi(*Text.Mid(Index, 2), nullptr, 16));
	}
	return Out;
}
} // namespace

bool FSwcInteractor::Load(FString& OutReport)
{
	const FString Path = FPaths::Combine(ContentFolder(), TEXT("player/interactions.json"));
	FString Text;
	TSharedPtr<FJsonObject> Document;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document)
		|| !Document.IsValid() || !Document->TryGetArrayField(TEXT("templates"), List))
	{
		OutReport = FString::Printf(TEXT("interactions: no %s (tools/convert-player.sh interactions)"), *Path);
		return false;
	}
	Templates.clear();
	int32 Options = 0;
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		const TSharedPtr<FJsonObject> Info = Value->AsObject();
		swc::interact::Template Template;
		Template.name = TCHAR_TO_UTF8(*Info->GetStringField(TEXT("name")));
		Template.asset = HexBytes(Info->GetStringField(TEXT("asset")));
		Template.sub = static_cast<uint32>(Info->GetNumberField(TEXT("sub")));
		Template.root = HexNumber(Info->GetStringField(TEXT("root")));
		for (const TSharedPtr<FJsonValue>& BoundValue : Info->GetArrayField(TEXT("bounds")))
		{
			const TSharedPtr<FJsonObject> BoundInfo = BoundValue->AsObject();
			swc::interact::Bound Bound;
			Bound.slot = static_cast<uint32>(BoundInfo->GetNumberField(TEXT("slot")));
			Bound.component = HexNumber(BoundInfo->GetStringField(TEXT("component")));
			Bound.entity = HexNumber(BoundInfo->GetStringField(TEXT("entity")));
			const TArray<TSharedPtr<FJsonValue>>& Position = BoundInfo->GetArrayField(TEXT("position"));
			for (int32 Axis = 0; Axis < 3 && Axis < Position.Num(); ++Axis)
			{
				Bound.position[Axis] = static_cast<float>(Position[Axis]->AsNumber());
			}
			Bound.rangeSq = static_cast<float>(BoundInfo->GetNumberField(TEXT("rangeSq")));
			USwcObjectSubsystem::ReadShape(BoundInfo, Bound.shape);
			for (const TSharedPtr<FJsonValue>& OptionValue : BoundInfo->GetArrayField(TEXT("options")))
			{
				const TSharedPtr<FJsonObject> OptionInfo = OptionValue->AsObject();
				swc::interact::Option Option;
				Option.name = TCHAR_TO_UTF8(*OptionInfo->GetStringField(TEXT("name")));
				Option.id = static_cast<uint32>(OptionInfo->GetNumberField(TEXT("id")));
				Option.action = static_cast<uint8>(OptionInfo->GetNumberField(TEXT("action")));
				Option.committed = OptionInfo->GetBoolField(TEXT("committed"));
				Option.delay = static_cast<float>(OptionInfo->GetNumberField(TEXT("delay")));
				Option.hold = static_cast<float>(OptionInfo->GetNumberField(TEXT("hold")));
				Option.entity = HexNumber(OptionInfo->GetStringField(TEXT("entity")));
				Bound.options.push_back(Option);
				++Options;
			}
			Template.bounds.push_back(Bound);
		}
		// A crafting station (stage MVP-J): its Assembly and its name key.
		const TSharedPtr<FJsonObject>* Assembly = nullptr;
		if (Info->TryGetObjectField(TEXT("assembly"), Assembly))
		{
			Template.station = TCHAR_TO_UTF8(*(*Assembly)->GetStringField(TEXT("key")));
			Template.stationFee = (*Assembly)->GetBoolField(TEXT("fee"));
			Template.interactName = TCHAR_TO_UTF8(*Info->GetStringField(TEXT("interactName")));
		}
		// The personal loot bag: its focus comes from the bag entities of the creature view, not from the objects of the snapshot.
		if (Template.name.rfind("loot:", 0) == 0)
		{
			LootTemplate = Template;
		}
		else
		{
			Templates.push_back(Template);
		}
	}
	OutReport = FString::Printf(TEXT("interactions: %d templates of server objects and %s, %d options (%s)"), static_cast<int32>(Templates.size()),
		LootTemplate ? TEXT("the personal loot bag") : TEXT("no loot bag"), Options, *Path);
	return !Templates.empty() || LootTemplate.has_value();
}

namespace
{
/** The shape of a bound for the focus line (AST-198). */
FString ShapeText(const swc::interact::Shape& Shape)
{
	using EType = swc::interact::Shape::Type;
	switch (Shape.type)
	{
	case EType::sphere:
		return FString::Printf(TEXT("sphere %.2f m"), Shape.radius);
	case EType::box:
		return FString::Printf(TEXT("box %.2f x %.2f x %.2f m"), Shape.dimensions[0], Shape.dimensions[1], Shape.dimensions[2]);
	case EType::capsule:
		return FString::Printf(TEXT("capsule %.2f m, height %.2f m"), Shape.radius, Shape.height);
	case EType::cylinder:
		return FString::Printf(TEXT("cylinder %.2f m, height %.2f m"), Shape.radius, Shape.height);
	default:
		return TEXT("no shape: the range");
	}
}
} // namespace

void FSwcInteractor::Update(const FSwcRepConnection::FSnapshot& Snapshot, FSwcRepConnection& Connection, const swc::net::WorldPosition& Feet,
	bool bLocked, double Now, USwcCreatureSubsystem* Creatures)
{
	const swc::net::RepClient::OwnEntity& Own = Snapshot.Own;
	if (!bSeeded)
	{
		bSeeded = true;
		SeenCancels = Own.interactionCancels;
		SeenResults = Own.interactionResults;
	}
	// The answers 6709: the first one after the 6705 frees the input (the lock of a conversation).
	const swc::interact::Conversation::Phase TalkBefore = Talk.phase();
	Talk.update(Own.lockEnds);
	if (TalkBefore == swc::interact::Conversation::Phase::closing && Talk.phase() == swc::interact::Conversation::Phase::closed)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: the answer 6709: the input of the character is free again"));
	}
	// The replies and the ends of the session first: they belong to the request that is out now.
	if (Own.interactionCancels != SeenCancels)
	{
		SeenCancels = Own.interactionCancels;
		if (Use.phase() != swc::interact::Use::Phase::idle)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the server ended the use of %s (3746)"), UTF8_TO_TCHAR(Use.option().name.c_str()));
			if (bUseTalk && Talk.scene())
			{
				UE_LOG(LogSwcBackend, Log, TEXT("interact: the conversation closes (the session client sent 2473; the client sends 6705)"));
				Talk.server_ended();
			}
		}
		Use.server_cancel();
	}
	if (Own.interactionResults != SeenResults)
	{
		SeenResults = Own.interactionResults;
		UE_LOG(LogSwcBackend, Log, TEXT("interact: the reply to %s: %s"), UTF8_TO_TCHAR(Use.option().name.c_str()),
			Own.lastInteractionResult == 1 ? TEXT("accepted") : TEXT("refused"));
		const bool bTalkReply = bUseTalk && Use.phase() != swc::interact::Use::Phase::idle;
		if (bUseStation && Use.phase() != swc::interact::Use::Phase::idle && Own.lastInteractionResult != 1 && bStationOpen)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the station use is refused: the crafting screen closes"));
			bStationOpen = false;
		}
		Use.reply(Own.lastInteractionResult, Now);
		if (bTalkReply && Own.lastInteractionResult != 1 && Talk.scene())
		{
			// A refusal clears the records of the client: the conversation closes at once (6705, no 2473; Soulwarden interactions.md).
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the conversation is refused: it closes (6705, no 2473)"));
			Talk.refused();
		}
	}
	// The focus: the objects of the server with a place.
	std::vector<swc::interact::Object> Objects;
	for (const TPair<uint32, swc::net::RepClient::OtherEntity>& Pair : Snapshot.Objects)
	{
		swc::interact::Object Object;
		Object.interest = Pair.Key;
		Object.actor = Pair.Value.actor;
		Object.sliceAsset = Pair.Value.sliceAsset;
		Object.sliceSub = Pair.Value.sliceSubId;
		Object.place = Pair.Value.place;
		Object.rotation = Pair.Value.placeRotation;
		Objects.push_back(Object);
	}
	// The use of a bag ends at its Looted claim, or when its record stops (the client sends 2473: live check L6 of the backend).
	if (bUseBag && Use.phase() != swc::interact::Use::Phase::idle)
	{
		const bool bGone = !Snapshot.Objects.ContainsByPredicate(
			[this](const TPair<uint32, swc::net::RepClient::OtherEntity>& Pair) { return Pair.Value.actor == UseBag; });
		if (IsLooted(Own, UseBag) || bGone)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the bag is %s: the client ends the use (2473)"), bGone ? TEXT("gone") : TEXT("Looted"));
			Use.finish();
		}
	}
	// The cancel of a gather by the player: a move key that goes down during the work (live check L4 of the backend stage 3.6a).
	const bool bMoveKeyDown = bMoveKeys && !bMoveKeysWas;
	bMoveKeysWas = bMoveKeys;
	if (bMoveKeyDown && bUseGather && (Use.phase() == swc::interact::Use::Phase::requested || Use.phase() == swc::interact::Use::Phase::executed))
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: a move key in the work: the client ends the gather (2473)"));
		Use.finish();
	}
	// A gather or an open (GatherOption): the request stays until the Gathering state ends the work (Done, Failure) or its time passed.
	if (bUseGather && Use.phase() != swc::interact::Use::Phase::idle)
	{
		const swc::interact::GatherWatch::Step Before = Gather.step();
		const swc::interact::GatherWatch::Step Step = Gather.update(Now, Own.gathering);
		if (Step != Before)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the work %s (remaining %.2f s, speed %.2f, G %016llx)%s"), GatherStepName(Step), Own.gathering.remaining,
				Own.gathering.speed, Own.gathering.target, Step == swc::interact::GatherWatch::Step::running ? TEXT("") : TEXT(": the client ends the use (2473)"));
		}
		if (Step == swc::interact::GatherWatch::Step::done || Step == swc::interact::GatherWatch::Step::failed || Step == swc::interact::GatherWatch::Step::late)
		{
			Use.finish();
		}
	}
	// The log of the loot: the items of the bag of the character and the claims of the LootTracker at each change.
	if (Own.bagRevision != SeenBag)
	{
		SeenBag = Own.bagRevision;
		int32 Items = 0;
		uint32 Quantity = 0;
		for (const soulwarden::backend::tests::WireItem& Item : Own.bag)
		{
			Items += Item.empty() ? 0 : 1;
			Quantity += Item.empty() ? 0 : Item.quantity;
		}
		UE_LOG(LogSwcBackend, Log, TEXT("loot: the bag of the character: %d of %d slot(s) with an item, quantity %u (revision %u)"), Items, static_cast<int32>(Own.bag.size()),
			Quantity, Own.bagRevision);
	}
	if (Own.lootRevision != SeenLoot)
	{
		SeenLoot = Own.lootRevision;
		FString Claims;
		for (const swc::net::LootClaim& Claim : Own.lootClaims)
		{
			Claims += FString::Printf(TEXT(" %s:%s"), UTF8_TO_TCHAR(swc::net::hex_of(std::span(Claim.actor).first(8)).c_str()),
				Claim.state == swc::net::kLootRolled ? TEXT("Rolled") : Claim.state == swc::net::kLootLooted ? TEXT("Looted") : *FString::FromInt(Claim.state));
		}
		UE_LOG(LogSwcBackend, Log, TEXT("loot: the LootTracker: %d claim(s)%s"), static_cast<int32>(Own.lootClaims.size()), *Claims);
	}
	UpdateLootedBags(Own, Creatures, Now);
	// The range of the server is from the position field of the report that has the request (docs/INTERACTIONS.md "The range"):
	// the next field, from the last one and the feet, as the server reads it. The focus, the prompt, and the hold use it.
	const swc::net::WorldPosition Reported = swc::net::position_field(swc::net::RepClient::next_reported(Snapshot.Reported, Feet));
	// The camera of the focus score (AST-198; retail 0x490FED0): its place and its forward vector in engine m. With no camera: a
	// camera 1.6 m above the character, to the north.
	float Camera[3]{Reported.x, Reported.y, Reported.z + 1.6f};
	float Forward[3]{0.0f, 1.0f, 0.0f};
	const UWorld* World = Creatures != nullptr ? Creatures->GetWorld() : nullptr;
	if (const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr; Controller != nullptr && Controller->PlayerCameraManager != nullptr)
	{
		const FVector Location = Controller->PlayerCameraManager->GetCameraLocation();
		const swc::net::WorldPosition At = SwcUnrealToEngine(Location);
		const swc::net::WorldPosition Ahead = SwcUnrealToEngine(Location + Controller->PlayerCameraManager->GetCameraRotation().Vector() * 100.0);
		const FVector3f Direction = FVector3f(Ahead.x - At.x, Ahead.y - At.y, Ahead.z - At.z).GetSafeNormal();
		Camera[0] = At.x;
		Camera[1] = At.y;
		Camera[2] = At.z;
		Forward[0] = Direction.X;
		Forward[1] = Direction.Y;
		Forward[2] = Direction.Z;
	}
	if (Use.phase() == swc::interact::Use::Phase::idle)
	{
		bUseBag = false;
		bUseGather = false;
		bUseTalk = false;
		if (bStationOpen)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the station use ended: the crafting screen closes"));
			bStationOpen = false;
		}
		bUseStation = false;
		// The candidates of the three sources (the fixtures of the server, the bags, the objects of the level data) have the point of the
		// character in their shape (AST-198; research prompt-range.md); the focus is the smallest camera line score of all of them.
		Focus = bLocked ? std::nullopt : swc::interact::focus(Templates, Objects, Reported, Camera, Forward);
		FocusName = Focus ? FString(UTF8_TO_TCHAR(Focus->object->name.c_str())) : FString();
		FocusShape = Focus ? TEXT("a fixture of the server") : FString();
		bFocusBag = false;
		FocusPrompt = FSwcInteractPrompt();
		// A crafting station of a settlement (stage MVP-J): a server child with an Assembly; its option OpenCraftingOption keeps its
		// request while the crafting screen shows.
		bFocusStation = Focus && !Focus->object->station.empty() && Focus->option->action == StationAction;
		if (Focus)
		{
			FocusPrompt.Kind = FSwcInteractPrompt::EKind::Fixture;
			FocusPrompt.Interest = Focus->interest;
			FocusPrompt.Name = Focus->object->interactName.empty() ? FocusName : FString(UTF8_TO_TCHAR(Focus->object->interactName.c_str()));
			FocusPrompt.Options.Emplace(UTF8_TO_TCHAR(Focus->option->name.c_str()), Focus->option->hold);
			FocusPrompt.Place = SwcEngineToUnreal(Focus->place);
		}
		// The bag with an enabled bound and no Looted claim whose box has the point of the character (the Interaction entity of the bag:
		// 2 x 2 x 2 m, axis-aligned: the view has no yaw of the bag), with the smallest camera line score; its range (5 m) is the check of
		// the use. Its first option (TakeAllOption).
		FSwcLootBag Bag;
		float BagScore = 0.0f;
		bool bBag = false;
		const FVector From(SwcEngineToUnreal(Reported));
		if (!bLocked && LootTemplate && !LootTemplate->bounds.empty() && Creatures != nullptr)
		{
			TArray<FSwcLootBag> Bags;
			Creatures->GetLootBags(From, Bags);
			for (const FSwcLootBag& Each : Bags)
			{
				const float Place[3]{Each.EnginePlace.x, Each.EnginePlace.y, Each.EnginePlace.z};
				float Score = 0.0f;
				if (Each.bEnabled && Each.InteractSlot != 0 && !Each.Options.IsEmpty() && !IsLooted(Own, Each.Actor)
					&& swc::interact::contains(LootTemplate->bounds.front().shape, Each.EnginePlace, swc::net::Quaternion{0.0F, 0.0F, 0.0F, 1.0F}, Reported, Each.RangeSq)
					&& swc::interact::line_score(Camera, Forward, Place, Score) && (!bBag || Score < BagScore))
				{
					Bag = Each;
					BagScore = Score;
					bBag = true;
				}
			}
		}
		if (bBag && (!Focus || BagScore < Focus->score))
		{
			// The options of the bag of the creature view are in the order of the bound of the data (the key of a pair is the index of
			// the option in the options of interaction-data.json, not its CRC-32 id): the first one is TakeAllOption.
			const swc::interact::Bound& Bound = LootTemplate->bounds.front();
			const auto Option = Bound.options.begin();
			if (Option != Bound.options.end())
			{
				swc::interact::Candidate Candidate;
				Candidate.interest = Bag.Interest;
				Candidate.object = &*LootTemplate;
				Candidate.option = &*Option;
				Candidate.request = swc::interact::request_of(*LootTemplate, Bound, *Option, Bag.Actor);
				// The ids of the creature view (the backend rule of a server child): the target and the instance of the option entity.
				Candidate.request.reference.group = Bag.Group;
				Candidate.request.reference.target = Bag.Target;
				Candidate.request.optionEntity = Bag.Options[0].Value;
				Candidate.place = Bag.EnginePlace;
				Candidate.distanceSq = swc::interact::distance_sq(Reported, Bag.EnginePlace);
				Candidate.rangeSq = Bag.RangeSq;
				Candidate.score = BagScore;
				Focus = Candidate;
				bFocusStation = false;
				FocusName = TEXT("loot bag");
				FocusShape = ShapeText(LootTemplate->bounds.front().shape);
				bFocusBag = true;
				FocusPrompt = FSwcInteractPrompt();
				FocusPrompt.Kind = FSwcInteractPrompt::EKind::Bag;
				FocusPrompt.Interest = Bag.Interest;
				for (const swc::interact::Option& Each : Bound.options)
				{
					FocusPrompt.Options.Emplace(UTF8_TO_TCHAR(Each.name.c_str()), Each.hold);
				}
				FocusPrompt.Place = SwcEngineToUnreal(Bag.EnginePlace);
			}
		}
		// The objects of the level data (stage P4.1 part 2): the nearest bound in range of the object layer when it is nearer. Its first
		// option: ReadLore of a lore page, GatherOption of a node or a chest. The ids and the request come from the layer.
		bFocusGather = false;
		bFocusNoTool = false;
		bFocusTalk = false;
		USwcObjectSubsystem* Layer = Creatures != nullptr && Creatures->GetWorld() != nullptr ? Creatures->GetWorld()->GetSubsystem<USwcObjectSubsystem>() : nullptr;
		FSwcWorldObjectTarget Object;
		float ObjectScore = 0.0f;
		// The options of the card (stage MVP-F, docs/INTERACTIONS.md "The options of a card"; the native rules of AST-231, research
		// prompt-options.md): each option of the bound with a true authored show flag; on an entity with the NPC component only the
		// action 63 (OpenConversation: the predicate of the NPC client facet). The first one has the key E, the second one the key T
		// (ui_interact_sec): the two key hints of an option row of the original card (Icon_E, Icon_N).
		TArray<int32, TInlineAllocator<4>> Shown;
		const auto FillShown = [&Object, &Shown]()
		{
			constexpr uint32 OpenConversationAction = 63;
			Shown.Reset();
			for (int32 Index = 0; Index < Object.Options.Num(); ++Index)
			{
				const FSwcWorldObjectOption& Each = Object.Options[Index];
				if (Each.bShow && (!Object.bNpc || Each.Option.action == OpenConversationAction))
				{
					Shown.Add(Index);
				}
			}
			return !Shown.IsEmpty();
		};
		bFocusHasSecond = false;
		// The npc object of a person that is not present for the character (its presence rule: persons.json "presenceRule", the backend
		// rule) has no body and no focus: two persons of one spawner place (Aelstrom WW_01 and the season person NPC_03976 on the
		// beach; AST-248). The retail client has no entity for it.
		const USwcPersonSubsystem* Present = Creatures != nullptr && Creatures->GetWorld() != nullptr ? Creatures->GetWorld()->GetSubsystem<USwcPersonSubsystem>() : nullptr;
		const auto Hidden = [Present, this](const FSwcWorldObjectTarget& Target)
		{
			// A quest object with InteractOnlyOnQuest and no Active task with its tag (AST-241).
			if (Target.bQuestOnly && !QuestTags.Contains(Target.QuestTag))
			{
				return true;
			}
			const FSwcPerson* Person = Target.bNpc && Present != nullptr ? Present->FindByInteraction(Target.Key) : nullptr;
			return Person != nullptr && !Person->bVisible;
		};
		if (!bLocked && Layer != nullptr && Layer->FindFocusObject(From, Camera, Forward, Object, ObjectScore, Hidden) && FillShown()
			&& (!Focus || ObjectScore < Focus->score))
		{
			ObjectOption = Object.Options[Shown[0]].Option;
			if (Shown.Num() > 1)
			{
				SecondObjectOption = Object.Options[Shown[1]].Option;
				SecondRequest = Object.Options[Shown[1]].Request;
				bFocusHasSecond = true;
			}
			swc::interact::Candidate Candidate;
			Candidate.option = &ObjectOption;
			Candidate.request = Object.Options[Shown[0]].Request;
			Candidate.place = Object.EnginePlace;
			Candidate.distanceSq = swc::interact::distance_sq(Reported, Object.EnginePlace);
			Candidate.rangeSq = Object.RangeSq;
			Candidate.score = ObjectScore;
			Focus = Candidate;
			bFocusStation = false;
			// The prompt of a gather has the name of the row and, with no tool, the refusal ("YOUNG TREE  No equipped Axe").
			FocusName = Object.Name.IsEmpty() ? FString::Printf(TEXT("%s %s"), *Object.Kind.ToString(), *Object.Key)
				: Object.bToolMissing || Object.bRankMissing ? FString::Printf(TEXT("%s  %s"), *Object.Name.ToUpper(), *Object.RefusalText) : Object.Name.ToUpper();
			bFocusBag = false;
			FocusShape = ShapeText(Object.Shape);
			bFocusGather = ObjectOption.action == GatherAction;
			// No tool or a rank under the gate of the row: E sends nothing (the server would refuse; the card says why).
			bFocusNoTool = Object.bToolMissing || Object.bRankMissing;
			bFocusTalk = ObjectOption.action == ConversationAction;
			FocusKey = Object.Key;
			FocusUseKind = Object.UseKind;
			FocusTool = Object.ToolItem != 0 ? Object.ToolSlot : 0;
			FocusPrompt = FSwcInteractPrompt();
			FocusPrompt.Kind = FSwcInteractPrompt::EKind::Object;
			FocusPrompt.ObjectKey = Object.Key;
			FocusPrompt.ObjectKind = Object.Kind;
			FocusPrompt.Name = Object.Name;
			// A person (AST-231; unifiedinteractcard.luac OnInteractFocus): the card has its name (interactName) and its title (the
			// npc data of the root entity), as the retail card of an inn keeper ("OSEYE KAMAU", "Innkeeper of The Autumn Rest").
			const USwcPersonSubsystem* Persons = Object.bNpc ? Creatures->GetWorld()->GetSubsystem<USwcPersonSubsystem>() : nullptr;
			if (const FSwcPerson* Person = Persons != nullptr ? Persons->FindByInteraction(Object.Key) : nullptr)
			{
				FocusPrompt.Name = Person->Name;
				FocusPrompt.Title = Person->Title;
			}
			for (const int32 Index : Shown)
			{
				FocusPrompt.Options.Emplace(UTF8_TO_TCHAR(Object.Options[Index].Option.name.c_str()), Object.Options[Index].Option.hold);
			}
			FocusPrompt.bToolMissing = Object.bToolMissing;
			FocusPrompt.GatheringType = Object.GatheringType;
			// The text key of the refusal: the rule of the object layer (ui_notool + the gathering type).
			FocusPrompt.RefusalKey = Object.bToolMissing ? TEXT("ui_notool") + Object.GatheringType.ToLower()
				: Object.bRankMissing ? TEXT("ui_lowskill") + Object.GatheringType.ToLower() : FString();
			FocusPrompt.bRankMissing = Object.bRankMissing;
			FocusPrompt.RequiredRank = Object.RequiredRank;
			FocusPrompt.Place = SwcEngineToUnreal(Object.EnginePlace);
		}
		// The persons with the option OpenConversation and no npc object of the interaction data (28 persons: the capital children and
		// the persons with no binding of the backend; docs/PERSONS.md "The Talk of a person with no data"; the person children of client
		// spawners have npc objects with the client id of tag 7 since AST-248): the client shows the prompt and sends the request with
		// ids that no object of the data has (G from the key of the person; the actor is G in the other byte order, then 8 zero bytes;
		// the option entity is G with 2 added to its high word), and the backend refuses it (3743 result 2).
		USwcPersonSubsystem* PersonLayer = Creatures != nullptr && Creatures->GetWorld() != nullptr ? Creatures->GetWorld()->GetSubsystem<USwcPersonSubsystem>() : nullptr;
		const swc::interact::Option* TalkOption = Layer != nullptr ? Layer->FindOptionByAction(ConversationAction) : nullptr;
		const FSwcPerson* TalkPerson = !bLocked && !Focus && PersonLayer != nullptr && TalkOption != nullptr
			? PersonLayer->FindTalkWithNoData(SwcEngineToUnreal(Reported), PersonTalkRange * 100.0) : nullptr;
		if (TalkPerson != nullptr)
		{
			ObjectOption = *TalkOption;
			const uint64 Group = (static_cast<uint64>(FCrc::StrCrc32(*(TalkPerson->Key + TEXT("|high")))) << 32) | FCrc::StrCrc32(*TalkPerson->Key);
			swc::interact::Candidate Candidate;
			Candidate.option = &ObjectOption;
			for (int32 Index = 0; Index < 8; ++Index)
			{
				Candidate.request.reference.actor[Index] = static_cast<uint8>(Group >> (8 * Index));
			}
			Candidate.request.reference.group = Group;
			Candidate.request.reference.target = FCrc::StrCrc32(*(TalkPerson->Key + TEXT("|target")));
			Candidate.request.action = ObjectOption.action;
			Candidate.request.baseAction = ObjectOption.committed ? ObjectOption.action : 0;
			Candidate.request.optionEntity = Group + (static_cast<uint64>(2) << 32);
			Candidate.request.committed = ObjectOption.committed;
			Candidate.request.holdTime = ObjectOption.delay;
			Candidate.request.optionId = ObjectOption.id;
			Candidate.place = SwcUnrealToEngine(TalkPerson->Place);
			Candidate.distanceSq = swc::interact::distance_sq(Reported, Candidate.place);
			Candidate.rangeSq = PersonTalkRange * PersonTalkRange;
			Focus = Candidate;
			bFocusStation = false;
			FocusName = TalkPerson->Name.ToUpper();
			bFocusBag = false;
			FocusShape = TEXT("person");
			bFocusTalk = true;
			FocusKey = FString(PersonTalkPrefix) + TalkPerson->Key;
			FocusUseKind.Reset();
			FocusTool = 0;
			FocusPrompt = FSwcInteractPrompt();
			FocusPrompt.Kind = FSwcInteractPrompt::EKind::Object;
			FocusPrompt.ObjectKey = FocusKey;
			FocusPrompt.ObjectKind = FName(TEXT("npc"));
			FocusPrompt.Name = TalkPerson->Name;
			FocusPrompt.Options.Emplace(UTF8_TO_TCHAR(ObjectOption.name.c_str()), ObjectOption.hold);
			FocusPrompt.Place = TalkPerson->Place;
		}
	}
	// AST-198: the focus for the checks: a log line at each change (the distance from the point of the character to the centre of the
	// bound, its horizontal part, and the shape), and the same line on the screen with -SwcDebugLines.
	if (Use.phase() == swc::interact::Use::Phase::idle)
	{
		FString Line = TEXT("none");
		if (Focus)
		{
			const float Dx = Reported.x - Focus->place.x;
			const float Dy = Reported.y - Focus->place.y;
			Line = FString::Printf(TEXT("%s, %.2f m from the bound centre, horizontal %.2f m (%s)"), *FocusName, FMath::Sqrt(Focus->distanceSq),
				FMath::Sqrt(Dx * Dx + Dy * Dy), *FocusShape);
		}
		const FString Key = Focus ? FString::Printf(TEXT("%s %.1f %.1f"), *FocusName, Focus->place.x, Focus->place.y) : FString();
		if (Key != LastFocusLine)
		{
			LastFocusLine = Key;
			UE_LOG(LogSwcBackend, Log, TEXT("interact: focus %s"), *Line);
		}
		if (GEngine != nullptr && SwcDebugLines())
		{
			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0x53574650), 1.0f, FColor::Orange, TEXT("focus: ") + Line);
		}
	}
	// The bag panel of the UI (stage P2.4): the open with OpenStorageOption, and the close (2473).
	if (Use.phase() == swc::interact::Use::Phase::idle)
	{
		OpenBagInterest = 0;
	}
	if (bPendingClose)
	{
		bPendingClose = false;
		if (OpenBagInterest != 0 && Use.phase() != swc::interact::Use::Phase::idle)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the panel of the bag %u closes: the client ends the use (2473)"), OpenBagInterest);
			Use.finish();
		}
	}
	if (PendingOpen != 0)
	{
		const uint32 Interest = PendingOpen;
		PendingOpen = 0;
		TArray<FSwcLootBag> Bags;
		if (Creatures != nullptr)
		{
			Creatures->GetLootBags(FVector(SwcEngineToUnreal(Reported)), Bags);
		}
		const FSwcLootBag* Bag = Bags.FindByPredicate([Interest](const FSwcLootBag& Item) { return Item.Interest == Interest; });
		// The range rule of the server (the position field of the report: "The range" of docs/INTERACTIONS.md).
		const bool bReady = Bag != nullptr && Use.phase() == swc::interact::Use::Phase::idle && !bLocked && LootTemplate
			&& !LootTemplate->bounds.empty() && LootTemplate->bounds.front().options.size() > 1 && Bag->bEnabled && !IsLooted(Own, Bag->Actor)
			&& swc::interact::in_range(Reported, Bag->EnginePlace, Bag->RangeSq) && !Bag->Options.IsEmpty();
		if (!bReady)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the panel of the bag %u: no open (a use runs, or the bag is not in its range)"), Interest);
		}
		else
		{
			const swc::interact::Bound& Bound = LootTemplate->bounds.front();
			OpenOption = Bound.options[1];
			OpenOption.hold = 0.0F;
			swc::interact::Candidate Candidate;
			Candidate.interest = Bag->Interest;
			Candidate.object = &*LootTemplate;
			Candidate.option = &OpenOption;
			Candidate.request = swc::interact::request_of(*LootTemplate, Bound, OpenOption, Bag->Actor);
			Candidate.request.reference.group = Bag->Group;
			Candidate.request.reference.target = Bag->Target;
			Candidate.request.optionEntity = Bag->Options[Bag->Options.Num() > 1 ? 1 : 0].Value;
			Candidate.place = Bag->EnginePlace;
			Candidate.distanceSq = swc::interact::distance_sq(Reported, Bag->EnginePlace);
			Candidate.rangeSq = Bag->RangeSq;
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the panel of the bag %u (%.2f m of %.2f m): %s, action %u"), Interest,
				FMath::Sqrt(Candidate.distanceSq), FMath::Sqrt(Bag->RangeSq),
				UTF8_TO_TCHAR(OpenOption.name.c_str()), OpenOption.action);
			Use.press(Now, Candidate, true);
			UsePlace = SwcEngineToUnreal(Bag->EnginePlace);
			bUseBag = true;
			UseBag = Bag->Actor;
			OpenBagInterest = Interest;
		}
	}
	// The key T (ui_interact_sec) at an object with a second option on its card: the press uses the second option (the key T then
	// works as the key E for the hold, the request, and the release). A bag keeps its own rule (the panel: swc/interact.lua).
	const bool bSecondDown = bSecondKey && !bSecondKeyWas;
	bSecondKeyWas = bSecondKey;
	if (bSecondDown && !bKeyDown && !bLocked && Focus && bFocusHasSecond && Use.phase() == swc::interact::Use::Phase::idle)
	{
		ObjectOption = SecondObjectOption;
		Focus->option = &ObjectOption;
		Focus->request = SecondRequest;
		bFocusGather = ObjectOption.action == GatherAction;
		bFocusTalk = ObjectOption.action == ConversationAction;
		bChooseSecond = true;
		UE_LOG(LogSwcBackend, Log, TEXT("interact: T at %s: the second option %s"), *FocusName, UTF8_TO_TCHAR(ObjectOption.name.c_str()));
	}
	if (bChooseSecond)
	{
		bKeyDown = bKeyDown || bSecondKey;
		if (!bSecondKey && Use.phase() == swc::interact::Use::Phase::idle)
		{
			bChooseSecond = false;
		}
	}
	if (bLocked)
	{
		if (Talk.scene())
		{
			// The death or a loading screen ends the conversation: 6705 (the abort of the use sends 2473).
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the conversation closes (the death or a loading screen)"));
			Talk.server_ended();
		}
		Use.abort();
	}
	// The crafting screen ends the station use (its Escape, its exit button): the client sends 2473 (protocol notes 4.36: "Escape: the
	// client sends 2473 with the reference").
	if (bPendingStationEnd)
	{
		bPendingStationEnd = false;
		if (bUseStation && Use.phase() != swc::interact::Use::Phase::idle)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the crafting screen ends the station use (2473)"));
			Use.finish();
		}
	}
	// The key E. In a conversation with no conversation screen it ends the conversation (with the screen, E is its Next key).
	// A gather with no tool for its row sends nothing (protocol notes 4.44, live check L6: no request in the log).
	if ((bKeyDown && !bKeyWas && Talk.scene() && !bConversationScreen) || (bPendingTalkEnd && Talk.scene()))
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: %s in the conversation: the player ends it (6705, then 2473)"), bPendingTalkEnd ? TEXT("the UI") : TEXT("E"));
		Talk.player_close();
		Use.finish();
		TopicsAfterCancel = MoveTemp(PendingTopics);
	}
	else if (bKeyDown && !bKeyWas && bStationOpen)
	{
		// The crafting screen has the input (its own key actions).
	}
	else if (bKeyDown && !bKeyWas && Talk.locked())
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: E while the input waits for the answer 6709: no use"));
	}
	else if (bKeyDown && !bKeyWas && !Focus)
	{
		// A press with no focus: the state of the search (a check of a run: why no prompt).
		USwcObjectSubsystem* Probe = Creatures != nullptr && Creatures->GetWorld() != nullptr ? Creatures->GetWorld()->GetSubsystem<USwcObjectSubsystem>() : nullptr;
		FSwcWorldObjectTarget Nearest;
		const bool bNearest = Probe != nullptr && Probe->FindNearestObject(SwcEngineToUnreal(Feet), Nearest);
		UE_LOG(LogSwcBackend, Log, TEXT("interact: E with no focus (locked %d, use phase %d, feet %.1f %.1f %.1f; the nearest object %s)"), bLocked ? 1 : 0,
			static_cast<int32>(Use.phase()), Feet.x, Feet.y, Feet.z,
			bNearest ? *FString::Printf(TEXT("%s at %.1f m"), *Nearest.Name, Nearest.Distance) : TEXT("none"));
	}
	else if (bKeyDown && !bKeyWas && Focus && !bLocked && bFocusNoTool)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: E at %s: no request (the tool or rank rule of the client)"), *FocusName);
	}
	else if (bKeyDown && !bKeyWas && Focus && !bLocked)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: E at %s (interest %u, %.2f m of %.2f m): %s, action %u, delay %.1f s, hold %.1f s"), *FocusName,
			Focus->interest, FMath::Sqrt(Focus->distanceSq), FMath::Sqrt(Focus->rangeSq), UTF8_TO_TCHAR(Focus->option->name.c_str()), Focus->option->action,
			Focus->option->delay, Focus->option->hold);
		// A bag, a gather, and a conversation keep their request until their source ends them (the Looted claim; the Gathering
		// state; the end of the conversation).
		Use.press(Now, *Focus, bFocusBag || bFocusGather || bFocusTalk || bFocusStation);
		UsePlace = SwcEngineToUnreal(Focus->place);
		if (bFocusBag)
		{
			bUseBag = true;
			UseBag = Focus->request.reference.actor;
		}
		if (bFocusGather)
		{
			bUseGather = true;
			UseKind = FocusUseKind;
			UseTool = FocusTool;
			Gather.start(Now, Snapshot.Own.gathering);
		}
		if (bFocusTalk)
		{
			bUseTalk = true;
			TalkKey = FocusKey;
		}
		if (bFocusStation)
		{
			bUseStation = true;
			StationKey = UTF8_TO_TCHAR(Focus->object->station.c_str());
			bStationFee = Focus->object->stationFee;
			StationName = UTF8_TO_TCHAR(Focus->object->interactName.c_str());
		}
	}
	else if (!bKeyDown && bKeyWas)
	{
		Use.release(Now);
	}
	bKeyWas = bKeyDown;
	bPendingTalkEnd = false;
	// A hold whose character went out of the range ends with no request: the server would refuse it (it measures at the request).
	if (Use.moved(Reported))
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: the hold of %s ends with no request: the character is %.2f m from the bound (range %.2f m)"),
			UTF8_TO_TCHAR(Use.option().name.c_str()), FMath::Sqrt(swc::interact::distance_sq(Reported, Use.place())), FMath::Sqrt(Use.rangeSq()));
		bUseBag = false;
		bUseGather = false;
		bUseTalk = false;
		bUseStation = false;
	}
	const uint32 Executions = Use.executions();
	Use.tick(Now);
	if (Use.executions() != Executions)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: the execution of %s"), UTF8_TO_TCHAR(Use.option().name.c_str()));
		if (bUseTalk)
		{
			// The client opens the conversation at its request, before any reply (protocol notes 4.35).
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the conversation opens (object %s): the input of the character is blocked"), *TalkKey);
			Talk.open();
		}
		if (bUseStation)
		{
			// The client opens the crafting screen at its request (the station facts of the client: 0x2E6A440, 0x2E61E00; protocol notes
			// 4.36); a refusal (3743 result 2) and the end of the use (3746, 2473, the death, a loading screen) close it.
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the crafting screen opens: %s (%s, fee %d)"), *StationKey, *StationName, bStationFee ? 1 : 0);
			bStationOpen = true;
		}
	}
	// The end of a conversation first: 6705, then 2473 (the order of the retail client, 2 ms between them).
	if (Talk.take().endCutscene)
	{
		Connection.EndCutscene();
	}
	const swc::interact::Use::Effects Effects = Use.take();
	if (Effects.requestChanged)
	{
		Connection.SetInteraction(Effects.request);
		if (!Effects.request)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the request ends with no message (the use completed)"));
		}
	}
	if (Effects.cancel)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: the client cancels the use of %s (2473)"), UTF8_TO_TCHAR(Use.option().name.c_str()));
		Connection.CancelInteraction();
		if (!TopicsAfterCancel.empty())
		{
			// The finished topics of the conversation after its 2473 (AST-248; backend live check L3: 6705, 2473, then 5397).
			Connection.FinishedTopics(std::vector<std::uint32_t>(TopicsAfterCancel.begin(), TopicsAfterCancel.end()));
			TopicsAfterCancel.clear();
		}
	}
}

void FSwcInteractor::UpdateLootedBags(const swc::net::RepClient::OwnEntity& Own, USwcCreatureSubsystem* Creatures, double Now)
{
	if (Creatures == nullptr)
	{
		return;
	}
	TArray<FSwcLootBag> Bags;
	Creatures->GetLootBags(FVector::ZeroVector, Bags);
	for (const FSwcLootBag& Bag : Bags)
	{
		if (!IsLooted(Own, Bag.Actor))
		{
			continue;
		}
		double& Since = LootedSince.FindOrAdd(Bag.Interest, Now);
		if (Since == Now)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the bag %u is Looted: no prompt; the model hides in %.1f s"), Bag.Interest, BagHideDelay);
		}
		if (Now - Since >= BagHideDelay && Since >= 0.0)
		{
			Creatures->SetLootBagHidden(Bag.Interest, true);
			Since = -1.0;   // hidden
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the model of the bag %u hides (HideMeshDelay)"), Bag.Interest);
		}
	}
}

bool FSwcInteractor::GetPrompt(double Now, FSwcInteractPrompt& Out) const
{
	// No prompt in a conversation and until the answer 6709 (the conversation screen replaces it), and none at an open station.
	if (!Focus || Talk.locked() || bStationOpen)
	{
		Out = FSwcInteractPrompt();
		return false;
	}
	Out = FocusPrompt;
	Out.Phase = Use.phase() == swc::interact::Use::Phase::holding ? TEXT("hold") : Use.phase() == swc::interact::Use::Phase::requested ? TEXT("use")
		: Use.phase() == swc::interact::Use::Phase::executed ? TEXT("done") : TEXT("idle");
	// A gather: the progress ring of the work (the Gathering state), not of the request (as DebugText).
	Out.Progress = bUseGather && Gather.step() == swc::interact::GatherWatch::Step::running ? Gather.progress(Now) : Use.progress(Now);
	Out.WorkTime = bUseGather && Gather.step() == swc::interact::GatherWatch::Step::running ? static_cast<float>(Gather.work_time()) : 0.0f;
	return Out.Kind != FSwcInteractPrompt::EKind::None;
}

FString FSwcInteractor::DebugText(double Now) const
{
	if (Talk.locked())
	{
		return Talk.scene() ? FString(TEXT("conversation (E ends it)")) : FString(TEXT("the conversation ended: the input waits for 6709"));
	}
	if (!Focus)
	{
		return FString();
	}
	const TCHAR* Phase = Use.phase() == swc::interact::Use::Phase::holding ? TEXT("hold") : Use.phase() == swc::interact::Use::Phase::requested ? TEXT("use")
		: Use.phase() == swc::interact::Use::Phase::executed ? TEXT("done") : TEXT("E");
	// A gather: the progress ring of the work (the Gathering state), not of the request.
	const float Progress = bUseGather && Gather.step() == swc::interact::GatherWatch::Step::running ? Gather.progress(Now) : Use.progress(Now);
	return FString::Printf(TEXT("%s %s (%s), %.0f %%"), Phase, UTF8_TO_TCHAR(Focus->option->name.c_str()), *FocusName, Progress * 100.0f);
}
