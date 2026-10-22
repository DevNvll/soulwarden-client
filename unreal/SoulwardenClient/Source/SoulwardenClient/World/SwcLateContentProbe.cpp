#include "World/SwcLateContentProbe.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Creatures/SwcCreature.h"
#include "Creatures/SwcEffectActor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ShaderCompiler.h"
#include "SwcBackendSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "World/SwcLoadingGate.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcLate, Log, All);

namespace
{
constexpr double ReportRadiusCm = 15000.0;   // the late content that counts: within 150 m of the character
constexpr double KnownRadiusCm = 40000.0;    // the snapshot: within 400 m (a creature that walks in is not late content)
constexpr double ScanSeconds = 0.25;
constexpr double RingsCm[] = {30000.0, 50000.0, 102400.0};   // the rings of the cells: 300 m, 500 m, the loading range of 1,024 m
constexpr double RingLimitSeconds = 180.0;
}

bool USwcLateContentProbe::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcLateContentProbe::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcLateContentProbe, STATGROUP_Tickables);
}

void USwcLateContentProbe::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	FString Value;
	bEnabled = FParse::Param(FCommandLine::Get(), TEXT("SwcLateProbe")) || FParse::Value(FCommandLine::Get(), TEXT("SwcLateProbe="), Value);
	if (!Value.IsEmpty())
	{
		WindowSeconds = FMath::Clamp(FCString::Atod(*Value), 1.0, 120.0);
	}
	if (bEnabled)
	{
		UE_LOG(LogSwcLate, Log, TEXT("late: the probe is on (%.0f s after each loading screen, %.0f m)"), WindowSeconds, ReportRadiusCm / 100.0);
	}
}

bool USwcLateContentProbe::CharacterPlace(FVector& Out) const
{
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		return false;
	}
	Out = Pawn->GetActorLocation();
	return true;
}

bool USwcLateContentProbe::Describe(const UPrimitiveComponent* Component, FSeen& Out)
{
	const AActor* Owner = Component->GetOwner();
	Out.bVisible = Component->IsRegistered() && Component->IsVisible() && !Component->bHiddenInGame && (Owner == nullptr || !Owner->IsHidden());
	Out.Mesh = nullptr;
	if (const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Component))
	{
		Out.Mesh = Static->GetStaticMesh();
	}
	else if (const USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Component))
	{
		Out.Mesh = Skinned->GetSkinnedAsset();
	}
	uint32 Hash = 0;
	for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
	{
		Hash = HashCombine(Hash, GetTypeHash(Component->GetMaterial(Index)));
	}
	Out.MaterialHash = Hash;
	return true;
}

double USwcLateContentProbe::Nearest(const UPrimitiveComponent* Component, const FVector& Point)
{
	// The bounds of a new instanced component are a coarse sphere (some included the origin of the world: 0 m); the distance is
	// the one of its nearest instance (the bounds of the mesh at the scale of the instance), else of its box.
	if (const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
		Instanced != nullptr && Instanced->GetStaticMesh() != nullptr && Instanced->GetInstanceCount() > 0)
	{
		const double Radius = Instanced->GetStaticMesh()->GetBounds().SphereRadius;
		double Best = TNumericLimits<double>::Max();
		FTransform Transform;
		for (int32 Index = 0; Index < Instanced->GetInstanceCount(); ++Index)
		{
			if (Instanced->GetInstanceTransform(Index, Transform, true))
			{
				Best = FMath::Min(Best, FVector::Dist(Transform.GetLocation(), Point) - Radius * Transform.GetMaximumAxisScale());
			}
		}
		return FMath::Max(0.0, Best);
	}
	return FMath::Sqrt(Component->Bounds.GetBox().ComputeSquaredDistanceToPoint(Point));
}

bool USwcLateContentProbe::IsServerEntity(const AActor* Owner)
{
	// A creature or an effect of the server shows when its record comes: the server sends it at its interest radius (game data);
	// that is not a late load. A creature that shows later than its record (its assets load) is "shown", and counts.
	return Owner != nullptr && (Owner->IsA<ASwcCreature>() || Owner->IsA<ASwcEffectActor>());
}

bool USwcLateContentProbe::InDrawRange(const UPrimitiveComponent* Component, const FVector& Camera)
{
	// A new component that is beyond its draw distance from the camera draws nothing (the ground cover builds its batches
	// to 148 m and culls its instances at 100 m); when the character comes near, that is the draw distance, not a late load.
	double Limit = Component->CachedMaxDrawDistance;
	if (const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component); Instanced && Instanced->InstanceEndCullDistance > 0)
	{
		Limit = Limit > 0.0 ? FMath::Min(Limit, double(Instanced->InstanceEndCullDistance)) : double(Instanced->InstanceEndCullDistance);
	}
	return Limit <= 0.0 || Nearest(Component, Camera) <= Limit;
}

