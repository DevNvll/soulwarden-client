#pragma once

#include "CoreMinimal.h"
#include "Interact/SwcGatherUse.h"
#include "Interact/SwcInteraction.h"
#include "Net/SwcRepConnection.h"

#include <optional>
#include <vector>

class USwcCreatureSubsystem;

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
 */
class FSwcInteractor
{
public:
	/** Reads interactions.json (no UObject: the worker thread of the player tables can call it). */
	bool Load(FString& OutReport);
	/** The key E went down or up. */
	void SetKey(bool bDown) { bKeyDown = bDown; }
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
	const swc::interact::Use& GetUse() const { return Use; }

private:
	/** The Looted claims: the model of each bag hides HideMeshDelay after the claim. */
	void UpdateLootedBags(const swc::net::RepClient::OwnEntity& Own, USwcCreatureSubsystem* Creatures, double Now);

	/** The templates of the fixtures (the focus of the objects of the snapshot) and the template of the personal loot bag. */
	std::vector<swc::interact::Template> Templates;
	std::optional<swc::interact::Template> LootTemplate;
	swc::interact::Use Use;
	std::optional<swc::interact::Candidate> Focus;
	FString FocusName;
	bool bKeyDown = false;
	bool bKeyWas = false;
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
	/** The revisions of the bag of the character and of the LootTracker that the log saw. */
	uint32 SeenBag = 0;
	uint32 SeenLoot = 0;
};
