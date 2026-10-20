#include "World/SwcCollisionChunks.h"
#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "World/SwcLoadingGate.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcCollision, Log, All);

namespace
{
/** The side of a region (m): the region of a place is its engine x and y divided by it. */
constexpr double RegionMeters = 2048.0;

FString ContentFolder()
{
	FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	if (Content.IsEmpty())
	{
		FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
		if (Work.IsEmpty())
		{
			Work = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden-client"));
		}
		Content = FPaths::Combine(Work, TEXT("content"));
	}
	return FPaths::Combine(Content, TEXT("regions"));
}

template <typename T>
bool Take(const TArray<uint8>& Bytes, int64& Offset, T& Out)
{
	if (Offset + static_cast<int64>(sizeof(T)) > Bytes.Num())
	{
		return false;
	}
	FMemory::Memcpy(&Out, Bytes.GetData() + Offset, sizeof(T));
	Offset += sizeof(T);
	return true;
}
} // namespace

bool USwcCollisionChunks::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcCollisionChunks::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcCollisionChunks, STATGROUP_Tickables);
}

void USwcCollisionChunks::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	bEnabled = !FParse::Param(FCommandLine::Get(), TEXT("SwcNoCollisionChunks"));
	Folder = ContentFolder();
	UE_LOG(LogSwcCollision, Log, TEXT("collision: the collision chunks %s, range %.0f m (%s)"), bEnabled ? TEXT("on") : TEXT("off"), RangeCm / 100.0, *Folder);
	if (bEnabled)
	{
		// The loading gate (docs/WORLD.md "The loading"): the collider batches within the near radius of the place of a loading
		// screen are built before it ends (their regions read, their meshes loaded).
		SwcLoadingGate::Register(&World, TEXT("collision"), [this](const FVector& Place, FString& Why)
		{
			int32 Missing = 0;
			bool bRegions = true;
			const double EngineX = Place.X / 100.0;
			const double EngineY = -Place.Y / 100.0;
			const double NearM = SwcLoadingGate::NearCm / 100.0;
			for (int32 RX = FMath::FloorToInt((EngineX - NearM) / RegionMeters); RX <= FMath::FloorToInt((EngineX + NearM) / RegionMeters); ++RX)
			{
				for (int32 RY = FMath::FloorToInt((EngineY - NearM) / RegionMeters); RY <= FMath::FloorToInt((EngineY + NearM) / RegionMeters); ++RY)
				{
					const FRegion* Region = Regions.Find(FIntPoint(RX, RY));
					if (Region == nullptr || (!Region->bNoFile && (!Region->Data.IsValid() || !*Region->bRead)))
					{
						bRegions = false;
						continue;
					}
					if (Region->bNoFile)
					{
						continue;
					}
					for (const TPair<FIntPoint, TArray<FInstance>>& Pair : Region->Data->Batches)
					{
						const FVector Center((Pair.Key.X + 0.5) * BatchCm, (Pair.Key.Y + 0.5) * BatchCm, Place.Z);
						const FBatch* Batch = Batches.Find(Pair.Key);
						Missing += FVector::Dist2D(Center, Place) <= SwcLoadingGate::NearCm + BatchCm * 0.71 && (Batch == nullptr || !Batch->bBuilt);
					}
				}
			}
			Why = FString::Printf(TEXT("%d collider batch(es) to build%s"), Missing, bRegions ? TEXT("") : TEXT(", regions to read"));
			return bRegions && Missing == 0;
		});
	}
}

void USwcCollisionChunks::Deinitialize()
{
	SwcLoadingGate::Unregister(GetWorld(), TEXT("collision"));
	Super::Deinitialize();
}

bool USwcCollisionChunks::ReadRegionFile(const FString& Path, FRegionData& Data)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 12 || FMemory::Memcmp(Bytes.GetData(), "SWCK", 4) != 0)
	{
		return false;
	}
	int64 Offset = 4;
	uint32 Version = 0;
	uint32 Entries = 0;
	if (!Take(Bytes, Offset, Version) || Version != 1 || !Take(Bytes, Offset, Entries))
	{
		return false;
	}
	for (uint32 Index = 0; Index < Entries; ++Index)
	{
		uint16 Length = 0;
		if (!Take(Bytes, Offset, Length) || Offset + Length > Bytes.Num())
		{
			return false;
		}
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Offset), Length);
		Data.Meshes.Emplace(FString(Text.Length(), Text.Get()));
		Offset += Length;
	}
	uint32 Count = 0;
	if (!Take(Bytes, Offset, Count))
	{
		return false;
	}
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		uint32 Entry = 0;
		double T[3];
		float Q[4];
		float S[3];
		if (!Take(Bytes, Offset, Entry) || !Take(Bytes, Offset, T) || !Take(Bytes, Offset, Q) || !Take(Bytes, Offset, S) || Entry >= Entries)
		{
			return false;
		}
		FInstance Instance;
		Instance.Entry = Entry;
		Instance.Transform = FTransform(FQuat(Q[0], Q[1], Q[2], Q[3]), FVector(T[0], T[1], T[2]), FVector(S[0], S[1], S[2]));
		const FIntPoint Batch(FMath::FloorToInt(T[0] / BatchCm), FMath::FloorToInt(T[1] / BatchCm));
		Data.Batches.FindOrAdd(Batch).Add(Instance);
		++Data.Instances;
	}
	return true;
}

