#pragma once

#include "CoreMinimal.h"

namespace swc::ui { class Runtime; }
class USwcBackendSubsystem;

/**
 * The game modes of the UI (C-13 step 1b part B of ut567; docs/UI.md "The entrance of a story instance"): each frame it compares the
 * states of the character and the entrance use of the client with the UI and calls swc/gamemodes.lua:
 *   Swc.HostGameModes(text)           the GameModeParticipant state (3312) at each change: "M mode map enteredSeconds" for each entry and
 *                                     "Q 0|1" (PlayerCanQuit, bit 22 of the flags: the leave button of the escape menu)
 *   Swc.HostDungeonResult(result)     each 3094 (OnDungeonActivateResult: eDungeonActivateResponse)
 *   Swc.HostDungeon("open", mode)     the entrance window of the use of OpenDungeonMenu (action 71); "close" when the use ends
 * The requests of the scripts: "dungeon enter DIFFICULTY" (the Enter: 72 with the base action 71), "dungeon end" (the window closes: 2473),
 * "gamemode event EVENT" (the leave of the escape menu: 1980 with 9BB080D0).
 */
struct FSwcUiGameModes
{
	void Update(swc::ui::Runtime& Runtime, USwcBackendSubsystem& Backend);
	/** @return True when the request of the scripts was a game mode request (it is handled). */
	static bool HandleRequest(const FString& Request, USwcBackendSubsystem& Backend);

private:
	bool bShown = false;
	uint32 ShownMode = 0;
	uint32 SentRevision = ~0u;
	uint32 SentResults = ~0u;
};
