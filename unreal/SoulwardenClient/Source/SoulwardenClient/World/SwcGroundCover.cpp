#include "World/SwcGroundCover.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/SwcLoadingGate.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcCover, Log, All);

namespace
{
constexpr int32 Tiles = 128;              // tiles of a region side (region.vegetation: 16 m tiles, 2,048 m)
constexpr double TileM = 16.0;
constexpr int32 BatchTiles = 4;           // a batch: 4 x 4 tiles (64 m; research/terrain.md: 32 or 64 m batches)
constexpr double BatchM = TileM * BatchTiles;
constexpr int32 BatchesPerRegion = Tiles / BatchTiles;
constexpr double RegionM = 2048.0;
constexpr double PackedToM = 2048.0 / 65535.0;   // the x, y, and z of a record
constexpr double UpdateSeconds = 0.25;
constexpr double BuildBudgetSeconds = 0.004;   // the batches of one update in play: the nearest always, more within 4 ms (the
                                               // loading work: two batches in one update made frames of up to 2.4 s)
constexpr double ScreenBudgetSeconds = 0.05;   // during a loading screen: each frame, 50 ms (the cover near the place is
                                               // built before the screen ends: the loading gate)
constexpr double ScreenMarginM = 16.0;         // during a screen the reach is this much more: the camera arm and the first steps
                                               // after the screen (a travel built one batch at 100 to 142 m in the first 0.6 s)

TAutoConsoleVariable<int32> CVarShow(TEXT("swc.GroundCover.Show"), 1,
	TEXT("1: the ground cover is visible; 0: hidden, the batches stay (a matched view with and without the cover: "
		 "tools/match-view.sh --variant VIEW@nocover=\"swc.GroundCover.Show 0\")."));
// Test switches of the cover color check (docs/WORLD.md, "The T3 check"): the cover casts the sun shadows (the source
// vegetation has a shadow pass, vegetation.cfxb ShadowGen), and a scale of the back light (BackDiffuseMultiplier of
// M_SwcFoliage through dynamic instances; 1 = the instances as they are).
TAutoConsoleVariable<int32> CVarShadow(TEXT("swc.GroundCover.Shadow"), 0,
	TEXT("1: the ground cover casts shadows; 0: no shadows (the default until stage T4)."));
TAutoConsoleVariable<float> CVarBackLight(TEXT("swc.GroundCover.BackLight"), 1.0f,
	TEXT("A scale of BackDiffuseMultiplier of the cover materials (a test of the back light; 1: the instances as they are)."));
TAutoConsoleVariable<int32> CVarBlockers(TEXT("swc.GroundCover.Blockers"), 1,
	TEXT("1: a record of a slot that a vegetation blocker of the level claims does not draw (the claims of cover.bin, research/"
		 "ground-cover.md); 0: all records (a before and after test; the batches build again)."));

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

struct FReader
{
	const TArray<uint8>& Bytes;
	int64 Offset = 0;
	bool bFailed = false;

	template <typename T>
	T Read()
	{
		T Value{};
		if (Offset + static_cast<int64>(sizeof(T)) > Bytes.Num())
		{
			bFailed = true;
			return Value;
		}
		FMemory::Memcpy(&Value, Bytes.GetData() + Offset, sizeof(T));
		Offset += sizeof(T);
		return Value;
	}

	FString Text()
	{
		const uint16 Length = Read<uint16>();
		if (bFailed || Offset + Length > Bytes.Num())
		{
			bFailed = true;
			return FString();
		}
		FString Out = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Offset), Length));
		Offset += Length;
		return Out;
	}
};

/** The Unreal transform of one 16-byte record of a region (research/terrain.md, section 3). The rotation of the engine
 *  space (X east, Y north, Z up) in Unreal space is S R S with S = diag(1, -1, 1): the quaternion (-x, y, -z, w), as the
 *  cells of the level (converter/swc_convert/cells.py). */
