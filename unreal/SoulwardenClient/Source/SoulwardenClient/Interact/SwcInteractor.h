#pragma once

#include "CoreMinimal.h"
#include "Interact/SwcConversation.h"
#include "Interact/SwcGatherUse.h"
#include "Interact/SwcInteraction.h"
#include "Net/SwcRepConnection.h"

#include <optional>
#include <vector>

class USwcCreatureSubsystem;

/**
 * The prompt of the focus for the UI (the original prompt card of lyshineui/markers/unifiedinteract.dynamicuicanvas; stage WF7):
 * FSwcInteractor::GetPrompt.
 */
struct FSwcInteractPrompt
{
	/** The kind of the focus: a loot bag of the creature view, an object of the level data (the object layer), a fixture of the
	 *  server (interactions.json). */
	enum class EKind : uint8
	{
		None,
		Bag,
		Object,
		Fixture
	};
	EKind Kind = EKind::None;
	/** The interest number of a bag or a fixture; the key and the kind of an object of the level data (FSwcWorldObjectTarget). */
	uint32 Interest = 0;
	FString ObjectKey;
	FName ObjectKind;
	/** The name of the focus: the name of the row of an object, the template name of a fixture (empty for a bag). */
	FString Name;
	/** A gather in its work (AST-231, the work ring of the card): the time of the work (s; 0: no work), and the moments of impact of the
	 *  swings so far (GatherMomentOfImpact of the Gather fragment; USwcPlayerActions::GetGatherImpacts). */
	float WorkTime = 0.0f;
	int32 Impacts = 0;
	/** The title of a person under its name (AST-231: the card reads NpcComponentRequestBus GetNpcData title; empty: none). */
	FString Title;
	/** The options of the prompt in the order of the bound (the first one is the option of the key E; for a bag the second one,
	 *  OpenStorageOption, opens the panel): the option name of the data and its hold time (s). */
	TArray<TPair<FString, float>> Options;
	/** The use: "idle", "hold", "use", or "done", and its progress 0..1 (the hold, the delay, or the work of a gather). */
	FString Phase;
	float Progress = 0.0f;
	/** No tool for the row (E sends nothing): the gathering type of the row and the text key of the refusal ("ui_notoolchopping"). */
	bool bToolMissing = false;
	FString GatheringType;
	FString RefusalKey;
	/** The rank of the character is under the gate of the row (E sends nothing), and the required rank of the row. */
	bool bRankMissing = false;
	int32 RequiredRank = 0;
	/** The place of the focus (Unreal world space). */
	FVector Place = FVector::ZeroVector;
};

/**
 * The interactor of the player (stage P2.10, docs/INTERACTIONS.md): the objects of the server with a template of
 * SWC_CONTENT/player/interactions.json (tools/convert-player.sh interactions: the fixtures and the personal loot bag of the backend
 * copy), the focus (the nearest bound in its range), and the use of its option with the key E (swc::interact::Use). The request
 * goes into the input entries; the replies 3743 and the ends 3746 of the session change the use. The prompt of the UI comes later
 * (the UI track): a development run shows it in the debug text.
 *
 * The loot bags (stage P2.10): the creature view has the bag entities (USwcCreatureSubsystem::FindNearestLootBag); the key E at a
 * bag is its first option, TakeAllOption (the hold of the prompt 0.5 s): the server moves the items, the claim of the bag in the
 * LootTracker becomes Looted, and the client ends its use (2473). A Looted bag has no prompt, and its model hides 1.5 s after the
 * claim (HideMeshDelay of the slice).
 *
 * The objects of the level data (stage P4.1 part 2, docs/OBJECTS.md): the object layer of the creature track
 * (USwcObjectSubsystem::FindNearestObject) gives the nearest bound in range of a lore page, a chest, a node, a station; it is the focus
 * when it is nearer than the focus above. GatherOption (a node, a chest) is a timed use of the server: the request stays until the
 * Gathering state ends the work (swc::interact::GatherWatch), then the client ends the use (2473).
 *
 * The Talk (stage P4.8, docs/PERSONS.md "The Talk"): OpenConversation (action 63, committed, hold 0.5 s) of a person of the object
 * layer (kind npc) keeps its request until the end of the conversation (swc::interact::Conversation). The conversation opens at the
 * request; E in the conversation ends it (6705, then 2473); a refusal (3743 result 2) and the end by the server (3746) close it with
 * 6705. The input of the character stays blocked until the answer 6709.
 */
