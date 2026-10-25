// The Unreal water (docs/WATER.md "The Unreal water"; decision of the user, 2026-10-25, AST-200): with -SwcWaterMode=Unreal the
// water subsystem spawns the bodies of the Unreal Water plugin from the source shapes (the specs of tools/convert-water.sh bodies,
// SWC_CONTENT/water/unreal-bodies.json): one water zone with local tessellation over the world regions, the ocean at the ocean level,
// and the rivers, lakes, and custom bodies near the view. The source surfaces, the ocean grid, and the sky cube do not show in this
// mode. The quadtree queries (the depth classes, the wade, the swim), the loading gate, and the shore foam decals do not change.

#include "Water/SwcWaterSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "Water/SwcWaterBodies.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyOceanComponent.h"
#include "WaterSplineComponent.h"
#include "WaterSplineMetadata.h"
#include "WaterWaves.h"
#include "WaterZoneActor.h"
#include "World/SwcLoadingGate.h"

namespace SwcUnrealWater
{
/** The materials of the plugin (the defaults of its editor settings, WaterEditorSettings.cpp). The names of the client instances
 *  in the folder of -SwcWaterMaterials (Scripts/water_unreal_materials.py) replace them when they exist. */
struct FMaterialSlot
{
	const TCHAR* Instance;
	const TCHAR* Plugin;
};
const FMaterialSlot SlotOcean{TEXT("MI_SwcWater_Ocean"), TEXT("/Water/Materials/WaterSurface/Water_Material_Ocean.Water_Material_Ocean")};
const FMaterialSlot SlotLake{TEXT("MI_SwcWater_Lake"), TEXT("/Water/Materials/WaterSurface/Water_Material_Lake.Water_Material_Lake")};
const FMaterialSlot SlotRiver{TEXT("MI_SwcWater_River"), TEXT("/Water/Materials/WaterSurface/Water_Material_River.Water_Material_River")};
const FMaterialSlot SlotCustom{TEXT("MI_SwcWater_Custom"), TEXT("/Water/Materials/WaterSurface/Water_Material_CustomMesh.Water_Material_CustomMesh")};
const FMaterialSlot SlotRiverToLake{TEXT("MI_SwcWater_RiverToLake"),
	TEXT("/Water/Materials/WaterSurface/Transitions/Water_Material_River_To_Lake_Transition.Water_Material_River_To_Lake_Transition")};
const FMaterialSlot SlotRiverToOcean{TEXT("MI_SwcWater_RiverToOcean"),
	TEXT("/Water/Materials/WaterSurface/Transitions/Water_Material_River_To_Ocean_Transition.Water_Material_River_To_Ocean_Transition")};
const FMaterialSlot SlotUnderwater{TEXT("MI_SwcWater_Underwater"),
	TEXT("/Water/Materials/PostProcessing/M_UnderWater_PostProcess_Volume.M_UnderWater_PostProcess_Volume")};
const FMaterialSlot SlotUnderwaterCustom{TEXT("MI_SwcWater_UnderwaterCustom"),
	TEXT("/Water/Materials/PostProcessing/MI_UnderWater_PostProcess_Volume_Custom.MI_UnderWater_PostProcess_Volume_Custom")};
const FMaterialSlot SlotFarMesh{TEXT("MI_SwcWater_FarMesh"), TEXT("/Water/Materials/WaterSurface/Water_FarMesh.Water_FarMesh")};
const TCHAR* WaterInfoPath = TEXT("/Water/Materials/WaterInfo/DrawWaterInfo.DrawWaterInfo");
const TCHAR* OceanWavesPath = TEXT("/Water/Waves/GerstnerWaves_Ocean.GerstnerWaves_Ocean");
const TCHAR* LakeWavesPath = TEXT("/Water/Waves/GerstnerWaves_Lake.GerstnerWaves_Lake");

/** A body spawns when the view is this near to it (cm), at most SpawnsPerTick in one tick (decisions of the client, as the surfaces of
 *  the source path: BuildRange, BuildsPerTick). */
constexpr double SpawnRange = 300000.0;
constexpr int32 SpawnsPerTick = 4;
/** The window of the local tessellation of the water zone (cm, the diameter; the water info texture and the water mesh follow the
 *  view in it): 4 km. The render target of 2048 texels gives about 2 m for each texel [decision of the client, to tune in stage 2]. */
constexpr double TessellationWindow = 400000.0;
constexpr int32 WaterInfoResolution = 2048;
/** The margin of the water zone and of the ocean around the world regions (cm). */
constexpr double ZoneMargin = 400000.0;
/** The depth of the ocean collision under the ocean level (cm): the underwater post process finds the camera in it. */
constexpr double OceanCollisionDepth = 20000.0;
/** The river depth where the quadtree has no water under a spline point and the river has no other depth (m) [decision of the client]. */
constexpr double DefaultRiverDepth = 1.5;
/** The move of the base normal sample of the source river shader (watervolume.cfx: B.xy = q Tilling - f t (0.25, 0), f = WaterFlowSpeed
 *  x FlowScale 0.25; q.x along in tiles of TileLength m): the speed of the pattern in m/s is WaterFlowSpeed x 0.25 x 0.25 x TileLength /
 *  Tilling [inference: the plugin velocity is the speed of its flow map along the spline]. */
constexpr double FlowScale = 0.25 * 0.25;

FVector ToUnreal(double X, double Y, double Z)
{
	return FVector(X * 100.0, -Y * 100.0, Z * 100.0);
}

/** @return An engine vector (m) in Unreal space (cm): the same mirror of y as the points. */
FVector VectorOf(const TSharedPtr<FJsonValue>& Value)
{
	const TArray<TSharedPtr<FJsonValue>>& Cells = Value->AsArray();
	return ToUnreal(Cells[0]->AsNumber(), Cells[1]->AsNumber(), Cells.Num() > 2 ? Cells[2]->AsNumber() : 0.0);
}

/** Sets a private setting of the water zone by its name (the local tessellation). */
bool SetBoolByName(UObject* Object, const TCHAR* Name, bool bValue)
{
	FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), FName(Name));
	UE_CLOG(Property == nullptr, LogSwcWater, Warning, TEXT("water (Unreal): no bool property %s in %s"), Name, *Object->GetClass()->GetName());
	if (Property != nullptr)
	{
		Property->SetPropertyValue_InContainer(Object, bValue);
	}
	return Property != nullptr;
}

