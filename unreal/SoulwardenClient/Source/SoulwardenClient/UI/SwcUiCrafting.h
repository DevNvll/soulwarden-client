#pragma once

#include "CoreMinimal.h"

namespace swc::ui { class Runtime; }
class USwcBackendSubsystem;

/**
 * The crafting screen of the UI (stage MVP-J, docs/CRAFTING.md): each frame it compares the station use of the client
 * (USwcBackendSubsystem::GetCraftingStation) with the screen and calls swc/crafting.lua (Swc.HostStation): "open" with the Assembly
 * key, the fee flag, and the name key of the station; "close" when the use ends (Escape of the screen, a refusal 3743 result 2, the
 * end by the server 3746, a death, a loading screen). The request "station end" of the screen goes to
 * USwcBackendSubsystem::EndCraftingStation (2473).
 */
struct FSwcUiCrafting
{
	void Update(swc::ui::Runtime& Runtime, USwcBackendSubsystem& Backend);
	/** @return True when the request of the scripts was a crafting request (it is handled). */
	static bool HandleRequest(const FString& Request, USwcBackendSubsystem& Backend);

private:
	FString ShownKey;
};
