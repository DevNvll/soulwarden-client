// The Unreal water (docs/WATER.md "The Unreal water"; decisions of the user, 2026-10-25, AST-200): by default (swc.WaterMode 1; the source
// path with -SwcWaterMode=Source) the water subsystem spawns the bodies of the Unreal Water plugin from the source shapes (the specs of
// tools/convert-water.sh bodies, SWC_CONTENT/water/unreal-bodies.json): one water zone with local tessellation over the world regions, the
// ocean at the ocean level, and the rivers, lakes, and custom bodies near the view, with the source tone of their water fog. The source
// surfaces, the ocean grid, and the sky cube do not show in this mode. The quadtree queries (the depth classes, the wade, the swim), the
// loading gate, and the shore foam decals do not change.

#include "Water/SwcWaterSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Algo/Reverse.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
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
#include "WaterMeshComponent.h"
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
/** The extent of the far mesh of the zone (cm) around the tessellated window: the ocean to the horizon, as the 20 km ocean grid of the source
 *  path [decision of the client]. The plugin adds no far mesh with the default 0 (UWaterMeshComponent::RebuildWaterMesh): the far view showed
 *  the sea floor of the Landscape (stage 2a, ocean-far: L* 54 against 37). */
constexpr float FarMeshExtent = 2000000.0f;
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

/** The source tone (docs/WATER.md "The source tone"): the water of a volume or a river has the dark color of its source water fog. */
TAutoConsoleVariable<int32> CVarWaterSourceTone(TEXT("swc.WaterSourceTone"), 1,
	TEXT("1: the scattering and the absorption of each spawned lake, river, custom body, and of the ocean come from its source water fog (the dark tone of the source); ")
	TEXT("0: the values of the plugin material."));
/** The source in-scatter is FogColor x FogColorScale x the sun color (E / pi, watervolume.cfx); the in-scatter of Single Layer Water is the
 *  sun E x the phase (isotropic: 1 / (4 pi)) plus the ambient light, x the albedo. Thus the albedo is about 4 x FogColor x FogColorScale
 *  (swc::water::volume_tone, ocean_tone) [inference: the ambient part is not counted; the scale 4 has the smallest difference to the source
 *  path at the lake view, docs/WATER.md "The source tone"]. */
TAutoConsoleVariable<float> CVarWaterToneAlbedoScale(TEXT("swc.WaterToneAlbedoScale"), 4.0f,
	TEXT("The albedo of the source tone: this value x FogColor x FogColorScale (docs/WATER.md \"The source tone\")."));
/** The mode of the water at run time: 0 the source path (the default until the start-at-place fault of the Unreal water is fixed, docs/WATER.md
 *  "Stage 2a"), 1 the Unreal water (-SwcWaterMode=Unreal sets 1 at the start; -SwcWaterMode=Source sets 0). */
TAutoConsoleVariable<int32> CVarWaterMode(TEXT("swc.WaterMode"), 0,
	TEXT("0: the source water shader (the default for now; docs/WATER.md); 1: the Unreal water (the Water plugin). -SwcWaterMode=Unreal sets 1 at the start."));

/** The ocean waves (docs/WATER.md "Stage 2b"): 1 the fixed list of 24 waves of the source field (its size and amount of the ocean of the place),
 *  0 the ocean waves of the plugin (GerstnerWaves_Ocean). 0 until the check of stage 2b. */
TAutoConsoleVariable<int32> CVarWaterOceanWaves(TEXT("swc.WaterOceanWaves"), 0,
	TEXT("1: the ocean waves of the source (24 Gerstner waves scaled by WavesSize, lengths / WavesAmount; docs/WATER.md \"Stage 2b\"); ")
	TEXT("0: the waves of the plugin."));
/** The diagnostic of the zone (2a): a value below 0 logs the state of the zone for the view each 2 s (the window, the water mesh, the
 *  ocean, and the water height range); 0 off. */