/** Sets an FVector or FVector2D setting by its name (the water zone; the ocean extents, whose setters are editor only). */
template <typename T>
bool SetStructByName(UObject* Object, const TCHAR* Name, const T& Value)
{
	FStructProperty* Property = FindFProperty<FStructProperty>(Object->GetClass(), FName(Name));
	const bool bMatch = Property != nullptr && Property->Struct == TBaseStructure<T>::Get();
	UE_CLOG(!bMatch, LogSwcWater, Warning, TEXT("water (Unreal): no property %s of the right struct in %s"), Name, *Object->GetClass()->GetName());
	if (bMatch)
	{
		*Property->ContainerPtrToValuePtr<T>(Object) = Value;
	}
	return bMatch;
}

/** Makes all scene components of a spawned (deferred) body movable: the plugin makes render data after BeginPlay only for a movable
 *  component, and a static child cannot attach to a movable parent. */
void MakeMovable(AActor& Actor)
{
	TInlineComponentArray<USceneComponent*> Components(&Actor);
	for (USceneComponent* Component : Components)
	{
		Component->SetMobility(EComponentMobility::Movable);
	}
}
} // namespace SwcUnrealWater

UMaterialInterface* USwcWaterSubsystem::UnrealMaterial(const TCHAR* Instance, const TCHAR* PluginPath) const
{
	if (!UnrealMaterialFolder.IsEmpty())
	{
		if (UMaterialInterface* Own = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *UnrealMaterialFolder, Instance, Instance), nullptr,
				LOAD_NoWarn | LOAD_Quiet))
		{
			return Own;
		}
	}
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, PluginPath);
	UE_CLOG(Material == nullptr, LogSwcWater, Warning, TEXT("water (Unreal): no material %s (the Water plugin)"), PluginPath);
	return Material;
}

