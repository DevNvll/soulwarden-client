#pragma once

#include "CoreMinimal.h"
#include "Net/Codec/SwcRepClient.h"

namespace swc::ui { class Runtime; }
class USwcBackendSubsystem;

/**
 * The quests of the UI (Linear AST-236; docs/QUESTS.md; research quest-plan.md Q1): the owner quest state (the Objectives state, slot 22, and
 * the pin readiness, slot 73) to swc/quests.lua, and the quest calls of the scripts to the backend.
 *   - 2709: once in each world session, when the HUD runs (the backend sends the two quest states only after it): the auto-pin flags of
 *     the options (the retail default: all on, CommunityGoal off).
 *   - The state: at each change, Swc.HostQuests(lines): "A instance objective territory parent", "T instance index task progress state",
 *     "K instance" (tracked), "S instance index startNs", "C instance" (a completed notice). The instances are decimal texts (full u64).
 *   - The pin readiness: Swc.HostQuestPinReady(on). After it, the pinned list of the state goes back to the backend once (1458): the
 *     backend must not keep an empty start list (research quest-plan.md "Wire and display contract").
 *   - The requests of swc/quests.lua: "quest pins I,J,..." (1458), "quest screen CRC" (2617), "quest abandon I" (3836), "quest reset I"
 *     (5600).
 */
struct FSwcUiQuests
{
	void Feed(swc::ui::Runtime& Runtime, const swc::net::RepClient::OwnEntity& Own, USwcBackendSubsystem& Backend);
	/** A new world session (a travel keeps it; a character change makes a new one): the state and the calls start again. */
	void Reset();
	/** @return True when the request of the scripts was a quest request (it is handled). */
	static bool HandleRequest(const FString& Request, USwcBackendSubsystem& Backend);

private:
	bool bEnabled = false;
	bool bRestored = false;
	uint32 SentRevision = 0;
	uint32 SentReadyRevision = 0;
	bool bSentAny = false;
	uint64 Group = 0;
};