FTransform RecordTransform(const uint8* Record, const FIntPoint& Region)
{
	uint32 Word;
	uint16 X, Y, Z;
	FMemory::Memcpy(&Word, Record, 4);
	FMemory::Memcpy(&X, Record + 4, 2);
	FMemory::Memcpy(&Y, Record + 6, 2);
	FMemory::Memcpy(&Z, Record + 8, 2);
	const uint8 Scale = Record[10];
	const double Qz = static_cast<double>(Word & 0x7FF) / 511.0 - 1.0;
	const double Qy = static_cast<double>((Word >> 11) & 0x3FF) / 255.0 - 1.0;
	const double Qx = static_cast<double>((Word >> 21) & 0x3FF) / 255.0 - 1.0;
	const double Qw = FMath::Sqrt(FMath::Max(0.0, 1.0 - FMath::Min(1.0, Qx * Qx + Qy * Qy + Qz * Qz))) * ((Word >> 31) ? -1.0 : 1.0);
	FQuat Rotation(-Qx, Qy, -Qz, Qw);
	Rotation.Normalize();
	const double EngineX = Region.X * RegionM + X * PackedToM;
	const double EngineY = Region.Y * RegionM + Y * PackedToM;
	const double EngineZ = Z * PackedToM;
	return FTransform(Rotation, FVector(EngineX * 100.0, -EngineY * 100.0, EngineZ * 100.0), FVector(Scale / 100.0));
}
} // namespace

bool USwcGroundCover::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcGroundCover::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcGroundCover, STATGROUP_Tickables);
}

void USwcGroundCover::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	// On by default (stage T3, 2026-10-19): -SwcNoGroundCover or SWC_GROUND_COVER=0 turns it off.
	bEnabled = !FParse::Param(FCommandLine::Get(), TEXT("SwcNoGroundCover"))
		&& FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_GROUND_COVER")) != TEXT("0");
	double RangeM = 100.0;
	FParse::Value(FCommandLine::Get(), TEXT("SwcCoverRange="), RangeM);
	RangeCm = FMath::Clamp(RangeM, 16.0, 1000.0) * 100.0;
	Folder = ContentFolder();
	UE_LOG(LogSwcCover, Log, TEXT("cover: ground cover %s, range %.0f m (%s)"), bEnabled ? TEXT("on") : TEXT("off"), RangeCm / 100.0, *Folder);
	if (bEnabled)
	{
		// The loading gate: the batches within the range of the place of a loading screen are built before it ends.
		SwcLoadingGate::Register(&World, TEXT("cover"), [this](const FVector& Place, FString& Why)
		{
			TArray<TPair<double, FIntPoint>> Wanted;
			WantedBatches(Place, ScreenMarginM, Wanted);
			Why = FString::Printf(TEXT("%d batch(es) to build"), Wanted.Num());
			return Wanted.IsEmpty();
		});
	}
}

void USwcGroundCover::SetRegionsFolder(const FString& RegionsFolder, const TOptional<FVector>& InFixedView)
{
	const FString Wanted = RegionsFolder.IsEmpty() ? ContentFolder() : RegionsFolder;
	FixedView = InFixedView;
	if (Wanted == Folder)
	{
		return;
	}
	for (TPair<FIntPoint, FBatch>& Batch : Batches)
	{
		for (const TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>& Component : Batch.Value.Components)
		{
			if (Component.IsValid())
			{
				Component->DestroyComponent();
			}
		}
	}
	Batches.Reset();
	Regions.Reset();
	Folder = Wanted;
	NextUpdate = 0.0;
	UE_LOG(LogSwcCover, Log, TEXT("cover: the regions of %s"), *Folder);
}

void USwcGroundCover::Deinitialize()
{
	SwcLoadingGate::Unregister(GetWorld(), TEXT("cover"));
	Super::Deinitialize();
}

void USwcGroundCover::WantedBatches(const FVector& View, double MarginM, TArray<TPair<double, FIntPoint>>& Wanted) const
{
	const double ViewX = View.X / 100.0;
	const double ViewY = -View.Y / 100.0;
	const double Reach = RangeCm / 100.0 + BatchM * 0.75 + MarginM;   // the batch center can be 0.71 batch from its farthest point
	const int32 MinX = FMath::FloorToInt((ViewX - Reach) / BatchM);
	const int32 MaxX = FMath::FloorToInt((ViewX + Reach) / BatchM);
	const int32 MinY = FMath::FloorToInt((ViewY - Reach) / BatchM);
	const int32 MaxY = FMath::FloorToInt((ViewY + Reach) / BatchM);
	for (int32 X = MinX; X <= MaxX; ++X)
	{
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			const double Distance = FMath::Sqrt(FMath::Square((X + 0.5) * BatchM - ViewX) + FMath::Square((Y + 0.5) * BatchM - ViewY));
			if (Distance <= Reach && !Batches.Contains(FIntPoint(X, Y)))
			{
				Wanted.Emplace(Distance, FIntPoint(X, Y));
			}
		}
	}
	Wanted.Sort([](const TPair<double, FIntPoint>& A, const TPair<double, FIntPoint>& B) { return A.Key < B.Key; });
}

