// A debug command of the world views (AST-261): the meshes on the view ray through a point of the screen. It tests the bounds of
// each instance of the instanced mesh components and the bounds of the other static mesh components (no collision needed: the cell
// meshes have none), and logs the nearest hits with the mesh, the material of slot 0, the component, and the actor.

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcPickView, Log, All);

namespace
{
struct FPickHit
{
	double Distance = 0.0;
	FString Text;
};

/** @return true and the entry distance (cm) when the ray (Origin, Direction) passes through Box. */
bool RayHitsBox(const FBox& Box, const FVector& Origin, const FVector& Direction, double& OutDistance)
{
	double Near = 0.0, Far = UE_DOUBLE_BIG_NUMBER;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (FMath::Abs(Direction[Axis]) < 1e-9)
		{
			if (Origin[Axis] < Box.Min[Axis] || Origin[Axis] > Box.Max[Axis])
			{
				return false;
			}
			continue;
		}
		double T1 = (Box.Min[Axis] - Origin[Axis]) / Direction[Axis];
		double T2 = (Box.Max[Axis] - Origin[Axis]) / Direction[Axis];
		if (T1 > T2)
		{
			Swap(T1, T2);
		}
		Near = FMath::Max(Near, T1);
		Far = FMath::Min(Far, T2);
		if (Near > Far)
		{
			return false;
		}
	}
	OutDistance = Near;
	return true;
}

FString Describe(const UStaticMeshComponent* Component, const UStaticMesh* Mesh, int32 Instance, const FBox& Box)
{
	const UMaterialInterface* Material = Component->GetMaterial(0);
	return FString::Printf(TEXT("mesh %s, material %s, instance %d, size m %.0f x %.0f x %.0f, nanite %d, component %s, actor %s"),
		*GetNameSafe(Mesh), *GetNameSafe(Material), Instance, Box.GetSize().X / 100.0, Box.GetSize().Y / 100.0, Box.GetSize().Z / 100.0,
		Mesh->HasValidNaniteData() ? 1 : 0, *Component->GetName(), *GetNameSafe(Component->GetOwner()));
}

void PickView(const TArray<FString>& Args, UWorld* World)
{
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr || Controller->PlayerCameraManager == nullptr)
	{
		return;
	}
	// The point of the screen: fractions of the width and the height (0.5 0.5: the centre).
	const double X = Args.Num() > 0 ? FCString::Atod(*Args[0]) : 0.5;
	const double Y = Args.Num() > 1 ? FCString::Atod(*Args[1]) : 0.5;
	const int32 Count = Args.Num() > 2 ? FMath::Max(FCString::Atoi(*Args[2]), 1) : 12;
	const FMinimalViewInfo& View = Controller->PlayerCameraManager->GetCameraCacheView();
	const double Aspect = View.AspectRatio > 0.0f ? View.AspectRatio : 16.0 / 9.0;
	const double TanHalfH = FMath::Tan(FMath::DegreesToRadians(View.FOV * 0.5));
	const FRotationMatrix Rotation(View.Rotation);
	const FVector Direction = (Rotation.GetUnitAxis(EAxis::X) + Rotation.GetUnitAxis(EAxis::Y) * ((X * 2.0 - 1.0) * TanHalfH)
		+ Rotation.GetUnitAxis(EAxis::Z) * ((1.0 - Y * 2.0) * TanHalfH / Aspect)).GetSafeNormal();
	const FVector Origin = View.Location;
	TArray<FPickHit> Hits;
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		const UStaticMeshComponent* Component = *It;
		const UStaticMesh* Mesh = Component->GetStaticMesh();
		if (Component->GetWorld() != World || Mesh == nullptr || !Component->IsVisible() || Component->GetOwner() == nullptr
			|| Component->GetOwner()->IsHidden())
		{
			continue;
		}
		const FBox Local = Mesh->GetBoundingBox();
		if (const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component))
		{
			for (int32 Index = 0; Index < Instanced->GetInstanceCount(); ++Index)
			{
				FTransform Transform;
				Instanced->GetInstanceTransform(Index, Transform, true);
				const FBox Box = Local.TransformBy(Transform);
				double Distance;
				if (RayHitsBox(Box, Origin, Direction, Distance) && Distance < 100000.0)
				{
					Hits.Add({Distance, Describe(Component, Mesh, Index, Box)});
				}
			}
		}
		else
		{
			const FBox Box = Component->Bounds.GetBox();
			double Distance;
			if (RayHitsBox(Box, Origin, Direction, Distance) && Distance < 100000.0)
			{
				Hits.Add({Distance, Describe(Component, Mesh, -1, Box)});
			}
		}
	}
	Hits.Sort([](const FPickHit& A, const FPickHit& B) { return A.Distance < B.Distance; });
	UE_LOG(LogSwcPickView, Log, TEXT("pick view %.3f %.3f: %d hits of the bounds (camera %s, direction %s)"), X, Y, Hits.Num(),
		*Origin.ToString(), *Direction.ToString());
	for (int32 Index = 0; Index < FMath::Min(Hits.Num(), Count); ++Index)
	{
		UE_LOG(LogSwcPickView, Log, TEXT("pick view   %.1f m: %s"), Hits[Index].Distance / 100.0, *Hits[Index].Text);
	}
}

FAutoConsoleCommandWithWorldAndArgs CommandPickView(TEXT("swc.PickView"),
	TEXT("swc.PickView X Y [N]: log the N (12) nearest meshes whose bounds the view ray through the screen point (X, Y: fractions of the width and the ")
	TEXT("height) passes, the nearest first (the instances of the instanced mesh components; a debug aid of AST-261)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PickView));
} // namespace
