#include "World/SwcSurfaceLayer.h"

#include "World/SwcLoadingGate.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcSurface, Log, All);

namespace
{
/** The road meshes of the cells within this range of the view (the loading range of the cells of the level). */
constexpr double RoadRangeCm = 102400.0;
/** A decal with no view distance in the source shows within this range. */
constexpr double DefaultDecalRangeCm = 10000.0;
/** No decal shows farther than this range (the source: Max View Distance is 125 m or less in the scanned slices). */
constexpr double MaxDecalRangeCm = 30000.0;
/** A region side (engine 2,048 m) in Unreal cm; region r_+X_+Y covers Unreal X [X, X + 1) and Y (-(Y + 1), -Y] sides. */
constexpr double RegionCm = 204800.0;
constexpr double UpdateSeconds = 0.5;

/** 1: the layer shows its road meshes and decals; 0: it removes them (a cost check in one run: tools/match-view.sh
 * --variant VIEW@off=swc.SurfaceLayer 0). Only when the layer is on (-SwcSurfaceLayer). */
TAutoConsoleVariable<int32> CVarSurfaceLayer(TEXT("swc.SurfaceLayer"), 1,
	TEXT("The road and decal layer of the materials track: 1 shows it, 0 removes the road meshes and the decals."));

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
	return FPaths::Combine(Content, TEXT("decals"));
}

FVector ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object->TryGetArrayField(Field, Values) || Values->Num() < 3)
	{
		return FVector::ZeroVector;
	}
	return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
}
} // namespace

bool USwcSurfaceLayer::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcSurfaceLayer::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcSurfaceLayer, STATGROUP_Tickables);
}

void USwcSurfaceLayer::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	// On by default (the M5 cost check: at most about 0.3 ms of GPU time at Oxboro, docs/MATERIALS.md); -SwcNoSurfaceLayer
	// or SWC_SURFACE_LAYER=0 turns it off (-SwcSurfaceLayer stays accepted). swc.SurfaceLayer 0 hides it in a run.
	bEnabled = !FParse::Param(FCommandLine::Get(), TEXT("SwcNoSurfaceLayer"))
		&& FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_SURFACE_LAYER")) != TEXT("0");
	Folder = ContentFolder();
	UE_LOG(LogSwcSurface, Log, TEXT("surface: layer %s (%s)"), bEnabled ? TEXT("on") : TEXT("off"), *Folder);
	if (bEnabled)
	{
		// The loading gate (docs/WORLD.md "The loading"): the roads and the decals in range of the place of a loading screen
		// are shown before it ends.
		SwcLoadingGate::Register(&World, TEXT("surface"), [this](const FVector& Place, FString& Why)
		{
			int32 Roads = 0;
			int32 Decals = 0;
			int32 Reads = 0;
			for (const TPair<FIntPoint, FRegion>& Pair : Regions)
			{
				Reads += Pair.Value.Parsed.IsValid() ? 1 : 0;
				for (const FRoad& Road : Pair.Value.Roads)
				{
					// The roads near the character (a road mesh is a chunk of its region: its center within twice the near radius).
					Roads += !Road.Component.IsValid() && FVector::Dist2D(Road.Center, Place) < 2.0 * SwcLoadingGate::NearCm;
				}
				for (const FDecal& Decal : Pair.Value.Decals)
				{
					Decals += !Decal.Component.IsValid() && FVector::Dist(Decal.Transform.GetLocation(), Place) < FMath::Min(Decal.RangeCm, SwcLoadingGate::NearCm);
				}
			}
			Why = FString::Printf(TEXT("%d road(s), %d decal(s) to show, %d region file(s) to read"), Roads, Decals, Reads);
			return Roads == 0 && Decals == 0 && Reads == 0 && UpdatedAt.Equals(Place, 1.0);
		});
	}
}

void USwcSurfaceLayer::Deinitialize()
{
	SwcLoadingGate::Unregister(GetWorld(), TEXT("surface"));
	Super::Deinitialize();
}

