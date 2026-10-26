#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/SwcCameraData.h"
#include "SwcCameraManager.generated.h"

class ASwcPlayerCharacter;

/** An entity that the target lock can take: its interest number and its feet (Unreal cm). */
struct FSwcLockCandidate
{
	uint32 Id = 0;
	FVector Position = FVector::ZeroVector;
	/** The lock point (the CameraLockTargetComponent of the slice of a creature; stage P3.5) and the pitch limits of the observer of
	 *  its client facet (degrees; m_limitObserverPitch, m_observerPitchMin, m_observerPitchMax). */
	FVector LockPoint = FVector::ZeroVector;
	bool bLimitPitch = false;
	float PitchMin = 0.0f;
	float PitchMax = 0.0f;
};

/**
 * The camera of the player with the values of the game data (research feel-values.md R1; docs/CAMERA.md): the camera
 * state of the camera action list (MidCamera, DynamicCamera in a sprint, LockedCamera with a target lock, AimCamera,
 * DeathCamera), the SmoothCD blend of its values, the pivot with its Z smoothing, the zoom, the collision cast, the
 * FOV option, the pitch limits, and the target lock of the CameraLockComponent of the player slice.
 */
UCLASS()
class SOULWARDENCLIENT_API ASwcCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	/** The mouse wheel: notches up (positive) move the camera nearer. */
	void AddZoom(float Notches);
	/** A camera state that replaces the state of the action list (drive scripts; empty: none). */
	void SetForcedState(const FString& State);
	void SetDead(bool bInDead) { bDead = bInDead; }
	/**
	 * The camera of a conversation (stage P4.8, docs/PERSONS.md "The Talk"): a cut to the camera of the line (Unreal world space) and
	 * its vertical FOV (degrees), in place of the state camera; the state camera goes on below it. bOn false: the cut back.
	 */
	void SetSceneView(bool bOn, const FVector& Location = FVector::ZeroVector, const FVector& LookAt = FVector::ZeroVector, float VerticalFov = 0.0f);
	bool HasSceneView() const { return bScene; }
	/**
	 * The camera requests of the screens (swc/camera.lua; docs/UI.md "The camera of the screens"). SetUiState: the top state override
	 * of the stack of the scripts (empty: none) and its blend in seconds; the drive state (SetForcedState) wins over it, and a name that
	 * the camera data does not have is ignored.
	 */
	void SetUiState(const FString& State, float Blend);
	/**
	 * The look-at point of a screen (JavCameraControllerRequestBus SetCameraLookAt; Unreal world space). While a UI state is on, the
	 * view turns toward it from the pivot: the orbit of the Talk (UI_Conversation and conversationscreenv2.luac GetNpcLookAtPosition,
	 * docs/PERSONS.md "The Talk camera"). bOn false: none.
	 */
	void SetUiLookAt(bool bOn, const FVector& LookAt = FVector::ZeroVector);
	/** The depth of field of a screen (JavelinCameraRequestBus SetDepthOfField: the five native values; bOn false: off). */
	void SetUiDepthOfField(bool bOn, float Distance = 0.0f, float Blur = 0.0f, float Near = 0.0f, float Far = 0.0f, float Range = 0.0f);
	/** The camera of an in-game scene (stage Q5, Scenes/SwcScenePlayer): a place, a rotation, and a vertical FOV (degrees). It wins over the
	 *  camera of a conversation. */
	void SetSceneCamera(bool bOn, const FVector& Location, const FRotator& Rotation, float VerticalFov);
	/** The entities of the backend that the target lock can take, each frame. */
	void SetLockCandidates(TArray<FSwcLockCandidate>&& InCandidates) { Candidates = MoveTemp(InCandidates); }
	/** Development runs draw a marker at each candidate (the creatures have no model before stage 1.9). */
	void SetDrawCandidates(bool bDraw) { bDrawCandidates = bDraw; }

	/** The target lock key (toggle), the next target to the right (+1) or to the left (-1), and the mouse while locked. */
	void ToggleLock();
	void CycleTarget(int32 Direction);
	void AddLockMouse(const FVector2D& Counts);
	bool IsLocked() const { return LockId != 0; }

	/** The values of the current state for the controller: the look sensitivity, free look. */
	FVector2D GetSensitivity() const { return FVector2D(HorizontalSensitivity.Value, VerticalSensitivity.Value); }
	/** IsADS of the current state (the ADS sensitivity options of the player apply). */
	bool IsAds() const { return bAds; }
	bool AllowsFreeLook() const;
	const FString& GetStateName() const { return StateName; }
	/** The zoom option and the smoothed zoom (m), for the camera state log of the checks (AST-251). */
	float GetSavedZoom() const { return SavedZoom; }
	float GetZoom() const { return Zoom; }