bool USwcWaterSubsystem::LoadUnreal(const FString& Folder)
{
	FString Path = FPaths::Combine(Folder, TEXT("unreal-bodies.json"));
	FParse::Value(FCommandLine::Get(), TEXT("-SwcWaterBodies="), Path);
	FParse::Value(FCommandLine::Get(), TEXT("-SwcWaterMaterials="), UnrealMaterialFolder);
	// -SwcWaterInfoMesh=Plugin: the water info meshes of the plugin (an editor run only); the default builds them in the client (the cooked
	// game has no builder of the plugin).
	FString InfoMesh;
	bPluginInfoMeshes = FParse::Value(FCommandLine::Get(), TEXT("-SwcWaterInfoMesh="), InfoMesh) && InfoMesh.Equals(TEXT("Plugin"), ESearchCase::IgnoreCase);
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		UE_LOG(LogSwcWater, Warning, TEXT("water (Unreal): no %s (tools/convert-water.sh bodies write): the source water path"), *Path);
		return false;
	}
	// The bodies of the specs; a custom body takes the surface mesh of the same region and index (the order of REGION.water).
	TMap<TPair<FString, int32>, int32> Surfaces;
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		Surfaces.Add({Bodies[Index].Region, Bodies[Index].RegionIndex}, Index);
	}
	int32 Counts[3] = {0, 0, 0};
	int32 Missing = 0;
	for (const auto& Pair : Doc->GetObjectField(TEXT("regions"))->Values)
	{
		const FString Region(Pair.Key);
		for (const TSharedPtr<FJsonValue>& Value : Pair.Value->AsArray())
		{
			FUnrealBody Body;
			Body.Spec = Value->AsObject();
			Body.Region = Region;
			Body.RegionIndex = static_cast<int32>(Body.Spec->GetNumberField(TEXT("index")));
			const FString Type = Body.Spec->GetStringField(TEXT("type"));
			const int32* Surface = Surfaces.Find({Region, Body.RegionIndex});
			if (Surface == nullptr || Bodies[*Surface].Body.name != TCHAR_TO_UTF8(*Body.Spec->GetStringField(TEXT("name"))))
			{
				++Missing;
				continue;
			}
			Body.Surface = *Surface;
			Body.Center = Bodies[*Surface].Center;
			Body.Radius = Bodies[*Surface].Radius;
			Body.Kind = Type == TEXT("river") ? EUnrealBody::River : Type == TEXT("lake") ? EUnrealBody::Lake : EUnrealBody::Custom;
			++Counts[static_cast<int32>(Body.Kind)];
			UnrealBodies.Add(MoveTemp(Body));
		}
	}
	const TSharedPtr<FJsonObject> Zone = Doc->GetObjectField(TEXT("zone"));
	const TArray<TSharedPtr<FJsonValue>>& Low = Zone->GetArrayField(TEXT("min"));
	const TArray<TSharedPtr<FJsonValue>>& High = Zone->GetArrayField(TEXT("max"));
	// Unreal y is the mirror of engine y: the engine maximum is the Unreal minimum.
	UnrealZoneMin = FVector2D(Low[0]->AsNumber() * 100.0, -High[1]->AsNumber() * 100.0);
	UnrealZoneMax = FVector2D(High[0]->AsNumber() * 100.0, -Low[1]->AsNumber() * 100.0);
	UE_LOG(LogSwcWater, Log, TEXT("water (Unreal): %s: %d rivers, %d lakes, %d custom bodies%s; materials %s"), *Path, Counts[0], Counts[1], Counts[2],
		Missing ? *FString::Printf(TEXT(", %d spec(s) with no surface of the same name (an old REGION.water?)"), Missing) : TEXT(""),
		UnrealMaterialFolder.IsEmpty() ? TEXT("of the plugin") : *UnrealMaterialFolder);
	return true;
}