AActor* USwcSurfaceLayer::OwnerActor()
{
	if (AActor* Actor = Owner.Get())
	{
		return Actor;
	}
	FActorSpawnParameters Parameters;
	Parameters.Name = MakeUniqueObjectName(GetWorld(), AActor::StaticClass(), TEXT("SwcSurfaceLayer"));
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

bool USwcSurfaceLayer::ReadRegion(const FString& RegionsFolder, const FIntPoint& Key, FParsed& Region)
{
	const FString Name = FString::Printf(TEXT("r_+%02d_+%02d"), Key.X, Key.Y);
	const FString Path = FPaths::Combine(RegionsFolder, Name, TEXT("layer.json"));
	FString Text;
	TSharedPtr<FJsonObject> Layer;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layer)
		|| !Layer.IsValid())
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : Layer->GetArrayField(TEXT("roads")))
	{
		const TSharedPtr<FJsonObject> Road = Value->AsObject();
		FRoad Entry;
		// The asset of the road mesh: Interchange puts the mesh of the glTF FOLDER/STEM.gltf (its node STEM) into
		// FOLDER/STEM/StaticMeshes/STEM (Scripts/import_surface_layer.py).
		const FString Stem = FPaths::GetBaseFilename(Road->GetStringField(TEXT("file")));
		Entry.Mesh = FSoftObjectPath(FString::Printf(TEXT("%s/%s/StaticMeshes/%s.%s"), *Road->GetStringField(TEXT("unreal")),
			*Stem, *Stem, *Stem));
		const FVector Origin = ReadVector(Road, TEXT("origin m"));
		Entry.Location = FVector(Origin.X * 100.0, -Origin.Y * 100.0, Origin.Z * 100.0);   // engine m -> Unreal cm
		Entry.Center = Entry.Location + FVector(12800.0, -12800.0, 0.0);
		Entry.SortPriority = static_cast<int32>(Road->GetNumberField(TEXT("sort priority")));
		Region.Roads.Add(MoveTemp(Entry));
	}
	TArray<FSoftObjectPath> Materials;
	TSet<FString> Seen;
	for (const TSharedPtr<FJsonValue>& Value : Layer->GetArrayField(TEXT("decals")))
	{
		const TSharedPtr<FJsonObject> Decal = Value->AsObject();
		FDecal Entry;
		const FString MaterialKey = Decal->GetStringField(TEXT("material"));
		Entry.Material = FSoftObjectPath(FString::Printf(TEXT("/SwcGameContent/Materials/%s.%s"), *MaterialKey, *MaterialKey));
		if (!Seen.Contains(MaterialKey))
		{
			Seen.Add(MaterialKey);
			Materials.Add(Entry.Material);
		}
		const TArray<TSharedPtr<FJsonValue>>& Rotation = Decal->GetArrayField(TEXT("rotation"));
		const FQuat Quat(Rotation[0]->AsNumber(), Rotation[1]->AsNumber(), Rotation[2]->AsNumber(), Rotation[3]->AsNumber());
		Entry.Transform = FTransform(Quat, ReadVector(Decal, TEXT("location")));
		Entry.Size = ReadVector(Decal, TEXT("size"));
		Entry.SortPriority = static_cast<int32>(Decal->GetNumberField(TEXT("sort priority")));
		// The Color and the Opacity of the DecalConfiguration (the source multiplies the decal alpha by them:
		// deferredshading.cfxb DecalPassPS, g_DecalDiffuse).
		const TArray<TSharedPtr<FJsonValue>>* Color = nullptr;
		if (Decal->TryGetArrayField(TEXT("color"), Color) && Color->Num() >= 4)
		{
			Entry.Color = FLinearColor((*Color)[0]->AsNumber(), (*Color)[1]->AsNumber(), (*Color)[2]->AsNumber(),
				(*Color)[3]->AsNumber());
		}
		double Opacity = 1.0;
		if (Decal->TryGetNumberField(TEXT("opacity"), Opacity))
		{
			Entry.Color.A *= static_cast<float>(Opacity);
		}
		double FadeM = 0.0;
		Entry.RangeCm = Decal->TryGetNumberField(TEXT("fade m"), FadeM) && FadeM > 0.0
			? FMath::Min(FadeM * 100.0, MaxDecalRangeCm) : DefaultDecalRangeCm;
		Region.Decals.Add(MoveTemp(Entry));
	}
	Region.Materials = MoveTemp(Materials);
	UE_LOG(LogSwcSurface, Log, TEXT("surface: region %s: %d road meshes, %d decals, %d decal materials"), *Name,
		Region.Roads.Num(), Region.Decals.Num(), Region.Materials.Num());
	return true;
}

