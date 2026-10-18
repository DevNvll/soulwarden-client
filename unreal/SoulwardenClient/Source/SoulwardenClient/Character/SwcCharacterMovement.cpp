#include "Character/SwcCharacterMovement.h"

#include "Character/SwcLocomotion.h"
#include "GameFramework/Character.h"

namespace
{
// The values of the game data (research feel-values.md R2; m/s and m/s^2, here cm). [D] = action lists of the player.
/** Sprint (default forward movement, also side input in strafe mode): 5.5 m/s, 20 / 20 m/s^2 [D playeractions_sprint]. */
constexpr float SprintSpeed = 550.0f;
constexpr float SprintAccel = 2000.0f;
constexpr float SprintDecel = 2000.0f;
/** Sprint_Start: the sprint speed with 20 m/s^2 unarmed (10 with a weapon drawn) [D]. It becomes Sprint at 4 m/s or after 1.13 s. */
constexpr float SprintStartAccel = 2000.0f;
constexpr float SprintMinimumSpeed = 400.0f;
constexpr float SprintStartMaximumTime = 1.13f;
/** Run (Nav, backward input or sprint blocked): 3.25 m/s, 35 / 35 m/s^2 unarmed; Nav_Start 12 / 35 [D playeractions_nav]. */
constexpr float NavSpeed = 325.0f;
constexpr float NavAccel = 3500.0f;
constexpr float NavStartAccel = 1200.0f;
constexpr float NavDecel = 3500.0f;
/** Walk (Left Ctrl, or no "can run"): 1.65 m/s, 25 / 25 m/s^2 [D Stand_Walk]. */
constexpr float WalkSpeed = 165.0f;
constexpr float WalkAccel = 2500.0f;
/** Stop (Nav_Stop, no input): 40 m/s^2 unarmed (12 with a melee or magic weapon) [D]. */
constexpr float StopDecel = 4000.0f;
/** The input angles of strafe mode: forward within 110 degrees of the camera forward (sprint); the rest is backward (run) [D]. */
constexpr float ForwardConeDegrees = 110.0f;
/** ClearMotion: the velocity is cleared when the input turns more than 80 (Sprint_Start) or 125 (Sprint) degrees from it [D]. */
constexpr float SprintStartClearDegrees = 80.0f;
constexpr float SprintClearDegrees = 125.0f;
/** The dead zone of the move input (MinInputMagnitude 0.2) [D playerinput_navigation]. */
constexpr float InputDeadZone = 0.2f;
/** CameraRelativeFacing TurnRate: the fraction of the remaining angle for each 1/30 s [D; C 0x2A919A0]. */
constexpr float TurnRateNav = 0.30f;
constexpr float TurnRateSprint = 1.0f;
constexpr float TurnRateSprintStart = 0.33f;
/** The direction tags of the jump fragments: ForwardNav within 30 degrees of forward, BackNav within 70 degrees of back,
 *  LeftNav and RightNav for the rest [D playeractions_nav]. */
constexpr float ForwardNavDegrees = 30.0f;
constexpr float BackNavDegrees = 70.0f;
/** The default times of the jump when a fragment has no marker: Enable_Gravity 0.40 s, Fall_From_Jump 0.5 s after the
 *  jump [D ADB Jump, playeractions_traversal Jump]. */
constexpr float DefaultEnableGravity = 0.40f;
constexpr float DefaultFallFromJump = 0.5f;
/** With no root track (the clip is not converted): the simple method of research R2.8.4, v = sqrt(2 g h) for the apex
 *  of the clip (0.714 m running, 1.178 m standing). */
constexpr float FallbackRunningJumpSpeed = 374.0f;
constexpr float FallbackStandingJumpSpeed = 481.0f;
/** Standing_Jump has air control: 2.5 m/s with 30 m/s^2 [D CharacterRelativeMovement]; Fall_From_Jump and Fall have none. */
constexpr float StandingJumpAirSpeed = 250.0f;
constexpr float StandingJumpAirAccel = 3000.0f;
/** Fall: a horizontal drag of 0.5 m/s^2, and 80 m/s^2 above 7 m/s [D playeractions_fall]. */
constexpr float FallDrag = 50.0f;
constexpr float FallFastDrag = 8000.0f;
constexpr float FallFastSpeed = 700.0f;
/** Landing classes by the time in the air [D playeractions_fall]: a fall is Soft below 0.25 s, Normal to 0.80 s, then
 *  Hard; Fall_From_Jump is Normal, then Hard after 0.3 s. */
constexpr float SoftLandTime = 0.25f;
constexpr float HardLandTime = 0.80f;
constexpr float JumpHardLandTime = 0.3f;
/** Land: CanTransition 0 s (Soft), 0.167 s (Normal), 0.5 s (Hard: a forced stop) [D ADB Land markers]; the idle after
 *  0.5 s with no input. */
constexpr float SoftLandHold = 0.0f;
constexpr float NormalLandHold = 0.167f;
constexpr float HardLandHold = 0.5f;
constexpr float LandIdleTime = 0.5f;

/** @return The angle of the input in the camera frame in degrees: 0 forward, positive to the right. */
float InputAngle(const FVector2D& Move)
{
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Move.X, Move.Y)));
}
} // namespace

