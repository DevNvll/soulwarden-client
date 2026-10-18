#include "Character/SwcCharacterMovement.h"

#include "Character/SwcLocomotion.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "SwcBackendSubsystem.h"

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
/** The rates with a weapon drawn (stage P2.6) [D]: Sprint_Start `Stand+Armed` (1H/2H melee, 1H/2H ranged, bow) 10 / 20 m/s^2;
 *  Nav and Nav_Start `Combat_Nav_Stand` (also the magic weapons; the first conditional that holds, before NavStart) 25 / 25;
 *  Nav_Stop `CombatNav+Melee`, `CombatNav+Magic` 12 m/s^2. */
constexpr float ArmedSprintStartAccel = 1000.0f;
constexpr float ArmedNavAccel = 2500.0f;
constexpr float ArmedStopDecel = 1200.0f;
/** SkipSprintStartTimer (0.85 s) of the exit of a dodge: the sprint at once, no Sprint_Start [D playeractions_dodge]. */
constexpr float SkipSprintStartTime = 0.85f;
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
	// Autorun (stage P3.3): a virtual W; the back key and a new press of W end it (the deactivation of the action AutoRun).
	const bool bForward = Move2.Y > 0.5;
	if (AutoRun.on() && (Move2.Y < -0.5 || (bForward && !bWasForward)))
	{
		CancelAutoRun(Move2.Y < -0.5 ? TEXT("the back key") : TEXT("a new press of W"));
	}
	bWasForward = bForward;
	RawInput = Move2;
	const auto [Side, Ahead] = AutoRun.input(static_cast<float>(Move2.X), static_cast<float>(Move2.Y));
	Input = FVector2D(Side, Ahead);
	CameraYaw = Yaw;
	bWalkHeld = bWalk;
}

bool USwcCharacterMovement::ToggleAutoRun()
{
	const bool bOn = AutoRun.toggle();
	UE_LOG(LogSwcBackend, Log, TEXT("move: autorun %s"), bOn ? TEXT("on") : TEXT("off (the key)"));
	return bOn;
}

void USwcCharacterMovement::CancelAutoRun(const TCHAR* Cause)
{
	if (AutoRun.on())
	{
		AutoRun.cancel();
		UE_LOG(LogSwcBackend, Log, TEXT("move: autorun off (%s)"), Cause);
	}
}

void USwcCharacterMovement::TogglePosture(bool bProne)
{
	if (bProne)
	{
		Posture.toggle_prone();
	}
	else
	{
		Posture.toggle_crouch();
	}
	UE_LOG(LogSwcBackend, Log, TEXT("move: the key %s: the desired posture %s (now %s)"), bProne ? TEXT("Z") : TEXT("C"),
		UTF8_TO_TCHAR(swc::move::posture_text(Posture.desired())), UTF8_TO_TCHAR(swc::move::posture_text(Posture.posture())));
}

void USwcCharacterMovement::ForcePosture(swc::move::Posture Target)
{
	if (Posture.posture() == Target && !Posture.pending())
	{
		return;
	}
	if (!HasRoomFor(Target))
	{
		return;
	}
	ActiveTransition.reset();
	if (Move == ESwcMove::PostureTransition)
	{
		SetMove(ESwcMove::Idle);
	}
	Posture.force(Target);
	ApplyCapsule(Target);
	UE_LOG(LogSwcBackend, Log, TEXT("move: the posture %s at once (an action)"), UTF8_TO_TCHAR(swc::move::posture_text(Target)));
}

void USwcCharacterMovement::SetLimits(const swc::move::Limits& InLimits)
{
	// The server bits stay the ones of the movement state (SetPermissions).
	const bool bSprint = Limits.serverSprint;
	const bool bRun = Limits.serverRun;
	const bool bNav = Limits.serverNav;
	const bool bDodge = Limits.serverDodge;
	Limits = InLimits;
	Limits.serverSprint = bSprint;
	Limits.serverRun = bRun;
	Limits.serverNav = bNav;
	Limits.serverDodge = bDodge;
}

const swc::move::Data* USwcCharacterMovement::Rules() const
{
	return Locomotion.IsValid() && Locomotion->Rules.loaded() ? &Locomotion->Rules : nullptr;
}

