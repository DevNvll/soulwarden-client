#pragma once

#include "CoreMinimal.h"
#include "Boards/SwcTownOffers.h"
#include "Net/Codec/SwcRepClient.h"

#include <memory>

namespace swc::ui { class Runtime; }
class USwcBackendSubsystem;

/**
 * The mission boards of the UI (C-12 of ut567; QG6a; docs/UI.md "The mission boards"): each frame it compares the board use of the client
 * (USwcBackendSubsystem::GetMissionBoard, action 51) with the screen and calls swc/boards.lua:
 *   Swc.HostBoardTerritory()            -> the territory of the screen (Hud.LocalPlayer.CurrentAreaTerritory.ClaimKey)
 *   Swc.HostBoardOffers(text)           the town offers of that territory (Boards/SwcTownOffers with ui/gamedata/missiontown.json): a line
 *                                       "S seed seconds" and one line "O mission bucket project available" for each offer
 *   Swc.HostBoardStates(text)           the board projects (1746) "P key project secondsLeft progress" and the claims (3086) "C key level",
 *                                       "U category level", "A project type state" (the lines after a C line are of that claim)
 *   Swc.HostBoardProvider(text)         the provider state (3829) of the faction board: "X secondsLeft" (the expiration) and one line
 *                                       "F instance mission available origin destination" for each explicit offer (field 2, in the order of
 *                                       3816), and the daily bonuses (GameEvent 497) "B modifier used" (C-12 phase 2)
 *   Swc.HostBoard("open", actor, kind)  the screen of the board actor (32 hex digits; kind "town" or "faction": a person); "close" when
 *                                       the use ends
 * The offers change with the seed window, the board projects, the town entry of the provider state, the ranks, and the level. The requests
 * of the screen: "board end" (the end of the use), "board accept MISSION PROJECT" (3819), "board select INDEX" (3816), "board complete
 * INSTANCE" (2461).
 */
struct FSwcUiBoards
{
	void Update(swc::ui::Runtime& Runtime, USwcBackendSubsystem& Backend, const FString& ContentDir);
	/** @return True when the request of the scripts was a board request (it is handled). */
	static bool HandleRequest(const FString& Request, USwcBackendSubsystem& Backend);

private:
	bool LoadTown(const FString& ContentDir);
	void SendOffers(swc::ui::Runtime& Runtime, USwcBackendSubsystem& Backend, const swc::net::RepClient::OwnEntity& Own,
		const swc::net::RepClient::OtherEntity& Board, bool bForce);

	std::unique_ptr<swc::boards::TownData> Town;
	bool bTownTried = false;
	bool bShown = false;
	swc::net::ActorId ShownActor{};
	FString OffersKey;
	uint32 SentBoardRevision = ~0u;
	uint32 SentClaimRevision = ~0u;
	uint32 SentProviderRevision = ~0u;
	uint32 SentDailyRevision = ~0u;
};
