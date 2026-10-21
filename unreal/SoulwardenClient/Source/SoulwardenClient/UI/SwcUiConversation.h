#pragma once

#include "CoreMinimal.h"

namespace swc::ui { class Runtime; }
class UWorld;
class USwcBackendSubsystem;

/**
 * The conversation screen of the UI (stage MVP-H, docs/PERSONS.md "The conversation screen"): each frame it compares the Talk
 * scene of the client (USwcBackendSubsystem::GetConversationSceneKey) with the screen and calls swc/conversation.lua
 * (Swc.HostConversation): "open" with the key, the speaker, the line key, the name, and the title of the person; "close" when the
 * scene ends (the end by the player, a refusal 3743 result 2, the end by the server, a death, a loading screen). The request
 * "conversation end" of the screen (its Next key at the last line, its Back key) goes to USwcBackendSubsystem::EndConversation.
 */
struct FSwcUiConversation
{
	void Update(swc::ui::Runtime& Runtime, UWorld* World, USwcBackendSubsystem& Backend);
	/** @return True when the request of the scripts was a conversation request (it is handled). */
	static bool HandleRequest(const FString& Request, USwcBackendSubsystem& Backend);

private:
	FString ShownKey;
};
