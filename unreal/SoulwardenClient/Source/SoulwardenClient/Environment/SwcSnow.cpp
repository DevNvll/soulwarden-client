#include "Environment/SwcSnow.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace env = swc::environment;

namespace
{
TAutoConsoleVariable<float> CVarSnowFall(TEXT("swc.SnowFall"), 0.0f,
	TEXT("AST-257: a multiplier of the flake count of the Snow volumes: 0 (the default, as retail: r_SnowFlakeLegacy 0, no volume flakes in the retail takes of 2026-10-22); 1: the legacy count of the source (a validation mode [I])."));
TAutoConsoleVariable<int32> CVarSnowSurface(TEXT("swc.SnowSurface"), 1,
	TEXT("AST-257: 1: the surface snow and frost of the nearest Snow volume (MF_SwcSnowSurface, MPC_SwcWeather); 0: off."));
TAutoConsoleVariable<float> CVarSnowSurfaceGain(TEXT("swc.SnowSurfaceGain"), 6.0f,
	TEXT("AST-257: a gain on SnowBrightness of the surface snow: 6 [I] (calibrated on the retail shrine views of 2026-10-22: the caps of the pots, the rock tops, the ground; the global factor of the source is not read [U]); 1: the source value alone."));
TAutoConsoleVariable<int32> CVarSnowRoof(TEXT("swc.SnowRoof"), 1,
	TEXT("AST-257: 1: the roof map hides the flakes under a roof; 0: off (a check of the map)."));

/** The cluster rules of the source (research/snow.md section 2, 0x5FF16CD to 0x5FF1866), Unreal cm. */
constexpr double SpawnHalfXY = 1500.0;
constexpr double SpawnBelow = 100.0;
constexpr double SpawnAbove = 900.0;
constexpr double RetainCm = 3000.0;
constexpr double LocalHalf = 1000.0;
/** The surface snow of a volume shows while the camera is within this distance (m, horizontal) of its radius [I]. */
constexpr double SurfaceRangeM = 2000.0;
/** A camera step longer than this in one frame is a teleport: the clusters spawn again around the new place (a rule of this client). */
constexpr double TeleportCm = 5000.0;

FVector ToUnreal(const env::Vec3& V)
{
	return FVector(V.x * 100.0, -V.y * 100.0, V.z * 100.0);
}
} // namespace

FSwcSnow::FSwcSnow()
	: Random(0x5357)
{
}

FSwcSnow::~FSwcSnow() = default;

UStaticMesh* FSwcSnow::BuildMesh(UObject* Outer)
{
	// One cluster: FlakesPerCluster quads. Each corner has the place of its flake (the quad has no area in the mesh: the material
	// expands it toward the camera), UV0 the corner (-1 to 1), UV1 the scale and the phase, UV2 the index and the atlas tile.
	FMeshDescription Description;
	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	UVs.SetNumChannels(3);
	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Flake");
	FRandomStream Stream(0x534E4F57);
	static const FVector2f Corners[4] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
	for (int32 Flake = 0; Flake < FlakesPerCluster; ++Flake)
	{
		const FVector3f Place(Stream.FRandRange(-LocalHalf, LocalHalf), Stream.FRandRange(-LocalHalf, LocalHalf), Stream.FRandRange(-LocalHalf, LocalHalf));
		const float Scale = Stream.FRandRange(0.005f, 0.010f);
		const float Phase = Stream.FRand();
		const float Tile = static_cast<float>(Stream.RandHelper(4));
		FVertexInstanceID Instances[4];
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVertexID Vertex = Description.CreateVertex();
			Positions[Vertex] = Place;
			const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
			Normals[Instance] = FVector3f(0.0f, 0.0f, 1.0f);
			Tangents[Instance] = FVector3f(1.0f, 0.0f, 0.0f);
			Signs[Instance] = 1.0f;
			UVs.Set(Instance, 0, Corners[Corner]);
			UVs.Set(Instance, 1, FVector2f(Scale, Phase));
			UVs.Set(Instance, 2, FVector2f(static_cast<float>(Flake), Tile));
			Instances[Corner] = Instance;
		}
		Description.CreateTriangle(Group, {Instances[0], Instances[1], Instances[2]});
		Description.CreateTriangle(Group, {Instances[0], Instances[2], Instances[3]});
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material.Get(), TEXT("Flake")));
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bMarkPackageDirty = false;
	Params.bBuildSimpleCollision = false;
	Params.bCommitMeshDescription = false;
	Mesh->BuildFromMeshDescriptions({&Description}, Params);
	return Mesh;
}

