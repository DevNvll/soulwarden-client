#pragma once

#include "CoreMinimal.h"
#include "WaterBodyCustomActor.h"
#include "WaterBodyCustomComponent.h"
#include "WaterBodyLakeActor.h"
#include "WaterBodyLakeComponent.h"
#include "WaterBodyRiverActor.h"
#include "WaterBodyRiverComponent.h"

#include "SwcWaterBodies.generated.h"

/*
 * The water bodies of the Unreal Water plugin that the client spawns at run time from the source shapes (docs/WATER.md "The Unreal
 * water", -SwcWaterMode=Unreal; Water/SwcWaterUnreal.cpp). The plugin makes the render data of a river, a lake, or a custom body only in
 * the editor (UWaterBodyComponent::UpdateAll: "GIsEditor || IsBodyDynamic()"); the ocean is dynamic. These components are dynamic: their
 * render data (the water info meshes, the river and lake meshes) is made at run time, as for the ocean. They are movable (the plugin
 * makes render data after BeginPlay only for a component that allows dynamic data changes) and never change the Landscape. The actors
 * select the components (the way of the plugin: "create a new WaterBodyActor class ... to override").
 */

/**
 * Builds the two water info meshes of a body at run time (the surface and the dilated surface of UWaterBodyComponent::GenerateWaterBodyMesh,
 * with normals and tangents, as FWaterBodyMeshBuilder::BuildWaterInfoMeshes of the plugin) and marks its water zone for a rebuild. The
 * plugin builds these meshes only in the editor (WITH_EDITOR: a cooked game has only the meshes of bodies that were placed and cooked);
 * the water info texture and the water mesh of a spawned body need them. No conservative raster data: the CPU water quadtree
 * (r.Water.WaterMesh.GPUQuadTree 0, the default) does not use it. @return False when the body made no triangle.
 */
bool SwcBuildWaterInfoMeshes(UWaterBodyComponent& Body);

UCLASS(MinimalAPI)
class USwcWaterBodyRiverComponent : public UWaterBodyRiverComponent
{
	GENERATED_BODY()

public:
	USwcWaterBodyRiverComponent(const FObjectInitializer& ObjectInitializer);
	virtual bool IsBodyDynamic() const override { return true; }
};

UCLASS(MinimalAPI)
class USwcWaterBodyLakeComponent : public UWaterBodyLakeComponent
{
	GENERATED_BODY()

public:
	USwcWaterBodyLakeComponent(const FObjectInitializer& ObjectInitializer);
	virtual bool IsBodyDynamic() const override { return true; }
};

UCLASS(MinimalAPI)
class USwcWaterBodyCustomComponent : public UWaterBodyCustomComponent
{
	GENERATED_BODY()

public:
	USwcWaterBodyCustomComponent(const FObjectInitializer& ObjectInitializer);
	virtual bool IsBodyDynamic() const override { return true; }
};

UCLASS(MinimalAPI, NotPlaceable, Transient)
class ASwcWaterBodyRiver : public AWaterBodyRiver
{
	GENERATED_BODY()

public:
	ASwcWaterBodyRiver(const FObjectInitializer& ObjectInitializer);
};

UCLASS(MinimalAPI, NotPlaceable, Transient)
class ASwcWaterBodyLake : public AWaterBodyLake
{
	GENERATED_BODY()

public:
	ASwcWaterBodyLake(const FObjectInitializer& ObjectInitializer);
};

UCLASS(MinimalAPI, NotPlaceable, Transient)
class ASwcWaterBodyCustom : public AWaterBodyCustom
{
	GENERATED_BODY()

public:
	ASwcWaterBodyCustom(const FObjectInitializer& ObjectInitializer);
};
