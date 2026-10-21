#include "World/SwcWorldSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
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
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
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
/** The runtime grid of the Landscape proxies and the far mesh groups (Scripts/build_world.py, FAR_GRID). */
const FName FarGrid(TEXT("SwcFar"));

/** A test switch of the cost of the wind (docs/WORLD.md "The wind flag"): 0 turns off the world position offset of each mesh
 * component with the wind (SwcWind, the custom primitive data 0, above 0), so Nanite and the virtual shadow maps treat it as
 * static; 1 turns it on again. Components that stream in after the change keep the default (on); the ground cover reads it
 * for each new batch. */
void SetWindEvaluate(IConsoleVariable* Variable)
{
	const bool bOn = Variable->GetInt() != 0;
	int32 Changed = 0;
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* Component = *It;
		const UWorld* World = Component->GetWorld();
		const TArray<float>& Data = Component->GetDefaultCustomPrimitiveData().Data;
		if (World != nullptr && World->IsGameWorld() && Data.Num() > 0 && Data[0] > 0.0f &&
			Component->bEvaluateWorldPositionOffset != bOn)
		{
			Component->SetEvaluateWorldPositionOffset(bOn);
			++Changed;
		}
	}
	UE_LOG(LogSwcWorldTools, Log, TEXT("world: swc.Wind.Evaluate %d: %d components with the wind changed"), bOn ? 1 : 0, Changed);
}
TAutoConsoleVariable<int32> CVarWindEvaluate(TEXT("swc.Wind.Evaluate"), 1,
	TEXT("1: the wind moves the vegetation (the world position offset); 0: off for each component with the wind (a cost test)."),
	FConsoleVariableDelegate::CreateStatic(&SetWindEvaluate));
} // namespace

bool USwcWorldSubsystem::ImpostorHidden(const AActor* Impostor, const FVector& View) const
{
	// A group within the loading range hides when the cells of the World Partition under it are in the world (before, it hid by
	// the distance alone: the ground there was bare until its cells came, up to 50 s after the loading screen; AST-203), at once
	// when it is near the view. A group beyond the range shows.
	const UWorldPartitionSubsystem* Partition = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
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
	return bHide;
}

bool USwcWorldSubsystem::GetImpostorView(FVector& View) const
{
	// The view of the loading gate: during a loading screen the place of the character (the far meshes there are hidden before
	// the screen ends; the camera is at the origin of the world before the spawn), else the camera (also of a flythrough).
	if (SwcLoadingGate::GetView(GetWorld(), View))
	{
		return true;
	}
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (Controller == nullptr || Controller->PlayerCameraManager == nullptr)
	{
		return false;
	}
	View = Controller->PlayerCameraManager->GetCameraLocation();
	return true;
}

void USwcWorldSubsystem::AddLevelImpostors(ULevel* Level, UWorld* World)
{
	// The far mesh groups stream in the far grid: a new group gets its state at once (it must not show for a frame near the view).
	if (World != GetWorld() || Level == nullptr)
	{
		return;
	}
	FVector View;
	const bool bView = GetImpostorView(View);
	for (AActor* Actor : Level->Actors)
	{
		if (Actor != nullptr && Actor->ActorHasTag(TEXT("SwcImpostor")))
		{
			Impostors.AddUnique(Actor);
			if (bView && Actor->IsHidden() != ImpostorHidden(Actor, View))
			{
				Actor->SetActorHiddenInGame(!Actor->IsHidden());
			}
		}
	}
}

void USwcWorldSubsystem::UpdateImpostors()
{
	FVector View;
	if (!GetImpostorView(View))
	{
		return;
	}
	Impostors.RemoveAll([](const TWeakObjectPtr<AActor>& Actor) { return !Actor.IsValid(); });   // the far grid unloads groups
	for (const TWeakObjectPtr<AActor>& Actor : Impostors)
	{
		AActor* Impostor = Actor.Get();
		if (const bool bHide = ImpostorHidden(Impostor, View); Impostor->IsHidden() != bHide)
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

void USwcWorldSubsystem::Deinitialize()
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	SwcLoadingGate::Unregister(GetWorld(), TEXT("far grid"));
	Super::Deinitialize();
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
	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &USwcWorldSubsystem::AddLevelImpostors);
	// The loading gate: the far grid (the Landscape proxies and the far mesh groups) within its loading range of the place is in
	// the world before the screen ends (no bare horizon after it). A level with no far grid: no cell, ready at once.
	SwcLoadingGate::Register(&World, TEXT("far grid"), [this](const FVector& Place, FString& Why)
	{
		FWorldPartitionStreamingQuerySource Query(Place);
		Query.bUseGridLoadingRange = true;
		Query.TargetGrids.Add(FarGrid);
		Query.TargetBehavior = EStreamingSourceTargetBehavior::Include;
		// The cells of the far grid in the query, and those not in the world yet (the level of 2026-10-20 has no far grid: the
		// trial was taken back, and IsStreamingCompleted of the empty grid stayed false in the ground test, AST-216).
		int32 Cells = 0;
		int32 Waiting = 0;
		const UWorldPartition* Partition = GetWorld()->GetWorldPartition();
		if (Partition != nullptr && Partition->RuntimeHash != nullptr)
		{
			Partition->RuntimeHash->ForEachStreamingCellsQuery(Query, [&Cells, &Waiting](const UWorldPartitionRuntimeCell* Cell)
			{
				++Cells;
				Waiting += Cell->GetCurrentState() != EWorldPartitionRuntimeCellState::Activated ? 1 : 0;
				return true;
			});
		}
		Why = FString::Printf(TEXT("the Landscape and the far meshes of the loading range load (%d of %d cells)"), Cells - Waiting, Cells);
		return Waiting == 0;
	});
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
