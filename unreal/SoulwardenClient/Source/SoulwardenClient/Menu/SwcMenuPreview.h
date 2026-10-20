#pragma once

#include "CoreMinimal.h"
#include "Character/SwcPlayerLooks.h"
#include "GameFramework/Actor.h"
#include "SwcMenuPreview.generated.h"

class UCameraComponent;
class USkeletalMeshComponent;
struct FStreamableHandle;

/**
 * The character of the main menu (stage F3 of docs/FRONTEND.md): the body of the selected character from its menu document
 * (FSwcPlayerLooks::ResolveMenu), with no world pawn and no paperdoll. The base of the gender is the leader (not drawn, it plays
 * the idle clip); each part follows its pose (Leader Pose), as the world character. The assets load asynchronously; a newer
 * selection replaces an older load (the generation of the request), so an old body never replaces a new one.
 *
 * The camera of the character view: the end key of the camera sequence IntroToCharacter of the NightHaven scene relative to
 * the Player Entity (research frontend-native.md "Full curve parameters" and the Idle defaults): 1.118 m, -1.908 m, 1.078 m from
 * the feet in game space, pitch -3.5 and yaw 31.5 degrees; the body yaw -147.88 degrees. The vertical FOV of 55 degrees of
 * MainCamera (the fork axis is open: the study of FE7 gives the vertical convention as an inference). Stage F4 adds the scene.
 */
UCLASS()
class SOULWARDENCLIENT_API ASwcMenuPreview : public AActor
{
	GENERATED_BODY()

public:
	ASwcMenuPreview();

	/** Shows a look: the old parts stay until the assets of the new look are loaded. */
	void ShowLook(const FSwcPlayerLook& Look, const FString& BaseMesh, const FString& IdleAnimation);
	/** Removes the body (an invalid document, no character): no stale parts. */
	void Clear();
	/** The landing screen shows or hides the body (TransformBus SetWorldZ: the menu height or Z -1000). */
	void SetBodyVisible(bool bVisible);
	/** @return The count of the drawn parts (a log line and a check). */
	int32 PartCount() const { return Parts.Num(); }
	/** @return The generation of the shown look, and of the last request. */
	int32 ShownGeneration() const { return Shown; }
	int32 RequestedGeneration() const { return Requested; }
	/** Sets the horizontal FOV of the camera from the vertical FOV of the menu camera and the aspect of the viewport. */
	void UpdateFov(float Aspect);

private:
	void Build(int32 Generation, FSwcPlayerLook Look, FString BaseMesh, FString IdleAnimation);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Leader;
	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> Parts;
	TSharedPtr<FStreamableHandle> Loading;
	int32 Requested = 0;
	int32 Shown = 0;
	bool bBodyVisible = true;
};