class FSwcInteractor
{
public:
	/** Reads interactions.json (no UObject: the worker thread of the player tables can call it). */
	bool Load(FString& OutReport);
	/** The key E went down or up. */
	void SetKey(bool bDown) { bKeyDown = bDown; }
	/** The key T (ui_interact_sec): the second option of the card of an object (stage MVP-F). */
	void SetSecondKey(bool bDown) { bSecondKey = bDown; }
	/** The move keys of the player (any of them down). A move key that goes down during a gather ends the work: the client sends 2473
	 *  (Soulwarden gathering stage 3.6a, live check L4: "a walk (S) during the ring: the client sends 2473 and the ring stops"). */
	void SetMoveKeys(bool bDown) { bMoveKeys = bDown; }
	/**
	 * One frame: the focus at the feet of the character (engine m), the key, the timers, the replies and the ends of the session,
	 * and the effects for the connection. bLocked: the player cannot use an object now (the death, a loading screen).
	 */
	void Update(const FSwcRepConnection::FSnapshot& Snapshot, FSwcRepConnection& Connection, const swc::net::WorldPosition& Feet, bool bLocked,
		double Now, USwcCreatureSubsystem* Creatures);
	/** A timed use plays (the action DelayedInteraction: the kneel, the weapons in their sheaths, no movement), or a gather works (the
	 *  action Gather: the Gather fragment of the row and the tool; stage P5.2). */
	bool IsTimedUse() const
	{
		return (Use.timed() && Use.phase() == swc::interact::Use::Phase::requested) || (bUseGather && Use.phase() != swc::interact::Use::Phase::idle);
	}
	/** The use fragment of the timed use: "delayed" (the kneel), or the Gather fragment of a gather ("gather|ACTION|TOOLTAGS"). */
	FString GetUseKind() const { return bUseGather ? UseKind : FString(TEXT("delayed")); }
	/** The paperdoll slot of the tool in the hand during a gather (0: none). */
	uint32 GetUseTool() const { return bUseGather ? UseTool : 0; }
	/** The place of the object of the use (Unreal world space; the character turns to it: InteractionAlignment), or none. */
	const FVector* GetUsePlace() const { return Use.phase() != swc::interact::Use::Phase::idle ? &UsePlace : nullptr; }
	/** The prompt and the progress for the debug text of a development run. */
	FString DebugText(double Now) const;
	/** The prompt of the focus for the UI (stage WF7). @return False with no focus (Out.Kind None). */
	bool GetPrompt(double Now, FSwcInteractPrompt& Out) const;
	const swc::interact::Use& GetUse() const { return Use; }
	/**
	 * The bag panel of the UI (stage P2.4, the UI track; protocol notes 4.42): the next Update opens the bag of the interest with its
	 * second option, OpenStorageOption (action 26; the open of the panel has no hold), when no use runs and the bag is in its range
	 * with no Looted claim. The use stays until CloseBag (the panel closes: 2473), the Looted claim, or the end of the bag.
	 */
	void OpenBag(uint32 Interest) { PendingOpen = Interest; }
	void CloseBag() { bPendingClose = true; }
	/** The interest of the bag whose panel use runs (0: none). */
	uint32 GetOpenBag() const { return OpenBagInterest; }
	/** The conversation (stage P4.8), and the key of the object of its person (the placement UUID: USwcPersonSubsystem::FindByInteraction). */
	const swc::interact::Conversation& GetConversation() const { return Talk; }
	const FString& GetConversationKey() const { return TalkKey; }
	/** The conversation screen of the UI ends the conversation (its Back key, Escape; its Next key at the last line): the next Update
	 *  ends it as the key E does (6705, then 2473). */
	void EndConversation() { bPendingTalkEnd = true; }
	/** The conversation screen of the UI shows: the key E is its Next key (the screen ends the conversation at the last line). */
	void SetConversationScreen(bool bShown) { bConversationScreen = bShown; }
	/** The crafting station of the use whose screen shows (stage MVP-J): its Assembly key, the fee flag, and its name key. @return False
	 *  with no open station. */
	bool GetStation(FString& OutKey, bool& bOutFee, FString& OutName) const
	{
		OutKey = StationKey;
		bOutFee = bStationFee;
		OutName = StationName;
		return bStationOpen;
	}
	/** The crafting screen ends the station use (its Escape, its exit): the next Update sends 2473. */
	void EndStation() { bPendingStationEnd = true; }

private:
	/** The Looted claims: the model of each bag hides HideMeshDelay after the claim. */
	void UpdateLootedBags(const swc::net::RepClient::OwnEntity& Own, USwcCreatureSubsystem* Creatures, double Now);

