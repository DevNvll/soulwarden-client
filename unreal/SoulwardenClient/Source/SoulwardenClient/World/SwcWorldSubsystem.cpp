#include "World/SwcWorldSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "World/SwcFlythrough.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcWorldTools, Log, All);

bool USwcWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USwcWorldSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
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
