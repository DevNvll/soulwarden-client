#pragma once

#include "CoreMinimal.h"
#include "Water/SwcWaterData.h"
#include "Character/SwcMoveRules.h"
#include "GameFramework/CharacterMovementComponent.h"

#include <optional>

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
	/** A posture transition (stage P3.3): its fragment plays (an idle transition holds the character, a sprint one moves it). */
	PostureTransition,
	/** A stun of a status of the server (stage P3.3): r_Stun, r_Stun_Loop while it lasts, then r_Stun_End. */
	Stun,
	/** Stage WF5 (docs/WATER.md): the swim (Swim_Idle, Swim_Nav) and its exit to the wade. */
	Swim,
	SwimExit,
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
	/**
	 * An action of the full body (an attack) moves the character: the horizontal velocity of this frame (cm/s; the root
	 * track of its fragment, AnimationDrivenMotion, docs/ACTIONS.md). While it plays, the move keys do not move the
	 * character, the jump key does nothing, and the strafe turn stops (the action turns the character).
	 */
	void SetActionMotion(bool bActive, const FVector& HorizontalVelocity);
	/** The end of an action: the movement starts from its velocity, at most MaxSpeed (cm/s; 0: no limit). */
	void EndActionMotion(float MaxSpeed);
	/**
	 * The weapon in the hand (stage P2.6, docs/MOVEMENT.md): the armed rates of the sprint start (a melee, ranged, or bow
	 * weapon), of the run (also a magic weapon), and of the stop (a melee or magic weapon); the aim walks and has no sprint.
	 */
	void SetCombat(bool bInArmedSprint, bool bInArmedNav, bool bInArmedStop, bool bInAiming);
	/** The exit of a dodge: the sprint at once for 0.85 s (SkipSprintStartTimer). */
	void SkipSprintStart();

	/** Stage P3.3 (docs/MOVEMENT.md "The postures", "Autorun", "The limits"). The keys C (crouch) and Z (prone): the desired
	 *  posture of the data; the posture follows it when its transition can start. */
	void TogglePosture(bool bProne);
	/** A posture of an action at once (a jump, an attack, a fall: stand; a weapon draw from prone: crouch), when there is room. */
	void ForcePosture(swc::move::Posture Posture);
	swc::move::Posture GetPosture() const { return Posture.posture(); }
	/** The key `=`. @return The new state. */
	bool ToggleAutoRun();
	/** An end of autorun of the data (an attack, a use of an object). */
	void CancelAutoRun(const TCHAR* Cause);
	bool IsAutoRun() const { return AutoRun.on(); }
	/** The Mannequin tags of the weapon in the hand (its tag set, "2H_Melee", "GreatAxe"; empty: unarmed). */
	void SetWeaponTags(const TArray<FString>& Tags) { WeaponTags = Tags; }
	/** The limits of the load and of the statuses (the subsystem each frame; the server bits stay the ones of SetPermissions). */
	void SetLimits(const swc::move::Limits& InLimits);
	const swc::move::Limits& GetLimits() const { return Limits; }
	/** The Conditional of the data that gives the speed now (empty: the Default), and the movement action. */
	const FString& GetMotionName() const { return MotionName; }
	bool IsStunned() const { return Move == ESwcMove::Stun; }
	/** The base name of the fragment (with no posture tag): "Idle", "Strafe", "Sprint", ... */
	FString GetBaseFragment() const;
	/** The movement rules of the data (movement.json of the locomotion), or null. */
	const swc::move::Data* GetRules() const { return Rules(); }
	bool IsActionMotion() const { return bActionMotion; }
	/** The camera yaw of the input (Unreal degrees) and the move keys (x right, y forward). */
	double GetCameraYaw() const { return CameraYaw; }
	FVector2D GetMoveInput() const { return Input; }

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
	/**
	 * The count of the blocks of the movement by the body of a creature (its body block: the object type Pawn; stage P3.5): the
	 * collision of the character (character_collided) of the transitions inside Charge.
	 */
	uint32 GetCreatureBlocks() const { return CreatureBlocks; }

	/** Stage WF5 (docs/WATER.md): the depth class of the feet (the rule of the retail client; the input report has it), the depth
	 *  of the feet below the water surface (m; 0 with no water), and the swim. A swim or a wade blocks the jump, the dodge, and
	 *  prone; the swim also blocks the attacks, the abilities, and the interactions (playeractions_swim, player.grid). */
	swc::water::DepthClass GetWaterClass() const { return WaterClass; }
	float GetWaterDepth() const { return FeetDepth; }
	bool IsSwimming() const { return Move == ESwcMove::Swim || Move == ESwcMove::SwimExit; }
	/** @return True when the data permits a dodge in the water now (no wade, no swim). */
	bool CanDodgeInWater() const { return !IsSwimming() && WaterClass < swc::water::DepthClass::shallow; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void ProcessLanded(const FHitResult& Hit, float RemainingTime, int32 Iterations) override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.0f, const FVector& MoveDelta = FVector::ZeroVector) override;

	/** The facts of the conditions of the data now. */
	swc::move::Facts MakeFacts() const;
	/** The rules of the data, or null (no movement.json). */
	const swc::move::Data* Rules() const;
	/** The posture: a transition to the desired posture when it can start; the capsule of a posture (the feet stay); the room. */
	void UpdatePosture();
	bool CanStartPosture(swc::move::Posture Target) const;
	bool HasRoomFor(swc::move::Posture Target) const;
	void ApplyCapsule(swc::move::Posture Target);
	/** The stun of a status: @return True while the stun holds the character. */
	bool UpdateStun();
	/** Stage WF5: the depth class and the depth of the feet at the water of their cell (USwcWaterSubsystem). */
	void UpdateWater();
	/** The swim: its start, its hold, and its exit. @return True while the swim holds the movement state. */
	bool UpdateSwim();
	/** The velocity of the swim: forward in the facing of the character, and back to the swim height. */
	void SwimVelocity(float DeltaTime);
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
	bool bActionMotion = false;
	FVector ActionVelocity = FVector::ZeroVector;
	bool bArmedSprint = false;
	bool bArmedNav = false;
	bool bArmedStop = false;
	bool bAiming = false;
	float SkipSprintStartLeft = 0.0f;
	uint32 CreatureBlocks = 0;
	/** Stage P3.3. */
	swc::move::PostureState Posture;
	swc::move::AutoRun AutoRun;
	swc::move::Limits Limits;
	TArray<FString> WeaponTags;
	FVector2D RawInput = FVector2D::ZeroVector;
	bool bWasForward = false;
	std::optional<swc::move::Transition> ActiveTransition;
	swc::move::Posture TransitionTo = swc::move::Posture::stand;
	/** The end of the transition (s of its state): its mark CanTransition, and with no move input the end of its clip. */
	float TransitionMark = 0.0f;
	float TransitionLength = 0.0f;
	enum class EStun : uint8 { None, Start, Loop, End };
	EStun StunPhase = EStun::None;
	float StunTime = 0.0f;
	FString MotionName;
	FString MotionAction;
	FString LastMotionLog;
	/** Stage WF5. */
	swc::water::DepthClass WaterClass = swc::water::DepthClass::none;
	float FeetDepth = 0.0f;
	bool bInWater = false;
	/** The water surface of the cell of the feet (engine m). */
	double WaterSurface = 0.0;
};
