#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/WorldSubsystem.h"

#include "SwcLumenTest.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * The Lumen mesh data test (stage E2, docs/ENVIRONMENT.md): with the console variable swc.LumenTest 1 (-SwcLumenTest at the start),
 * the static mesh components of the level that use a mesh of the test list get its copy with a mesh distance field and Lumen cards
 * (/SwcGameContent/Test/E2 of tools/lumen-test.sh; the map SWC_CONTENT/environment/lumen-test.json); swc.LumenTest 0 gives the
 * originals back (a compare in one run). A copy replaces each use of its mesh (the shared meshes of the house kit change in all
 * houses). After a swap the client waits for the distance fields and the cards of the copies (USwcLumenTestLibrary). The world
 * meshes stay with no distance field and no cards. Off by default.
 */
/** The wait for the mesh data of Lumen (stage E2): the distance fields and the Lumen cards of the static meshes are built on worker
 *  threads; a measure needs them complete. */
UCLASS()
class SOULWARDENCLIENT_API USwcLumenTestLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Blocks until the distance field and the Lumen card builds of all meshes are complete (an editor build; else nothing).
	 *  @return The seconds of the wait. */
	UFUNCTION(BlueprintCallable, Category = "Soulwarden|Environment")
	static float WaitForMeshBuilds();
};

UCLASS()
class SOULWARDENCLIENT_API USwcLumenTestSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void SwapBack();

	bool bLoaded = false;
	/** The original mesh object path to its copy (loaded at the first use). */
	TMap<FString, FString> Copies;
	UPROPERTY()
	TMap<FString, TObjectPtr<UStaticMesh>> Loaded;
	/** The components that use a copy, with their original mesh (KeepOriginals holds the originals). */
	TMap<TWeakObjectPtr<UStaticMeshComponent>, UStaticMesh*> Originals;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> KeepOriginals;
	double NextScan = 0.0;
	int32 LastLogged = -1;
};
