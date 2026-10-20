#include "SwcGameMode.h"

#include "SwcPlayerController.h"

ASwcGameMode::ASwcGameMode()
{
	PlayerControllerClass = ASwcPlayerController::StaticClass();
	// No default pawn (the loading work, docs/WORLD.md "The loading"): the engine spawned one at the origin of the world before
	// the character, and the start loading screen of the UI ended with it, 25 s before the spawn (the user saw the world load).
	// The controller has a pawn only when the backend spawns the character, after the loading gate.
	DefaultPawnClass = nullptr;
}