void USwcWaterSubsystem::SpawnUnrealZone()
{
	using namespace SwcUnrealWater;
	UWorld* World = GetWorld();
	const FVector2D Center = (UnrealZoneMin + UnrealZoneMax) * 0.5;
	const FVector2D Extent = UnrealZoneMax - UnrealZoneMin + FVector2D(2.0 * ZoneMargin);
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Spawn.bDeferConstruction = true;
	// The zone: the world regions and the margin; the water info texture and the water mesh in a window about the view.
	UnrealZone = World->SpawnActor<AWaterZone>(AWaterZone::StaticClass(), FTransform(FVector(Center, OceanLevel * 100.0)), Spawn);
	UnrealZone->SetZoneExtent(Extent);
	UnrealZone->SetRenderTargetResolution(FIntPoint(WaterInfoResolution, WaterInfoResolution));
	SetBoolByName(UnrealZone.Get(), TEXT("bEnableLocalOnlyTessellation"), true);
	SetStructByName(UnrealZone.Get(), TEXT("LocalTessellationExtent"), FVector(TessellationWindow, TessellationWindow, 20000.0));
	UnrealZone->SetFarMeshMaterial(UnrealMaterial(SlotFarMesh.Instance, SlotFarMesh.Plugin));
	UnrealZone->FinishSpawning(FTransform(FVector(Center, OceanLevel * 100.0)));

	// The ocean at the ocean level (terrain.json, 40 m), over the whole zone. The plugin ocean is the zone rectangle less the polygon of
	// its spline (an island): a small triangle under the margin, far from the world regions.
	AWaterBodyOcean* OceanActor = World->SpawnActor<AWaterBodyOcean>(AWaterBodyOcean::StaticClass(), FTransform::Identity, Spawn);
	MakeMovable(*OceanActor);
	UWaterBodyOceanComponent* Body = CastChecked<UWaterBodyOceanComponent>(OceanActor->GetWaterBodyComponent());
	Body->SetWorldLocation(FVector(Center, OceanLevel * 100.0));
	Body->bAffectsLandscape = false;
	Body->SetWaterZoneOverride(TSoftObjectPtr<AWaterZone>(UnrealZone.Get()));
	UWaterSplineComponent* Spline = OceanActor->GetWaterSpline();
	Spline->SetRelativeTransform(FTransform::Identity);
	Spline->ClearSplinePoints(false);
	const FVector Corner(-Extent.X * 0.5 + 1000.0, -Extent.Y * 0.5 + 1000.0, 0.0);
	for (const FVector& Point : {Corner, Corner + FVector(1000.0, 0.0, 0.0), Corner + FVector(0.0, 1000.0, 0.0)})
	{
		Spline->AddSplinePoint(Point, ESplineCoordinateSpace::Local, false);
	}
	Spline->SetClosedLoop(true, false);
	Spline->UpdateSpline();
	// The setters of the extents are editor only (WITH_EDITOR); the cooked game sets the properties.
	SetStructByName(Body, TEXT("OceanExtents"), Extent);
	SetStructByName(Body, TEXT("CollisionExtents"), FVector(Extent.X * 0.5, Extent.Y * 0.5, OceanCollisionDepth));
	Body->SetWaterAndUnderWaterPostProcessMaterial(UnrealMaterial(SlotOcean.Instance, SlotOcean.Plugin),
		UnrealMaterial(SlotUnderwater.Instance, SlotUnderwater.Plugin));
	Body->SetWaterInfoMaterial(LoadObject<UMaterialInterface>(nullptr, WaterInfoPath));
	if (UWaterWavesAsset* Waves = LoadObject<UWaterWavesAsset>(nullptr, OceanWavesPath))
	{
		UWaterWavesAssetReference* Reference = NewObject<UWaterWavesAssetReference>(OceanActor);
		Reference->SetWaterWavesAsset(Waves);
		OceanActor->SetWaterWaves(Reference);
	}
	OceanActor->FinishSpawning(FTransform::Identity);
	if (!bPluginInfoMeshes)
	{
		UE_CLOG(!SwcBuildWaterInfoMeshes(*Body), LogSwcWater, Warning, TEXT("water (Unreal): the ocean made no water info mesh"));
	}
	UnrealOcean = OceanActor;
	UE_LOG(LogSwcWater, Log, TEXT("water (Unreal): the zone %.0f x %.0f m at (%.0f, %.0f) m, local tessellation %.0f m, %d texels; the ocean at %.1f m"),
		Extent.X / 100.0, Extent.Y / 100.0, Center.X / 100.0, Center.Y / 100.0, TessellationWindow / 100.0, WaterInfoResolution, OceanLevel);
}