USwcCharacterMovement::USwcCharacterMovement()
{
	// The capsule and the ground rules of the player (research R2.4.3, R2.8.1): walkable slope 70 degrees; the physics
	// step of the client is 0 (javelin.physics-character-step-up-meters), and the automatic step-up clip takes obstacles
	// from 0.15 to 0.65 m: until that clip exists, a step of 0.65 m.
	SetWalkableFloorAngle(70.0f);
	MaxStepHeight = 65.0f;
	GravityScale = 1.0f;
	AirControl = 0.0f;
	FallingLateralFriction = 0.0f;
	GroundFriction = 0.0f;
	BrakingFriction = 0.0f;
	bUseSeparateBrakingFriction = true;
	BrakingDecelerationWalking = StopDecel;
	BrakingDecelerationFalling = 0.0f;
	BrakingDecelerationFlying = 0.0f;
	MaxWalkSpeed = SprintSpeed;
	MaxAcceleration = SprintAccel;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
	NavAgentProps.bCanJump = true;
	NavAgentProps.bCanFly = true;
}

void USwcCharacterMovement::SetInput(const FVector2D& Move2, double Yaw, bool bWalk)
{
	Input = Move2;
	CameraYaw = Yaw;
	bWalkHeld = bWalk;
}

void USwcCharacterMovement::RequestJump()
{
	bJumpRequested = true;
}

void USwcCharacterMovement::SetPermissions(uint8 DataBits, uint8 MoreDataBits)
{
	// dataBits 0x40 can sprint, 0x80 can run; moreDataBits 0x01 can navigate, 0x02 can dodge (protocol notes 4.6).
	bCanSprint = (DataBits & 0x40) != 0;
	bCanRun = (DataBits & 0x80) != 0;
	bCanNav = (MoreDataBits & 0x01) != 0;
	bCanDodge = (MoreDataBits & 0x02) != 0;
}

void USwcCharacterMovement::SetLocomotion(const TSharedPtr<FSwcLocomotion>& Data)
{
	Locomotion = Data;
	JumpClip = nullptr;
}

void USwcCharacterMovement::SetActionMotion(bool bActive, const FVector& HorizontalVelocity)
{
	bActionMotion = bActive;
	ActionVelocity = bActive ? FVector(HorizontalVelocity.X, HorizontalVelocity.Y, 0.0) : FVector::ZeroVector;
}

void USwcCharacterMovement::EndActionMotion(float MaxSpeed)
{
	bActionMotion = false;
	ActionVelocity = FVector::ZeroVector;
	if (MaxSpeed > 0.0f)
	{
		GroundVelocity = GroundVelocity.GetClampedToMaxSize2D(MaxSpeed);
		const FVector Horizontal = FVector(Velocity.X, Velocity.Y, 0.0).GetClampedToMaxSize(MaxSpeed);
		Velocity.X = Horizontal.X;
		Velocity.Y = Horizontal.Y;
	}
}

