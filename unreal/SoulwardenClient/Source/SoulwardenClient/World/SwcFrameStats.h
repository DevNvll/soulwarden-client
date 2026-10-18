#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcFrameStats.generated.h"

/**
 * The frame time of a run (the cost checks of the materials track, docs/MATERIALS.md): -SwcFrameStats=SECONDS logs every
 * SECONDS one line "SWC FRAMES {...}" with the count of frames, the p50, p95, and largest frame time (ms, the game
 * thread delta) and GPU time (ms, RHIGetGPUFrameCycles: not capped by the vertical sync), and the view place (engine m).
 * Take it with the picture capture off (-SwcFrames slows the client).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcFrameStats : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	double Seconds = 0.0;
	double WindowStart = 0.0;
	TArray<float> FrameMs;
	TArray<float> GpuMs;
};
