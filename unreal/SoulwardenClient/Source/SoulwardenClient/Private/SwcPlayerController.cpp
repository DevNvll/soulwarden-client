#include "SwcPlayerController.h"

#include "SwcBackendSubsystem.h"

void ASwcPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (USwcBackendSubsystem* Backend = GameInstance->GetSubsystem<USwcBackendSubsystem>())
		{
			Backend->LogEndpoints();
		}
	}
}
