#include "World/SwcWorldSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Net/SwcSpace.h"
#include "World/SwcFlythrough.h"
#include "World/SwcLoadingGate.h"
#include "World/SwcWorldStreaming.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcWorldTools, Log, All);

bool USwcWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcWorldSubsystem, STATGROUP_Tickables);
}

namespace
{
/** The loading range of the main runtime partition of the world level (Scripts/build_world.py, RANGE_CM). */
constexpr double LoadingRangeCm = 102400.0;
/** A group of far meshes nearer than this to the view is hidden at once (never a far mesh near the character). */
constexpr double ImpostorNearCm = 25600.0;
/** The half diagonal of a group of 512 m. */
constexpr double ImpostorGroupRadiusCm = 36204.0;
} // namespace

void USwcWorldSubsystem::UpdateImpostors()
{
	// The view of the loading gate: during a loading screen the place of the character (the far meshes there are hidden before
	// the screen ends; the camera is at the origin of the world before the spawn), else the camera (also of a flythrough).
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	FVector View;
	if (!SwcLoadingGate::GetView(GetWorld(), View))
	{
		if (Controller == nullptr || Controller->PlayerCameraManager == nullptr)
		{
			return;
		}
		View = Controller->PlayerCameraManager->GetCameraLocation();
	}
	// A group within the loading range hides when the cells of the World Partition under it are in the world (before, it hid by
	// the distance alone: the ground there was bare until its cells came, up to 50 s after the loading screen; AST-203), at once
	// when it is near the view. A group beyond the range shows.
	const UWorldPartitionSubsystem* Partition = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
	for (const TWeakObjectPtr<AActor>& Actor : Impostors)
	{
		AActor* Impostor = Actor.Get();
		if (Impostor == nullptr)
		{
			continue;
		}
		FVector Origin, Extent;
		Impostor->GetActorBounds(false, Origin, Extent);
		const double Distance = FVector::Dist2D(Origin, View);
		bool bHide = Distance < LoadingRangeCm;
		if (bHide && !Impostor->IsHidden() && Partition != nullptr && Distance - ImpostorGroupRadiusCm > ImpostorNearCm)
		{
			// The part of the group within the range: a sphere about its center (the cells beyond the range never load).
			FWorldPartitionStreamingQuerySource Query(FVector(Origin.X, Origin.Y, View.Z));
			Query.bUseGridLoadingRange = false;
			Query.Radius = static_cast<float>(FMath::Clamp(LoadingRangeCm - Distance, 100.0, ImpostorGroupRadiusCm));
			bHide = Partition->IsStreamingCompleted(EWorldPartitionRuntimeCellState::Activated, {Query}, false);
		}
		if (Impostor->IsHidden() != bHide)
		{
			Impostor->SetActorHiddenInGame(bHide);
		}
	}
}

void USwcWorldSubsystem::UpdateGroundTest()
{
	if (GroundTests.IsEmpty())
	{
		return;
	}
	if (GroundTestStart == 0.0)
	{
		GroundTestStart = FPlatformTime::Seconds();
	}
	const FVector Place = GroundTests[0];
	if (!SwcWorldStreaming::IsReadyAt(GetWorld(), Place))
	{
		return;
	}
	FHitResult Hit;
	const bool bGround = GetWorld()->LineTraceSingleByChannel(Hit, Place + FVector(0.0, 0.0, 200.0), Place - FVector(0.0, 0.0, 5000.0), ECC_WorldStatic);
	// All the static surfaces on the line (an object query does not stop at the first one), with their meshes.
	TArray<FHitResult> Hits;
	GetWorld()->LineTraceMultiByObjectType(Hits, Place + FVector(0.0, 0.0, 200.0), Place - FVector(0.0, 0.0, 5000.0), FCollisionObjectQueryParams(ECC_WorldStatic));
	FString Surfaces;
	for (const FHitResult& Each : Hits)
	{
		const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Each.GetComponent());
		Surfaces += FString::Printf(TEXT("%s[%.2f, \"%s\"]"), Surfaces.IsEmpty() ? TEXT("") : TEXT(", "), Each.ImpactPoint.Z / 100.0,
			Mesh && Mesh->GetStaticMesh() ? *Mesh->GetStaticMesh()->GetName() : Each.GetComponent() ? *Each.GetComponent()->GetName() : TEXT(""));
	}
	UE_LOG(LogSwcWorldTools, Log, TEXT("SWC GROUND TEST {\"place m\": [%.1f, %.1f, %.1f], \"seconds\": %.1f, \"ground\": %s, \"ground m\": %.2f, \"actor\": \"%s\", \"surfaces\": [%s]}"),
		Place.X / 100.0, -Place.Y / 100.0, Place.Z / 100.0, FPlatformTime::Seconds() - GroundTestStart, bGround ? TEXT("true") : TEXT("false"),
		bGround ? Hit.ImpactPoint.Z / 100.0 : 0.0, bGround && Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT(""), *Surfaces);
	SwcWorldStreaming::Release();
	GroundTests.RemoveAt(0);
	GroundTestStart = 0.0;
	if (GroundTests.IsEmpty())
	{
		FGenericPlatformMisc::RequestExit(false);
	}
}

void USwcWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateGroundTest();
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextImpostorUpdate)
	{
		NextImpostorUpdate = Now + 0.5;
		UpdateImpostors();
	}
	if (!FrameFolder.IsEmpty() && ++Frames % FrameEvery == 0)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FrameFolder, FString::Printf(TEXT("frame_%06d.png"), Pictures++)), false, false);
	}
}

void USwcWorldSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	for (TActorIterator<AActor> It(&World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("SwcImpostor")))
		{
			Impostors.Add(*It);
		}
	}
	if (!Impostors.IsEmpty())
	{
		UE_LOG(LogSwcWorldTools, Log, TEXT("world: %d groups of far meshes (shown beyond %.0f m)"), Impostors.Num(), LoadingRangeCm / 100.0);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("-SwcFrames="), FrameFolder))
	{
		FParse::Value(FCommandLine::Get(), TEXT("-SwcFramesEvery="), FrameEvery);
		FrameEvery = FMath::Max(FrameEvery, 1);
		UE_LOG(LogSwcWorldTools, Log, TEXT("frames: a picture every %d frames in %s"), FrameEvery, *FrameFolder);
	}
	FString Tests;
	if (FParse::Value(FCommandLine::Get(), TEXT("-SwcGroundTest="), Tests, false))
	{
		TArray<FString> Places;
		Tests.ParseIntoArray(Places, TEXT(";"));
		for (const FString& Place : Places)
		{
			TArray<FString> Values;
			if (Place.ParseIntoArray(Values, TEXT(",")) == 3)
			{
				GroundTests.Add(SwcEngineToUnreal(swc::net::WorldPosition{FCString::Atof(*Values[0]), FCString::Atof(*Values[1]), FCString::Atof(*Values[2])}));
			}
		}
		if (APlayerController* Controller = World.GetFirstPlayerController())
		{
			Controller->bEnableStreamingSource = false;   // only the source of the wait streams
		}
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