swc::move::Facts USwcCharacterMovement::MakeFacts() const
{
	swc::move::Facts Facts;
	Facts.tags.insert(swc::move::posture_tag(Posture.posture()));
	if (WeaponTags.IsEmpty())
	{
		Facts.tags.insert("Unarmed");
	}
	for (const FString& Tag : WeaponTags)
	{
		Facts.tags.insert(TCHAR_TO_UTF8(*Tag));
	}
	if (bAiming)
	{
		// The aim of a ranged weapon or a bow (AimDownSights, stage P2.6).
		Facts.tags.insert("Aim");
		Facts.tags.insert("AimDownSights");
	}
	if (AutoRun.on())
	{
		Facts.tags.insert("AutoRun");
	}
	if (bWalkHeld)
	{
		Facts.inputs.insert("walk");
	}
	Limits.apply(Facts);
	Facts.moveSpeed = static_cast<float>(GroundVelocity.Size2D() / 100.0);
	return Facts;
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
	Limits.serverSprint = bCanSprint;
	Limits.serverRun = bCanRun;
	Limits.serverNav = bCanNav;
	Limits.serverDodge = bCanDodge;
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

void USwcCharacterMovement::SetCombat(bool bInArmedSprint, bool bInArmedNav, bool bInArmedStop, bool bInAiming)
{
	bArmedSprint = bInArmedSprint;
	bArmedNav = bInArmedNav;
	bArmedStop = bInArmedStop;
	if (bInAiming && !bAiming)
	{
		CancelAutoRun(TEXT("the aim"));
	}
	bAiming = bInAiming;
}

void USwcCharacterMovement::SkipSprintStart()
{
	SkipSprintStartLeft = SkipSprintStartTime;
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

FString USwcCharacterMovement::GetBaseFragment() const
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
	case ESwcMove::PostureTransition:
		return ActiveTransition ? FString(UTF8_TO_TCHAR(ActiveTransition->fragment.c_str())) : FString(TEXT("Idle"));
	case ESwcMove::Stun:
		return StunPhase == EStun::Start ? TEXT("r_Stun") : StunPhase == EStun::End ? TEXT("r_Stun_End") : TEXT("r_Stun_Loop");
	default:
		return TEXT("Idle");
	}
}

