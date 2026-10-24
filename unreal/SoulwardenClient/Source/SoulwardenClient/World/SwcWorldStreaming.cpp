#include "World/SwcWorldStreaming.h"

#include "Camera/CameraComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/SwcLoadingGate.h"
#include "WorldPartition/WorldPartition.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcWorld, Log, All);

namespace
{
TWeakObjectPtr<AActor> Source;
TWeakObjectPtr<UWorldPartitionStreamingSourceComponent> SourceComponent;
FVector Place = FVector(TNumericLimits<double>::Max());
double Since = 0.0;
double StreamedSince = 0.0;
double NextReport = 0.0;   // the next line of a long wait (each 5 s)
double ContentSince = 0.0;   // the time when the cells, the ground, and the checks of the systems were first ready
TWeakObjectPtr<UWorld> GateWorld;
// The light of the place (the rule of 2026-10-20: the light is settled when the screen ends): a camera of the source actor at the
// place is the view of the player during the wait, so the renderer works there behind the screen (Lumen, the real-time sky
// capture, the volumetric fog, the eye adaptation; before, the camera stayed at the origin of the world or at the old place).
TWeakObjectPtr<UCameraComponent> PlaceCamera;
bool bCameraSet = false;
uint64 CameraFrame = 0;
TArray<float> Exposures;   // the exposure of each frame rendered at the place, the newest last
uint64 LastExposureFrame = 0;

constexpr double CameraBackCm = 350.0;   // the camera of the place: behind and above the feet, as the camera of the character
constexpr double CameraUpCm = 200.0;
constexpr float CameraPitch = -10.0f;
constexpr float ScreenExposureSpeed = 20.0f;   // EV per second behind the screen (the engine default: 3 up, 1 down)
constexpr double TravelCameraDelay = 1.0;      // a travel: the loading screen of the UI shows first (it can come a frame later)
constexpr int32 LightFrames = 12;              // the exposure of the last 12 frames at the place ...
constexpr double LightStableEv = 0.05;         // ... is within 0.05 EV
constexpr double LightLimitSeconds = 10.0;     // at most this long after the content (then the screen ends with a note)

// The engine work near the picture (the shader jobs, the meshes and textures that compile, the texture streaming) after
// the content is ready: at most this long (the streamer can want the mips of far textures for a long time).
constexpr double EngineLimitSeconds = 20.0;

// A ground at most this far below the place of the server is the ground of the place (the server Z is about 0.5 m
// above the feet; the Landscape below the floor of the inn of Oxboro is 2.06 m below it).
constexpr double NearGroundBelowCm = 150.0;

// The failure of a load (AST-216): a wait that ends at its limit with the content of the place not ready, or a place with no
// ground at all, gets one retry with a new streaming source; then the wait fails with a reason (HasFailed): play does not start
// into broken content, and no test floor hides a missing world floor.
constexpr int32 MaxRetries = 1;
int32 Retries = 0;
FVector RetryPlace = FVector(TNumericLimits<double>::Max());
bool bFailed = false;
FString FailReason;

/** -SwcForceLoadFailure=N (a test of the failure path): the first N waits fail 5 s after their start. */
int32& ForcedFailures()
{
	static int32 Count = []
	{
		int32 Value = 0;
		FParse::Value(FCommandLine::Get(), TEXT("SwcForceLoadFailure="), Value);
		return Value;
	}();
	return Count;
}

/** A failed wait: a retry with a new streaming source (Release; the next call makes the source again), or the failure. */
bool RetryOrFail(const FString& Why, double Waited)
{
	if (Retries < MaxRetries)
	{
		++Retries;
		UE_LOG(LogSwcWorld, Warning, TEXT("world: the wait at the place failed after %.0f s (%s): a retry with a new streaming source (%d of %d)"),
			Waited, *Why, Retries, MaxRetries);
		SwcWorldStreaming::Release();
		return false;
	}
	bFailed = true;
	FailReason = Why;
	UE_LOG(LogSwcWorld, Error, TEXT("world: the load at the place failed after %.0f s and %d retry: %s"), Waited, Retries, *Why);
	return false;
}

/** @return The Z (cm) of the first ground below 2 m above Location (down 50 m), or no value. */
TOptional<double> GroundBelow(UWorld* World, const FVector& Location)
{
	FHitResult Hit;
	FCollisionQueryParams Query(TEXT("SwcWorldGround"), true);
	if (World->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 200.0), Location - FVector(0.0, 0.0, 5000.0),
		ECC_WorldStatic, Query))
	{
		return Hit.ImpactPoint.Z;
	}
	return {};
}
} // namespace