AActor* USwcGroundCover::OwnerActor()
{
	if (AActor* Actor = Owner.Get())
	{
		return Actor;
	}
	FActorSpawnParameters Parameters;
	Parameters.Name = MakeUniqueObjectName(GetWorld(), AActor::StaticClass(), TEXT("SwcGroundCover"));
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

bool USwcGroundCover::ReadRegionFile(const FString& Path, FRegionData& Data)
{
	if (!FFileHelper::LoadFileToArray(Data.Bytes, *Path) || Data.Bytes.Num() < 12 || FMemory::Memcmp(Data.Bytes.GetData(), "SWCV", 4) != 0)
	{
		return false;
	}
	FReader Reader{Data.Bytes, 4};
	if (Reader.Read<uint32>() != 1)
	{
		return false;
	}
	const uint32 Count = Reader.Read<uint32>();
	for (uint32 Index = 0; Index < Count && !Reader.bFailed; ++Index)
	{
		FDescriptor& Descriptor = Data.Descriptors.AddDefaulted_GetRef();
		const FString Mesh = Reader.Text();
		if (!Mesh.IsEmpty())
		{
			Descriptor.Mesh = FSoftObjectPath(Mesh);
		}
		const uint16 Slots = Reader.Read<uint16>();
		for (uint16 Slot = 0; Slot < Slots && !Reader.bFailed; ++Slot)
		{
			const FString Name = Reader.Text();
			const FString Key = Reader.Text();
			// import_content.py: the material instance of a material record is /SwcGameContent/Materials/KEY.
			Descriptor.Slots.Emplace(FName(*Name), FSoftObjectPath(FString::Printf(TEXT("/SwcGameContent/Materials/%s.%s"), *Key, *Key)));
		}
		Descriptor.Bending = Reader.Read<float>();
		Reader.Read<uint8>();   // AutoMerge (not used yet)
	}
	const uint64 Length = Reader.Read<uint64>();
	Data.Source = Reader.Offset;
	if (Reader.bFailed || Data.Source + static_cast<int64>(Length) > Data.Bytes.Num() || Length < 8 + Tiles * Tiles * 4)
	{
		return false;
	}
	Data.TileOffsets.SetNumUninitialized(Tiles * Tiles);
	FMemory::Memcpy(Data.TileOffsets.GetData(), Data.Bytes.GetData() + Data.Source + 8, Tiles * Tiles * 4);
	// After the source bytes (a file of 2026-10-20 or later): the sample slots that the vegetation blockers of the level claim
	// (converter cover.py and cover_areas.py; research/ground-cover.md). A record draws only when the slot of its ordinal is free.
	Data.ClaimOffsets.Init(-1, Tiles * Tiles);
	if (Data.Source + static_cast<int64>(Length) + 4 <= Data.Bytes.Num())
	{
		Reader.Offset = Data.Source + static_cast<int64>(Length);
		const uint32 Claims = Reader.Read<uint32>();
		for (uint32 Claim = 0; Claim < Claims && !Reader.bFailed; ++Claim)
		{
			const uint16 Tile = Reader.Read<uint16>();
			if (Tile >= Tiles * Tiles || Reader.Offset + 128 > Data.Bytes.Num())
			{
				return false;
			}
			Data.ClaimOffsets[Tile] = static_cast<int32>(Reader.Offset);
			Reader.Offset += 128;
		}
		if (Reader.bFailed)
		{
			return false;
		}
	}
	return true;
}

USwcGroundCover::FRegion& USwcGroundCover::RegionAt(const FIntPoint& Key)
{
	FRegion& Region = Regions.FindOrAdd(Key);
	if (!Region.bStarted)
	{
		Region.bStarted = true;
		const FString Path = FPaths::Combine(Folder, FString::Printf(TEXT("r_+%02d_+%02d"), Key.X, Key.Y), TEXT("cover.bin"));
		if (!FPaths::FileExists(Path))
		{
			Region.bNoFile = true;
			return Region;
		}
		Region.Data = MakeShared<FRegionData, ESPMode::ThreadSafe>();
		Region.bRead = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
		TSharedPtr<FRegionData, ESPMode::ThreadSafe> Data = Region.Data;
		TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> bRead = Region.bRead;
		Async(EAsyncExecution::ThreadPool, [Path, Data, bRead, Key]()
		{
			const double Start = FPlatformTime::Seconds();
			const bool bOk = ReadRegionFile(Path, *Data);
			if (!bOk)
			{
				Data->Descriptors.Reset();
				Data->TileOffsets.Reset();
			}
			UE_LOG(LogSwcCover, Log, TEXT("cover: region r_+%02d_+%02d read: %s, %d descriptors, %.1f MB, %.2f s"), Key.X, Key.Y,
				bOk ? TEXT("ok") : TEXT("failed"), Data->Descriptors.Num(), Data->Bytes.Num() / 1048576.0, FPlatformTime::Seconds() - Start);
			*bRead = true;
		});
	}
	else if (Region.Data.IsValid() && *Region.bRead && !Region.Assets.IsValid() && !Region.Data->TileOffsets.IsEmpty())
	{
		TArray<FSoftObjectPath> Paths;
		for (const FDescriptor& Descriptor : Region.Data->Descriptors)
		{
			if (Descriptor.Mesh.IsValid())
			{
				Paths.AddUnique(Descriptor.Mesh);
				for (const TPair<FName, FSoftObjectPath>& Slot : Descriptor.Slots)
				{
					Paths.AddUnique(Slot.Value);
				}
			}
		}
		if (Paths.IsEmpty())
		{
			// No mesh in the region (r_+00_+00: the ocean): no request, no batch (a request with no asset gives no handle).
			Region.bNoFile = true;
			return Region;
		}
		Region.Assets = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths);
		UE_LOG(LogSwcCover, Log, TEXT("cover: region r_+%02d_+%02d: %d meshes and materials load"), Key.X, Key.Y, Paths.Num());
	}
	return Region;
}

