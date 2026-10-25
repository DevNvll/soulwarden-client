#include "Water/SwcWaterBodies.h"

#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshTangents.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "WaterSplineComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "WaterRuntimeSettings.h"
#include "WaterZoneActor.h"

namespace
{
/** The common values of a body of the client: movable (render data after BeginPlay), no change of the Landscape. */
void SetUpBody(UWaterBodyComponent& Component)
{
	Component.Mobility = EComponentMobility::Movable;
	Component.bAffectsLandscape = false;
}
/** The trace channel of the underwater post process of the plugin (SwcWaterTraceChannel), before the first world makes its water subsystem
 *  (UWaterSubsystem reads the setting in its Initialize); only with -SwcWaterMode=Unreal. */
struct FSwcWaterTraceSetting
{
	FSwcWaterTraceSetting()
	{
		FCoreDelegates::OnPostEngineInit.AddLambda([]()
		{
			FString Mode;
			if (FParse::Value(FCommandLine::Get(), TEXT("-SwcWaterMode="), Mode) && Mode.Equals(TEXT("Unreal"), ESearchCase::IgnoreCase))
			{
				GetMutableDefault<UWaterRuntimeSettings>()->CollisionChannelForWaterTraces = SwcWaterTraceChannel;
			}
		});
	}
} GSwcWaterTraceSetting;
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
	// Only a body that the water mesh draws goes into the water info (a custom body draws its own mesh).
	if (!Body.AffectsWaterInfo())
	{
		return true;
	}
	bool bMade = Body.GenerateWaterBodyMesh(Mesh, &Dilated) && Mesh.TriangleCount() > 0;
	// The dilated outline of a lake or the ocean (ShapeDilation / 2 outward, 20 m by default) fails for some concave outlines and for the
	// small island of the ocean of the client: then a smaller dilation, and at last none (the water info has no margin there).
	for (const float Dilation : {Body.ShapeDilation * 0.25f, 0.0f})
	{
		if (bMade || (Body.GetWaterBodyType() != EWaterBodyType::Lake && Body.GetWaterBodyType() != EWaterBodyType::Ocean))
		{
			break;
		}
		Mesh.Clear();
		Dilated.Clear();
		for (FDynamicMesh3* Each : {&Mesh, &Dilated})
		{
			Each->EnableAttributes();
			Each->Attributes()->EnablePrimaryColors();
			Each->Attributes()->EnableTangents();
		}
		Body.ShapeDilation = Dilation;
		bMade = Body.GenerateWaterBodyMesh(Mesh, Dilation > 0.0f ? &Dilated : nullptr) && Mesh.TriangleCount() > 0;
		UE_CLOG(bMade, LogTemp, Log, TEXT("water (Unreal): %s: the dilation %.0f m"), *GetNameSafe(Body.GetOwner()), Dilation / 100.0f);
	}
	if (!bMade)
	{
		const UWaterSplineComponent* Spline = Body.GetWaterSpline();
		UE_LOG(LogTemp, Warning, TEXT("water (Unreal): %s: no water info mesh (type %d, %d spline points, %.1f m long, %d triangles, %d dilated)"),
			*GetNameSafe(Body.GetOwner()), static_cast<int32>(Body.GetWaterBodyType()), Spline ? Spline->GetNumberOfSplinePoints() : -1,
			Spline ? Spline->GetSplineLength() / 100.0 : 0.0, Mesh.TriangleCount(), Dilated.TriangleCount());
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

void SwcWaterCollisionAsOverlap(UWaterBodyComponent& Body)
{
	for (UPrimitiveComponent* Component : Body.GetCollisionComponents(/* bInOnlyEnabledComponents = */ false))
	{
		if (Component != nullptr)
		{
			Component->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
			Component->SetCollisionResponseToChannel(SwcWaterTraceChannel, ECR_Block);
		}
	}
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
