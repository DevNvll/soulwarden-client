#pragma once

#include "CoreMinimal.h"

/** One camera state of gamecamera.xml with its include chain resolved (research feel-values.md R1.3). Meters, degrees. */
struct FSwcCameraState
{
	FString Name;
	/** The arm behind the pivot (the zoom adds to it). */
	float Distance = 2.0f;
	/** The pivot from the feet of the character: to the right, forward, up. */
	FVector3f Offset = FVector3f(0.5f, 0.0f, 1.7f);
	/** The FOV of the state (vertical), and UseUserDefinedFov: the FOV option of the player replaces it. */
	float Fov = 50.0f;
	bool bUserFov = true;
	float PitchDown = -70.0f;
	float PitchUp = 60.0f;
	float MinZoom = 0.0f;
	float MaxZoom = 6.0f;
	/** The smooth time of a pull-in after a hit of the collision cast (0: at once). */
	float DistanceDampening = 0.0f;
	float HorizontalSensitivity = 1.0f;
	float VerticalSensitivity = 1.0f;
	bool bAllowFreeLook = true;
	bool bCollides = true;
	bool bZSmoothing = true;
	/** ZoomLagOverride, or a negative value: the global zoomLag. */
	float ZoomLag = -2.0f;
	float NearPlane = 0.1f;
	/** FromTransition: the blend time for a blend that comes from the named state. */
	TMap<FString, float> Transitions;
};

/**
 * The camera data of the client (SWC_CONTENT/player/camera.json, made by tools/convert-player.sh camera from
 * libs/camera/gamecamera.xml and the camera action list playeractions_camera.actionlist).
 */
class FSwcCameraData
{
public:
	/** Reads camera.json. @return False with a reason; the defaults are then the values of MidCamera (research R1.1). */
	bool Load(FString& OutReport);

	/** @return A state, or the default state (MidCamera) when the data has no state of this name. */
	const FSwcCameraState& State(const FString& Name) const;
	/** The ActivateCamera entry of the action list: its camera state and its SmoothTime. */
	bool Activation(const FString& Entry, FString& OutCamera, float& OutSmoothTime) const;

	/** The settings of gamecamera.xml that the client uses. */
	float ZoomAmount = 1.0f;
	float ZoomLag = 0.1f;
	float CastRadius = 0.084f;
	float FocusCastRadius = 0.098f;
	float CastExtension = 0.04f;
	float OffCollisionSmoothTime = 0.35f;
	TArray<float> ZCurveDefault = {-0.6f, 6.0f, -0.3f, 3.0f, 0.0f, 1.0f, 0.75f, 2.2f, 1.0f, 5.0f};
	TArray<float> ZCurveAirborne = {-0.6f, 7.0f, -0.25f, 5.0f, 0.0f, 1.0f, 0.5f, 1.0f, 1.0f, 2.2f};
	TArray<float> ZCurveAiming = {-0.5f, 10.0f, 0.0f, 8.0f, 0.5f, 10.0f};
	float ZCurveBlendOut = 0.3f;
	float ZUpperGradient = 0.2f;
	TArray<float> ZFovScalar = {40.0f, 0.7f, 70.0f, 1.2f};
	float LookAtSmoothingTime = 0.1f;

private:
	TMap<FString, FSwcCameraState> States;
	TMap<FString, TPair<FString, float>> Activations;
	FSwcCameraState Default;
};