	/** The templates of the fixtures (the focus of the objects of the snapshot) and the template of the personal loot bag. */
	std::vector<swc::interact::Template> Templates;
	std::optional<swc::interact::Template> LootTemplate;
	swc::interact::Use Use;
	std::optional<swc::interact::Candidate> Focus;
	FString FocusName;
	/** The focus line of the checks (AST-198): the shape of the focus, and the line of the last log. */
	FString FocusShape;
	FString LastFocusLine;
	bool bKeyDown = false;
	bool bKeyWas = false;
	bool bMoveKeys = false;
	/** The second option of the focus (the key T), and the press of the key T that chose it. */
	swc::interact::Option SecondObjectOption;
	swc::net::InteractionRequest SecondRequest;
	bool bFocusHasSecond = false;
	bool bSecondKey = false;
	bool bSecondKeyWas = false;
	bool bChooseSecond = false;
	bool bMoveKeysWas = false;
	uint32 SeenCancels = 0;
	uint32 SeenResults = 0;
	bool bSeeded = false;
	/** The bag of the focus and of the use (its actor), and the time of the Looted claim of each bag (the hide). */
	bool bFocusBag = false;
	swc::net::ActorId UseBag{};
	bool bUseBag = false;
	/** The object of the focus (its first option; the candidate points to it), and the gather or open that runs. */
	swc::interact::Option ObjectOption;
	bool bFocusGather = false;
	bool bUseGather = false;
	swc::interact::GatherWatch Gather;
	/** The object of the focus: no tool for its row (the prompt has the refusal text, E sends nothing), its use fragment, and its
	 *  tool slot; the use fragment and the tool of the gather that works. */
	bool bFocusNoTool = false;
	FString FocusUseKind;
	uint32 FocusTool = 0;
	FString UseKind;
	uint32 UseTool = 0;
	FVector UsePlace = FVector::ZeroVector;
	TMap<uint32, double> LootedSince;
	/** The values of the prompt of the UI at the focus (GetPrompt). */
	FSwcInteractPrompt FocusPrompt;
	/** The revisions of the bag of the character and of the LootTracker that the log saw. */
	uint32 SeenBag = 0;
	uint32 SeenLoot = 0;
	/** The bag panel of the UI: the open that waits for the next Update, the close, the bag of the open use, and its option. */
	uint32 PendingOpen = 0;
	bool bPendingClose = false;
	uint32 OpenBagInterest = 0;
	swc::interact::Option OpenOption;
	/** The Talk: the focus is a person with OpenConversation (its object key), the use is a conversation, and the conversation. */
	bool bFocusTalk = false;
	FString FocusKey;
	bool bUseTalk = false;
	bool bPendingTalkEnd = false;
	bool bConversationScreen = false;
	FString TalkKey;
	swc::interact::Conversation Talk;
	/** The station use (stage MVP-J): the focus is a station, the use is a station use, its screen shows, and the end by the screen. */
	bool bFocusStation = false;
	bool bUseStation = false;
	bool bStationOpen = false;
	bool bPendingStationEnd = false;
	FString StationKey;
	bool bStationFee = false;
	FString StationName;
};
