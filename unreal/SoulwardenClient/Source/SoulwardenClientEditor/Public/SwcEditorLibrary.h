#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SwcEditorLibrary.generated.h"

class AActor;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UStaticMesh;

/** Helpers for the editor scripts of the world track (Python: unreal.SwcEditorLibrary). Editor only. */
UCLASS()
class SOULWARDENCLIENTEDITOR_API USwcEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Waits for all asset and shader compilation and loads all texture mips. A commandlet does not tick, thus a
	 *  script calls this before a render. */
	UFUNCTION(BlueprintCallable, Category = "Soulwarden|Editor")
	static void FinishCompilation();

	/** Renders a scene capture Frames times (temporal effects need more than one frame) and writes its render target
	 *  as PNG. @return False when the capture has no 8-bit render target or the file cannot be written. */
	UFUNCTION(BlueprintCallable, Category = "Soulwarden|Editor")
	static bool CaptureToPng(USceneCaptureComponent2D* Capture, const FString& Path, int32 Frames = 1);

	/** Spawns an actor of a class in the editor world (the placement subsystem of the editor, which
	 *  EditorActorSubsystem uses, does not exist in a commandlet). */
	UFUNCTION(BlueprintCallable, Category = "Soulwarden|Editor")
	static AActor* SpawnActorOfClass(TSubclassOf<AActor> Class, const FTransform& Transform, const FString& Label);

	/** Spawns a static mesh actor (static mobility) in the editor world. */
	UFUNCTION(BlueprintCallable, Category = "Soulwarden|Editor")
	static AActor* SpawnMeshActor(UStaticMesh* Mesh, const FTransform& Transform, const FString& Label);

	/** Makes one Landscape in the editor world from a raw file of 16-bit heights (little-endian, row 0 = the smallest
	 *  Y): ComponentsPerSide x ComponentsPerSide components of one section of QuadsPerSection quads. In a World
	 *  Partition world, the Landscape becomes streaming proxies of GridSize x GridSize components (the steps of the
	 *  New Landscape tool of the editor); bAlwaysLoaded makes the proxies not spatially loaded (the terrain to the
	 *  horizon; the Landscape LOD makes far components cheap). @return The Landscape, or null when the file has a wrong
	 *  size. */
	UFUNCTION(BlueprintCallable, Category = "Soulwarden|Editor")
	static AActor* CreateLandscape(const FString& RawFile, int32 ComponentsPerSide, int32 QuadsPerSection, const FVector& Location,
		const FVector& Scale, UMaterialInterface* Material, const FString& Label, int32 GridSize = 2, bool bAlwaysLoaded = true);

	/** Sets the cell size and the loading range (cm) of the main runtime partition of the World Partition of the editor
	 *  world (a runtime hash set; the members are private: reflection). @return False when the world has none. */
	UFUNCTION(BlueprintCallable, Category = "Soulwarden|Editor")
	static bool ConfigureStreaming(int32 CellSize, int32 LoadingRange);

	/** Sets whether an actor of a World Partition level is spatially loaded (false: always loaded, for example the
	 *  sun and the sky). */
	UFUNCTION(BlueprintCallable, Category = "Soulwarden|Editor")
	static void SetSpatiallyLoaded(AActor* Actor, bool bSpatiallyLoaded);

	/** Spawns the instance actors of a file of the converter (converter/swc_convert/cells.py): each actor has one
	 *  instanced static mesh component for each mesh of its cell, with the instances in world space.
	 *  @return The number of actors, or -1 when the file cannot be read. */
	UFUNCTION(BlueprintCallable, Category = "Soulwarden|Editor")
	static int32 SpawnInstanceActors(const FString& File);
};
