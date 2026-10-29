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
	void Feed(swc::ui::Runtime& Runtime, const swc::net::RepClient::OwnEntity& Own, USwcBackendSubsystem& Backend, const FString& ContentDir);
	/** A new world session (a travel keeps it; a character change makes a new one): the state and the calls start again. */
	void Reset();
	/**
	 * The place cues of the points of interest (research poi-cues.md; SWC_CONTENT/ui/gamedata/poi-cues.json): two detectors for each
	 * point, the discovery sphere (the DiscoveryRadius around the placement; none with no discovery achievement) and the charted shape
	 * (the polygon of the data, 180 m high). An entry sends 2297 with the CRC of its achievement while the bit of the achievement is
	 * not set (the shape with no charted achievement: 0); each exit sends 6416. The state is tested 5 times a second from the feet of
	 * the character (engine m): a travel is a new test. The backend uses the cues only as a cue (its place tasks use its own position).
	 */
	void FeedPlaces(const swc::net::WorldPosition& Feet, const swc::net::RepClient::OwnEntity& Own, USwcBackendSubsystem& Backend,
		const FString& ContentDir, double Now);
	/** @return True when the request of the scripts was a quest request (it is handled). */
	static bool HandleRequest(const FString& Request, USwcBackendSubsystem& Backend);

private:
	struct FPoi
	{
		uint16 Territory = 0;
		std::array<uint8, 16> Actor{};
		uint64 Group = 0;
		uint64 Local = 0;
		FVector Center = FVector::ZeroVector;
		float Radius = 0.0f;
		float Reach = 0.0f;
		TArray<FVector2D> Shape;
		uint32 DiscoveredCrc = 0;
		int32 DiscoveredBit = -1;
		uint32 ChartedCrc = 0;
		int32 ChartedBit = -1;
		bool bInSphere = false;
		bool bInShape = false;
	};
	TArray<FPoi> Pois;
	bool bPoisLoaded = false;
	double NextPlaceTest = 0.0;
	bool bEnabled = false;
	bool bRestored = false;
	uint32 SentRevision = 0;
	/** The encounter roots of the last call (QG5): a hash of the links and of the revisions of the linked roots. */
	uint64 SentEncounters = 0;
	/** The generated task list of each recipe slice (the GUID text of its asset; SWC_GAMEDATA_DIR/encounter-data.json): the title, then
	 *  one "DESCRIPTION|TARGET|TIMELIMIT|HIDE" entry for each task, tab-separated (protocol notes 4.62 "The task tree"). */
	TMap<FString, FString> EncounterTrees;
	bool bEncounterTreesLoaded = false;
	void LoadEncounterTrees(const FString& ContentDir);
	uint32 SentReadyRevision = 0;
	bool bSentAny = false;
	uint64 Group = 0;
};
