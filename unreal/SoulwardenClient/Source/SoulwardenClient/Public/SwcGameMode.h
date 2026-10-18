#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SwcGameMode.generated.h"

/** The game mode of the client. The world state comes from the Soulwarden backend, not from this game mode. */
UCLASS()
class SOULWARDENCLIENT_API ASwcGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASwcGameMode();
};
