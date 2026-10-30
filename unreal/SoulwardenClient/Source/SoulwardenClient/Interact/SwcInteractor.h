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
	/** A quest object (the marker type Master_Quest of its slice: the Gatherable card, "E NAME"; AST-250). */
	bool bQuestObject = false;
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
	/** The NPC row of the npc object of the conversation (FSwcWorldObjectTarget::NpcId), and its name (the card name): the conversation
	 *  screen of a board, a note, or a person with no record of the person layer (AST-287). Empty for none. */
	const FString& GetConversationNpcId() const { return TalkNpcId; }
	const FString& GetConversationName() const { return TalkName; }
	/** The conversation screen of the UI ends the conversation (its Back key, Escape; its Next key at the last line): the next Update
	 *  ends it as the key E does (6705, then 2473), then sends the finished topics of the screen (5397; AST-248: only with a topic, as the
	 *  retail client in the live checks of the backend stage 2.5c). */
	void EndConversation(const std::vector<uint32>& FinishedTopics = {})
	{
		bPendingTalkEnd = true;
		PendingTopics = FinishedTopics;
	}
	/** The conversation screen of the UI shows: the key E is its Next key (the screen ends the conversation at the last line). */
	void SetConversationScreen(bool bShown) { bConversationScreen = bShown; }
	/** The InteractTags of the Active quest tasks of the character (lower case; AST-241): a quest object with InteractOnlyOnQuest has a
	 *  focus only with its tag here (backend gathering.md stage 1.8c: else the refusal notOnQuest, and the retail client shows no prompt). */
	void SetQuestTags(const TSet<FString>& Tags) { QuestTags = Tags; }
	/** The crafting station of the use whose screen shows (stage MVP-J): its Assembly key, the fee flag, and its name key. @return False
	 *  with no open station. */
	bool GetStation(FString& OutKey, bool& bOutFee, FString& OutName) const
	{
		OutKey = StationKey;
		bOutFee = bStationFee;
		OutName = StationName;
		return bStationOpen;
	}
	/** C-12: the mission board of the use whose screen shows (a town board: the option OpenMissionBoardOption, action 51): the actor of the
	 *  board (the record of its TerritoryInterface state). @return False with no open board. */
	bool GetBoard(swc::net::ActorId& OutActor) const
	{
		OutActor = BoardActor;
		return bBoardOpen;
	}
	/** The board screen ends the board use (its Escape, its back button): the next Update sends 2473. */
	void EndBoard() { bPendingBoardEnd = true; }
	/** C-12 phase 2: the conversation option 8 (Open Faction Board; 0x376E845 -> helper 0x374EF70): after the end of the conversation the
	 *  client sends action 51 OpenMissionBoardOption at the same person (the hidden board option of its npc object), with no hold.
	 *  @return False when the person of the conversation has no board option. */
	bool OpenPersonBoard()
	{
		bPendingPersonBoard = bTalkHasBoardOption;
		return bTalkHasBoardOption;
	}
	/** True when the open board use came from a person (a faction representative: the faction screen), false for a board object. */
	bool IsBoardFromPerson() const { return bBoardFromPerson; }
	/** The board screen of the UI shows (the guard: no screen 2 s after the open ends the use). */
	void SetBoardScreen(bool bShown) { bBoardScreen = bShown; }
	/** C-13 step 1b: the entrance of a story instance whose window shows (the option OpenDungeonMenu, action 71, committed): its game mode
	 *  (the CRC-32 of the lower-case GameModeId). @return False with no open entrance. */
	bool GetDungeon(uint32& OutMode) const
	{
		OutMode = DungeonMode;
		return bDungeonOpen;
	}
	/** The Enter of the entrance window (LocalPlayerUIRequestsBus EnterDungeon): the next Update sends the Enter request (72 with the base
	 *  action 71) of the entrance. @return False with no open entrance or no Enter option of the object. */
	bool EnterDungeon()
	{
		bPendingDungeonEnter = bDungeonOpen && bDungeonHasEnter;
		return bPendingDungeonEnter;
	}
	/** The entrance window ends the use (its Escape, its back button, a result that closes it): the next Update sends 2473. */
	void EndDungeon() { bPendingDungeonEnd = true; }
	/** The entrance window of the UI shows (the guard: no window 2 s after the open ends the use). */
	void SetDungeonScreen(bool bShown) { bDungeonScreen = bShown; }
	/** The crafting screen ends the station use (its Escape, its exit): the next Update sends 2473. */
	void EndStation() { bPendingStationEnd = true; }
	/** The crafting screen of the UI shows (the station guard: no screen 2 s after the open ends the use; AST-287). */
	void SetStationScreen(bool bShown) { bStationScreen = bShown; }

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
	/** The finished topics of the screen for the 5397 after the 2473 of the end (AST-248). */
	std::vector<uint32> PendingTopics;
	std::vector<uint32> TopicsAfterCancel;
	TSet<FString> QuestTags;
	bool bConversationScreen = false;
	FString TalkKey;
	/** The NPC row and the name of the focus and of the conversation (AST-287). */
	FString FocusNpcId;
	FString TalkNpcId;
	FString TalkName;
	swc::interact::Conversation Talk;
	/** The guards of the Talk: no screen, no answer 6709 (AST-287). */
	swc::interact::TalkGuard TalkGuard;
	/** C-12: the board use (the town board: action 51), as the station use: the focus, the use, the screen, the end by the screen. */
	bool bFocusBoard = false;
	bool bUseBoard = false;
	bool bBoardOpen = false;
	bool bPendingBoardEnd = false;
	bool bBoardScreen = false;
	double BoardOpenedAt = 0.0;
	swc::net::ActorId BoardActor{};
	/** C-12 phase 2: the board option of the npc object of the focus and of the conversation (action 51, hidden on the card), its
	 *  request, place, and range; the pending open after the conversation; the source of the open board. */
	bool bFocusHasBoardOption = false;
	swc::interact::Option FocusBoardOption;
	swc::net::InteractionRequest FocusBoardRequest;
	bool bTalkHasBoardOption = false;
	swc::interact::Option TalkBoardOption;
	swc::net::InteractionRequest TalkBoardRequest;
	swc::net::WorldPosition TalkBoardPlace;
	float TalkBoardRangeSq = 0.0f;
	bool bPendingPersonBoard = false;
	bool bBoardFromPerson = false;
	swc::interact::Option PersonBoardOption;
	/** C-13 step 1b: the entrance use (OpenDungeonMenu, action 71), as the board use: the focus, the use, the window, the end by the window;
	 *  the mode of the entrance; the Enter option of the object (action 72, hidden on the card) and its request; the committed request of
	 *  the use (the client puts it back after the Enter request: the retail client restores its record after a one-shot, 0x49175F0; the
	 *  backend takes the same block again as the same use, and a block that does not change is no new request). */
	bool bFocusDungeon = false;
	uint32 FocusDungeonMode = 0;
	bool bFocusHasEnter = false;
	swc::net::InteractionRequest FocusEnterRequest;
	bool bUseDungeon = false;
	bool bDungeonOpen = false;
	bool bPendingDungeonEnd = false;
	bool bPendingDungeonEnter = false;
	bool bDungeonScreen = false;
	double DungeonOpenedAt = 0.0;
	uint32 DungeonMode = 0;
	bool bDungeonHasEnter = false;
	swc::net::InteractionRequest DungeonEnterRequest;
	swc::net::InteractionRequest DungeonBaseRequest;
	/** The time of the last Enter request (-1: the committed request is in the block). */
	double DungeonEnterSentAt = -1.0;
	/** The station use (stage MVP-J): the focus is a station, the use is a station use, its screen shows, and the end by the screen. */
	bool bFocusStation = false;
	bool bUseStation = false;
	bool bStationOpen = false;
	bool bPendingStationEnd = false;
	/** The crafting screen shows, and the time of the open of the station use (the station guard). */
	bool bStationScreen = false;
	double StationOpenedAt = 0.0;
	FString StationKey;
	bool bStationFee = false;
	FString StationName;
};
