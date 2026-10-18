#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcWorldSubsystem.generated.h"

/**
 * The world tools of a game world (stage 1.3): -SwcFlythrough=PLAN.json starts the fly-through (ASwcFlythrough);
 * -SwcFrames=FOLDER writes a picture of the view every -SwcFramesEvery=N frames (default 2) for a video.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	FString FrameFolder;
	int32 FrameEvery = 2;
	int64 Frames = 0;
	int32 Pictures = 0;
};