bool USwcGroundCover::BuildBatch(const FIntPoint& Batch, FBatch& Out)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SwcGroundCover_BuildBatch);
	const FIntPoint RegionKey(FMath::FloorToInt(static_cast<double>(Batch.X) / BatchesPerRegion),
		FMath::FloorToInt(static_cast<double>(Batch.Y) / BatchesPerRegion));
	if (RegionKey.X < 0 || RegionKey.Y < 0)
	{
		return true;   // no region: an empty batch
	}
	FRegion& Region = RegionAt(RegionKey);
	if (Region.bNoFile || (Region.Data.IsValid() && *Region.bRead && Region.Data->TileOffsets.IsEmpty()))
	{
		return true;   // no cover file, or a file that does not read: an empty batch
	}
	if (!Region.Data.IsValid() || !*Region.bRead || !Region.Assets.IsValid() || !Region.Assets->HasLoadCompleted())
	{
		return false;   // later
	}
	AActor* Actor = OwnerActor();
	if (Actor == nullptr)
	{
		return false;
	}
	const FRegionData& Data = *Region.Data;
	const int32 FirstX = (Batch.X - RegionKey.X * BatchesPerRegion) * BatchTiles;
	const int32 FirstY = (Batch.Y - RegionKey.Y * BatchesPerRegion) * BatchTiles;
	TMap<int32, TArray<FTransform>> ByDescriptor;
	for (int32 TileY = FirstY; TileY < FirstY + BatchTiles; ++TileY)
	{
		for (int32 TileX = FirstX; TileX < FirstX + BatchTiles; ++TileX)
		{
			// The tile index: row (16 m steps north from the south edge of the region) x 128 + column (measured on Oxboro).
			int64 Offset = Data.Source + Data.TileOffsets[TileY * Tiles + TileX];
			if (Offset + 2 > Data.Bytes.Num())
			{
				continue;
			}
			uint16 Groups;
			FMemory::Memcpy(&Groups, Data.Bytes.GetData() + Offset, 2);
			Offset += 2;
			for (uint16 Group = 0; Group < Groups; ++Group)
			{
				uint16 Descriptor, Count;
				FMemory::Memcpy(&Descriptor, Data.Bytes.GetData() + Offset, 2);
				FMemory::Memcpy(&Count, Data.Bytes.GetData() + Offset + 2, 2);
				Offset += 4;
				if (Offset + 16 * static_cast<int64>(Count) > Data.Bytes.Num())
				{
					break;
				}
				if (Descriptor < Data.Descriptors.Num() && Data.Descriptors[Descriptor].Mesh.IsValid())
				{
					TArray<FTransform>& Transforms = ByDescriptor.FindOrAdd(Descriptor);
					const int32 Claims = Data.ClaimOffsets.IsEmpty() ? -1 : Data.ClaimOffsets[TileY * Tiles + TileX];
					for (uint16 Record = 0; Record < Count; ++Record)
					{
						const uint8* Bytes = Data.Bytes.GetData() + Offset + 16 * Record;
						const uint16 Ordinal = static_cast<uint16>(Bytes[12] | (Bytes[13] << 8));   // the sample slot of the tile
						if (bBlockers && Claims >= 0 && Ordinal < 1024 && (Data.Bytes[Claims + Ordinal / 8] & (1 << (Ordinal % 8))) != 0)
						{
							++ClaimedRecords;
							continue;   // a blocker of the level has this slot
						}
						Transforms.Add(RecordTransform(Bytes, RegionKey));
					}
				}
				Offset += 16 * static_cast<int64>(Count);
			}
		}
	}
	for (TPair<int32, TArray<FTransform>>& Pair : ByDescriptor)
	{
		const FDescriptor& Descriptor = Data.Descriptors[Pair.Key];
		UStaticMesh* Mesh = Cast<UStaticMesh>(Descriptor.Mesh.ResolveObject());
		if (Mesh == nullptr)
		{
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(bShadow);   // off until T4 (swc.GroundCover.Shadow)
		Component->InstanceEndCullDistance = static_cast<int32>(RangeCm);
		Component->SetDefaultCustomPrimitiveDataFloat(0, Descriptor.Bending);   // "SwcWind" (E1)
		static const IConsoleVariable* WindEvaluate = IConsoleManager::Get().FindConsoleVariable(TEXT("swc.Wind.Evaluate"));
		if (Descriptor.Bending > 0.0f && WindEvaluate != nullptr && WindEvaluate->GetInt() == 0)
		{
			Component->SetEvaluateWorldPositionOffset(false);   // the cost test of the wind (swc.Wind.Evaluate)
		}
		for (const TPair<FName, FSoftObjectPath>& Slot : Descriptor.Slots)
		{
			const int32 Index = Mesh->GetMaterialIndex(Slot.Key);
			if (UMaterialInterface* Material = Cast<UMaterialInterface>(Slot.Value.ResolveObject()); Material && Index != INDEX_NONE)
			{
				Component->SetMaterial(Index, Material);
			}
		}
		ApplyBackLight(Component);
		Component->SetupAttachment(Actor->GetRootComponent());
		Component->SetVisibility(bShown);
		Component->AddInstances(Pair.Value, false, true, false);
		Component->RegisterComponent();
		Out.Components.Add(Component);
		Out.Instances += Pair.Value.Num();
	}
	return true;
}