FString USwcCharacterMovement::GetFragment() const
{
	// The fragments of crouch and prone (player.adb: Idle, Strafe, Sprint, Sprint_Start with the tag Crouch or Prone).
	FString Base = GetBaseFragment();
	const bool bLoop = Move == ESwcMove::Idle || Move == ESwcMove::SprintStart || Move == ESwcMove::Sprint || Move == ESwcMove::NavStart
		|| Move == ESwcMove::Nav || Move == ESwcMove::Walk || Move == ESwcMove::Stop;
	if (bLoop && Posture.posture() != swc::move::Posture::stand)
	{
		Base += TEXT("+");
		Base += UTF8_TO_TCHAR(swc::move::posture_tag(Posture.posture()));
	}
	return Base;
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
	SkipSprintStartLeft = FMath::Max(SkipSprintStartLeft - DeltaTime, 0.0f);
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
		// A fall stands the character up (the fall actions set Stand; research p33-movement.md Q1).
		if (Posture.posture() != swc::move::Posture::stand || Posture.pending())
		{
			ActiveTransition.reset();
			Posture.force(swc::move::Posture::stand);
			ApplyCapsule(swc::move::Posture::stand);
		}
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
	if (UpdateStun())
	{
		bJumpRequested = false;
		return;
	}
	if (bActionMotion)
	{
		// An action of the full body holds the movement state: its root track moves the character.
		bJumpRequested = false;
		ActiveTransition.reset();
		SetMove(ESwcMove::Idle);
		return;
	}
	UpdatePosture();
	if (Move == ESwcMove::PostureTransition)
	{
		bJumpRequested = false;
		return;
	}
	// The root of a status (NoNav) and the server bit "can navigate" stop the character (Nav_Stop).
	const bool bInput = HasMoveInput() && Limits.can_nav();
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
		// A jump needs the ground, IfCanNav and IfCanDodge, not IfEncumbered, not Overburdened [D playeractions_traversal]; from
		// crouch or prone it stands the character up first (Jump sets Stand; with room above).
		if (Limits.can_jump() && HasRoomFor(swc::move::Posture::stand))
		{
			if (Posture.posture() != swc::move::Posture::stand)
			{
				ForcePosture(swc::move::Posture::stand);
			}
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
	const bool bLowPosture = Posture.posture() != swc::move::Posture::stand;
	// The speed of the sprint threshold (MoveSpeed 4 m/s) with the multiplier of the speed table: a slowed sprint (2.75 m/s with
	// 0.5) stays a sprint (an inference; with the plain threshold the state went between Sprint and Sprint_Start).
	const float SprintThreshold = SprintMinimumSpeed * FMath::Min(Limits.speed_multiplier(), 1.0f);
	// The aim (AimDownSights) has no sprint (Sprint_Start needs "not ADS"); the walk key has none (Sprint_Start: not walk); no
	// IfCanSprint (NoSprint, the server bit), IfEncumbered, or Overburdened: no sprint [D playeractions_sprint].
	if (bWalkHeld || !Limits.can_run() || bAiming)
	{
		SetMove(ESwcMove::Walk);
	}
	else if (bBackward || !Limits.can_sprint())
	{
		const bool bStart = Move == ESwcMove::Idle || Move == ESwcMove::Stop || Move == ESwcMove::Land;
		SetMove(bStart ? ESwcMove::NavStart : (Move == ESwcMove::NavStart && Speed < NavSpeed * 0.98f ? ESwcMove::NavStart : ESwcMove::Nav));
	}
	else if (Move == ESwcMove::Sprint && (Speed >= SprintThreshold - 1.0f || bLowPosture))
	{
		SetMove(ESwcMove::Sprint);
	}
	else if (Move == ESwcMove::SprintStart)
	{
		// The sprint of crouch and prone has no speed threshold (the State conditions of Sprint accept the tag Crouch or Prone).
		if (Speed >= SprintThreshold - 1.0f || StateTime >= SprintStartMaximumTime || bLowPosture)
		{
			SetMove(ESwcMove::Sprint);
		}
	}
	else
	{
		// SkipSprintStartTimer after a dodge: the sprint at once.
		SetMove(SkipSprintStartLeft > 0.0f ? ESwcMove::Sprint : ESwcMove::SprintStart);
	}
}

bool USwcCharacterMovement::UpdateStun()
{
	// A stun of a status of the server (the column Stunned): r_Stun, then r_Stun_Loop while it lasts, then r_Stun_End
	// [D playeractions_reaction: Stun_Loop with ClearMotion; the end with r_Stun_End].
	const auto Length = [this](const TCHAR* Name)
	{
		const FSwcFragment* Fragment = FragmentData(Name);
		return Fragment != nullptr && Locomotion->Clips.IsValidIndex(Fragment->Clip)
			? Locomotion->Clips[Fragment->Clip].Duration / FMath::Max(Fragment->Speed, 0.01f) : 0.0f;
	};
	if (Limits.stunned())
	{
		if (Move != ESwcMove::Stun)
		{
			ActiveTransition.reset();
			SetMove(ESwcMove::Stun);
			StunPhase = EStun::Start;
			StunTime = 0.0f;
			GroundVelocity = FVector::ZeroVector;
			CancelAutoRun(TEXT("a stun"));
			UE_LOG(LogSwcBackend, Log, TEXT("move: a stun of the server: the movement stops"));
		}
		if (StunPhase == EStun::End)
		{
			StunPhase = EStun::Loop; // A new stun in the end of the last one.
		}
		if (StunPhase == EStun::Start && StateTime >= Length(TEXT("r_Stun")))
		{
			StunPhase = EStun::Loop;
		}
		return true;
	}
	if (Move != ESwcMove::Stun)
	{
		return false;
	}
	if (StunPhase != EStun::End)
	{
		StunPhase = EStun::End;
		StunTime = StateTime;
	}
	if (StateTime - StunTime < Length(TEXT("r_Stun_End")))
	{
		return true;
	}
	StunPhase = EStun::None;
	SetMove(ESwcMove::Idle);
	return false;
}

bool USwcCharacterMovement::HasRoomFor(swc::move::Posture Target) const
{
	// IfCanResizeCharacterController: the capsule of the posture at the feet hits nothing that blocks (a taller capsule only).
	const UCapsuleComponent* Capsule = CharacterOwner != nullptr ? CharacterOwner->GetCapsuleComponent() : nullptr;
	if (Capsule == nullptr || GetWorld() == nullptr)
	{
		return true;
	}
	const float Current = Capsule->GetUnscaledCapsuleHalfHeight();
	const float Wanted = swc::move::capsule_half_height(Target);
	if (Wanted <= Current + 0.5f)
	{
		return true;
	}
	const FVector Center = UpdatedComponent->GetComponentLocation() + FVector(0.0, 0.0, Wanted - Current);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SwcPostureRoom), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Params, Response);
	// A little smaller than the capsule: a contact with the ground is no block.
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetUnscaledCapsuleRadius() - 2.0f, Wanted - 2.0f);
	return !GetWorld()->OverlapBlockingTestByChannel(Center, FQuat::Identity, UpdatedComponent->GetCollisionObjectType(), Shape, Params, Response);
}