protected:
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

private:
	/** @return The FOV option of the player: the row Field of View of the Options screen (UI/SwcGameSettings), else 57. */
	float PlayerFov() const;
	struct FBlend
	{
		float Value = 0.0f;
		float Rate = 0.0f;
		float Target = 0.0f;
		void Set(float InTarget, bool bAtOnce)
		{
			Target = InTarget;
			if (bAtOnce)
			{
				Value = InTarget;
				Rate = 0.0f;
			}
		}
	};

	void SelectState(const ASwcPlayerCharacter& Character);
	/** The depth of field of a screen on the view (SetUiDepthOfField), or no override of the depth of field. */
	void ApplyUiDepthOfField(FMinimalViewInfo& POV) const;
	/** The turn of the body in the inventory states (UpdateViewTarget): toward the camera at the open, to the view at the end. */
	void UpdateBodyTurn(ASwcPlayerCharacter& Character, float CameraYaw, float DeltaTime);
	void UpdateZ(const FVector& TargetPivot, const ASwcPlayerCharacter& Character, const FSwcCameraState& State, float DeltaTime);
	FRotator UpdateLock(const FVector& Pivot, const ASwcPlayerCharacter& Character, float DeltaTime);
	const FSwcLockCandidate* FindCandidate(uint32 Id) const;
	void Unlock(const TCHAR* Cause);

	FSwcCameraData Data;
	bool bLoaded = false;
	FString StateName;
	FString ForcedState;
	bool bDead = false;
	float BlendTime = 0.0f;
	FBlend Distance;
	FBlend OffsetSide;
	FBlend OffsetForward;
	FBlend OffsetUp;
	FBlend Fov;
	/** The FOV option that the current state uses (the row Field of View; PlayerFov). */
	float UserFov = 57.0f;
	FBlend PitchDown;
	FBlend PitchUp;
	FBlend HorizontalSensitivity;
	FBlend VerticalSensitivity;
	bool bAds = false;
	/** The zoom option of the player (m_cameraZoom, 2.0 m) and the smoothed zoom. */
	float SavedZoom = 2.0f;
	float Zoom = 2.0f;
	float ZoomRate = 0.0f;
	/** The pivot Z smoothing. */
	bool bHasPivot = false;
	FVector LastTargetPivot = FVector::ZeroVector;
	double SmoothedZ = 0.0;
	int32 OptionalCurve = 0;
	float OptionalTimer = 0.0f;
	/** The arm after the collision cast (cm), and the push-out after a hit. */
	float CurrentDistance = 0.0f;
	float DistanceRate = 0.0f;
	bool bPushOut = false;
	double LastHitTime = 0.0;
	/** The target lock. */
	TArray<FSwcLockCandidate> Candidates;
	bool bDrawCandidates = false;
	uint32 LockId = 0;
	FVector LockPoint = FVector::ZeroVector;
	FVector LockPointRate = FVector::ZeroVector;
	FQuat LockStart = FQuat::Identity;
	float LockBlend = 1.0f;
	float LockBlendRate = 0.0f;
	float LineOfSightLost = 0.0f;
	TArray<TPair<double, float>> LockMouse;
	double LockMouseIgnoreUntil = 0.0;
	FRotator LastRotation = FRotator::ZeroRotator;
	/** The camera requests of the screens: the top override and the blend of its change (negative: none since the last switch), the
	 *  look-at point, and the depth of field (the native values). */
	FString UiState;
	float UiBlend = -1.0f;
	bool bUiLookAt = false;
	FVector UiLookAt = FVector::ZeroVector;
	bool bUiDof = false;
	float UiDofDistance = 0.0f;
	float UiDofBlur = 0.0f;
	/** The turn of the body of the inventory states: on while the body turns to its target yaw (degrees), and the state of the last turn. */
	bool bBodyTurn = false;
	float BodyTurnYaw = 0.0f;
	float BodyTurnTime = 0.1f;
	bool bBodyFacesCamera = false;
	/** The camera of a conversation (SetSceneView). */
	bool bScene = false;
	bool bSceneCamera = false;
	FVector SceneCameraLocation = FVector::ZeroVector;
	FRotator SceneCameraRotation = FRotator::ZeroRotator;
	float SceneCameraFov = 40.0f;
	FVector SceneLocation = FVector::ZeroVector;
	FRotator SceneRotation = FRotator::ZeroRotator;
	float SceneFov = 0.0f;
};
