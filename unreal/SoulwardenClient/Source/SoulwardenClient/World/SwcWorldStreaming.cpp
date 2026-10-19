#include "World/SwcWorldStreaming.h"

#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
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

// A ground at most this far below the place of the server is the ground of the place (the server Z is about 0.5 m
// above the feet; the Landscape below the floor of the inn of Oxboro is 2.06 m below it).
constexpr double NearGroundBelowCm = 150.0;

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

bool SwcWorldStreaming::IsReadyAt(UWorld* World, const FVector& Location, double TimeoutSeconds)
{
	if (World == nullptr || World->GetWorldPartition() == nullptr)
	{
		return true;
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
		// Only the cells at the place: a sphere of 80 m (cells of 256 m), first in the queue. IsStreamingCompleted then
		// waits for these cells, not for all the cells in the loading range of 1 km (the character has its own source).
		FStreamingSourceShape Shape;
		Shape.bUseGridLoadingRange = false;
		Shape.Radius = 8000.0f;
		Component->Shapes.Add(Shape);
		Component->Priority = EStreamingSourcePriority::Highest;
		Component->RegisterComponent();
		Component->EnableStreamingSource();
		Source = Actor;
		SourceComponent = Component;
		Place = Location;
		Since = FPlatformTime::Seconds();
		StreamedSince = 0.0;
		NextReport = Since + 5.0;
		UE_LOG(LogSwcWorld, Log, TEXT("world: a streaming source at %.0f %.0f %.0f cm: the ground of the place loads"), Location.X, Location.Y, Location.Z);
	}
	const double Now = FPlatformTime::Seconds();
	const double Waited = Now - Since;
	const bool bStreamed = SourceComponent.IsValid() && SourceComponent->IsStreamingCompleted();
	const TOptional<double> Ground = GroundBelow(World, Location);
	const bool bNearGround = Ground.IsSet() && Ground.GetValue() >= Location.Z - NearGroundBelowCm;
	if (bStreamed && bNearGround)
	{
		UE_LOG(LogSwcWorld, Log, TEXT("world: the cells of the place are loaded and it has ground (%.1f s)"), Waited);
		return true;
	}
	if (Now >= NextReport)
	{
		// A long wait: the state of the two conditions, and the first surface below (its component).
		NextReport = Now + 5.0;
		FHitResult Hit;
		FCollisionQueryParams Query(TEXT("SwcWorldGround"), true);
		const bool bHit = World->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 200.0), Location - FVector(0.0, 0.0, 5000.0),
			ECC_WorldStatic, Query);
		UE_LOG(LogSwcWorld, Log, TEXT("world: the wait at the place, %.0f s: cells %s; %s"), Waited, bStreamed ? TEXT("loaded") : TEXT("not loaded"),
			bHit ? *FString::Printf(TEXT("the first surface %.2f m below the place (%s of %s)"), (Location.Z - Hit.ImpactPoint.Z) / 100.0,
				Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("?"), Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("?"))
			: TEXT("no surface below"));
	}
	// The cells are loaded, and the place has no ground near it: no ground at all, or only a ground far below (a place in
	// the air: the travel of a development route to 9230 4168 116.0 is 4.43 m above the Landscape, with no mesh there).
	// The physics of a new cell is in the scene after a frame or two: 3 s later the place has no floor, and the
	// character drops to the ground below (the authority of the server place). Before, a ground far below kept the wait
	// to the limit of 90 s while the cells were loaded.
	StreamedSince = bStreamed ? (StreamedSince > 0.0 ? StreamedSince : Now) : 0.0;
	if (StreamedSince > 0.0 && Now - StreamedSince > 3.0)
	{
		if (Ground.IsSet())
		{
			UE_LOG(LogSwcWorld, Warning, TEXT("world: the cells of the place are loaded; the ground is %.2f m below the place (%.1f s)"),
				(Location.Z - Ground.GetValue()) / 100.0, Waited);
		}
		else
		{
			UE_LOG(LogSwcWorld, Warning, TEXT("world: the cells of the place are loaded, but it has no ground (%.1f s)"), Waited);
		}
		return true;
	}
	if (Waited > TimeoutSeconds)
	{
		if (Ground.IsSet())
		{
			UE_LOG(LogSwcWorld, Warning, TEXT("world: no ground near the place after %.0f s (cells %s; a ground %.2f m below the place)"),
				Waited, bStreamed ? TEXT("loaded") : TEXT("not loaded"), (Location.Z - Ground.GetValue()) / 100.0);
		}
		else
		{
			UE_LOG(LogSwcWorld, Warning, TEXT("world: no ground at the place after %.0f s (cells %s)"), Waited, bStreamed ? TEXT("loaded") : TEXT("not loaded"));
		}
		return true;
	}
	return false;
}

void SwcWorldStreaming::Release()
{
	if (AActor* Actor = Source.Get())
	{
		Actor->Destroy();
	}
	Source.Reset();
	SourceComponent.Reset();
	Place = FVector(TNumericLimits<double>::Max());
}