const FSwcFragment* USwcCharacterMovement::FragmentData(const FString& Name) const
{
	return Locomotion.IsValid() ? Locomotion->Fragment(Name) : nullptr;
}

bool USwcCharacterMovement::HasMoveInput() const
{
	return Input.Size() >= InputDeadZone;
}

float USwcCharacterMovement::GetHorizontalSpeed() const
{
	return static_cast<float>(Velocity.Size2D() / 100.0);
}

FString USwcCharacterMovement::GetFragment() const
{
	switch (Move)
	{
	case ESwcMove::SprintStart:
		return TEXT("Sprint_Start");
	case ESwcMove::Sprint:
		return TEXT("Sprint");
	case ESwcMove::NavStart:
	case ESwcMove::Nav:
	case ESwcMove::Walk:
	case ESwcMove::Stop:
		return TEXT("Strafe");
	case ESwcMove::Jump:
		return JumpFragment;
	case ESwcMove::Fall:
		return TEXT("Fall");
	case ESwcMove::Land:
		return TEXT("Land+") + LandClass;
	default:
		return TEXT("Idle");
	}
}

void USwcCharacterMovement::SetMove(ESwcMove Next)
{
	if (Next != Move)
	{
		Move = Next;
		StateTime = 0.0f;
	}
}

void USwcCharacterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	StateTime += DeltaTime;
	UpdateState(DeltaTime);
	Turn(DeltaTime);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// The yaw rate of the character for the lean of the sprint (positive = to the left: the Unreal yaw turns to the right).
	if (CharacterOwner != nullptr && DeltaTime > 0.0f)
	{
		const float Yaw = static_cast<float>(CharacterOwner->GetActorRotation().Yaw);
		TurnSpeed = -FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(LastYaw, Yaw)) / DeltaTime;
		LastYaw = Yaw;
	}
}

void USwcCharacterMovement::StartJump(bool bInput)
{
	// The fragment: Jump_Standing with no input, else Jump with the direction tag of the input [D ADB Jump].
	bStandingJump = !bInput;
	if (bStandingJump)
	{
		JumpFragment = TEXT("Jump_Standing");
	}
	else
	{
		const float Angle = InputAngle(Input);
		JumpFragment = FMath::Abs(Angle) <= ForwardNavDegrees ? TEXT("Jump+ForwardNav")
			: FMath::Abs(Angle) >= 180.0f - BackNavDegrees ? TEXT("Jump+BackNav")
			: Angle > 0.0f ? TEXT("Jump+RightNav") : TEXT("Jump+LeftNav");
	}
	const FSwcFragment* Fragment = FragmentData(JumpFragment);
	JumpClip = Fragment != nullptr && Locomotion->Clips.IsValidIndex(Fragment->Clip) && !Locomotion->Clips[Fragment->Clip].Root.IsEmpty()
		? &Locomotion->Clips[Fragment->Clip] : nullptr;
	EnableGravity = Fragment != nullptr ? Fragment->Marker(TEXT("Enable_Gravity"), DefaultEnableGravity) : DefaultEnableGravity;
	FallFromJump = Fragment != nullptr ? Fragment->Marker(TEXT("GotoFallFromJump"), DefaultFallFromJump) : DefaultFallFromJump;
	JumpYaw = CharacterOwner != nullptr ? CharacterOwner->GetActorRotation().Yaw : CameraYaw;
	StandingAirVelocity = FVector2D(Velocity.X, Velocity.Y);
	AirTime = 0.0f;
	JumpTime = 0.0f;
	RootTime = 0.0f;
	bJumped = true;
	SetMove(ESwcMove::Jump);
	if (JumpClip != nullptr)
	{
		// The clip moves the character: no gravity until Enable_Gravity.
		bRootMotion = true;
		SetMovementMode(MOVE_Flying);
	}
	else
	{
		bRootMotion = false;
		Velocity.Z = bStandingJump ? FallbackStandingJumpSpeed : FallbackRunningJumpSpeed;
		SetMovementMode(MOVE_Falling);
	}
}

