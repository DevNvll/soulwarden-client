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
#include "SwcBackendSubsystem.h"

namespace
{
/** HideMeshDelay of the slice of the personal loot bag (interaction-data.json: lootContainers.personal, 1.5 s at looted). */
constexpr double BagHideDelay = 1.5;
/** GatherResource: the action of GatherOption (interaction-data.json), a timed use of the server (a node, a chest). */
constexpr uint8 GatherAction = 14;

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
	// The replies and the ends of the session first: they belong to the request that is out now.
	if (Own.interactionCancels != SeenCancels)
	{
		SeenCancels = Own.interactionCancels;
		if (Use.phase() != swc::interact::Use::Phase::idle)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the server ended the use of %s (3746)"), UTF8_TO_TCHAR(Use.option().name.c_str()));
		}
		Use.server_cancel();
	}
	if (Own.interactionResults != SeenResults)
	{
		SeenResults = Own.interactionResults;
		UE_LOG(LogSwcBackend, Log, TEXT("interact: the reply to %s: %s"), UTF8_TO_TCHAR(Use.option().name.c_str()),
			Own.lastInteractionResult == 1 ? TEXT("accepted") : TEXT("refused"));
		Use.reply(Own.lastInteractionResult, Now);
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
	if (Use.phase() == swc::interact::Use::Phase::idle)
	{
		bUseBag = false;
		bUseGather = false;
		Focus = bLocked ? std::nullopt : swc::interact::focus(Templates, Objects, Feet);
		FocusName = Focus ? FString(UTF8_TO_TCHAR(Focus->object->name.c_str())) : FString();
		bFocusBag = false;
		// The nearest bag in range with an enabled bound and no Looted claim: its first option (TakeAllOption).
		FSwcLootBag Bag;
		const FVector From(SwcEngineToUnreal(Feet));
		if (!bLocked && LootTemplate && !LootTemplate->bounds.empty() && Creatures != nullptr && Creatures->FindNearestLootBag(From, Bag)
			&& !IsLooted(Own, Bag.Actor) && Bag.bEnabled && !Bag.Options.IsEmpty() && (!Focus || Bag.Distance * Bag.Distance < Focus->distanceSq))
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
				Candidate.distanceSq = Bag.Distance * Bag.Distance;
				Focus = Candidate;
				FocusName = TEXT("loot bag");
				bFocusBag = true;
			}
		}
		// The objects of the level data (stage P4.1 part 2): the nearest bound in range of the object layer when it is nearer. Its first
		// option: ReadLore of a lore page, GatherOption of a node or a chest. The ids and the request come from the layer.
		bFocusGather = false;
		USwcObjectSubsystem* Layer = Creatures != nullptr && Creatures->GetWorld() != nullptr ? Creatures->GetWorld()->GetSubsystem<USwcObjectSubsystem>() : nullptr;
		FSwcWorldObjectTarget Object;
		if (!bLocked && Layer != nullptr && Layer->FindNearestObject(From, Object) && !Object.Options.IsEmpty()
			&& (!Focus || Object.Distance * Object.Distance < Focus->distanceSq))
		{
			ObjectOption = Object.Options[0].Option;
			swc::interact::Candidate Candidate;
			Candidate.option = &ObjectOption;
			Candidate.request = Object.Options[0].Request;
			Candidate.place = Object.EnginePlace;
			Candidate.distanceSq = Object.Distance * Object.Distance;
			Focus = Candidate;
			FocusName = FString::Printf(TEXT("%s %s"), *Object.Kind.ToString(), *Object.Key);
			bFocusBag = false;
			bFocusGather = ObjectOption.action == GatherAction;
		}
	}
	if (bLocked)
	{
		Use.abort();
	}
	// The key E.
	if (bKeyDown && !bKeyWas && Focus && !bLocked)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: E at %s (interest %u, %.1f m): %s, action %u, delay %.1f s, hold %.1f s"), *FocusName, Focus->interest,
			FMath::Sqrt(Focus->distanceSq), UTF8_TO_TCHAR(Focus->option->name.c_str()), Focus->option->action, Focus->option->delay, Focus->option->hold);
		// A bag and a gather keep their request until their source ends them (the Looted claim; the Gathering state).
		Use.press(Now, *Focus, bFocusBag || bFocusGather);
		if (bFocusBag)
		{
			bUseBag = true;
			UseBag = Focus->request.reference.actor;
		}
		if (bFocusGather)
		{
			bUseGather = true;
			Gather.start(Now, Snapshot.Own.gathering);
		}
	}
	else if (!bKeyDown && bKeyWas)
	{
		Use.release(Now);
	}
	bKeyWas = bKeyDown;
	const uint32 Executions = Use.executions();
	Use.tick(Now);
	if (Use.executions() != Executions)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: the execution of %s"), UTF8_TO_TCHAR(Use.option().name.c_str()));
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

FString FSwcInteractor::DebugText(double Now) const
{
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
