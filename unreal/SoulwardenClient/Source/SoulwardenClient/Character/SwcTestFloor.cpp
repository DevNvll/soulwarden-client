#include "Character/SwcTestFloor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

namespace
{
/** The engine cube: 100 cm on each side, its pivot at its center. */
const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
const TCHAR* GridMaterialPath = TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial");

/** A box: the center, the rotation, and the size in cm. */
void Box(UWorld* World, const FVector& Center, const FRotator& Rotation, const FVector& Size, const TCHAR* Name)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubePath);
	UMaterialInterface* Grid = LoadObject<UMaterialInterface>(nullptr, GridMaterialPath);
	FActorSpawnParameters Parameters;
	Parameters.Name = MakeUniqueObjectName(World, AStaticMeshActor::StaticClass(), Name);
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Center, Rotation, Parameters);
	if (Actor == nullptr || Cube == nullptr)
	{
		return;
	}
	UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(Cube);
	if (Grid != nullptr)
	{
		Mesh->SetMaterial(0, Grid);
	}
	Actor->SetActorScale3D(Size / 100.0);
}
} // namespace

bool SwcTestFloor::HasGround(UWorld* World, const FVector& Feet)
{
	FHitResult Hit;
	return World->LineTraceSingleByChannel(Hit, Feet + FVector(0.0, 0.0, 100.0), Feet - FVector(0.0, 0.0, 5000.0), ECC_WorldStatic);
}

void SwcTestFloor::Spawn(UWorld* World, const FVector& Feet, const FVector& Forward)
{
	const FVector Ahead = Forward.GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead);
	const double Yaw = Ahead.Rotation().Yaw;
	// The floor: 100 x 100 m, 1 m thick, its top at the feet.
	Box(World, Feet - FVector(0.0, 0.0, 50.0), FRotator(0.0, Yaw, 0.0), FVector(10000.0, 10000.0, 100.0), TEXT("SwcTestFloor"));
	// Three lanes to the right, each one 3 m wide, for a run in the forward direction (tests/drive/rules.txt). A ramp rises
	// toward the forward direction from its start; a step is 2 m deep.
	const auto Ramp = [&](double Degrees, double Length, double Side, double Start, const TCHAR* Name)
	{
		const double Angle = FMath::DegreesToRadians(Degrees);
		const double Thickness = 20.0;
		const FVector Center = Feet + Right * Side + Ahead * (Start + Length / 2.0 * FMath::Cos(Angle))
			+ FVector(0.0, 0.0, Length / 2.0 * FMath::Sin(Angle) - Thickness / 2.0 * FMath::Cos(Angle));
		Box(World, Center, FRotator(Degrees, Yaw, 0.0), FVector(Length, 300.0, Thickness), Name);
	};
	const auto Step = [&](double Height, double Side, double Start, const TCHAR* Name)
	{
		Box(World, Feet + Right * Side + Ahead * (Start + 100.0) + FVector(0.0, 0.0, Height / 2.0), FRotator(0.0, Yaw, 0.0),
			FVector(200.0, 300.0, Height), Name);
	};
	// Lane A, 25 m to the right: a ramp of 30 degrees from 6 m (10 m long, 5 m high), then a step of 0.5 m at 22 m.
	Ramp(30.0, 1000.0, 2500.0, 600.0, TEXT("SwcTestRamp30"));
	Step(50.0, 2500.0, 2200.0, TEXT("SwcTestStep50"));
	// Lane B, 31 m to the right: a ramp of 75 degrees at 34 m. Lane C, 37 m to the right: a step of 0.8 m at 38 m.
	Ramp(75.0, 400.0, 3100.0, 3400.0, TEXT("SwcTestRamp75"));
	Step(80.0, 3700.0, 3800.0, TEXT("SwcTestStep80"));
}