USwcCollisionChunks::FRegion& USwcCollisionChunks::RegionAt(const FIntPoint& Key, bool bNow)
{
	FRegion& Region = Regions.FindOrAdd(Key);
	if (!Region.bStarted)
	{
		Region.bStarted = true;
		const FString Path = FPaths::Combine(Folder, FString::Printf(TEXT("r_+%02d_+%02d"), Key.X, Key.Y), TEXT("collision-chunks.bin"));
		if (!FPaths::FileExists(Path))
		{
			Region.bNoFile = true;
			return Region;
		}
		Region.Data = MakeShared<FRegionData, ESPMode::ThreadSafe>();
		Region.bRead = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
		TSharedPtr<FRegionData, ESPMode::ThreadSafe> Data = Region.Data;
		TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> bRead = Region.bRead;
		const auto Read = [Path, Data, bRead, Key]()
		{
			const double Start = FPlatformTime::Seconds();
			const bool bOk = ReadRegionFile(Path, *Data);
			if (!bOk)
			{
				Data->Meshes.Reset();
				Data->Batches.Reset();
			}
			UE_LOG(LogSwcCollision, Log, TEXT("collision: region r_+%02d_+%02d read: %s, %d meshes, %d instances, %.2f s"), Key.X, Key.Y,
				bOk ? TEXT("ok") : TEXT("failed"), Data->Meshes.Num(), Data->Instances, FPlatformTime::Seconds() - Start);
			*bRead = true;
		};
		if (bNow)
		{
			Read();   // the region of the character: its file now (a few 100 kB)
		}
		else
		{
			Async(EAsyncExecution::ThreadPool, Read);
		}
	}
	return Region;
}

AActor* USwcCollisionChunks::OwnerActor()
{
	if (AActor* Actor = Owner.Get())
	{
		return Actor;
	}
	FActorSpawnParameters Parameters;
	Parameters.Name = MakeUniqueObjectName(GetWorld(), AActor::StaticClass(), TEXT("SwcCollisionChunks"));
	Parameters.ObjectFlags |= RF_Transient;
	AActor* Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	if (Actor)
	{
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Static);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
	}
	Owner = Actor;
	return Actor;
}

void USwcCollisionChunks::Build(const FRegionData& Data, const TArray<FInstance>& Instances, FBatch& Batch)
{
	AActor* Actor = OwnerActor();
	if (Actor == nullptr)
	{
		return;
	}
	Batch.bBuilt = true;
	TMap<uint32, TArray<FTransform>> ByMesh;
	for (const FInstance& Instance : Instances)
	{
		ByMesh.FindOrAdd(Instance.Entry).Add(Instance.Transform);
	}
	for (const TPair<uint32, TArray<FTransform>>& Group : ByMesh)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(Data.Meshes[Group.Key].ResolveObject());
		if (Mesh == nullptr)
		{
			continue;
		}
		// The settings of the collision instances of the cells (SwcEditorLibrary::SpawnInstanceActors).
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Actor, NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetStaticMesh(Mesh);
		Component->SetVisibility(false);
		Component->SetHiddenInGame(true);
		Component->SetCastShadow(false);
		Component->bAffectDistanceFieldLighting = false;
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->ComponentTags.Add(TEXT("SwcCollision"));
		Component->SetupAttachment(Actor->GetRootComponent());
		Component->RegisterComponent();
		Component->AddInstances(Group.Value, false, true, false);
		Batch.Components.Add(Component);
		Batch.Instances += Group.Value.Num();
	}
}