void USwcCharacterMovement::UpdateState(float DeltaTime)
{
	if (MovementMode == MOVE_None)
	{
		// The client holds the character (the test floor is not in the physics scene yet).
		bJumpRequested = false;
		return;
	}
	if (IsFalling() || IsFlying())
	{
		AirTime += DeltaTime;
		JumpTime += DeltaTime;
		if (bRootMotion && RootTime >= EnableGravity)
		{
			// Enable_Gravity: the character falls with the velocity of the clip.
			bRootMotion = false;
			SetMovementMode(MOVE_Falling);
		}
		// The jump fragment until Fall_From_Jump, then the fall fragment.
		if (Move != ESwcMove::Jump || (bJumped && JumpTime >= FallFromJump))
		{
			SetMove(ESwcMove::Fall);
		}
		bJumpRequested = false;
		return;
	}
	if (bActionMotion)
	{
		// An action of the full body holds the movement state: its root track moves the character.
		bJumpRequested = false;
		SetMove(ESwcMove::Idle);
		return;
	}
	const bool bInput = HasMoveInput() && bCanNav;
	// The speed of the data: a character that runs into a wall keeps its state.
	const float Speed = static_cast<float>(GroundVelocity.Size2D());
	if (Move == ESwcMove::Land)
	{
		if (StateTime < LandHold || (!bInput && StateTime < LandIdleTime))
		{
			bJumpRequested = false;
			return;
		}
	}
	if (bJumpRequested)
	{
		bJumpRequested = false;
		// A jump needs the ground, "can navigate", and "can dodge" (IfCanNav, IfCanDodge) [D playeractions_traversal].
		if (bCanNav && bCanDodge)
		{
			StartJump(bInput);
			return;
		}
	}
	if (!bInput)
	{
		SetMove(Speed > 2.0f ? ESwcMove::Stop : ESwcMove::Idle);
		return;
	}
	const bool bBackward = FMath::Abs(InputAngle(Input)) > ForwardConeDegrees;
	if (bWalkHeld || !bCanRun)
	{
		SetMove(ESwcMove::Walk);
	}
	else if (bBackward || !bCanSprint)
	{
		const bool bStart = Move == ESwcMove::Idle || Move == ESwcMove::Stop || Move == ESwcMove::Land;
		SetMove(bStart ? ESwcMove::NavStart : (Move == ESwcMove::NavStart && Speed < NavSpeed * 0.98f ? ESwcMove::NavStart : ESwcMove::Nav));
	}
	else if (Move == ESwcMove::Sprint && Speed >= SprintMinimumSpeed)
	{
		SetMove(ESwcMove::Sprint);
	}
	else if (Move == ESwcMove::SprintStart)
	{
		if (Speed >= SprintMinimumSpeed || StateTime >= SprintStartMaximumTime)
		{
			SetMove(ESwcMove::Sprint);
		}
	}
	else
	{
		SetMove(ESwcMove::SprintStart);
	}
}

void USwcCharacterMovement::Turn(float DeltaTime)
{
	// Strafe mode: while the character moves, it turns to the camera yaw, step = (1 - (1 - r)^(30 dt)) x the remaining angle
	// (CameraRelativeFacing, C 0x2A919A0). At idle and in the air it does not turn.
	if (CharacterOwner == nullptr || bActionMotion || !HasMoveInput() || IsFalling() || IsFlying() || Move == ESwcMove::Land)
	{
		return;
	}
	const float Rate = Move == ESwcMove::Sprint ? TurnRateSprint : Move == ESwcMove::SprintStart ? TurnRateSprintStart : TurnRateNav;
	const float Alpha = Rate >= 1.0f ? 1.0f : 1.0f - FMath::Pow(1.0f - Rate, 30.0f * DeltaTime);
	FRotator Rotation = CharacterOwner->GetActorRotation();
	Rotation.Yaw += FMath::FindDeltaAngleDegrees(Rotation.Yaw, CameraYaw) * Alpha;
	CharacterOwner->SetActorRotation(FRotator(0.0, Rotation.Yaw, 0.0));
}

