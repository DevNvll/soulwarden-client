#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SwcFlythrough.generated.h"

class UCameraComponent;
class UWorldPartitionStreamingSourceComponent;

/**
 * The fly-through of the world level (stage 1.3 evidence): a camera that flies along a path of engine points (m) at a
 * speed, with a World Partition streaming source. It waits for the streaming at the start, logs the frame rate and the
 * streaming state each second, can write a picture every N frames (a video), and ends the client at the end of the
 * path with a summary line ("SWC FLYTHROUGH ..."). Started by -SwcFlythrough=PLAN.json (USwcWorldSubsystem).
 *
 * Plan: {"points": [[x, y, z], ...], "speed": m/s, "look ahead": m, "pitch": degrees down,
 *        "frames": {"folder": path, "every": N}}  (frames are optional)
 */
UCLASS()
class SOULWARDENCLIENT_API ASwcFlythrough : public AActor
{
	GENERATED_BODY()

public:
	ASwcFlythrough();

	/** Reads the plan. @return False when the file has no path of two points or more. */
	bool LoadPlan(const FString& File);

	virtual void Tick(float DeltaSeconds) override;

private:
	FVector PointAt(double Distance) const;

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY()
	TObjectPtr<UWorldPartitionStreamingSourceComponent> Streaming;

	TArray<FVector> Points;      // Unreal cm
	TArray<double> Distances;    // the distance along the path at each point, cm
	double Speed = 3000.0;       // cm/s
	double LookAhead = 6000.0;   // cm
	double PitchDown = 12.0;     // degrees
	FString FrameFolder;
	int32 FrameEvery = 0;

	bool bStarted = false;
	double Travelled = 0.0;
	double StartWait = 0.0;
	int32 Frames = 0;
	TArray<float> FrameTimes;
	TArray<float> GameThreadTimes;   // ms (GGameThreadTime, the value of stat unit)
	TArray<float> GpuTimes;          // ms (RHIGetGPUFrameCycles)
	double SecondStart = 0.0;
	int32 SecondFrames = 0;
	int32 StreamingIncompleteSeconds = 0;
	int32 Pictures = 0;
};