namespace
{
/** Makes the camera of the place the view of the player (at the world entry at once; for a travel 1 s after the start). */
void UpdatePlaceCamera(UWorld* World)
{
	APlayerController* Controller = World->GetFirstPlayerController();
	if (bCameraSet || Controller == nullptr || !PlaceCamera.IsValid() || !Source.IsValid())
	{
		return;
	}
	if (Controller->GetPawn() != nullptr && FPlatformTime::Seconds() - Since < TravelCameraDelay)
	{
		return;
	}
	Controller->SetViewTarget(Source.Get());
	bCameraSet = true;
	CameraFrame = GFrameCounter;
}

/** Keeps the exposure of each new frame rendered at the place (call in each frame of the wait). */
void SampleLight()
{
	SwcLoadingGate::FLight Light;
	if (SwcLoadingGate::GetLight(Light) && bCameraSet && Light.Frame > CameraFrame + 2 && Light.Frame != LastExposureFrame && Light.Exposure > 0.0f)
	{
		LastExposureFrame = Light.Frame;
		Exposures.Add(Light.Exposure);
	}
}

/** @return True when LightFrames frames were rendered at the place and their exposure is within LightStableEv. */
bool LightSettled(FString& Why)
{
	if (Exposures.Num() < LightFrames)
	{
		Why = FString::Printf(TEXT("%d of %d frames at the place"), Exposures.Num(), LightFrames);
		return false;
	}
	float Low = TNumericLimits<float>::Max();
	float High = 0.0f;
	for (int32 Index = Exposures.Num() - LightFrames; Index < Exposures.Num(); ++Index)
	{
		Low = FMath::Min(Low, Exposures[Index]);
		High = FMath::Max(High, Exposures[Index]);
	}
	const double Spread = FMath::Log2(static_cast<double>(High) / Low);
	Why = FString::Printf(TEXT("exposure %.4f, %.3f EV in the last %d frames (%d frames at the place)"), Exposures.Last(), Spread, LightFrames,
		Exposures.Num());
	return Spread <= LightStableEv;
}
} // namespace