FString USwcLateContentProbe::EngineWork() const
{
	FString Work;
	SwcLoadingGate::EngineReady(Work);
	return Work;
}

void USwcLateContentProbe::Start(const FVector& Place, const TCHAR* InWhat)
{
	What = InWhat;
	bActive = true;
	Late = 0;
	Entities = 0;
	ExposureLow = TNumericLimits<float>::Max();
	ExposureHigh = 0.0f;
	Seen.Reset();
	Reported.Reset();
	WindowStart = FPlatformTime::Seconds();
	RingSeconds.Init(-1.0, UE_ARRAY_COUNT(RingsCm));
	NextScan = WindowStart + ScanSeconds;
	NextLight = WindowStart;
	UE_LOG(LogSwcLate, Log, TEXT("late: the screen of %s ends at %.1f %.1f %.1f m; %s"), *What, Place.X / 100.0, -Place.Y / 100.0, Place.Z / 100.0,
		*EngineWork());
	Scan(true);
}

void USwcLateContentProbe::Scan(bool bFirst)
{
	FVector Place;
	if (!CharacterPlace(Place))
	{
		return;
	}
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const APawn* Pawn = Controller->GetPawn();
	const FVector Camera = Controller->PlayerCameraManager != nullptr ? Controller->PlayerCameraManager->GetCameraLocation() : Place;
	const double Since = FPlatformTime::Seconds() - WindowStart;
	int32 Known = 0;
	for (TObjectIterator<UPrimitiveComponent> It(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::Garbage); It; ++It)
	{
		const UPrimitiveComponent* Component = *It;
		if (Component->GetWorld() != GetWorld() || !Component->IsRegistered())
		{
			continue;
		}
		const AActor* Owner = Component->GetOwner();
		if (Owner != nullptr && (Owner == Pawn || Owner->GetAttachParentActor() == Pawn || Owner->IsA<APlayerController>()))
		{
			continue;   // the character and its gear
		}
		const FBoxSphereBounds& Bounds = Component->Bounds;
		const double Distance = FMath::Max(0.0, FVector::Dist(Bounds.Origin, Place) - Bounds.SphereRadius);
		if (Distance > KnownRadiusCm)
		{
			continue;
		}
		FSeen Now;
		Describe(Component, Now);
		const TWeakObjectPtr<const UPrimitiveComponent> Key(Component);
		FSeen* Before = Seen.Find(Key);
		if (bFirst || Before == nullptr)
		{
			if (!bFirst && Now.bVisible && Distance <= ReportRadiusCm && !Reported.Contains(Key))
			{
				if (const double Exact = Nearest(Component, Place); Exact <= ReportRadiusCm && InDrawRange(Component, Camera))
				{
					const bool bEntity = IsServerEntity(Owner);
					++(bEntity ? Entities : Late);
					Reported.Add(Key);
					UE_LOG(LogSwcLate, Log, TEXT("late: %4.1f s %5.1f m %s %s %s (%s) mesh %s"), Since, Exact / 100.0, bEntity ? TEXT("entity") : TEXT("new"),
						Owner != nullptr ? *Owner->GetActorNameOrLabel() : TEXT("-"), *Component->GetClass()->GetName(), *Component->GetName(),
						Now.Mesh != nullptr ? *Now.Mesh->GetName() : TEXT("-"));
				}
			}
			Seen.Add(Key, Now);
			++Known;
			continue;
		}
		if (Distance <= ReportRadiusCm && Now.bVisible)
		{
			const TCHAR* Change = !Before->bVisible ? TEXT("shown") : Before->Mesh != Now.Mesh ? TEXT("mesh") :
				Before->MaterialHash != Now.MaterialHash ? TEXT("material") : nullptr;
			const double Exact = Change != nullptr ? Nearest(Component, Place) : 0.0;
			if (Change != nullptr && Exact <= ReportRadiusCm && InDrawRange(Component, Camera))
			{
				// An effect of the server shows when the server says (a gameplay event); a creature that shows after its record is late.
				const bool bEntity = Owner != nullptr && Owner->IsA<ASwcEffectActor>();
				++(bEntity ? Entities : Late);
				UE_LOG(LogSwcLate, Log, TEXT("late: %4.1f s %5.1f m %s %s %s (%s) mesh %s"), Since, Exact / 100.0, bEntity ? TEXT("entity") : Change,
					Owner != nullptr ? *Owner->GetActorNameOrLabel() : TEXT("-"), *Component->GetClass()->GetName(), *Component->GetName(),
					Now.Mesh != nullptr ? *Now.Mesh->GetName() : TEXT("-"));
			}
		}
		*Before = Now;
	}
	if (bFirst)
	{
		UE_LOG(LogSwcLate, Log, TEXT("late: %d components within %.0f m at the end of the screen"), Known, KnownRadiusCm / 100.0);
	}
}

