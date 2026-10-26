#include "UI/SwcGameViewportClient.h"

#include "Engine/GameInstance.h"
#include "UI/SwcUiSubsystem.h"

void USwcGameViewportClient::Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice)
{
	Super::Init(WorldContext, OwningGameInstance, bCreateNewAudioDevice);
	UE_LOG(LogSwcUi, Log, TEXT("ui: the game viewport client is %s (the typed text goes to the UI)"), *GetClass()->GetName());
}

bool USwcGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
	// The console of the engine first (it takes a character only while it is open).
	if (Super::InputChar(InViewport, ControllerId, Character))
	{
		return true;
	}
	UGameInstance* Instance = GetGameInstance();
	USwcUiSubsystem* Ui = Instance != nullptr ? Instance->GetSubsystem<USwcUiSubsystem>() : nullptr;
	return Ui != nullptr && Ui->UiChar(Character);
}
