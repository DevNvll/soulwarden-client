#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SwcPlayerController.generated.h"

/** The player controller. Input, camera, and the input report to the backend come in the stages P2 and P3. */
UCLASS()
class SOULWARDENCLIENT_API ASwcPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
};