void USwcLateContentProbe::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bEnabled)
	{
		return;
	}
	FVector Place;
	const bool bPawn = CharacterPlace(Place);
	const USwcBackendSubsystem* Backend = GetWorld()->GetGameInstance() != nullptr ? GetWorld()->GetGameInstance()->GetSubsystem<USwcBackendSubsystem>()
		: nullptr;
	const bool bWaiting = Backend != nullptr && Backend->IsWaitingForCells();
	if (bActive && bWaiting && !bWasWaiting)
	{
		// A new loading screen ends the window: a change behind the screen is not late content (AST-249: the far tiles of the old
		// region show again when its cells leave, at the start of the next travel).
		Finish(FPlatformTime::Seconds() - WindowStart, TEXT(" (a new loading screen ends the window)"));
	}
	if (bPawn && !bHadPawn)
	{
		Start(Place, TEXT("the world entry"));
	}
	else if (bPawn && bWasWaiting && !bWaiting)
	{
		Start(Place, TEXT("a travel or a respawn"));
	}
	bHadPawn = bPawn;
	bWasWaiting = bWaiting;
	const double Now = FPlatformTime::Seconds();
	if (bPawn && Now >= NextRingCheck)
	{
		NextRingCheck = Now + ScanSeconds;
		CheckRings(Place, Now);
	}
	if (!bActive)
	{
		return;
	}
	if (Now >= NextScan)
	{
		NextScan = Now + ScanSeconds;
		Scan(false);
	}
	if (Now >= NextLight)
	{
		// The light after the screen (the rule: the light is settled when the screen ends): the exposure of the eye adaptation
		// and the mean scene luminance of the game view, each second; the summary has their spread.
		NextLight = Now + 1.0;
		SwcLoadingGate::FLight Light;
		if (SwcLoadingGate::GetLight(Light) && Light.Exposure > 0.0f)
		{
			ExposureLow = FMath::Min(ExposureLow, Light.Exposure);
			ExposureHigh = FMath::Max(ExposureHigh, Light.Exposure);
			UE_LOG(LogSwcLate, Log, TEXT("late: light %4.1f s: exposure %.4f, mean scene luminance %.4f"), Now - WindowStart, Light.Exposure, Light.Luminance);
		}
	}
	if (Now - WindowStart >= WindowSeconds)
	{
		Finish(WindowSeconds, TEXT(""));
	}
}

void USwcLateContentProbe::Finish(const double Seconds, const TCHAR* Reason)
{
	if (ExposureHigh > 0.0f)
	{
		UE_LOG(LogSwcLate, Log, TEXT("late: the exposure after the screen of %s: %.4f to %.4f (%.2f EV)"), *What, ExposureLow, ExposureHigh,
			FMath::Log2(ExposureHigh / ExposureLow));
	}
	bActive = false;
	Seen.Reset();
	UE_LOG(LogSwcLate, Log, TEXT("late content: %d in %.1f s after the screen of %s (within %.0f m)%s; entities of the server: %d; %s"), Late,
		Seconds, *What, ReportRadiusCm / 100.0, Reason, Entities, *EngineWork());
}

void USwcLateContentProbe::CheckRings(const FVector& Place, double Now)
{
	// The far cells: the time after the screen when every cell of the World Partition within each ring of the character is
	// in the world (the content beyond 150 m that still streams after the screen).
	const UWorldPartitionSubsystem* Partition = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
	if (Partition == nullptr || RingSeconds.IsEmpty())
	{
		return;
	}
	for (int32 Index = 0; Index < RingSeconds.Num(); ++Index)
	{
		if (RingSeconds[Index] >= 0.0)
		{
			continue;
		}
		FWorldPartitionStreamingQuerySource Query(Place);
		Query.bUseGridLoadingRange = false;
		Query.Radius = static_cast<float>(FMath::Min(RingsCm[Index], 100000.0));   // 1,000 m: the source of the character is at the camera
		if (Partition->IsStreamingCompleted(EWorldPartitionRuntimeCellState::Activated, {Query}, false))
		{
			RingSeconds[Index] = Now - WindowStart;
			UE_LOG(LogSwcLate, Log, TEXT("late: the cells within %.0f m are in the world %.1f s after the screen of %s"), Query.Radius / 100.0,
				RingSeconds[Index], *What);
		}
		else if (Now - WindowStart > RingLimitSeconds)
		{
			RingSeconds[Index] = Now - WindowStart;
			UE_LOG(LogSwcLate, Log, TEXT("late: the cells within %.0f m are not in the world %.0f s after the screen of %s"), Query.Radius / 100.0,
				RingLimitSeconds, *What);
		}
	}
}