bool USwcCharacterMovement::CanStartPosture(swc::move::Posture Target) const
{
	// The gates of the transitions of the data: on the ground, no attack (AttackAction), no stun, no landing hold, the room above.
	return !IsFalling() && !IsFlying() && !bActionMotion && Move != ESwcMove::Stun && Move != ESwcMove::Land && Move != ESwcMove::Jump
		&& HasRoomFor(Target);
}

void USwcCharacterMovement::ApplyCapsule(swc::move::Posture Target)
{
	UCapsuleComponent* Capsule = CharacterOwner != nullptr ? CharacterOwner->GetCapsuleComponent() : nullptr;
	if (Capsule == nullptr)
	{
		return;
	}
	const float Old = Capsule->GetUnscaledCapsuleHalfHeight();
	const float New = swc::move::capsule_half_height(Target);
	if (FMath::IsNearlyEqual(Old, New, 0.1f))
	{
		return;
	}
	// The feet stay on the ground: the center goes down (or up) by the difference, and the mesh stays at the feet.
	Capsule->SetCapsuleHalfHeight(New, false);
	if (!IsFalling() && !IsFlying())
	{
		UpdatedComponent->MoveComponent(FVector(0.0, 0.0, New - Old), UpdatedComponent->GetComponentQuat(), false, nullptr, MOVECOMP_NoFlags,
			ETeleportType::TeleportPhysics);
	}
	if (USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh())
	{
		FVector Location = Mesh->GetRelativeLocation();
		Location.Z = -New;
		Mesh->SetRelativeLocation(Location);
	}
}

void USwcCharacterMovement::UpdatePosture()
{
	if (Move == ESwcMove::PostureTransition && ActiveTransition)
	{
		// The end: the mark CanTransition with move input, else the end of the clip (the fragment plays to its end).
		const float End = HasMoveInput() ? TransitionMark : FMath::Max(TransitionMark, TransitionLength);
		if (StateTime < End)
		{
			return;
		}
		Posture.arrive(TransitionTo);
		ActiveTransition.reset();
		SetMove(ESwcMove::Idle);
	}
	const std::optional<swc::move::Posture> Target = Posture.pending();
	if (!Target || !CanStartPosture(*Target))
	{
		return;
	}
	const bool bSprinting = Move == ESwcMove::Sprint || Move == ESwcMove::SprintStart;
	const bool bMoving = HasMoveInput() && GroundVelocity.Size2D() > 50.0;
	const swc::move::Posture From = Posture.posture();
	const swc::move::Transition Transition = swc::move::transition_of(From, *Target, bSprinting, bMoving);
	ApplyCapsule(*Target);
	const FSwcFragment* Fragment = Transition.instant() ? nullptr : FragmentData(UTF8_TO_TCHAR(Transition.fragment.c_str()));
	UE_LOG(LogSwcBackend, Log, TEXT("move: posture %s -> %s%s"), UTF8_TO_TCHAR(swc::move::posture_text(From)),
		UTF8_TO_TCHAR(swc::move::posture_text(*Target)), Fragment != nullptr ? *FString::Printf(TEXT(" (%s)"), UTF8_TO_TCHAR(Transition.fragment.c_str())) : TEXT(" (at once)"));
	if (Fragment == nullptr)
	{
		Posture.arrive(*Target);
		return;
	}
	ActiveTransition = Transition;
	TransitionTo = *Target;
	TransitionMark = Fragment->Marker(TEXT("CanTransition"), Transition.canTransition);
	TransitionLength = Locomotion->Clips.IsValidIndex(Fragment->Clip) ? Locomotion->Clips[Fragment->Clip].Duration / FMath::Max(Fragment->Speed, 0.01f) : 0.0f;
	if (!Transition.sprint)
	{
		GroundVelocity = FVector::ZeroVector; // ClearMotion of the idle transitions.
	}
	SetMove(ESwcMove::PostureTransition);
}

