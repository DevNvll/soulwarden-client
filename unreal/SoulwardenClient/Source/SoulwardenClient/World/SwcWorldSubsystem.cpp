#include "World/SwcWorldSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "World/SwcFlythrough.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcWorldTools, Log, All);

bool USwcWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcWorldSubsystem, STATGROUP_Tickables);
}

void USwcWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!FrameFolder.IsEmpty() && ++Frames % FrameEvery == 0)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FrameFolder, FString::Printf(TEXT("frame_%06d.png"), Pictures++)), false, false);
	}
}

void USwcWorldSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (FParse::Value(FCommandLine::Get(), TEXT("-SwcFrames="), FrameFolder))
	{
		FParse::Value(FCommandLine::Get(), TEXT("-SwcFramesEvery="), FrameEvery);
		FrameEvery = FMath::Max(FrameEvery, 1);
		UE_LOG(LogSwcWorldTools, Log, TEXT("frames: a picture every %d frames in %s"), FrameEvery, *FrameFolder);
	}
	FString Plan;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-SwcFlythrough="), Plan))
	{
		return;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASwcFlythrough* Flythrough = World.SpawnActor<ASwcFlythrough>(Parameters);
	if (!Flythrough || !Flythrough->LoadPlan(Plan))
	{
		UE_LOG(LogSwcWorldTools, Error, TEXT("flythrough: the plan %s cannot be read"), *Plan);
		return;
	}
	if (APlayerController* Controller = World.GetFirstPlayerController())
	{
		Controller->bEnableStreamingSource = false;   // only the camera of the fly-through streams
		Controller->SetViewTarget(Flythrough);
	}
}
