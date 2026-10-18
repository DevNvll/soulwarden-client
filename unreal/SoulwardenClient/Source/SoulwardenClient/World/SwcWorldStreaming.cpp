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

bool HasGround(UWorld* World, const FVector& Location)
{
	FHitResult Hit;
	FCollisionQueryParams Query(TEXT("SwcWorldGround"), true);
	return World->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 200.0), Location - FVector(0.0, 0.0, 5000.0),
		ECC_WorldStatic, Query);
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
		Component->RegisterComponent();
		Component->EnableStreamingSource();
		Source = Actor;
		SourceComponent = Component;
		Place = Location;
		Since = FPlatformTime::Seconds();
		StreamedSince = 0.0;
		UE_LOG(LogSwcWorld, Log, TEXT("world: a streaming source at %.0f %.0f %.0f cm: the ground of the place loads"), Location.X, Location.Y, Location.Z);
	}
	const double Now = FPlatformTime::Seconds();
	const double Waited = Now - Since;
	const bool bStreamed = SourceComponent.IsValid() && SourceComponent->IsStreamingCompleted();
	if (bStreamed && HasGround(World, Location))
	{
		UE_LOG(LogSwcWorld, Log, TEXT("world: the cells of the place are loaded and it has ground (%.1f s)"), Waited);
		return true;
	}
	StreamedSince = bStreamed ? (StreamedSince > 0.0 ? StreamedSince : Now) : 0.0;
	if (bStreamed && Now - StreamedSince > 3.0)
	{
		// The cells are loaded (the physics of a new cell is in the scene after a frame or two): the place has no ground.
		UE_LOG(LogSwcWorld, Warning, TEXT("world: the cells of the place are loaded, but it has no ground (%.1f s)"), Waited);
		return true;
	}
	if (Waited > TimeoutSeconds)
	{
		UE_LOG(LogSwcWorld, Warning, TEXT("world: no ground at the place after %.0f s (cells %s)"), Waited, bStreamed ? TEXT("loaded") : TEXT("not loaded"));
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