AActor* USwcWaterSubsystem::SpawnUnrealBody(const FUnrealBody& Entry)
{
	using namespace SwcUnrealWater;
	UWorld* World = GetWorld();
	const FJsonObject& Spec = *Entry.Spec;
	const swc::water::Body& Surface = Bodies[Entry.Surface].Body;
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Spawn.bDeferConstruction = true;
	Spawn.Name = MakeUniqueObjectName(World->PersistentLevel, AActor::StaticClass(), FName(*FString::Printf(TEXT("SwcWater_%s_%d"), *Entry.Region.Replace(TEXT("+"), TEXT("")), Entry.RegionIndex)));
	UClass* Class = Entry.Kind == EUnrealBody::River ? ASwcWaterBodyRiver::StaticClass()
		: Entry.Kind == EUnrealBody::Lake ? ASwcWaterBodyLake::StaticClass() : ASwcWaterBodyCustom::StaticClass();
	AWaterBody* Actor = World->SpawnActor<AWaterBody>(Class, FTransform::Identity, Spawn);
	if (Actor == nullptr)
	{
		return nullptr;
	}
	MakeMovable(*Actor);
	UWaterBodyComponent* Body = Actor->GetWaterBodyComponent();
	Body->SetWaterZoneOverride(TSoftObjectPtr<AWaterZone>(UnrealZone.Get()));
	Body->SetWaterInfoMaterial(LoadObject<UMaterialInterface>(nullptr, WaterInfoPath));
	UWaterSplineComponent* Spline = Actor->GetWaterSpline();
	Spline->SetRelativeTransform(FTransform::Identity);
	if (Entry.Kind == EUnrealBody::River)
	{
		// The source Bezier as the Hermite spline of the plugin (the key i + t is the parameter t of segment i; water_bodies.py).
		const TArray<TSharedPtr<FJsonValue>>& Points = Spec.GetArrayField(TEXT("points"));
		const TArray<TSharedPtr<FJsonValue>>& Arrive = Spec.GetArrayField(TEXT("arrive"));
		const TArray<TSharedPtr<FJsonValue>>& Leave = Spec.GetArrayField(TEXT("leave"));
		const TArray<TSharedPtr<FJsonValue>>& Widths = Spec.GetArrayField(TEXT("widths"));
		const TArray<TSharedPtr<FJsonValue>>* Depths = nullptr;
		Spec.TryGetArrayField(TEXT("depths"), Depths);
		const FVector Origin = VectorOf(Points[0]);
		Body->SetWorldLocation(Origin);
		Spline->ClearSplinePoints(false);
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			Spline->AddSplinePoint(VectorOf(Points[I]) - Origin, ESplineCoordinateSpace::Local, false);
		}
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			Spline->SetTangentsAtSplinePoint(I, VectorOf(Arrive[I]), VectorOf(Leave[I]), ESplineCoordinateSpace::Local, false);
		}
		Spline->UpdateSpline();
		// The metadata of each point: the full width, the depth, and the flow (cm, cm/s); linear by the key (FInterpCurvePoint).
		const TMap<FName, float>* Params = Materials.IsValidIndex(Surface.material) ? &Materials[Surface.material].Params : nullptr;
		const float* Flow = Params ? Params->Find(FName(TEXT("WaterFlowSpeed"))) : nullptr;
		const float* Tilling = Params ? Params->Find(FName(TEXT("Tilling"))) : nullptr;
		const double TileLength = Spec.HasField(TEXT("tileLength")) ? Spec.GetNumberField(TEXT("tileLength")) : 1.0;
		const double Velocity = (Flow ? *Flow : 0.0) * FlowScale * TileLength / FMath::Max(Tilling ? static_cast<double>(*Tilling) : 1.0, 0.01) * 100.0;
		double Known = 0.0;
		int32 KnownCount = 0;
		for (int32 I = 0; Depths && I < Depths->Num(); ++I)
		{
			if (!(*Depths)[I]->IsNull())
			{
				Known += (*Depths)[I]->AsNumber();
				++KnownCount;
			}
		}
		const double Fallback = KnownCount ? Known / KnownCount : DefaultRiverDepth;
		UWaterSplineMetadata* Metadata = Actor->GetWaterSplineMetadata();
		for (int32 I = 0; I < Points.Num() && I < Metadata->RiverWidth.Points.Num(); ++I)
		{
			const bool bDepth = Depths && I < Depths->Num() && !(*Depths)[I]->IsNull();
			Metadata->RiverWidth.Points[I].OutVal = static_cast<float>(Widths[I]->AsNumber() * 100.0);
			Metadata->Depth.Points[I].OutVal = static_cast<float>((bDepth ? (*Depths)[I]->AsNumber() : Fallback) * 100.0);
			Metadata->WaterVelocityScalar.Points[I].OutVal = static_cast<float>(Velocity);
		}
		UWaterBodyRiverComponent* RiverBody = CastChecked<UWaterBodyRiverComponent>(Body);
		RiverBody->SetLakeAndOceanTransitionMaterials(UnrealMaterial(SlotRiverToLake.Instance, SlotRiverToLake.Plugin), UnrealMaterial(SlotRiverToOcean.Instance, SlotRiverToOcean.Plugin));
		Body->SetWaterAndUnderWaterPostProcessMaterial(UnrealMaterial(SlotRiver.Instance, SlotRiver.Plugin),
			UnrealMaterial(SlotUnderwater.Instance, SlotUnderwater.Plugin));
	}
	else if (Entry.Kind == EUnrealBody::Lake)
	{
		// The outline of the flat top at its surface z, linear points, closed.
		const TArray<TSharedPtr<FJsonValue>>& Outline = Spec.GetArrayField(TEXT("outline"));
		const double Z = Spec.GetNumberField(TEXT("z")) * 100.0;
		FVector Origin = FVector::ZeroVector;
		for (const TSharedPtr<FJsonValue>& Point : Outline)
		{
			Origin += VectorOf(Point);
		}
		Origin /= FMath::Max(Outline.Num(), 1);
		Origin.Z = Z;
		Body->SetWorldLocation(Origin);
		Spline->ClearSplinePoints(false);
		for (int32 I = 0; I < Outline.Num(); ++I)
		{
			FVector Point = VectorOf(Outline[I]) - Origin;
			Point.Z = 0.0;
			Spline->AddSplinePoint(Point, ESplineCoordinateSpace::Local, false);
			Spline->SetSplinePointType(I, ESplinePointType::Linear, false);
		}
		Spline->SetClosedLoop(true, false);
		Spline->UpdateSpline();
		Body->SetWaterAndUnderWaterPostProcessMaterial(UnrealMaterial(SlotLake.Instance, SlotLake.Plugin),
			UnrealMaterial(SlotUnderwater.Instance, SlotUnderwater.Plugin));
		if (UWaterWavesAsset* Waves = LoadObject<UWaterWavesAsset>(nullptr, LakeWavesPath))
		{
			UWaterWavesAssetReference* Reference = NewObject<UWaterWavesAssetReference>(Actor);
			Reference->SetWaterWavesAsset(Waves);
			Actor->SetWaterWaves(Reference);
		}
	}
	else
	{
		// A tilted volume: the surface mesh of the converter (REGION.water) as the mesh of a custom body, about its center.
		UMaterialInterface* Material = UnrealMaterial(SlotCustom.Instance, SlotCustom.Plugin);
		UStaticMesh* Mesh = BuildMesh(Surface, Entry.Center, Material);
		Meshes.Add(Mesh);
		Body->SetWorldLocation(Entry.Center);
		Body->SetWaterMeshOverride(Mesh);
		Body->SetWaterAndUnderWaterPostProcessMaterial(Material, UnrealMaterial(SlotUnderwaterCustom.Instance, SlotUnderwaterCustom.Plugin));
	}
	Actor->FinishSpawning(FTransform::Identity);
	if (!bPluginInfoMeshes)
	{
		UE_CLOG(!SwcBuildWaterInfoMeshes(*Body), LogSwcWater, Warning, TEXT("water (Unreal): %s made no water info mesh"), *Actor->GetName());
	}
	return Actor;
}