void USwcGroundCover::ApplyBackLight(UHierarchicalInstancedStaticMeshComponent* Component)
{
	for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
	{
		UMaterialInterface* Current = Component->GetMaterial(Index);
		UMaterialInstanceDynamic* Made = Cast<UMaterialInstanceDynamic>(Current);
		UMaterialInterface* Original = Made != nullptr && BackLightInstances.FindRef(Made->Parent) == Made ? Made->Parent.Get() : Current;
		if (Original == nullptr)
		{
			continue;
		}
		if (BackLight == 1.0f)
		{
			if (Original != Current)
			{
				Component->SetMaterial(Index, Original);
			}
			continue;
		}
		TObjectPtr<UMaterialInstanceDynamic>& Instance = BackLightInstances.FindOrAdd(Original);
		if (Instance == nullptr)
		{
			Instance = UMaterialInstanceDynamic::Create(Original, this);
		}
		float Base = 1.0f;
		Original->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("BackDiffuseMultiplier")), Base);
		Instance->SetScalarParameterValue(TEXT("BackDiffuseMultiplier"), Base * BackLight);
		Component->SetMaterial(Index, Instance);
	}
}

void USwcGroundCover::Update(const FVector& View, double MarginM, double Budget)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SwcGroundCover_Update);
	const double ViewX = View.X / 100.0;
	const double ViewY = -View.Y / 100.0;
	const double Range = RangeCm / 100.0;
	const double Reach = Range + BatchM * 0.75;   // the batch center can be 0.71 batch from its farthest point
	// Remove the batches beyond the range (with one batch of slack).
	for (auto It = Batches.CreateIterator(); It; ++It)
	{
		const double CenterX = (It.Key().X + 0.5) * BatchM;
		const double CenterY = (It.Key().Y + 0.5) * BatchM;
		if (FMath::Sqrt(FMath::Square(CenterX - ViewX) + FMath::Square(CenterY - ViewY)) > Reach + BatchM)
		{
			for (const TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>& Component : It.Value().Components)
			{
				if (Component.IsValid())
				{
					Component->DestroyComponent();
				}
			}
			It.RemoveCurrent();
		}
	}
	// Make the missing batches within the range, the nearest first.
	TArray<TPair<double, FIntPoint>> Wanted;
	WantedBatches(View, MarginM, Wanted);
	int32 Built = 0;
	const double Start = FPlatformTime::Seconds();
	for (const TPair<double, FIntPoint>& Item : Wanted)
	{
		if (Built > 0 && FPlatformTime::Seconds() - Start > Budget)
		{
			break;
		}
		const double BatchStart = FPlatformTime::Seconds();
		FBatch Batch;
		if (BuildBatch(Item.Value, Batch))
		{
			LongestBuild = FMath::Max(LongestBuild, FPlatformTime::Seconds() - BatchStart);
			Batches.Add(Item.Value, MoveTemp(Batch));
			++Built;
		}
	}
}