TAutoConsoleVariable<float> CVarWaterZoneRefresh(TEXT("swc.WaterZoneRefresh"), 0.0f,
	TEXT("The diagnostic of the Unreal water zone: below 0 a log line of the zone state each 2 s (the window, the ocean, the water height range); 0 off."));

/** The shortest time between two updates of the source waves in a transition (s; each update rebuilds the GPU wave data) [decision of the
 *  client]. */
constexpr double WavesUpdateInterval = 0.1;

/** @return The RGBA of the Absorption and Scattering parameters of the plugin material for a tone (swc::water::tone_parameters). */
void ToneParameters(const swc::water::Tone& Tone, FLinearColor& Absorption, FLinearColor& Scattering)
{
	float A[4];
	float S[4];
	swc::water::tone_parameters(Tone, A, S);
	Absorption = FLinearColor(A[0], A[1], A[2], A[3]);
	Scattering = FLinearColor(S[0], S[1], S[2], S[3]);
}

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
	// -SwcWaterOceanDepth=M: the channel depth of the plugin ocean in m (a test; default 10).
	FParse::Value(FCommandLine::Get(), TEXT("-SwcWaterOceanDepth="), OceanChannelDepth);
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
	// The far mesh has its own instance: the ocean tone goes on it too.
	UMaterialInterface* FarMaterial = UnrealMaterial(SlotFarMesh.Instance, SlotFarMesh.Plugin);
	UnrealFarMaterial = FarMaterial != nullptr ? UMaterialInstanceDynamic::Create(FarMaterial, this) : nullptr;
	UnrealZone->SetFarMeshMaterial(UnrealFarMaterial);
	UnrealZone->GetWaterMeshComponent()->FarDistanceMeshExtent = FarMeshExtent;
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
	// The channel depth of the ocean (docs/WATER.md "Stage 2a", the start-at-beach fault): the zone takes the minimum of its water height
	// range from the lowest bounds of its bodies (AWaterZone::UpdateWaterInfoTexture), and the plugin decodes a height at that minimum as "no
	// water" (WaterInfoMerge.usf, WaterDataFunctions.ush): the surface is clear. The ocean bounds reach the channel depth below the ocean level
	// (UWaterBodyOceanComponent::CalcBounds; 0 by default): with 10 m the minimum is 30 m, below every body of the world (the lowest, a lake,
	// is at 39.28 m). The ocean does not change the Landscape (bAffectsLandscape false): the depth changes only its bounds.
	if (OceanChannelDepth > 0.0f)
	{
		FStructProperty* Curve = FindFProperty<FStructProperty>(Body->GetClass(), TEXT("CurveSettings"));
		if (Curve != nullptr && Curve->Struct == FWaterCurveSettings::StaticStruct())
		{
			Curve->ContainerPtrToValuePtr<FWaterCurveSettings>(Body)->ChannelDepth = OceanChannelDepth * 100.0f;
			UE_LOG(LogSwcWater, Log, TEXT("water (Unreal): the ocean channel depth %.1f m"), OceanChannelDepth);
		}
	}
	Body->SetWaterAndUnderWaterPostProcessMaterial(UnrealMaterial(SlotOcean.Instance, SlotOcean.Plugin),
		UnrealMaterial(SlotUnderwater.Instance, SlotUnderwater.Plugin));
	Body->SetWaterInfoMaterial(LoadObject<UMaterialInterface>(nullptr, WaterInfoPath));
	if (UWaterWavesAsset* Waves = LoadObject<UWaterWavesAsset>(nullptr, OceanWavesPath))
	{
		UWaterWavesAssetReference* Reference = NewObject<UWaterWavesAssetReference>(OceanActor);
		Reference->SetWaterWavesAsset(Waves);
		OceanActor->SetWaterWaves(Reference);
		UnrealPluginWaves = Reference;
	}
	OceanActor->FinishSpawning(FTransform::Identity);
	SwcWaterCollisionAsOverlap(*Body);
	if (!bPluginInfoMeshes)
	{
		UE_CLOG(!SwcBuildWaterInfoMeshes(*Body), LogSwcWater, Warning, TEXT("water (Unreal): the ocean made no water info mesh"));
	}
	UnrealOcean = OceanActor;
	// The ocean tone: the values of the ocean of the place (OceanNow: the mission ocean or the local override, blended in its transition time).
	FUnrealTone Tone;
	Tone.Body = Body;
	Tone.bOcean = true;
	Tone.Extra = UnrealFarMaterial.Get();
	if (UMaterialInstanceDynamic* Material = Body->GetWaterMaterialInstance())
	{
		Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Absorption")), Tone.PluginAbsorption);
		Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Scattering")), Tone.PluginScattering);
	}
	if (UnrealFarMaterial != nullptr)
	{
		UnrealFarMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Absorption")), Tone.PluginFarAbsorption);
		UnrealFarMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Scattering")), Tone.PluginFarScattering);
	}
	OceanToneIndex = UnrealTones.Add(MoveTemp(Tone));
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
		// The outline is counterclockwise in engine space; the mirror of y makes it clockwise in Unreal space. The lake triangulation of the
		// plugin fills a positive (counterclockwise) polygon: the points go in the order that gives a positive area in Unreal space.
		TArray<FVector> Points;
		double Area = 0.0;
		for (const TSharedPtr<FJsonValue>& Point : Outline)
		{
			FVector Local = VectorOf(Point) - Origin;
			Local.Z = 0.0;
			Points.Add(Local);
		}
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			const FVector& A = Points[I];
			const FVector& B = Points[(I + 1) % Points.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		if (Area < 0.0)
		{
			Algo::Reverse(Points);
		}
		Spline->ClearSplinePoints(false);
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			Spline->AddSplinePoint(Points[I], ESplineCoordinateSpace::Local, false);
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
	SwcWaterCollisionAsOverlap(*Body);
	AddUnrealTone(*Body, Surface);
	if (!bPluginInfoMeshes)
	{
		SwcBuildWaterInfoMeshes(*Body);
	}
	return Actor;
}