void USwcWaterSubsystem::UpdateUnreal(const FVector& View)
{
	if (UnrealZone == nullptr)
	{
		SpawnUnrealZone();
	}
	TArray<TPair<double, int32>> Wanted;
	for (int32 Index = 0; Index < UnrealBodies.Num(); ++Index)
	{
		const FUnrealBody& Entry = UnrealBodies[Index];
		const double Distance = FVector::Dist(View, Entry.Center) - Entry.Radius;
		if (!Entry.bSpawned && Distance <= SwcUnrealWater::SpawnRange)
		{
			Wanted.Emplace(Distance, Index);
		}
	}
	Wanted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	for (int32 K = 0; K < Wanted.Num() && K < SwcUnrealWater::SpawnsPerTick; ++K)
	{
		FUnrealBody& Entry = UnrealBodies[Wanted[K].Value];
		Entry.bSpawned = true;
		AActor* Actor = SpawnUnrealBody(Entry);
		UE_CLOG(Actor == nullptr, LogSwcWater, Warning, TEXT("water (Unreal): the body %s %d did not spawn"), *Entry.Region, Entry.RegionIndex);
		if (Actor != nullptr)
		{
			UnrealActors.Add(Actor);
			UE_LOG(LogSwcWater, Verbose, TEXT("water (Unreal): %s (%s) at %.0f m"), *Actor->GetName(),
				Entry.Kind == EUnrealBody::River ? TEXT("river") : Entry.Kind == EUnrealBody::Lake ? TEXT("lake") : TEXT("custom"), Wanted[K].Key / 100.0);
		}
	}
}

int32 USwcWaterSubsystem::UnrealBodiesToSpawnNear(const FVector& Place) const
{
	int32 Missing = UnrealZone == nullptr ? 1 : 0;
	for (const FUnrealBody& Entry : UnrealBodies)
	{
		Missing += !Entry.bSpawned && FVector::Dist(Place, Entry.Center) - Entry.Radius <= SwcLoadingGate::NearCm;
	}
	return Missing;
}