void USwcGroundCover::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bEnabled)
	{
		return;
	}
	if (const bool bShow = CVarShow.GetValueOnGameThread() != 0; bShow != bShown)
	{
		bShown = bShow;
		for (const TPair<FIntPoint, FBatch>& Pair : Batches)
		{
			for (const TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>& Component : Pair.Value.Components)
			{
				if (Component.IsValid())
				{
					Component->SetVisibility(bShown);
				}
			}
		}
		UE_LOG(LogSwcCover, Log, TEXT("cover: %s"), bShown ? TEXT("shown") : TEXT("hidden"));
	}
	if (const bool bWantBlockers = CVarBlockers.GetValueOnGameThread() != 0; bWantBlockers != bBlockers)
	{
		bBlockers = bWantBlockers;
		for (TPair<FIntPoint, FBatch>& Batch : Batches)
		{
			for (const TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>& Component : Batch.Value.Components)
			{
				if (Component.IsValid())
				{
					Component->DestroyComponent();
				}
			}
		}
		Batches.Reset();   // they build again with the new rule
		UE_LOG(LogSwcCover, Log, TEXT("cover: the vegetation blockers %s"), bBlockers ? TEXT("on") : TEXT("off"));
	}
	const bool bWantShadow = CVarShadow.GetValueOnGameThread() != 0;
	const float WantBackLight = CVarBackLight.GetValueOnGameThread();
	if (bWantShadow != bShadow || WantBackLight != BackLight)
	{
		const bool bBackChanged = WantBackLight != BackLight;
		bShadow = bWantShadow;
		BackLight = WantBackLight;
		for (const TPair<FIntPoint, FBatch>& Pair : Batches)
		{
			for (const TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>& Component : Pair.Value.Components)
			{
				if (Component.IsValid())
				{
					Component->SetCastShadow(bShadow);
					if (bBackChanged)
					{
						ApplyBackLight(Component.Get());
					}
				}
			}
		}
		UE_LOG(LogSwcCover, Log, TEXT("cover: shadows %d, back light x %.2f"), bShadow ? 1 : 0, BackLight);
	}
	// During a loading screen the cover builds at the place of the character (the loading gate) in each frame; in play it
	// builds near the camera each 0.25 s with a small budget.
	FVector Place;
	const bool bScreen = SwcLoadingGate::GetPlace(GetWorld(), Place);
	const double Now = FPlatformTime::Seconds();
	if (!bScreen && Now < NextUpdate)
	{
		return;
	}
	NextUpdate = Now + UpdateSeconds;
	FVector View;
	if (!SwcLoadingGate::GetView(GetWorld(), View))
	{
		if (!FixedView.IsSet())
		{
			return;   // no place and no character yet (the camera is at the origin of the world)
		}
		View = FixedView.GetValue();   // the menu scene
	}
	Update(View, bScreen ? ScreenMarginM : 0.0, bScreen ? ScreenBudgetSeconds : BuildBudgetSeconds);
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
		UE_LOG(LogSwcCover, Log, TEXT("cover: %d batches, %d components, %d instances; the longest batch build %.1f ms; %lld records of claimed slots left out since the start"),
			Batches.Num(), Components, Instances, LongestBuild * 1000.0, ClaimedRecords);
		LongestBuild = 0.0;
	}
}