void USwcCharacterMovement::JumpVelocity(float DeltaTime)
{
	// The root bone of the clip in the frame of the character at the jump start (engine x right, y forward, z up; m).
	const FVector From = JumpClip->RootAt(RootTime);
	const FVector To = JumpClip->RootAt(RootTime + DeltaTime);
	RootTime += DeltaTime;
	const FRotator Frame(0.0, JumpYaw, 0.0);
	const FVector Forward = Frame.Vector();
	const FVector Right = FRotationMatrix(Frame).GetUnitAxis(EAxis::Y);
	const FVector Step = (To - From) * 100.0;
	Velocity = (Forward * Step.Y + Right * Step.X + FVector::UpVector * Step.Z) / DeltaTime;
	if (bStandingJump)
	{
		// The standing jump has no horizontal root motion: its air control moves the character.
		if (JumpTime < FallFromJump && HasMoveInput())
		{
			const FRotator Camera(0.0, CameraYaw, 0.0);
			const FVector Target3 = (Camera.Vector() * Input.Y + FRotationMatrix(Camera).GetUnitAxis(EAxis::Y) * Input.X).GetSafeNormal2D()
				* StandingJumpAirSpeed;
			const FVector2D Target(Target3.X, Target3.Y);
			StandingAirVelocity += (Target - StandingAirVelocity).GetClampedToMaxSize(StandingJumpAirAccel * DeltaTime);
		}
		Velocity.X += StandingAirVelocity.X;
		Velocity.Y += StandingAirVelocity.Y;
	}
}

void USwcCharacterMovement::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (bRootMotion && JumpClip != nullptr && DeltaTime > 0.0f)
	{
		JumpVelocity(DeltaTime);
		return;
	}
	if (IsFalling() || IsFlying())
	{
		// No air control after a running jump or in a fall; the fall has a small drag; the standing jump has 2.5 m/s until
		// Fall_From_Jump.
		FVector Horizontal(Velocity.X, Velocity.Y, 0.0);
		if (bStandingJump && Move == ESwcMove::Jump && HasMoveInput())
		{
			const FRotator Frame(0.0, CameraYaw, 0.0);
			const FVector Target = (Frame.Vector() * Input.Y + FRotationMatrix(Frame).GetUnitAxis(EAxis::Y) * Input.X).GetSafeNormal2D() * StandingJumpAirSpeed;
			Horizontal += (Target - Horizontal).GetClampedToMaxSize(StandingJumpAirAccel * DeltaTime);
		}
		else if (!bJumped)
		{
			const double Speed = Horizontal.Size();
			const double Drag = (Speed > FallFastSpeed ? FallFastDrag : FallDrag) * DeltaTime;
			Horizontal = Speed > Drag ? Horizontal * ((Speed - Drag) / Speed) : FVector::ZeroVector;
		}
		Velocity.X = Horizontal.X;
		Velocity.Y = Horizontal.Y;
		return;
	}
	if (bActionMotion)
	{
		// The velocity of the action (the next movement state starts from it).
		GroundVelocity = ActionVelocity;
		Velocity.X = ActionVelocity.X;
		Velocity.Y = ActionVelocity.Y;
		return;
	}
	// The target and the rates of the state; each axis of the camera frame moves linearly to its target (research R2.8.2).
	float Speed = 0.0f;
	float Accel = 0.0f;
	float Decel = StopDecel;
	float ClearDegrees = 0.0f;
	switch (Move)
	{
	case ESwcMove::SprintStart:
		Speed = SprintSpeed;
		Accel = SprintStartAccel;
		Decel = SprintDecel;
		ClearDegrees = SprintStartClearDegrees;
		break;
	case ESwcMove::Sprint:
		Speed = SprintSpeed;
		Accel = SprintAccel;
		Decel = SprintDecel;
		ClearDegrees = SprintClearDegrees;
		break;
	case ESwcMove::NavStart:
		Speed = NavSpeed;
		Accel = NavStartAccel;
		Decel = NavDecel;
		break;
	case ESwcMove::Nav:
		Speed = NavSpeed;
		Accel = NavAccel;
		Decel = NavDecel;
		break;
	case ESwcMove::Walk:
		Speed = WalkSpeed;
		Accel = WalkAccel;
		Decel = WalkAccel;
		break;
	default:
		break;
	}
	const FRotator Frame(0.0, CameraYaw, 0.0);
	const FVector Forward = Frame.Vector();
	const FVector Right = FRotationMatrix(Frame).GetUnitAxis(EAxis::Y);
	const FVector2D Direction = HasMoveInput() && Speed > 0.0f ? Input.GetSafeNormal() : FVector2D::ZeroVector;
	const FVector Target = (Forward * Direction.Y + Right * Direction.X) * Speed;
	// The velocity of the data, not the velocity of the last move: a block (a wall, the face of a step) does not reset it,
	// thus the next move has its full length (the step-up of the engine needs it).
	FVector Current = GroundVelocity;
	if (ClearDegrees > 0.0f && !Current.IsNearlyZero() && !Target.IsNearlyZero()
		&& FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Current.GetSafeNormal(), Target.GetSafeNormal()))) > ClearDegrees)
	{
		Current = FVector::ZeroVector; // ClearMotion: "prevent sliding".
	}
	const auto Approach = [DeltaTime](double Value, double Goal, double Up, double Down)
	{
		const bool bFaster = Goal * Value >= 0.0 && FMath::Abs(Goal) > FMath::Abs(Value);
		const double Step = (bFaster ? Up : Down) * DeltaTime;
		return Value < Goal ? FMath::Min(Value + Step, Goal) : FMath::Max(Value - Step, Goal);
	};
	const double Side = Approach(FVector::DotProduct(Current, Right), FVector::DotProduct(Target, Right), Accel, Decel);
	const double Ahead = Approach(FVector::DotProduct(Current, Forward), FVector::DotProduct(Target, Forward), Accel, Decel);
	const FVector Result = Forward * Ahead + Right * Side;
	GroundVelocity = Result;
	Velocity.X = Result.X;
	Velocity.Y = Result.Y;
}