void USwcWaterSubsystem::AddUnrealTone(UWaterBodyComponent& Body, const swc::water::Body& Surface)
{
	FUnrealTone Tone;
	Tone.Body = &Body;
	Tone.Fog = FLinearColor(Surface.fog[0], Surface.fog[1], Surface.fog[2]);
	Tone.Density = Surface.density;
	Tone.Scale = Surface.scale;
	if (UMaterialInstanceDynamic* Material = Body.GetWaterMaterialInstance())
	{
		Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Absorption")), Tone.PluginAbsorption);
		Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Scattering")), Tone.PluginScattering);
	}
	ApplyUnrealTone(Tone);
	UnrealTones.Add(MoveTemp(Tone));
}

void USwcWaterSubsystem::ApplyUnrealTone(FUnrealTone& Tone) const
{
	UWaterBodyComponent* Body = Tone.Body.Get();
	if (Body == nullptr || AppliedTone < 0)
	{
		return;
	}
	FLinearColor Absorption = Tone.PluginAbsorption;
	FLinearColor Scattering = Tone.PluginScattering;
	if (AppliedTone == 1)
	{
		const float Fog[3] = {Tone.Fog.R, Tone.Fog.G, Tone.Fog.B};
		SwcUnrealWater::ToneParameters(Tone.bOcean ? swc::water::ocean_tone(Fog, Tone.Scale, Tone.Density, AppliedAlbedoScale)
			: swc::water::volume_tone(Fog, Tone.Scale, Tone.Density, AppliedAlbedoScale), Absorption, Scattering);
	}
	Tone.Materials.Reset();
	for (UMaterialInstanceDynamic* Material : {Body->GetWaterMaterialInstance(), Body->GetRiverToLakeTransitionMaterialInstance(),
			Body->GetRiverToOceanTransitionMaterialInstance()})
	{
		if (Material != nullptr)
		{
			Material->SetVectorParameterValue(TEXT("Absorption"), Absorption);
			Material->SetVectorParameterValue(TEXT("Scattering"), Scattering);
			Tone.Materials.Add(Material);
		}
	}
	if (UMaterialInstanceDynamic* Far = Tone.Extra.Get())
	{
		Far->SetVectorParameterValue(TEXT("Absorption"), AppliedTone == 1 ? Absorption : Tone.PluginFarAbsorption);
		Far->SetVectorParameterValue(TEXT("Scattering"), AppliedTone == 1 ? Scattering : Tone.PluginFarScattering);
	}
}

