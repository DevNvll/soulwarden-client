#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcWorldSubsystem.generated.h"

/**
 * The world tools of a game world (stage 1.3): -SwcFlythrough=PLAN.json starts the fly-through (ASwcFlythrough);
 * -SwcFrames=FOLDER writes a picture of the view every -SwcFramesEvery=N frames (default 2) for a video.
 * -SwcGroundTest=X,Y,Z;X,Y,Z (engine m) runs the wait of the spawn (SwcWorldStreaming::IsReadyAt) for each place,
 * logs the result and the time, and ends the client (a check with no backend).
 * The far meshes of the game (actors with the tag SwcImpostor, always loaded) show only where the World Partition
 * cells are not loaded: a group within the loading range of the view (the place of a loading screen, else the camera) hides
 * when the cells under it are in the world, and at once when it is within 256 m of the view. The groups and the Landscape
 * proxies stream in the runtime grid SwcFar (build_world.py); the loading gate waits for that grid within its range.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void UpdateImpostors();
	/** @return True when the far mesh group must be hidden at the view (its cells are in the world, or it is near). */
	bool ImpostorHidden(const AActor* Impostor, const FVector& View) const;
	/** The view of the far meshes: the place of a loading screen, else the camera. @return False with no view. */
	bool GetImpostorView(FVector& View) const;
	/** A level that the far grid streams in: its far mesh groups join the list with their state at once. */
	void AddLevelImpostors(ULevel* Level, UWorld* World);

	void UpdateGroundTest();

	TArray<FVector> GroundTests;   // Unreal cm
	double GroundTestStart = 0.0;
	TArray<TWeakObjectPtr<AActor>> Impostors;
	FDelegateHandle LevelAddedHandle;
	double NextImpostorUpdate = 0.0;
	FString FrameFolder;
	int32 FrameEvery = 2;
	int64 Frames = 0;
	int32 Pictures = 0;
};
