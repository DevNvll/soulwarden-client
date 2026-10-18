#include "Objects/SwcLevelInstances.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY(LogSwcObjects);

void USwcLevelInstances::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &USwcLevelInstances::AddLevel);
	RemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &USwcLevelInstances::RemoveLevel);
	// The levels that are in the world now.
	if (UWorld* World = GetWorld())
	{
		for (ULevel* Level : World->GetLevels())
		{
			AddLevel(Level, World);
		}
	}
}

void USwcLevelInstances::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (FParse::Value(FCommandLine::Get(), TEXT("SwcInstanceProbe="), ProbeMeters) && ProbeMeters > 0.0)
	{
		bProbeKeep = FParse::Param(FCommandLine::Get(), TEXT("SwcInstanceProbeKeep"));
		ProbeTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USwcLevelInstances::TickProbe), 2.0f);
	}
}

void USwcLevelInstances::Deinitialize()
{
	FTSTicker::RemoveTicker(ProbeTicker);
	FWorldDelegates::LevelAddedToWorld.Remove(AddedHandle);
	FWorldDelegates::LevelRemovedFromWorld.Remove(RemovedHandle);
	Cells.Reset();
	Hidden.Reset();
	Super::Deinitialize();
}

bool USwcLevelInstances::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FIntPoint USwcLevelInstances::CellOf(const FVector& Place)
{
	// Engine space: x = Unreal X / 100, y = -Unreal Y / 100 (Net/SwcSpace.h); the cells of cells.py count from the engine origin.
	return FIntPoint(FMath::FloorToInt32(Place.X / 100.0 / CellMeters), FMath::FloorToInt32(-Place.Y / 100.0 / CellMeters));
}

bool USwcLevelInstances::AddActor(AActor* Actor)
{
	// A cell actor of the level build: a plain actor with instanced static mesh components (SwcEditorLibrary::SpawnInstanceActors).
	if (Actor == nullptr || Actor->GetClass() != AActor::StaticClass() || Actor->FindComponentByClass<UInstancedStaticMeshComponent>() == nullptr)
	{
		return false;
	}
	Cells.FindOrAdd(CellOf(Actor->GetActorLocation())).AddUnique(Actor);
	return true;
}

void USwcLevelInstances::AddLevel(ULevel* Level, UWorld* World)
{
	if (Level == nullptr || World != GetWorld())
	{
		return;
	}
	int32 Added = 0;
	for (AActor* Actor : Level->Actors)
	{
		Added += AddActor(Actor) ? 1 : 0;
	}
	// Also a level that comes again after a stream out: its components are new, with the level transforms.
	if (Added > 0)
	{
		++Revision;
		UE_LOG(LogSwcObjects, Verbose, TEXT("level instances: %d cell actor(s) came, revision %u"), Added, Revision);
	}
}

void USwcLevelInstances::RemoveLevel(ULevel* Level, UWorld* World)
{
	if (Level == nullptr || World != GetWorld())
	{
		return;
	}
	for (AActor* Actor : Level->Actors)
	{
		if (Actor == nullptr)
		{
			continue;
		}
		const FIntPoint Cell = CellOf(Actor->GetActorLocation());
		if (TArray<TWeakObjectPtr<AActor>>* Actors = Cells.Find(Cell))
		{
			Actors->Remove(Actor);
			if (Actors->IsEmpty())
			{
				Cells.Remove(Cell);
			}
		}
	}
	// The saved transforms of the components that go away (a new load has the level transforms again).
	for (auto It = Hidden.CreateIterator(); It; ++It)
	{
		const UInstancedStaticMeshComponent* Component = It.Key().ResolveObjectPtr();
		if (Component == nullptr || Component->GetComponentLevel() == Level)
		{
			It.RemoveCurrent();
		}
	}
}

int32 USwcLevelInstances::FindInstancesAt(const FVector& Place, double RadiusCm, TArray<FSwcMeshInstanceRef>& Out)
{
	Out.Reset();
	const FIntPoint Center = CellOf(Place);
	const double RadiusSq = RadiusCm * RadiusCm;
	for (int32 DY = -1; DY <= 1; ++DY)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			TArray<TWeakObjectPtr<AActor>>* Actors = Cells.Find(Center + FIntPoint(DX, DY));
			if (Actors == nullptr)
			{
				continue;
			}
			for (const TWeakObjectPtr<AActor>& Actor : *Actors)
			{
				if (!Actor.IsValid())
				{
					continue;
				}
				TInlineComponentArray<UInstancedStaticMeshComponent*> Components(Actor.Get());
				for (UInstancedStaticMeshComponent* Component : Components)
				{
					for (const int32 Index : Component->GetInstancesOverlappingSphere(Place, static_cast<float>(RadiusCm), true))
					{
						// The origin of the instance: the level transform when it is hidden (its scale 0 keeps the place too).
						FTransform Transform;
						if (!Component->GetInstanceTransform(Index, Transform, true))
						{
							continue;
						}
						if (FVector::DistSquared(Transform.GetLocation(), Place) <= RadiusSq)
						{
							Out.Add({Component, Index, Component->GetStaticMesh(), Transform.GetLocation()});
						}
					}
				}
			}
		}
	}
	return Out.Num();
}