void USwcWaterSubsystem::UpdateUnrealOceanTone()
{
	if (!UnrealTones.IsValidIndex(OceanToneIndex))
	{
		return;
	}
	FUnrealTone& Tone = UnrealTones[OceanToneIndex];
	const FLinearColor Fog(OceanNow.FindRef(TEXT("FogColorR")), OceanNow.FindRef(TEXT("FogColorG")), OceanNow.FindRef(TEXT("FogColorB")));
	const float Scale = OceanNow.FindRef(TEXT("FogScale"));
	const float Density = OceanNow.FindRef(TEXT("Density"));
	if (Fog != Tone.Fog || Scale != Tone.Scale || Density != Tone.Density)
	{
		Tone.Fog = Fog;
		Tone.Scale = Scale;
		Tone.Density = Density;
		ApplyUnrealTone(Tone);
	}
}

void USwcWaterSubsystem::UpdateUnrealOceanWaves()
{
	AWaterBodyOcean* OceanBody = UnrealOcean.Get();
	if (OceanBody == nullptr)
	{
		return;
	}
	if (SwcUnrealWater::CVarWaterOceanWaves.GetValueOnGameThread() == 0)
	{
		if (bSourceWavesOn)
		{
			OceanBody->SetWaterWaves(UnrealPluginWaves);
			bSourceWavesOn = false;
			AppliedWavesSize = -1.0f;
			UE_LOG(LogSwcWater, Log, TEXT("water (Unreal): the ocean waves of the plugin"));
		}
		return;
	}
	// The source: the size blends in the transition of the override (OceanNow); the amount, the wind, and the direction change at its end.
	if (OceanBlend >= 1.0 && OceanTarget.Contains(TEXT("WavesAmount")))
	{
		WavesAmountNow = OceanTarget[TEXT("WavesAmount")];
	}
	const float Size = OceanNow.FindRef(TEXT("WavesSize"));
	const double Now = FPlatformTime::Seconds();
	if (bSourceWavesOn && Size == AppliedWavesSize && WavesAmountNow == AppliedWavesAmount)
	{
		return;
	}
	if (bSourceWavesOn && WavesAmountNow == AppliedWavesAmount && Now < NextWavesUpdate)
	{
		return;
	}
	if (UnrealSourceWaves == nullptr)
	{
		UnrealSourceWaves = NewObject<UGerstnerWaterWaves>(OceanBody);
		UnrealSourceWaves->GerstnerWaveGenerator = NewObject<USwcOceanWaveGenerator>(UnrealSourceWaves);
	}
	USwcOceanWaveGenerator* Generator = CastChecked<USwcOceanWaveGenerator>(UnrealSourceWaves->GerstnerWaveGenerator);
	const bool bAmountChange = WavesAmountNow != AppliedWavesAmount;
	Generator->Size = Size;
	Generator->Amount = WavesAmountNow;
	UnrealSourceWaves->RecomputeWaves(/* bAllowBPScript = */ false);
	if (!bSourceWavesOn)
	{
		OceanBody->SetWaterWaves(UnrealSourceWaves);
		bSourceWavesOn = true;
	}
	UE_CLOG(bAmountChange || AppliedWavesSize < 0.0f, LogSwcWater, Log, TEXT("water (Unreal): the ocean waves of the source: size %.3f, amount %.2f, %d waves, the largest height %.2f m"),
		Size, WavesAmountNow, UnrealSourceWaves->GetGerstnerWaves().Num(), UnrealSourceWaves->GetMaxWaveHeight() / 100.0f);
	AppliedWavesSize = Size;
	AppliedWavesAmount = WavesAmountNow;
	NextWavesUpdate = Now + SwcUnrealWater::WavesUpdateInterval;
}

