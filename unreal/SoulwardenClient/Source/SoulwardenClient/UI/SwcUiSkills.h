#pragma once

#include "CoreMinimal.h"

namespace swc::ui { class Runtime; }
class USwcBackendSubsystem;

/**
 * The character window of the UI (stage MVP-G, docs/UI.md "The character window"): each frame it gives swc/skills.lua the learned
 * nodes of the weapon trees (the ProgressionPoint state, slot 67: Swc.HostLearnedNodes) and the answers 2858 to the tree resets
 * (Swc.HostRespecResult). The requests of the screens go to the client of the session (protocol notes 4.13): "assign INDEX POOL
 * NODE" (1112), "apply" (2857), "bar INDEX PLACE ABILITY" (2701), "respec INDEX" (186).
 */
struct FSwcUiSkills
{
	void Update(swc::ui::Runtime& Runtime, USwcBackendSubsystem& Backend);
	/** @return True when the request of the scripts was a request of the trees or the bars (it is handled). */
	static bool HandleRequest(const FString& Request, USwcBackendSubsystem& Backend);

private:
	uint32 SentNodesRevision = 0;
	uint32 SentRespecAnswers = 0;
};