void USwcCollisionChunks::Update(const FVector& View)
{
	// The regions under the range of the character (engine m: x east = X / 100, y north = -Y / 100).
	const double EngineX = View.X / 100.0;
	const double EngineY = -View.Y / 100.0;
	const double RangeM = RangeCm / 100.0;
	for (int32 RX = FMath::FloorToInt((EngineX - RangeM) / RegionMeters); RX <= FMath::FloorToInt((EngineX + RangeM) / RegionMeters); ++RX)
	{
		for (int32 RY = FMath::FloorToInt((EngineY - RangeM) / RegionMeters); RY <= FMath::FloorToInt((EngineY + RangeM) / RegionMeters); ++RY)
		{
			const bool bHere = RX == FMath::FloorToInt(EngineX / RegionMeters) && RY == FMath::FloorToInt(EngineY / RegionMeters);
			FRegion& Region = RegionAt(FIntPoint(RX, RY), bHere);
			if (Region.bNoFile || !Region.Data.IsValid() || !*Region.bRead)
			{
				continue;
			}
			for (const TPair<FIntPoint, TArray<FInstance>>& Pair : Region.Data->Batches)
			{
				const FVector Center((Pair.Key.X + 0.5) * BatchCm, (Pair.Key.Y + 0.5) * BatchCm, View.Z);
				const double Distance = FVector::Dist2D(Center, View);
				FBatch* Batch = Batches.Find(Pair.Key);
				if (Batch == nullptr && Distance > RangeCm + BatchCm * 0.71)
				{
					continue;
				}
				if (Batch == nullptr)
				{
					Batch = &Batches.Add(Pair.Key);
					TArray<FSoftObjectPath> Paths;
					for (const FInstance& Instance : Pair.Value)
					{
						Paths.AddUnique(Region.Data->Meshes[Instance.Entry]);
					}
					if (Distance <= NearCm)
					{
						// The batch of the character: its collision meshes at once (small collision-only meshes).
						for (const FSoftObjectPath& Path : Paths)
						{
							Path.TryLoad();
						}
					}
					else
					{
						Batch->Assets = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths);
					}
				}
				if (!Batch->bBuilt && (!Batch->Assets.IsValid() || Batch->Assets->HasLoadCompleted()))
				{
					Build(*Region.Data, Pair.Value, *Batch);
				}
			}
		}
	}
	// The batch under the character is built (or there is none): the update can wait between its steps.
	const FIntPoint Here(FMath::FloorToInt(View.X / BatchCm), FMath::FloorToInt(View.Y / BatchCm));
	const FRegion* HereRegion = Regions.Find(FIntPoint(FMath::FloorToInt(EngineX / RegionMeters), FMath::FloorToInt(EngineY / RegionMeters)));
	const FBatch* HereBatch = Batches.Find(Here);
	const bool bWasReady = bNearReady;
	bNearReady = HereRegion != nullptr && (HereRegion->bNoFile || (HereRegion->Data.IsValid() && *HereRegion->bRead
		&& (!HereRegion->Data->Batches.Contains(Here) || (HereBatch != nullptr && HereBatch->bBuilt))));
	if (bNearReady && !bWasReady)
	{
		UE_LOG(LogSwcCollision, Log, TEXT("collision: the batch of the character is ready (%d instances)"), HereBatch != nullptr ? HereBatch->Instances : 0);
	}
	// The batches out of the range go away.
	for (auto It = Batches.CreateIterator(); It; ++It)
	{
		const FVector Center((It.Key().X + 0.5) * BatchCm, (It.Key().Y + 0.5) * BatchCm, View.Z);
		if (FVector::Dist2D(Center, View) > DropCm + BatchCm * 0.71)
		{
			for (const TWeakObjectPtr<UInstancedStaticMeshComponent>& Component : It.Value().Components)
			{
				if (Component.IsValid())
				{
					Component->DestroyComponent();
				}
			}
			It.RemoveCurrent();
		}
	}
}

void USwcCollisionChunks::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bEnabled)
	{
		return;
	}
	// The place of the character: during a loading screen the place where it will be (the loading gate), else its pawn.
	FVector Place;
	if (!SwcLoadingGate::GetPlace(GetWorld(), Place))
	{
		const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
		if (Controller == nullptr || Controller->GetPawn() == nullptr)
		{
			return;
		}
		Place = Controller->GetPawn()->GetActorLocation();
	}
	const FIntPoint Here(FMath::FloorToInt(Place.X / BatchCm), FMath::FloorToInt(Place.Y / BatchCm));
	const double Now = FPlatformTime::Seconds();
	if (Now < NextUpdate && bNearReady && Here == LastHere)
	{
		return;
	}
	LastHere = Here;
	NextUpdate = Now + UpdateSeconds;
	Update(Place);
	if (Now >= NextReport)
	{
		NextReport = Now + 10.0;
		int32 Instances = 0;
		int32 Components = 0;
		for (const TPair<FIntPoint, FBatch>& Pair : Batches)
		{
			Instances += Pair.Value.Instances;
			Components += Pair.Value.Components.Num();
		}
		UE_LOG(LogSwcCollision, Log, TEXT("collision: %d batches, %d components, %d instances of the collision chunks"), Batches.Num(), Components, Instances);
	}
}