void USwcWaterSubsystem::SetUnrealVisible(bool bUnreal)
{
	// The Unreal water: the zone (its water mesh and far mesh), the ocean, and the bodies; no collision when it hides (the underwater sweep
	// of the plugin then finds no body).
	TArray<AActor*> Actors{UnrealZone.Get(), UnrealOcean.Get()};
	for (const TObjectPtr<AActor>& Actor : UnrealActors)
	{
		Actors.Add(Actor.Get());
	}
	for (AActor* Actor : Actors)
	{
		if (Actor != nullptr)
		{
			Actor->SetActorHiddenInGame(!bUnreal);
			Actor->SetActorEnableCollision(bUnreal);
		}
	}
	// The source path: the surfaces and the ocean grid (the shore decals and the cloud layers of the sky capture stay as they are).
	if (WaterActor != nullptr)
	{
		TInlineComponentArray<UStaticMeshComponent*> Components(WaterActor.Get());
		for (UStaticMeshComponent* Component : Components)
		{
			if (!SkyClouds.Contains(Component))
			{
				Component->SetVisibility(!bUnreal);
			}
		}
	}
}

void USwcWaterSubsystem::UpdateUnreal(const FVector& View, float DeltaTime)
{
	UpdateOceanState(View, DeltaTime);
	const int32 Tone = SwcUnrealWater::CVarWaterSourceTone.GetValueOnGameThread() != 0 ? 1 : 0;
	const float AlbedoScale = SwcUnrealWater::CVarWaterToneAlbedoScale.GetValueOnGameThread();
	if (Tone != AppliedTone || AlbedoScale != AppliedAlbedoScale)
	{
		AppliedTone = Tone;
		AppliedAlbedoScale = AlbedoScale;
		for (FUnrealTone& Each : UnrealTones)
		{
			ApplyUnrealTone(Each);
		}
		UE_LOG(LogSwcWater, Log, TEXT("water (Unreal): the source tone %s (albedo scale %.2f), %d bodies"), Tone ? TEXT("on") : TEXT("off"), AlbedoScale, UnrealTones.Num());
	}
	UpdateUnrealOceanTone();
	UpdateUnrealOceanWaves();
	// The state of the zone for the view (a diagnostic each 2 s with swc.WaterZoneRefresh < 0).
	if (SwcUnrealWater::CVarWaterZoneRefresh.GetValueOnGameThread() < 0.0f && UnrealZone != nullptr && FPlatformTime::Seconds() >= NextZoneRefresh)
	{
		NextZoneRefresh = FPlatformTime::Seconds() + 2.0;
		FVector Center = FVector::ZeroVector;
		FBox Bounds(ForceInit);
		const bool bCenter = UnrealZone->GetDynamicWaterInfoCenter(0, Center);
		const bool bBounds = UnrealZone->GetDynamicWaterInfoBounds(0, Bounds);
		const UWaterBodyComponent* OceanComponent = UnrealOcean != nullptr ? UnrealOcean->GetWaterBodyComponent() : nullptr;
		const UWaterMeshComponent* DiagMesh = UnrealZone->GetWaterMeshComponent();
		if (OceanComponent != nullptr)
		{
			const FStructProperty* Saved = FindFProperty<FStructProperty>(OceanComponent->GetClass(), TEXT("SavedZoneLocation"));
			const FVector2D SavedZone = Saved != nullptr ? *Saved->ContainerPtrToValuePtr<FVector2D>(OceanComponent) : FVector2D::ZeroVector;
			const FVector2f Heights = UnrealZone->GetWaterHeightExtents();
			UE_LOG(LogSwcWater, Log, TEXT("water (Unreal): diag: ocean bounds %s extent %s; saved zone %.0f %.0f m; body index %d; zone water heights %.3f..%.3f m, ground min %.3f m"),
				*OceanComponent->Bounds.Origin.ToString(), *OceanComponent->Bounds.BoxExtent.ToString(), SavedZone.X / 100.0, SavedZone.Y / 100.0,
				OceanComponent->GetWaterBodyIndex(), Heights.X / 100.0, Heights.Y / 100.0, UnrealZone->GetGroundZMin() / 100.0);
		}
		UE_LOG(LogSwcWater, Log, TEXT("water (Unreal): diag: t %.1f s; mesh proxy %d, info texture %d; view %.0f %.0f %.0f m; zone center %s %.0f %.0f m; bounds %s %.0f..%.0f %.0f..%.0f m; ocean %s visible %d render %d tiles %d"),
			GetWorld()->GetTimeSeconds(), DiagMesh != nullptr && DiagMesh->SceneProxy != nullptr, UnrealZone->WaterInfoTextureArray != nullptr,
			View.X / 100.0, View.Y / 100.0, View.Z / 100.0, bCenter ? TEXT("ok") : TEXT("none"), Center.X / 100.0, Center.Y / 100.0, bBounds ? TEXT("ok") : TEXT("none"),
			Bounds.Min.X / 100.0, Bounds.Max.X / 100.0, Bounds.Min.Y / 100.0, Bounds.Max.Y / 100.0, OceanComponent ? *OceanComponent->Bounds.Origin.ToString() : TEXT("-"),
			OceanComponent ? OceanComponent->IsVisible() : -1, OceanComponent ? OceanComponent->ShouldRender() : -1,
			OceanComponent ? OceanComponent->ShouldGenerateWaterMeshTile() : -1);
	}
	// The first water info texture of the zone (the plugin makes it only when its gates pass: the PSOs of the water info meshes, and in an
	// editor binary the shader maps of the ground): until then the zone draws no water.
	if (!bInfoTextureLogged && UnrealZone != nullptr && UnrealZone->WaterInfoTextureArray != nullptr)
	{
		bInfoTextureLogged = true;
		UE_LOG(LogSwcWater, Log, TEXT("water (Unreal): the first water info texture of the zone at %.1f s (world time)"), GetWorld()->GetTimeSeconds());
	}
	// The plugin makes the instances of a body again on some changes (CreateOrUpdateWaterMID): a new instance gets the tone too.
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextToneCheck)
	{
		NextToneCheck = Now + 0.5;
		int32 Again = 0;
		for (FUnrealTone& Each : UnrealTones)
		{
			UWaterBodyComponent* Body = Each.Body.Get();
			if (Body == nullptr)
			{
				continue;
			}
			int32 Count = 0;
			bool bChanged = false;
			for (UMaterialInstanceDynamic* Material : {Body->GetWaterMaterialInstance(), Body->GetRiverToLakeTransitionMaterialInstance(),
					Body->GetRiverToOceanTransitionMaterialInstance()})
			{
				if (Material != nullptr)
				{
					bChanged |= !Each.Materials.IsValidIndex(Count) || Each.Materials[Count].Get() != Material;
					++Count;
				}
			}
			if (bChanged || Count != Each.Materials.Num())
			{
				ApplyUnrealTone(Each);
				++Again;
			}
		}
		UE_CLOG(Again > 0, LogSwcWater, Log, TEXT("water (Unreal): the source tone again on %d bod(ies) (new instances)"), Again);
	}
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
