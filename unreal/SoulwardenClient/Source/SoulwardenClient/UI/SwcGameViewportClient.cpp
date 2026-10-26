#include "UI/SwcGameViewportClient.h"

#include "Engine/GameInstance.h"
#include "UI/SwcUiSubsystem.h"

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
