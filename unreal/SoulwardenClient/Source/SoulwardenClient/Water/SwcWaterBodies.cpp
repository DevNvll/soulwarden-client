#include "Water/SwcWaterBodies.h"

#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshTangents.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "WaterZoneActor.h"

namespace
{
/** The common values of a body of the client: movable (render data after BeginPlay), no change of the Landscape. */
void SetUpBody(UWaterBodyComponent& Component)
{
	Component.Mobility = EComponentMobility::Movable;
	Component.bAffectsLandscape = false;
}
} // namespace

bool SwcBuildWaterInfoMeshes(UWaterBodyComponent& Body)
{
	using namespace UE::Geometry;
	FDynamicMesh3 Mesh(EMeshComponents::None);
	FDynamicMesh3 Dilated(EMeshComponents::None);
	for (FDynamicMesh3* Each : {&Mesh, &Dilated})
	{
		Each->EnableAttributes();
		Each->Attributes()->EnablePrimaryColors();
		Each->Attributes()->EnableTangents();
	}
	if (!Body.GenerateWaterBodyMesh(Mesh, &Dilated))
	{
		return false;
	}
	for (FDynamicMesh3* Each : {&Mesh, &Dilated})
	{
		FMeshNormals::InitializeOverlayToPerVertexNormals(Each->Attributes()->PrimaryNormals());
		FMeshTangentsf::ComputeDefaultOverlayTangents(*Each);
	}
	// The info mesh components of the actor (AWaterBody: the default subobjects WaterInfoMeshComponent and DilatedWaterInfoMeshComponent; their
	// class UWaterBodyInfoMeshComponent is private to the plugin, a static mesh component).
	AActor* Owner = Body.GetOwner();
	UMaterialInterface* Material = Body.GetWaterInfoMaterialInstance();
	TInlineComponentArray<UStaticMeshComponent*> Components(Owner);
	int32 Built = 0;
	for (UStaticMeshComponent* Component : Components)
	{
		const bool bDilated = Component->GetFName() == TEXT("DilatedWaterInfoMeshComponent");
		if (!bDilated && Component->GetFName() != TEXT("WaterInfoMeshComponent"))
		{
			continue;
		}
		const FDynamicMesh3& Source = bDilated ? Dilated : Mesh;
		if (Source.TriangleCount() == 0)
		{
			Component->SetStaticMesh(nullptr);
			continue;
		}
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(&Source, Description);
		UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Owner, NAME_None, RF_Transient);
		StaticMesh->bSupportRayTracing = false;
		StaticMesh->GetStaticMaterials().Add(FStaticMaterial(Material));
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true;
		Params.bMarkPackageDirty = false;
		Params.bBuildSimpleCollision = false;
		Params.bCommitMeshDescription = false;
		// The CPU water quadtree reads the vertices of the info mesh.
		Params.bAllowCpuAccess = true;
		StaticMesh->BuildFromMeshDescriptions({&Description}, Params);
		Component->SetStaticMesh(StaticMesh);
		++Built;
	}
	Body.MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All, /* bInOnlyWithinWaterBodyBounds = */ false);
	return Built > 0;
}

USwcWaterBodyRiverComponent::USwcWaterBodyRiverComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetUpBody(*this);
}

USwcWaterBodyLakeComponent::USwcWaterBodyLakeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetUpBody(*this);
}

USwcWaterBodyCustomComponent::USwcWaterBodyCustomComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetUpBody(*this);
}

ASwcWaterBodyRiver::ASwcWaterBodyRiver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyRiverComponentClass = USwcWaterBodyRiverComponent::StaticClass();
}

ASwcWaterBodyLake::ASwcWaterBodyLake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyLakeComponentClass = USwcWaterBodyLakeComponent::StaticClass();
}

ASwcWaterBodyCustom::ASwcWaterBodyCustom(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyCustomComponentClass = USwcWaterBodyCustomComponent::StaticClass();
}