bool FSwcSnow::Create(AActor* Owner, USceneComponent* Root)
{
	ClusterStates.SetNum(Clusters);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/SwcGameContent/Environment/M_SwcSnowFlake.M_SwcSnowFlake"), nullptr, LOAD_NoWarn);
	if (Owner == nullptr || Base == nullptr)
	{
		return false;   // no flakes; the surface snow still works (MPC_SwcWeather)
	}
	Material.Reset(UMaterialInstanceDynamic::Create(Base, Owner));
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("SwcSnowFlakes"));
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	Component->bAffectDistanceFieldLighting = false;
	Component->SetStaticMesh(BuildMesh(Owner));
	Component->SetMaterial(0, Material.Get());
	// The quads grow by the material (the flake size, the turbulence of 1 m or less): a margin on the bounds of the local places.
	Component->SetBoundsScale(1.5f);
	Component->SetupAttachment(Root);
	Component->RegisterComponent();
	Component->SetVisibility(false);
	TArray<FTransform> Transforms;
	Transforms.Init(FTransform::Identity, Clusters);
	Component->AddInstances(Transforms, false, true);
	Flakes = Component;
	return true;
}

void FSwcSnow::Spawn(FCluster& Cluster, const FVector& Camera, const FSwcSnowVolume& Volume)
{
	Cluster.Center = Camera + FVector(Random.FRandRange(-SpawnHalfXY, SpawnHalfXY), Random.FRandRange(-SpawnHalfXY, SpawnHalfXY),
		Random.FRandRange(-SpawnBelow, SpawnAbove));
	Cluster.Life = env::snow_cluster_life(Volume.GravityScale, Random.FRand());
	Cluster.Multiplier = Random.FRandRange(0.2, 0.4);
	Cluster.Age = 0.0;
}

void FSwcSnow::Update(UWorld& World, const TArray<FSwcSnowVolume>& Volumes, const FFrame& Frame, const AActor* Pawn)
{
	const env::Vec3 At{Frame.Camera.X / 100.0, -Frame.Camera.Y / 100.0, Frame.Camera.Z / 100.0};
	// The surface snow (MF_SwcSnowSurface through MPC_SwcWeather): the enabled volume nearest to the camera (horizontal) whose radius
	// is within SurfaceRangeM of it [I] (the source writes the state of each volume in turn; the last writer stays; the order is not
	// read [U]).
	const FSwcSnowVolume* Surface = nullptr;
	double Nearest = TNumericLimits<double>::Max();
	// The flakes: the volume of the camera with the largest count weight.
	const FSwcSnowVolume* Selected = nullptr;
	Weight = 0.0;
	for (const FSwcSnowVolume& Volume : Volumes)
	{
		if (!Volume.bEnabled)
		{
			continue;
		}
		const double W = env::snow_count_weight(At, Volume.Center, Volume.Radius);
		if (W > Weight)
		{
			Weight = W;
			Selected = &Volume;
		}
		const double Horizontal = FMath::Sqrt(FMath::Square(At.x - Volume.Center.x) + FMath::Square(At.y - Volume.Center.y)) - Volume.Radius;
		if (Horizontal < SurfaceRangeM && Horizontal < Nearest && (Volume.SnowAmount > 0.0 || Volume.FrostAmount > 0.0 || Volume.SurfaceFreezing > 0.0))
		{
			Nearest = Horizontal;
			Surface = &Volume;
		}
	}
	Count = Selected != nullptr ? env::snow_flake_count(FMath::RoundToInt32(Selected->FlakeCount * FMath::Max(0.0f, CVarSnowFall.GetValueOnGameThread())), Weight) : 0;
	Count = FMath::Min(Count, FlakesPerCluster);
	VolumeName = Selected != nullptr ? Selected->Name : FString();
	SurfaceName = Surface != nullptr && CVarSnowSurface.GetValueOnGameThread() != 0 ? Surface->Name : FString();
	if (Count > 0 || !SurfaceName.IsEmpty())
	{
		Occlusion.Update(World, Frame.Camera, Pawn);
	}
	const bool bRoof = CVarSnowRoof.GetValueOnGameThread() != 0 && Occlusion.GetTexture() != nullptr;
	UpdateSurface(World, SurfaceName.IsEmpty() ? nullptr : Surface, bRoof);
	UInstancedStaticMeshComponent* Component = Flakes.Get();
	if (Component == nullptr || Material == nullptr || Count <= 0)
	{
		if (Component != nullptr)
		{
			Component->SetVisibility(false);
		}
		bSpawned = false;
		return;
	}
	const bool bTeleport = FVector::Distance(Frame.Camera, LastCamera) > TeleportCm;
	LastCamera = Frame.Camera;
	TArray<FTransform> Transforms;
	Transforms.Reserve(Clusters);
	for (FCluster& Cluster : ClusterStates)
	{
		if (!bSpawned || bTeleport)
		{
			Spawn(Cluster, Frame.Camera, *Selected);
			Cluster.Age = Random.FRand() * Cluster.Life;   // the first clusters do not respawn at one time
		}
		Cluster.Age += Frame.DeltaSeconds;
		if (Cluster.Age >= Cluster.Life || FVector::Distance(Cluster.Center, Frame.Camera) > RetainCm)
		{
			Spawn(Cluster, Frame.Camera, *Selected);
		}
		else
		{
			// A direct displacement (no gravity acceleration; 0x5FF18A7), then the wind x the wind scale x the multiplier.
			Cluster.Center.Z -= env::snow_fall_speed(Selected->GravityScale, Cluster.Multiplier) * 100.0 * Frame.DeltaSeconds;
			Cluster.Center += Frame.Wind * (Selected->WindScale * Cluster.Multiplier * 100.0 * Frame.DeltaSeconds);
		}
		Transforms.Add(FTransform(Cluster.Center));
	}
	bSpawned = true;
	Component->BatchUpdateInstancesTransforms(0, Transforms, true, true, false);
	Component->SetVisibility(true);
	UMaterialInstanceDynamic* M = Material.Get();
	M->SetScalarParameterValue(TEXT("Count"), static_cast<float>(Count));
	M->SetScalarParameterValue(TEXT("FlakeSize"), static_cast<float>(Selected->FlakeSize));
	M->SetScalarParameterValue(TEXT("TanHalfFov"), static_cast<float>(FMath::Tan(FMath::DegreesToRadians(Frame.HorizontalFov * 0.5))));
	M->SetScalarParameterValue(TEXT("Turbulence"), static_cast<float>(Selected->Turbulence));
	M->SetScalarParameterValue(TEXT("TurbulenceFreq"), static_cast<float>(Selected->TurbulenceFreq));
	M->SetVectorParameterValue(TEXT("Light"), Frame.Light * static_cast<float>(Selected->FallBrightness));
	M->SetVectorParameterValue(TEXT("LightToward"), FLinearColor(Frame.LightToward));
	// The roof map relative to the camera (the material reads the camera-relative world position of each flake).
	FLinearColor Origin = Occlusion.GetOrigin();
	Origin.R = static_cast<float>(Occlusion.GetCorner().X - Frame.Camera.X);
	Origin.G = static_cast<float>(Occlusion.GetCorner().Y - Frame.Camera.Y);
	Origin.A = bRoof ? Origin.A : 0.0f;
	M->SetVectorParameterValue(TEXT("RoofOrigin"), Origin);
	M->SetScalarParameterValue(TEXT("CameraZ"), static_cast<float>(Frame.Camera.Z));
	if (UTexture* Roof = Occlusion.GetTexture())
	{
		M->SetTextureParameterValue(TEXT("RoofMap"), Roof);
	}
}

