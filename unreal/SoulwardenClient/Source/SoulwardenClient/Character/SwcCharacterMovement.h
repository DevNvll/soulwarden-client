#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SwcCharacterMovement.generated.h"

class FSwcLocomotion;
struct FSwcClip;
struct FSwcFragment;

/** The movement state of the player (the action lists of the retail client, research feel-values.md R2). */
UENUM()
enum class ESwcMove : uint8
{
	Idle,
	SprintStart,
	Sprint,
	NavStart,
	Nav,
	Walk,
	Stop,
	Jump,
	Fall,
	Land,
};

/**
 * The movement of the player with the values of the game data (research feel-values.md R2; docs/MOVEMENT.md). The retail
 * client moves each axis of the velocity in the camera frame linearly to its target speed with the acceleration or the
 * deceleration of the state (no friction); the speed comes from the data, the locomotion clips play in place. The
 * character turns to the camera yaw while it moves (strafe mode, the default keyboard setting). A jump is animation
 * driven: the root bone of its clip moves the character until the marker Enable_Gravity of its fragment, then gravity.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcCharacterMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	USwcCharacterMovement();

	/** The input of this frame: the move keys in the camera frame (x right, y forward), the camera yaw (Unreal degrees). */
	void SetInput(const FVector2D& Move, double CameraYaw, bool bWalk);
	/** The jump key (Space). */
	void RequestJump();
	/** The movement permissions of the movement state of the server (protocol notes 4.6). */
	void SetPermissions(uint8 DataBits, uint8 MoreDataBits);
	/** The locomotion data: the root motion of the jump clips and the markers of the fragments. */
	void SetLocomotion(const TSharedPtr<FSwcLocomotion>& Data);

	ESwcMove GetMoveState() const { return Move; }
	/** The fragment of the Mannequin data that the animation plays (locomotion.json). */
	FString GetFragment() const;
	/** The time since the start of the current state (s). */
	float GetStateTime() const { return StateTime; }
	/** The horizontal speed (m/s), and the yaw rate of the character (rad/s, positive = to the left). */
	float GetHorizontalSpeed() const;
	float GetTurnSpeed() const { return TurnSpeed; }
	/** True while the forward move key is down (the action bit of the input report). */
	bool IsForwardHeld() const { return Input.Y > 0.5; }
	bool HasMoveInput() const;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void ProcessLanded(const FHitResult& Hit, float RemainingTime, int32 Iterations) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

private:
	void SetMove(ESwcMove Next);
	void UpdateState(float DeltaTime);
	void Turn(float DeltaTime);
	void StartJump(bool bInput);
	/** The velocity of the root motion of the jump clip for this step (and the air control of the standing jump). */
	void JumpVelocity(float DeltaTime);
	const FSwcFragment* FragmentData(const FString& Name) const;

	TSharedPtr<FSwcLocomotion> Locomotion;
	FVector2D Input = FVector2D::ZeroVector;
	double CameraYaw = 0.0;
	bool bWalkHeld = false;
	bool bJumpRequested = false;
	bool bCanNav = true;
	bool bCanRun = true;
	bool bCanSprint = true;
	bool bCanDodge = true;
	ESwcMove Move = ESwcMove::Idle;
	float StateTime = 0.0f;
	/** The jump of this flight: its fragment, its clip, and its times from the data. */
	FString JumpFragment;
	const FSwcClip* JumpClip = nullptr;
	bool bJumped = false;
	bool bStandingJump = false;
	/** The root motion until Enable_Gravity (MOVE_Flying), then gravity. */
	bool bRootMotion = false;
	float RootTime = 0.0f;
	float EnableGravity = 0.0f;
	/** The time of the change to Fall_From_Jump, and the end of the air control of the standing jump. */
	float FallFromJump = 0.0f;
	/** The yaw of the character at the start of the jump: the frame of the root motion. */
	double JumpYaw = 0.0;
	FVector2D StandingAirVelocity = FVector2D::ZeroVector;
	float AirTime = 0.0f;
	float JumpTime = 0.0f;
	FString LandClass;
	/** CanTransition of the landing fragment (s). */
	float LandHold = 0.0f;
	float LastYaw = 0.0f;
	float TurnSpeed = 0.0f;
	/** The horizontal velocity of the data on the ground (cm/s). */
	FVector GroundVelocity = FVector::ZeroVector;
};