void USwcCharacterMovement::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	// On the ground the velocity of the data starts from the real velocity (a landing keeps the speed of the flight).
	GroundVelocity = FVector(Velocity.X, Velocity.Y, 0.0);
}

void USwcCharacterMovement::ProcessLanded(const FHitResult& Hit, float RemainingTime, int32 Iterations)
{
	// The landing class from the time in the air [D playeractions_fall].
	if (bJumped)
	{
		LandClass = JumpTime - FallFromJump <= JumpHardLandTime ? TEXT("Normal") : TEXT("Hard");
	}
	else
	{
		LandClass = AirTime < SoftLandTime ? TEXT("Soft") : AirTime < HardLandTime ? TEXT("Normal") : TEXT("Hard");
	}
	const FSwcFragment* Land = FragmentData(TEXT("Land+") + LandClass);
	const float DefaultHold = LandClass == TEXT("Hard") ? HardLandHold : LandClass == TEXT("Normal") ? NormalLandHold : SoftLandHold;
	LandHold = Land != nullptr ? Land->Marker(TEXT("CanTransition"), DefaultHold) : DefaultHold;
	const bool bInput = HasMoveInput() && bCanNav;
	bJumped = false;
	bStandingJump = false;
	bRootMotion = false;
	AirTime = 0.0f;
	JumpTime = 0.0f;
	// With move input the character goes on at once (Land_Sprint, or the run start for a backward input); with none it
	// plays the landing of its class.
	if (!bInput)
	{
		SetMove(ESwcMove::Land);
		if (LandClass == TEXT("Hard"))
		{
			Velocity = FVector::ZeroVector;
		}
	}
	else if (bWalkHeld || !bCanRun)
	{
		SetMove(ESwcMove::Walk);
	}
	else if (FMath::Abs(InputAngle(Input)) > ForwardConeDegrees || !bCanSprint)
	{
		SetMove(ESwcMove::NavStart);
	}
	else
	{
		SetMove(ESwcMove::SprintStart);
	}
	Super::ProcessLanded(Hit, RemainingTime, Iterations);
}