bool USwcLevelInstances::IsInstanceHidden(const FSwcMeshInstanceRef& Instance) const
{
	const TMap<int32, FTransform>* Saved = Instance.Component.IsValid() ? Hidden.Find(Instance.Component.Get()) : nullptr;
	return Saved != nullptr && Saved->Contains(Instance.Index);
}

void USwcLevelInstances::SetInstanceHidden(const FSwcMeshInstanceRef& Instance, bool bHidden)
{
	UInstancedStaticMeshComponent* Component = Instance.Component.Get();
	if (Component == nullptr || !Component->IsValidInstance(Instance.Index))
	{
		return;
	}
	if (bHidden)
	{
		TMap<int32, FTransform>& Saved = Hidden.FindOrAdd(Component);
		if (Saved.Contains(Instance.Index))
		{
			return;
		}
		FTransform Level;
		Component->GetInstanceTransform(Instance.Index, Level, true);
		Saved.Add(Instance.Index, Level);
		FTransform Gone = Level;
		Gone.SetScale3D(FVector::ZeroVector);
		Component->UpdateInstanceTransform(Instance.Index, Gone, true, true, true);
		return;
	}
	TMap<int32, FTransform>* Saved = Hidden.Find(Component);
	const FTransform* Level = Saved != nullptr ? Saved->Find(Instance.Index) : nullptr;
	if (Level == nullptr)
	{
		return;
	}
	Component->UpdateInstanceTransform(Instance.Index, *Level, true, true, true);
	Saved->Remove(Instance.Index);
	if (Saved->IsEmpty())
	{
		Hidden.Remove(Component);
	}
}

bool USwcLevelInstances::TickProbe(float DeltaSeconds)
{
	const APlayerController* Controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		return true;
	}
	if (bProbeKeep)
	{
		if (!bProbePlaceSet)
		{
			// The first place with instances (before the world entry the pawn is not at its place yet).
			if (FindInstancesAt(Pawn->GetActorLocation(), ProbeMeters * 100.0, ProbeInstances) == 0)
			{
				return true;
			}
			bProbePlaceSet = true;
			ProbePlace = Pawn->GetActorLocation();
		}
		else if (ProbeRevision == Revision)
		{
			return true;
		}
		ProbeRevision = Revision;
		FindInstancesAt(ProbePlace, ProbeMeters * 100.0, ProbeInstances);
		int32 Newly = 0;
		for (const FSwcMeshInstanceRef& Instance : ProbeInstances)
		{
			Newly += IsInstanceHidden(Instance) ? 0 : 1;
			SetInstanceHidden(Instance, true);
		}
		UE_LOG(LogSwcObjects, Display, TEXT("Swc instance probe: revision %u: %d instances within %.0f m of the first place, %d hidden now (%d cells known, %d hidden components)"),
			Revision, ProbeInstances.Num(), ProbeMeters, Newly, Cells.Num(), Hidden.Num());
		return true;
	}
	// A new search at each hide: a stream out and in of a cell makes new components (GetRevision).
	if (!bProbeHidden)
	{
		FindInstancesAt(Pawn->GetActorLocation(), ProbeMeters * 100.0, ProbeInstances);
	}
	bProbeHidden = !bProbeHidden;
	TSet<const UStaticMesh*> Meshes;
	for (const FSwcMeshInstanceRef& Instance : ProbeInstances)
	{
		SetInstanceHidden(Instance, bProbeHidden);
		Meshes.Add(Instance.Mesh.Get());
	}
	UE_LOG(LogSwcObjects, Display, TEXT("Swc instance probe: %s %d instances of %d meshes within %.0f m (%d cells known, revision %u, %d hidden components)"),
		bProbeHidden ? TEXT("hid") : TEXT("showed"), ProbeInstances.Num(), Meshes.Num(), ProbeMeters, Cells.Num(), Revision, Hidden.Num());
	return true;
}
