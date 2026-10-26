#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"

#include "SwcGameViewportClient.generated.h"

/**
 * The game viewport client of the client (USwcUiSubsystem::Initialize selects it, before UGameEngine::Init makes the viewport client; docs/UI.md "Typed text").
 * The typed text of the keyboard comes only as character events: the platform layer (SDL text input; each window that
 * accepts input has it from its creation) gives FSlateApplication::OnKeyChar, and Slate routes the character along the
 * focus path to the game viewport (FSceneViewport::OnKeyChar, then InputChar). The input pre-processor of the UI
 * (FSwcUiInputProcessor) sees no character event (IInputProcessor has no handler for it). This client gives each
 * character to the UI runtime (Swc.HostInput "text"): the text input that edits takes it.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
	virtual void Init(struct FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice = true) override;
	virtual bool InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character) override;
};
