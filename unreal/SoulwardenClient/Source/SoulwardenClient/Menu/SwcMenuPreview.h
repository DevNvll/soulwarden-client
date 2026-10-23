#pragma once

#include "CoreMinimal.h"
#include "Character/SwcPlayerLooks.h"
#include "Menu/SwcMenuCurves.h"
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
	/** The clip plays once and keeps its last pose (the creation clip character_create_player, stage Q3); a new body of a look
	 *  change goes on at the clip time of the first body (else each change starts the clip again). */
	void SetClipOnce(bool bOnce) { bClipOnce = bOnce; ClipStart = -1.0; }
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
	/**
	 * Reads the menu scene record (SWC_CONTENT/frontend/nighthaven.json, tools/convert-frontend.sh scene): the place and the
	 * rotation of the Player Entity, MainCamera (its world transform at the start, its FOV and clip planes, its parent), and the
	 * tracks of the camera sequences. @return False with no record (then the fixed character view stays).
	 */
	bool LoadScene(const FString& Path);
	/** The same with the entity names of an other record (stage Q3: the creation level FTUE_V2_CC, Player_Entity_Int,
	 *  Character_Create_Camera, #-Camera_Transitions): the body, the camera and its node in the sequences, the parent of the camera. */
	bool LoadScene(const FString& Path, const FString& PlayerName, const FString& CameraName, const FString& ParentName);
	/** @return True when the meshes of the scene are placed (or the scene has no record): the menu can show (stage F6). */
	bool IsScenePlaced() const { return bScenePlaced; }
	/** Plays a camera sequence of the scene from its start (CinematicRequestBus PlaySequenceByName of the landing screen). At its
	 *  end the camera keeps the last pose. @return False for a sequence with no MainCamera track. */
	bool PlaySequence(const FString& Name);
	void StopSequence(const FString& Name);
	virtual void Tick(float DeltaSeconds) override;
	/** Clears the menu mode of the environment (the token of PLAY removes the actor). */
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	/** Sets the menu mode of the client environment from the record ("environment": the preset and the hour of the TOD entity, the
	 *  sun path and the moon of the menu level). */
	void SetEnvironment(const TSharedPtr<class FJsonObject>& Doc);
	void Build(int32 Generation, FSwcPlayerLook Look, FString BaseMesh, FString IdleAnimation);
	/** Places the camera at a game world position and Euler angles (degrees; the rotation Rz Ry Rx of the game). */
	void PlaceCamera(const FVector& GameWorld, const FVector& EulerDegrees);
	/** The bounded scene subset of the record: the static meshes (an asynchronous load), the ground patch, and the lights. */
	void BuildScene(const TSharedPtr<class FJsonObject>& Doc);
	void BuildGround(const TSharedPtr<class FJsonObject>& Ground);
	void BuildLights(const TArray<TSharedPtr<class FJsonValue>>& Lights);

	UPROPERTY()
	TArray<TObjectPtr<UActorComponent>> SceneParts;
	/** The particle effects of the scene (Effects/SwcEffects: pooled components, stopped at the end). */
	TArray<TWeakObjectPtr<class UNiagaraComponent>> SceneEffects;
	TSharedPtr<FStreamableHandle> SceneLoading;
	bool bScenePlaced = false;
	/** The content folder of the scene record (its pictures). */
	FString SceneContent;
	/** The name of the camera entity of the record (its record and its node in the sequences). */
	FString CameraEntity = TEXT("MainCamera");
	/** The vertical FOV of the camera (degrees): 55 for the menu (F4), the record value for the creation camera (36). */
	double SceneVerticalFov = 55.0;
	/** SetClipOnce: the clip plays once; ClipStart is the world time of its start (-1: not started). */
	bool bClipOnce = false;
	double ClipStart = -1.0;

	struct FSequence
	{
		float End = 0.0f;
		/** PX, PY, PZ, RX, RY, RZ of MainCamera: local to its parent (Main_layer). */
		swc::menu::FCurve Tracks[6];
	};
	TMap<FString, FSequence> Sequences;
	/** The Player Entity in game world space (m), and the translation of the parent of MainCamera. */
	FVector PlayerWorld = FVector::ZeroVector;
	FVector CameraParent = FVector::ZeroVector;
	FString Playing;
	float PlayTime = 0.0f;

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
