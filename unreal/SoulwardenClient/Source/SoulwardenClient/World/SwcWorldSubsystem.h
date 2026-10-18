#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcWorldSubsystem.generated.h"

/** The world tools of a game world (stage 1.3): -SwcFlythrough=PLAN.json starts the fly-through (ASwcFlythrough). */
UCLASS()
class SOULWARDENCLIENT_API USwcWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
};