void USwcCharacterMovement::Turn(float DeltaTime)
{
	// Strafe mode: while the character moves, it turns to the camera yaw, step = (1 - (1 - r)^(30 dt)) x the remaining angle
	// (CameraRelativeFacing, C 0x2A919A0). At idle and in the air it does not turn.
	if (CharacterOwner == nullptr || bActionMotion || !HasMoveInput() || IsFalling() || IsFlying() || Move == ESwcMove::Land
		|| Move == ESwcMove::Stun || (Move == ESwcMove::PostureTransition && !(ActiveTransition && ActiveTransition->sprint)))
	{
		return;
	}
	// The TurnRate of the facing list of the action (stage P3.3, movement.json), else the values of the research.
	float Rate = Move == ESwcMove::Sprint ? TurnRateSprint : Move == ESwcMove::SprintStart ? TurnRateSprintStart : TurnRateNav;
	const TCHAR* Action = Move == ESwcMove::Sprint ? TEXT("Sprint") : Move == ESwcMove::SprintStart ? TEXT("Sprint_Start")
		: Move == ESwcMove::NavStart ? TEXT("Nav_Start") : TEXT("Nav");
	if (Move == ESwcMove::PostureTransition)
	{
		// The slide of a sprint to crouch faces the camera at once (CameraRelativeFacing 1.0); the dive turns slowly (0.03).
		Rate = TransitionTo == swc::move::Posture::crouch ? 1.0f : 0.03f;
	}
	else if (const swc::move::Data* Data = Rules())
	{
		const float FromData = swc::move::facing_of(*Data, TCHAR_TO_UTF8(Action), MakeFacts());
		Rate = FromData > 0.0f ? FromData : Rate;
	}
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
	// Jump_Slowed: a status of the category Slow scales the root motion of the jump by 0.85 [D playeractions_traversal].
	const FVector Step = (To - From) * 100.0 * Limits.jump_multiplier();
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
	float Decel = bArmedStop ? ArmedStopDecel : StopDecel;
	float ClearDegrees = 0.0f;
	switch (Move)
	{
	case ESwcMove::SprintStart:
		Speed = SprintSpeed;
		Accel = bArmedSprint ? ArmedSprintStartAccel : SprintStartAccel;
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
		Accel = bArmedNav ? ArmedNavAccel : NavStartAccel;
		Decel = bArmedNav ? ArmedNavAccel : NavDecel;
		break;
	case ESwcMove::Nav:
		Speed = NavSpeed;
		Accel = bArmedNav ? ArmedNavAccel : NavAccel;
		Decel = bArmedNav ? ArmedNavAccel : NavDecel;
		break;
	case ESwcMove::Walk:
		Speed = WalkSpeed;
		Accel = WalkAccel;
		Decel = WalkAccel;
		break;
	default:
		break;
	}
	// Stage P3.3: the motion list of the data (movement.json): the first Conditional of the action whose condition holds gives
	// the speed and the rates of each axis; Nav and Sprint have the multiplier of the speed table (the speed and the acceleration,
	// not the deceleration: ApplyMoveSpeedMultiplier).
	const TCHAR* Action = Move == ESwcMove::SprintStart ? TEXT("Sprint_Start") : Move == ESwcMove::Sprint ? TEXT("Sprint")
		: Move == ESwcMove::NavStart ? TEXT("Nav_Start") : (Move == ESwcMove::Nav || Move == ESwcMove::Walk) ? TEXT("Nav")
		: (Move == ESwcMove::Stop || Move == ESwcMove::Idle) ? TEXT("Nav_Stop") : nullptr;
	float SpeedSide = Speed;
	float AccelSide = Accel;
	float DecelSide = Decel;
	float Backwards = 1.0f;
	const swc::move::Data* Data = Rules();
	if (Data != nullptr && Action != nullptr)
	{
		if (const swc::move::Motion* Motion = swc::move::motion_of(*Data, TCHAR_TO_UTF8(Action), MakeFacts()))
		{
			const bool bScaled = Move != ESwcMove::Stop && Move != ESwcMove::Idle;
			const float Multiplier = bScaled ? Limits.speed_multiplier() : 1.0f;
			Speed = Motion->speed[1] * 100.0f * Multiplier;
			SpeedSide = Motion->speed[0] * 100.0f * Multiplier;
			Accel = Motion->accel[1] * 100.0f * Multiplier;
			AccelSide = Motion->accel[0] * 100.0f * Multiplier;
			Decel = Motion->decel[1] * 100.0f;
			DecelSide = Motion->decel[0] * 100.0f;
			Backwards = Motion->backwards;
			MotionAction = Action;
			MotionName = Motion->name.empty() ? FString(TEXT("Default")) : FString(UTF8_TO_TCHAR(Motion->name.c_str()));
		}
	}
	if (Move == ESwcMove::Stun || (Move == ESwcMove::PostureTransition && !(ActiveTransition && ActiveTransition->sprint)))
	{
		// The stun and an idle transition hold the character (ClearMotion).
		GroundVelocity = FVector::ZeroVector;
		Velocity.X = 0.0;
		Velocity.Y = 0.0;
		return;
	}
	if (Move == ESwcMove::PostureTransition && ActiveTransition)
	{
		// A sprint transition: the forced speed in the direction of the character (CharacterRelativeMovement), its deceleration,
		// and after CanTransition the deceleration of SlowToStop [D playeractions_sprintposetransition].
		const FVector Ahead = CharacterOwner != nullptr ? CharacterOwner->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
		const double Current = FVector::DotProduct(GroundVelocity, Ahead);
		const double Forced = ActiveTransition->forcedSpeed * 100.0;
		const double Rate = (StateTime < TransitionMark ? ActiveTransition->decel : ActiveTransition->decelAfter) * 100.0 * DeltaTime;
		const double Next = Current > Forced ? FMath::Max(Current - Rate, Forced) : FMath::Min(Current + 800.0 * DeltaTime, Forced);
		GroundVelocity = Ahead * Next;
		Velocity.X = GroundVelocity.X;
		Velocity.Y = GroundVelocity.Y;
		return;
	}
	const FRotator Frame(0.0, CameraYaw, 0.0);
	const FVector Forward = Frame.Vector();
	const FVector Right = FRotationMatrix(Frame).GetUnitAxis(EAxis::Y);
	const FVector2D Direction = HasMoveInput() && (Speed > 0.0f || SpeedSide > 0.0f) ? Input.GetSafeNormal() : FVector2D::ZeroVector;
	// The speed of each axis (x side, y forward); a backward input has BackwardsMultiplier (the sprint 0.55).
	const double AheadSpeed = Direction.Y < 0.0 ? Speed * Backwards : Speed;
	const FVector Target = Forward * (Direction.Y * AheadSpeed) + Right * (Direction.X * SpeedSide);
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
	const double Side = Approach(FVector::DotProduct(Current, Right), FVector::DotProduct(Target, Right), AccelSide, DecelSide);
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

void USwcCharacterMovement::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
	// A block by the body of a creature (only the body block of a creature has the object type Pawn and blocks the player).
	if (const UPrimitiveComponent* Other = Hit.GetComponent(); Other != nullptr && Other->GetOwner() != GetOwner()
		&& Other->GetCollisionObjectType() == ECC_Pawn)
	{
		++CreatureBlocks;
	}
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
	const bool bInput = HasMoveInput() && Limits.can_nav();
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
	else if (bWalkHeld || !Limits.can_run() || bAiming)
	{
		SetMove(ESwcMove::Walk);
	}
	else if (FMath::Abs(InputAngle(Input)) > ForwardConeDegrees || !Limits.can_sprint())
	{
		SetMove(ESwcMove::NavStart);
	}
	else
	{
		SetMove(ESwcMove::SprintStart);
	}
	Super::ProcessLanded(Hit, RemainingTime, Iterations);
}