bool SwcWorldStreaming::IsReadyAt(UWorld* World, const FVector& Location, double TimeoutSeconds, const FVector& Forward)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SwcWorldStreaming_IsReadyAt);   // the steps of the wait in a trace (AST-273)
	if (World == nullptr || World->GetWorldPartition() == nullptr)
	{
		return true;
	}
	if (!FVector::PointsAreNear(RetryPlace, Location, 100.0))
	{
		RetryPlace = Location;   // a new place: its own retry and failure state
		Retries = 0;
		bFailed = false;
		FailReason.Reset();
	}
	if (bFailed)
	{
		return false;   // the caller handles the failure (HasFailed)
	}
	if (!Source.IsValid() || !FVector::PointsAreNear(Place, Location, 100.0))
	{
		Release();
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Parameters);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		UWorldPartitionStreamingSourceComponent* Component = NewObject<UWorldPartitionStreamingSourceComponent>(Actor, TEXT("Streaming"));
		// Only the cells near the place: a sphere of 150 m (the near radius of the loading gate; cells of 256 m), first in the
		// queue. IsStreamingCompleted then waits for these cells, not for all the cells in the loading range of 1 km (the
		// character has its own source).
		// -SwcGateRing=M (a test of AST-203): the screen also waits for the cells within M m (the checks of the systems stay at 150 m).
		static const double RingCm = []
		{
			double Meters = 0.0;
			FParse::Value(FCommandLine::Get(), TEXT("SwcGateRing="), Meters);
			return FMath::Max(SwcLoadingGate::NearCm, Meters * 100.0);
		}();
		FStreamingSourceShape Shape;
		Shape.bUseGridLoadingRange = false;
		Shape.Radius = static_cast<float>(RingCm);
		Component->Shapes.Add(Shape);
		Component->Priority = EStreamingSourcePriority::Highest;
		Component->RegisterComponent();
		Component->EnableStreamingSource();
		// The camera of the place, with a fast eye adaptation behind the screen (the camera settings blend over the volumes).
		const FVector Facing = Forward.IsNearlyZero() ? FVector::ForwardVector : Forward.GetSafeNormal2D();
		UCameraComponent* Camera = NewObject<UCameraComponent>(Actor, TEXT("Camera"));
		Camera->SetupAttachment(Root);
		Camera->RegisterComponent();
		Camera->SetWorldLocationAndRotation(Location - Facing * CameraBackCm + FVector(0.0, 0.0, CameraUpCm),
			FRotator(CameraPitch, Facing.Rotation().Yaw, 0.0f));
		Camera->PostProcessSettings.bOverride_AutoExposureSpeedUp = true;
		Camera->PostProcessSettings.bOverride_AutoExposureSpeedDown = true;
		Camera->PostProcessSettings.AutoExposureSpeedUp = ScreenExposureSpeed;
		Camera->PostProcessSettings.AutoExposureSpeedDown = ScreenExposureSpeed;
		Camera->PostProcessBlendWeight = 1.0f;
		PlaceCamera = Camera;
		bCameraSet = false;
		Exposures.Reset();
		Source = Actor;
		SourceComponent = Component;
		Place = Location;
		Since = FPlatformTime::Seconds();
		StreamedSince = 0.0;
		ContentSince = 0.0;
		// The content systems load at this place from now on, in parallel with the cells (the loading gate).
		SwcLoadingGate::SetPlace(World, Location);
		GateWorld = World;
		NextReport = Since + 5.0;
		UE_LOG(LogSwcWorld, Log, TEXT("world: a streaming source at %.0f %.0f %.0f cm: the ground of the place loads"), Location.X, Location.Y, Location.Z);
	}
	UpdatePlaceCamera(World);
	SampleLight();
	const double Now = FPlatformTime::Seconds();
	const double Waited = Now - Since;
	bool bStreamed = false;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SwcWorldStreaming_IsStreamingCompleted);
		bStreamed = SourceComponent.IsValid() && SourceComponent->IsStreamingCompleted();
	}
	TOptional<double> Ground;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SwcWorldStreaming_GroundBelow);
		Ground = GroundBelow(World, Location);
	}
	const bool bNearGround = Ground.IsSet() && Ground.GetValue() >= Location.Z - NearGroundBelowCm;
	FString ChecksWhy;
	FString EngineWhy;
	bool bChecks = false;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SwcWorldStreaming_ChecksReady);
		bChecks = SwcLoadingGate::ChecksReady(World, Location, ChecksWhy);
	}
	bool bEngine = false;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SwcWorldStreaming_EngineReady);
		bEngine = SwcLoadingGate::EngineReady(EngineWhy);
	}
	FString LightWhy;
	const bool bLight = LightSettled(LightWhy);
	if (bStreamed && bNearGround && bChecks)
	{
		ContentSince = ContentSince > 0.0 ? ContentSince : Now;
		if ((bEngine || Now - ContentSince > EngineLimitSeconds) && (bLight || Now - ContentSince > LightLimitSeconds))
		{
			UE_LOG(LogSwcWorld, Log, TEXT("world: the cells of the place are loaded and it has ground (%.1f s); the content near the place "
				"is ready (%.1f s after the cells); %s%s; the light: %s%s"), Waited, Now - ContentSince, *EngineWhy,
				bEngine ? TEXT("") : TEXT(" (the limit of the engine work)"), *LightWhy, bLight ? TEXT("") : TEXT(" (the limit of the light)"));
			return true;
		}
	}
	else
	{
		ContentSince = 0.0;
	}
	if (Now >= NextReport)
	{
		// A long wait: the state of the two conditions, and the first surface below (its component).
		NextReport = Now + 5.0;
		FHitResult Hit;
		FCollisionQueryParams Query(TEXT("SwcWorldGround"), true);
		const bool bHit = World->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 200.0), Location - FVector(0.0, 0.0, 5000.0),
			ECC_WorldStatic, Query);
		UE_LOG(LogSwcWorld, Log, TEXT("world: the wait at the place, %.0f s: cells %s; %s; systems: %s; engine: %s"), Waited,
			bStreamed ? TEXT("loaded") : TEXT("not loaded"),
			bHit ? *FString::Printf(TEXT("the first surface %.2f m below the place (%s of %s)"), (Location.Z - Hit.ImpactPoint.Z) / 100.0,
				Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("?"), Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("?"))
			: TEXT("no surface below"), bChecks ? TEXT("ready") : *ChecksWhy, *EngineWhy);
		if (bCameraSet)
		{
			UE_LOG(LogSwcWorld, Log, TEXT("world: the light at the place: %s"), *LightWhy);
		}
	}
	// The cells are loaded, and the place has no ground near it: no ground at all, or only a ground far below (a place in
	// the air: the travel of a development route to 9230 4168 116.0 is 4.43 m above the Landscape, with no mesh there).
	// The physics of a new cell is in the scene after a frame or two: 3 s later the place has no floor, and the
	// character drops to the ground below (the authority of the server place). Before, a ground far below kept the wait
	// to the limit of 90 s while the cells were loaded. The engine work and the light must be done too: the floor can be a mesh
	// whose collision still builds (a travel of 426.7 m ended 3.7 s after the cells, and the character fell 1.62 m to the
	// ground below the cell surface at 0.73 m).
	StreamedSince = bStreamed ? (StreamedSince > 0.0 ? StreamedSince : Now) : 0.0;
	if (StreamedSince > 0.0 && Now - StreamedSince > 3.0 && !bNearGround && bChecks
		&& (bEngine || Now - StreamedSince > EngineLimitSeconds) && (bLight || Now - StreamedSince > LightLimitSeconds))
	{
		if (Ground.IsSet())
		{
			UE_LOG(LogSwcWorld, Warning, TEXT("world: the cells of the place are loaded; the ground is %.2f m below the place (%.1f s)"),
				(Location.Z - Ground.GetValue()) / 100.0, Waited);
			return true;   // a place in the air: the character drops to the ground below
		}
		// No ground at all in the open world (the Landscape covers every region): a missing world floor, not a test floor.
		return RetryOrFail(TEXT("the cells are loaded, but the place has no ground"), Waited);
	}
	if (ForcedFailures() > 0 && Waited > 5.0)
	{
		--ForcedFailures();
		return RetryOrFail(TEXT("a forced load failure (-SwcForceLoadFailure)"), Waited);
	}
	if (Waited > TimeoutSeconds)
	{
		// The limit with the content not ready: a retry, then the failure (before, play started here into broken content).
		const FString Why = !bStreamed ? FString::Printf(TEXT("the cells of the place did not load in %.0f s"), Waited)
			: !bChecks ? FString::Printf(TEXT("the content near the place is not ready (%s)"), *ChecksWhy)
			: Ground.IsSet() ? FString::Printf(TEXT("no ground near the place (a ground %.2f m below it)"), (Location.Z - Ground.GetValue()) / 100.0)
			: FString(TEXT("no ground at the place"));
		return RetryOrFail(Why, Waited);
	}
	return false;
}

bool SwcWorldStreaming::HasFailed(FString& Reason)
{
	Reason = FailReason;
	return bFailed;
}

void SwcWorldStreaming::ClearFailure()
{
	bFailed = false;
	FailReason.Reset();
	Retries = 0;
	RetryPlace = FVector(TNumericLimits<double>::Max());
}

void SwcWorldStreaming::Release()
{
	// The view goes back to the character (a travel; at the world entry the possession already did it).
	if (UWorld* World = GateWorld.Get(); World != nullptr && Source.IsValid())
	{
		if (APlayerController* Controller = World->GetFirstPlayerController(); Controller != nullptr && Controller->GetViewTarget() == Source.Get())
		{
			Controller->SetViewTarget(Controller->GetPawn() != nullptr ? static_cast<AActor*>(Controller->GetPawn()) : static_cast<AActor*>(Controller));
		}
	}
	PlaceCamera.Reset();
	bCameraSet = false;
	Exposures.Reset();
	SwcLoadingGate::ClearPlace(GateWorld.Get());
	GateWorld.Reset();
	if (AActor* Actor = Source.Get())
	{
		Actor->Destroy();
	}
	Source.Reset();
	SourceComponent.Reset();
	Place = FVector(TNumericLimits<double>::Max());
}
