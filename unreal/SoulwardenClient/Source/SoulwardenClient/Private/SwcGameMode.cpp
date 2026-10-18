#include "SwcGameMode.h"

#include "SwcPlayerController.h"

ASwcGameMode::ASwcGameMode()
{
	PlayerControllerClass = ASwcPlayerController::StaticClass();
}
