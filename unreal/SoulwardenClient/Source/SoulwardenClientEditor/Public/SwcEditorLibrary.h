#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SwcEditorLibrary.generated.h"

class AActor;
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
};