void FSwcSnow::UpdateSurface(UWorld& World, const FSwcSnowVolume* Surface, bool bRoof)
{
	if (!bCollectionLoaded)
	{
		bCollectionLoaded = true;
		Collection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/SwcGameContent/Environment/MPC_SwcWeather.MPC_SwcWeather"), nullptr, LOAD_NoWarn);
	}
	UMaterialParameterCollectionInstance* Instance = Collection.IsValid() ? World.GetParameterCollectionInstance(Collection.Get()) : nullptr;
	if (Instance == nullptr)
	{
		return;
	}
	if (Surface == nullptr)
	{
		// No volume near: the function gives its inputs back (SnowAmounts.w 0).
		Instance->SetVectorParameterValue(TEXT("SnowAmounts"), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		return;
	}
	const FVector Center = ToUnreal(Surface->Center);
	Instance->SetVectorParameterValue(TEXT("SnowCenter"), FLinearColor(static_cast<float>(Center.X), static_cast<float>(Center.Y),
		static_cast<float>(Center.Z), static_cast<float>(1.0 / FMath::Max(Surface->Radius * 100.0, 0.1))));
	Instance->SetVectorParameterValue(TEXT("SnowAmounts"), FLinearColor(static_cast<float>(Surface->SnowAmount), static_cast<float>(Surface->FrostAmount),
		static_cast<float>(Surface->SurfaceFreezing), 1.0f));
	const float Gain = CVarSnowSurfaceGain.GetValueOnGameThread();
	Instance->SetVectorParameterValue(TEXT("SnowBrightness"), FLinearColor(static_cast<float>(Surface->SnowBrightness) * Gain,
		static_cast<float>(Surface->FrostBrightness), 0.0f, 0.0f));
	FLinearColor Origin = Occlusion.GetOrigin();
	Origin.A = bRoof ? Origin.A : 0.0f;
	Instance->SetVectorParameterValue(TEXT("RoofOrigin"), Origin);
}

FString FSwcSnow::Describe() const
{
	const FString SurfaceText = SurfaceName.IsEmpty() ? FString(TEXT("no surface snow")) : FString::Printf(TEXT("the surface snow of %s"), *SurfaceName);
	if (Count <= 0)
	{
		return SurfaceText + TEXT("; ") + (VolumeName.IsEmpty() ? TEXT("no falling snow") : FString::Printf(TEXT("no falling snow (%s, weight %.3f)"), *VolumeName, Weight)) + TEXT("; ") + Occlusion.Describe();
	}
	return SurfaceText + FString::Printf(TEXT("; falling snow %s: weight %.3f, %d flakes in each of %d clusters; %s"), *VolumeName, Weight, Count, Clusters, *Occlusion.Describe());
}