void USwcSurfaceLayer::Update(const FVector& View)
{
	// The regions within the road range of the view (a region is read once; one with no layer file stays empty).
	const int32 MinX = FMath::FloorToInt((View.X - RoadRangeCm) / RegionCm);
	const int32 MaxX = FMath::FloorToInt((View.X + RoadRangeCm) / RegionCm);
	const int32 MinY = FMath::FloorToInt((-View.Y - RoadRangeCm) / RegionCm);
	const int32 MaxY = FMath::FloorToInt((-View.Y + RoadRangeCm) / RegionCm);
	for (int32 X = MinX; X <= MaxX; ++X)
	{
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			if (X < 0 || Y < 0)
			{
				continue;
			}
			FRegion& Region = Regions.FindOrAdd(FIntPoint(X, Y));
			if (!Region.bRead)
			{
				// The file of a region is read on a worker (AST-273 P8: four reads of 0.1 to 0.4 s each froze the frame of a travel and
				// of a region border); its data comes into the region in a later update.
				Region.bRead = true;
				Region.Parsed = MakeShared<FParsed, ESPMode::ThreadSafe>();
				Async(EAsyncExecution::ThreadPool, [Parsed = Region.Parsed, Path = Folder, Key = FIntPoint(X, Y)]()
				{
					ReadRegion(Path, Key, *Parsed);
					Parsed->bDone = true;
				});
			}
		}
	}
	for (TPair<FIntPoint, FRegion>& Pair : Regions)
	{
		FRegion& Region = Pair.Value;
		if (Region.Parsed.IsValid() && Region.Parsed->bDone)
		{
			Region.Roads = MoveTemp(Region.Parsed->Roads);
			Region.Decals = MoveTemp(Region.Parsed->Decals);
			if (!Region.Parsed->Materials.IsEmpty())
			{
				Region.Materials = UAssetManager::GetStreamableManager().RequestAsyncLoad(Region.Parsed->Materials);
			}
			Region.Parsed.Reset();
		}
	}
	AActor* Actor = OwnerActor();
	if (Actor == nullptr)
	{
		return;
	}
	RoadsShown = 0;
	DecalsShown = 0;
	const bool bShow = CVarSurfaceLayer.GetValueOnGameThread() != 0;
	for (TPair<FIntPoint, FRegion>& Pair : Regions)
	{
		FRegion& Region = Pair.Value;
		for (FRoad& Road : Region.Roads)
		{
			const bool bNear = bShow && FVector::Dist2D(Road.Center, View) < RoadRangeCm;
			UStaticMeshComponent* Component = Road.Component.Get();
			if (!bNear)
			{
				if (Component)
				{
					Component->DestroyComponent();
					Road.Component.Reset();
				}
				Road.Loading.Reset();
				continue;
			}
			if (Component)
			{
				++RoadsShown;
				continue;
			}
			UStaticMesh* Mesh = Cast<UStaticMesh>(Road.Mesh.ResolveObject());
			if (Mesh == nullptr)
			{
				if (!Road.Loading.IsValid())
				{
					Road.Loading = UAssetManager::GetStreamableManager().RequestAsyncLoad(Road.Mesh);
				}
				continue;
			}
			UStaticMeshComponent* New = NewObject<UStaticMeshComponent>(Actor);
			New->SetMobility(EComponentMobility::Movable);
			New->SetStaticMesh(Mesh);
			New->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			New->SetCastShadow(false);
			New->bReceivesDecals = false;
			New->TranslucencySortPriority = Road.SortPriority;   // the order of the mesh decals (the source sort priority)
			New->SetWorldLocation(Road.Location);
			New->RegisterComponent();
			Road.Component = New;
			++RoadsShown;
		}
		for (FDecal& Decal : Region.Decals)
		{
			const bool bNear = bShow && FVector::Dist(Decal.Transform.GetLocation(), View) < Decal.RangeCm;
			UDecalComponent* Component = Decal.Component.Get();
			if (!bNear)
			{
				if (Component)
				{
					Component->DestroyComponent();
					Decal.Component.Reset();
				}
				continue;
			}
			if (Component)
			{
				++DecalsShown;
				continue;
			}
			UMaterialInterface* Material = Cast<UMaterialInterface>(Decal.Material.ResolveObject());
			if (Material == nullptr)
			{
				continue;   // the async load of the materials of the region is not complete
			}
			UDecalComponent* New = NewObject<UDecalComponent>(Actor);
			New->SetDecalMaterial(Material);
			New->DecalSize = Decal.Size;
			New->SortOrder = Decal.SortPriority;
			New->SetDecalColor(Decal.Color);
			New->SetWorldTransform(Decal.Transform);
			New->RegisterComponent();
			Decal.Component = New;
			++DecalsShown;
		}
	}
}

void USwcSurfaceLayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bEnabled)
	{
		return;
	}
	// The loading gate: during a loading screen the layer builds at the place of the character in each frame.
	FVector Place;
	const bool bScreen = SwcLoadingGate::GetPlace(GetWorld(), Place);
	const double Now = FPlatformTime::Seconds();
	if ((!bScreen && Now < NextUpdate) || (bScreen && !SwcLoadingGate::IsSettled(GetWorld())))
	{
		return;
	}
	NextUpdate = Now + UpdateSeconds;
	FVector View;
	if (!SwcLoadingGate::GetView(GetWorld(), View))
	{
		return;
	}
	const int32 Roads = RoadsShown;
	const int32 Decals = DecalsShown;
	Update(View);
	UpdatedAt = View;
	if (Roads != RoadsShown || Decals != DecalsShown)
	{
		UE_LOG(LogSwcSurface, Log, TEXT("surface: %d road meshes, %d decals near the view"), RoadsShown, DecalsShown);
	}
}
