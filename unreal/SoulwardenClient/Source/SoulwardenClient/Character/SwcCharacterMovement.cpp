#include "Character/SwcCharacterMovement.h"

#include "Character/SwcLocomotion.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "SwcBackendSubsystem.h"
#include "Water/SwcWaterSubsystem.h"

namespace
{
// The values of the game data (research feel-values.md R2; m/s and m/s^2, here cm). [D] = action lists of the player.
/** Sprint (default forward movement, also side input in strafe mode): 5.5 m/s, 20 / 20 m/s^2 for each weapon (the native Sprint,
 *  swc::move::native_sprint; with no movement data). */
constexpr float SprintSpeed = 550.0f;
constexpr float SprintAccel = 2000.0f;
constexpr float SprintDecel = 2000.0f;
/** Sprint_Start becomes Sprint at the CAGE-TransitionInstant of its fragment: 1.4 s when the data has no mark (the stand fragments of
 *  player.adb and of the weapons, research p3g-move adb-sprint-start.txt). */
constexpr float DefaultSprintStartEnd = 1.4f;
/** Run (Nav, backward input or sprint blocked): 3.25 m/s, 35 / 35 m/s^2 unarmed; Nav_Start 12 / 35 [D playeractions_nav]. */
constexpr float NavSpeed = 325.0f;
constexpr float NavAccel = 3500.0f;
constexpr float NavStartAccel = 1200.0f;
constexpr float NavDecel = 3500.0f;
/** Walk (Left Ctrl, or no "can run"): 1.65 m/s, 25 / 25 m/s^2 [D Stand_Walk]. */
constexpr float WalkSpeed = 165.0f;
constexpr float WalkAccel = 2500.0f;
/** Stop (Nav_Stop, no input): the native Nav_Stop, 25 m/s^2 (18 from a sprint above 5.0 m/s; swc::move::native_stop_decel). */
constexpr float StopDecel = 2500.0f;
/** The rate of the combat stance (a weapon in the active slot, drawn or sheathed; with no movement data): 25 / 25 m/s^2. */
constexpr float ArmedNavAccel = 2500.0f;
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
/** Stage WF5, the swim (docs/WATER.md). The feet of a swimmer stay 1.400 m below the water surface, in Swim_Idle and Swim_Nav,
 *  with no bob; the swim starts at the step at which the feet reach that depth (the measurement of the retail client:
 *  ~/Games/soulwarden/work/evidence/unreal-references/set3-swim/swim-heights.json, result.swimmer_height, swim_start). */
constexpr float SwimFeetDepth = 1.400f;
/** The exit: the floor pushes the feet up while the swim goes on; the exit action starts at 1.29 m below the surface (1.2893 and
 *  1.2901 m), and the wade 0.5 s later (swim-heights.json, result.swim_exit). */
constexpr float SwimExitDepth = 1.29f;
constexpr float SwimExitTime = 0.5f;
/** Swim_Nav: CharacterRelativeMovement, forward only, 3.0 m/s, accel 3.0, decel 130 m/s^2, no gravity, no speed multiplier
 *  [D playeractions_swim; research wf5-swim/swim-rules.json]; measured 3.0 m/s (swim-heights.json, result.speeds_m_s). */
constexpr float SwimSpeed = 300.0f;
constexpr float SwimAccel = 300.0f;
constexpr float SwimDecel = 13000.0f;
/** The body turns to the direction of the input: InputRelativeFacing TurnRate 0.10 for each 1/30 s, exponential [D
 *  playeractions_swim]; measured: heading 0 to 90 in about 1.3 s (swim-heights.json, result.body_turn_in_the_swim). */
constexpr float SwimTurnRate = 0.10f;
/** After the entry the feet go back to the swim height: 1.3 cm below it, then 0.6 cm 0.4 s later (swim-heights.json,
 *  result.swim_entry_with_no_key): a return of about 1.7 for each second [inference from those samples]. */
constexpr float SwimReturnRate = 1.7f;

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
	if (bProne && (IsSwimming() || WaterClass >= swc::water::DepthClass::veryShallow))
	{
		// No prone in the water: ToggleProne needs IfWaterDepth LessThan Shallow, and prone goes to crouch at VeryShallow (research
		// wf5-swim/water-uses.json).
		UE_LOG(LogSwcBackend, Log, TEXT("move: the key Z: no prone in the water"));
		return;
	}
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
	if (bBlocking)
	{
		// The block (Block_Nav_Speed, SprintDisabled, Combat_Nav_Stand "not Blocking").
		Facts.tags.insert("Blocking");
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
	Facts.waterDepth = static_cast<std::uint8_t>(WaterClass);
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

void USwcCharacterMovement::SetCombat(bool bInArmedNav, bool bInAiming)
{
	bArmedNav = bInArmedNav;
	if (bInAiming && !bAiming)
	{
		CancelAutoRun(TEXT("the aim"));
	}
	bAiming = bInAiming;
}

void USwcCharacterMovement::SetBlocking(bool bInBlocking)
{
	if (bInBlocking && !bBlocking)
	{
		// ClearMotion of Block_UpperBody: the block starts from rest.
		ClearMotion();
		CancelAutoRun(TEXT("the block"));
	}
	bBlocking = bInBlocking;
}

void USwcCharacterMovement::ClearMotion()
{
	GroundVelocity = FVector::ZeroVector;
	Velocity.X = 0.0;
	Velocity.Y = 0.0;
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

namespace
{
/** The NavType tags of Nav_Stop (AST-199). */
const TCHAR* const NavTypeTags[3] = {TEXT("Walk"), TEXT("Run"), TEXT("Sprint")};
} // namespace

float USwcCharacterMovement::NavStopLength() const
{
	if (StopNavType == INDEX_NONE)
	{
		return 0.0f;
	}
	if (NavStopLengths[StopNavType] > 0.0f)
	{
		return NavStopLengths[StopNavType];
	}
	// The unarmed fragment: its mark NavStop_Exit (the walk: 0.33 s), else its end (the run 0.733 s, the sprint 1.5 s).
	const FSwcFragment* Fragment = FragmentData(FString(TEXT("Nav_Stop+")) + NavTypeTags[StopNavType]);
	return Fragment != nullptr ? Fragment->Marker(TEXT("NavStop_Exit"), Locomotion->Length(*Fragment)) : 0.0f;
}

FString USwcCharacterMovement::GetBaseFragment() const
{
	if ((Move == ESwcMove::Stop || Move == ESwcMove::Idle) && StopNavType != INDEX_NONE && StopFragmentTime < NavStopLength())
	{
		return FString(TEXT("Nav_Stop+")) + NavTypeTags[StopNavType];
	}
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
	case ESwcMove::Swim:
		// Swim_Nav with a move input (AnyNavInput), else Swim_Idle [D playeractions_swim].
		return HasMoveInput() ? TEXT("Swim_Nav") : TEXT("Swim_Idle");
	case ESwcMove::SwimExit:
		return TEXT("Swim_Nav");
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
	if (Next == Move)
	{
		return;
	}
	const auto IsSprint = [](ESwcMove State) { return State == ESwcMove::SprintStart || State == ESwcMove::Sprint; };
	const auto IsNav = [](ESwcMove State) { return State == ESwcMove::NavStart || State == ESwcMove::Nav || State == ESwcMove::Walk; };
	const float Speed = static_cast<float>(GroundVelocity.Size2D() / 100.0);
	// The clocks of the native actions: Sprint_Start and Sprint are one action (research p3g-move 3), Nav_Start, Nav, and the walk
	// one action (the context tag "running": the speed at its first frame, or 0.5 s in it).
	if (IsSprint(Next) && !IsSprint(Move))
	{
		SprintActionTime = 0.0f;
	}
	if (IsNav(Next) && !IsNav(Move))
	{
		NavActionTime = 0.0f;
		bNavRunningAtStart = Speed >= 1.65f;
	}
	// Nav_Stop: the rate of its request (the speed before the first frame), and one frame with the rate of the action before it.
	if ((Next == ESwcMove::Stop || Next == ESwcMove::Idle) && (IsSprint(Move) || IsNav(Move)))
	{
		StopDecelAtRequest = swc::move::native_stop_decel(Speed) * 100.0f;
		StopFirstDecel = LastMoveDecel;
	}
	else if (Next == ESwcMove::Stop || Next == ESwcMove::Idle)
	{
		StopDecelAtRequest = swc::move::native_stop_decel(Speed) * 100.0f;
		StopFirstDecel = StopDecelAtRequest;
	}
	// AST-199: the fragment Nav_Stop with the NavType tag of the speed at its request (swc::move::nav_stop_tag). It plays on after the
	// motion ends (Idle) until the end of its fragment; a move, a jump, or a posture change ends it.
	if (Next == ESwcMove::Stop && (IsSprint(Move) || IsNav(Move)) && Posture.posture() == swc::move::Posture::stand)
	{
		StopNavType = swc::move::nav_stop_tag(Speed, IsSprint(Move));
		StopFragmentTime = 0.0f;
	}
	else if (Next != ESwcMove::Stop && Next != ESwcMove::Idle)
	{
		StopNavType = INDEX_NONE;
	}
	Move = Next;
	StateTime = 0.0f;
}

void USwcCharacterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	StateTime += DeltaTime;
	StopFragmentTime += DeltaTime;
	SprintActionTime += DeltaTime;
	NavActionTime += DeltaTime;
	SkipSprintStartLeft = FMath::Max(SkipSprintStartLeft - DeltaTime, 0.0f);
	UpdateWater();
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
	if (UpdateSwim())
	{
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
		// crouch or prone it stands the character up first (Jump sets Stand; with room above). No jump in a wade (IfWaterDepth
		// LessThan Shallow of Jump, Jump_Diagonal, Standing_Jump; research wf5-swim/water-uses.json).
		if (Limits.can_jump() && HasRoomFor(swc::move::Posture::stand) && WaterClass < swc::water::DepthClass::shallow)
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
	// A wade has no sprint: each move in Shallow water is the Nav of the retail client (layer state 26, the native Nav at 2.75 m/s;
	// set3-swim/nav-velocity.json, wade-speed.json: the default move, the backward move, and Space have the same speed). A sprint
	// that comes into the wade goes to Nav (the D sprint of set3-swim/swim-walk-in-out: 5.5 m/s, then 2.75 m/s in the wade).
	const bool bWade = WaterClass >= swc::water::DepthClass::shallow;
	// The aim (AimDownSights) has no sprint (Sprint_Start needs "not ADS"); the walk key has none (Sprint_Start: not walk); no
	// IfCanSprint (NoSprint, the server bit), IfEncumbered, or Overburdened: no sprint [D playeractions_sprint]. The block has no
	// sprint (SprintDisabled: the tag Blocking): its move is the Nav (the retail block W and S: layer state 26, 2.0 m/s,
	// set6-combat/player-bitten-block.json).
	if (bWalkHeld || !Limits.can_run() || bAiming)
	{
		SetMove(ESwcMove::Walk);
	}
	else if (bBackward || !Limits.can_sprint() || bWade || bBlocking)
	{
		const bool bStart = Move == ESwcMove::Idle || Move == ESwcMove::Stop || Move == ESwcMove::Land;
		SetMove(bStart ? ESwcMove::NavStart : (Move == ESwcMove::NavStart && Speed < NavSpeed * 0.98f ? ESwcMove::NavStart : ESwcMove::Nav));
	}
	else if (Move == ESwcMove::Sprint)
	{
		// The native Sprint keeps its fragment while the sprint holds (no speed test; research p3g-move 3.5).
		SetMove(ESwcMove::Sprint);
	}
	else if (Move == ESwcMove::SprintStart)
	{
		// Sprint_Start goes to Sprint at its CAGE-TransitionInstant (1.4 s for the stand fragments of the data; research p3g-move 3.4:
		// the native code has no time or speed limit); crouch and prone have the Sprint fragment at once.
		if (StateTime >= SprintStartEnd() || bLowPosture)
		{
			SetMove(ESwcMove::Sprint);
		}
	}
	else
	{
		// The Sprint fragment at once (research p3g-move 3.3): SkipSprintStartTimer after a dodge, crouch or prone, a stand speed of
		// 4.02 m/s or more (|v|^2 >= 16.1604, 0x14807F1A8); else Sprint_Start.
		const bool bDirect = SkipSprintStartLeft > 0.0f || bLowPosture || Speed * Speed >= 161604.0f;
		SetMove(bDirect ? ESwcMove::Sprint : ESwcMove::SprintStart);
	}
}

float USwcCharacterMovement::SprintStartEnd() const
{
	if (SprintStartTransition > 0.0f)
	{
		return SprintStartTransition;
	}
	const FSwcFragment* Fragment = FragmentData(TEXT("Sprint_Start"));
	return Fragment != nullptr ? Fragment->Marker(TEXT("TransitionInstant"), DefaultSprintStartEnd) : DefaultSprintStartEnd;
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
	if (CharacterOwner != nullptr && IsSwimming() && HasMoveInput())
	{
		// The swim: the body turns to the direction of the input in the camera frame (InputRelativeFacing 0.10).
		const double Target = CameraYaw + InputAngle(Input);
		const float SwimAlpha = 1.0f - FMath::Pow(1.0f - SwimTurnRate, 30.0f * DeltaTime);
		FRotator Rotation = CharacterOwner->GetActorRotation();
		Rotation.Yaw += FMath::FindDeltaAngleDegrees(Rotation.Yaw, Target) * SwimAlpha;
		CharacterOwner->SetActorRotation(FRotator(0.0, Rotation.Yaw, 0.0));
		return;
	}
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
	if (Move == ESwcMove::Swim && DeltaTime > 0.0f)
	{
		SwimVelocity(DeltaTime);
		return;
	}
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
	// Nav_Stop (Stop, Idle): the native stop (swc::move::native_stop_decel), the first frame with the rate of the action before it.
	float Decel = StateTime < swc::move::kStopFirstFrame ? StopFirstDecel : StopDecelAtRequest;
	float ClearDegrees = 0.0f;
	switch (Move)
	{
	case ESwcMove::SprintStart:
		Speed = SprintSpeed;
		Accel = SprintAccel;
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
	// the speed and the rates of each axis; Nav and Sprint have the multiplier of the speed table on the speed, the acceleration,
	// and the deceleration (CameraRelativeMotion 0x2AA3830: the action-context multiplier scales the four vectors of the entry;
	// research nav-speed).
	const TCHAR* Action = Move == ESwcMove::SprintStart ? TEXT("Sprint_Start") : Move == ESwcMove::Sprint ? TEXT("Sprint")
		: Move == ESwcMove::NavStart ? TEXT("Nav_Start") : (Move == ESwcMove::Nav || Move == ESwcMove::Walk) ? TEXT("Nav")
		: (Move == ESwcMove::Stop || Move == ESwcMove::Idle) ? TEXT("Nav_Stop") : nullptr;
	float SpeedSide = Speed;
	float AccelSide = Accel;
	float DecelSide = Decel;
	float Backwards = 1.0f;
	const swc::move::Data* Data = Rules();
	if (Data != nullptr && (Move == ESwcMove::NavStart || Move == ESwcMove::Nav || Move == ESwcMove::Walk))
	{
		// Nav, Nav_Start, and the walk: the native Nav of the retail client (swc::move::native_nav, 0x1423FC970), not the motion list of
		// the XML: the same speed on each axis, BackwardsMultiplier 1.0, and the multiplier of the speed table on the speed and the rates.
		// The combat stance reads the weapon of the active slot, drawn or sheathed (research p3g-move 4); the context tag "running".
		swc::move::Facts NavFacts = MakeFacts();
		if (WeaponTags.IsEmpty() && !StanceTags.IsEmpty())
		{
			NavFacts.tags.erase("Unarmed");
			for (const FString& Tag : StanceTags)
			{
				NavFacts.tags.insert(TCHAR_TO_UTF8(*Tag));
			}
		}
		NavFacts.navRunning = bNavRunningAtStart || NavActionTime >= 0.5f;
		const swc::move::NavMotion Native = swc::move::native_nav(*Data, NavFacts, Limits.encumbered_movement(), Limits.encumbered(), bWalkHeld);
		const float Multiplier = Limits.speed_multiplier();
		Speed = SpeedSide = Native.speed * 100.0f * Multiplier;
		Accel = AccelSide = Native.accel * 100.0f * Multiplier;
		Decel = DecelSide = Native.decel * 100.0f * Multiplier;
		Backwards = 1.0f;
		MotionAction = TEXT("Nav (native)");
		MotionName = UTF8_TO_TCHAR(Native.branch);
	}
	else if (Data != nullptr && (Move == ESwcMove::SprintStart || Move == ESwcMove::Sprint))
	{
		// Sprint_Start and Sprint: the native Sprint (swc::move::native_sprint, 0x142401C40): 20 / 20 m/s^2 for each weapon, 100 after
		// S / 20 in the action; the BackwardsMultiplier of the motion list (0.55).
		const swc::move::NavMotion Native = swc::move::native_sprint(MakeFacts(), SprintActionTime);
		const float Multiplier = Limits.speed_multiplier();
		Speed = SpeedSide = Native.speed * 100.0f * Multiplier;
		Accel = AccelSide = Native.accel * 100.0f * Multiplier;
		Decel = DecelSide = Native.decel * 100.0f * Multiplier;
		const swc::move::Motion* Motion = swc::move::motion_of(*Data, TCHAR_TO_UTF8(Action), MakeFacts());
		Backwards = Motion != nullptr ? Motion->backwards : 0.55f;
		MotionAction = TEXT("Sprint (native)");
		MotionName = UTF8_TO_TCHAR(Native.branch);
	}
	else if (Data != nullptr && (Move == ESwcMove::Stop || Move == ESwcMove::Idle))
	{
		DecelSide = Decel;
		MotionAction = TEXT("Nav_Stop (native)");
		MotionName = StateTime < swc::move::kStopFirstFrame ? TEXT("the first frame") : StopDecelAtRequest < 2000.0f ? TEXT("Sprint") : TEXT("Run");
	}
	else if (Data != nullptr && Action != nullptr)
	{
		if (const swc::move::Motion* Motion = swc::move::motion_of(*Data, TCHAR_TO_UTF8(Action), MakeFacts()))
		{
			const bool bScaled = Move != ESwcMove::Stop && Move != ESwcMove::Idle;
			const float Multiplier = bScaled ? Limits.speed_multiplier() : 1.0f;
			Speed = Motion->speed[1] * 100.0f * Multiplier;
			SpeedSide = Motion->speed[0] * 100.0f * Multiplier;
			Accel = Motion->accel[1] * 100.0f * Multiplier;
			AccelSide = Motion->accel[0] * 100.0f * Multiplier;
			Decel = Motion->decel[1] * 100.0f * Multiplier;
			DecelSide = Motion->decel[0] * 100.0f * Multiplier;
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
	// The target of CameraRelativeMotion (swc::move::camera_relative_target): |dir x speed| x dir, and a backward target times
	// 1 - |dir.y| x (1 - BackwardsMultiplier) (the sprint 0.55).
	const swc::move::Planar Goal = swc::move::camera_relative_target({static_cast<float>(Direction.X), static_cast<float>(Direction.Y)}, {SpeedSide, Speed}, Backwards);
	const FVector Target = Forward * Goal.y + Right * Goal.x;
	// The velocity of the data, not the velocity of the last move: a block (a wall, the face of a step) does not reset it,
	// thus the next move has its full length (the step-up of the engine needs it).
	FVector Current = GroundVelocity;
	if (ClearDegrees > 0.0f && !Current.IsNearlyZero() && !Target.IsNearlyZero()
		&& FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Current.GetSafeNormal(), Target.GetSafeNormal()))) > ClearDegrees)
	{
		Current = FVector::ZeroVector; // ClearMotion: "prevent sliding".
	}
	// One step along the difference to the target (swc::move::camera_relative_step: the deceleration on an axis whose current speed
	// is above its target, else the acceleration).
	const swc::move::Planar Next = swc::move::camera_relative_step(
		{static_cast<float>(FVector::DotProduct(Current, Right)), static_cast<float>(FVector::DotProduct(Current, Forward))}, Goal,
		{AccelSide, Accel}, {DecelSide, Decel}, DeltaTime);
	const FVector Result = Forward * Next.y + Right * Next.x;
	GroundVelocity = Result;
	Velocity.X = Result.X;
	Velocity.Y = Result.Y;
	if (Move == ESwcMove::SprintStart || Move == ESwcMove::Sprint || Move == ESwcMove::NavStart || Move == ESwcMove::Nav || Move == ESwcMove::Walk)
	{
		LastMoveDecel = Decel;
	}
}

void USwcCharacterMovement::UpdateWater()
{
	const UWorld* World = GetWorld();
	const USwcWaterSubsystem* Water = World != nullptr ? World->GetSubsystem<USwcWaterSubsystem>() : nullptr;
	if (CharacterOwner == nullptr || Water == nullptr || !Water->IsLoaded() || CharacterOwner->GetCapsuleComponent() == nullptr)
	{
		WaterClass = swc::water::DepthClass::none;
		FeetDepth = 0.0f;
		bInWater = false;
		return;
	}
	// The feet: the bottom of the capsule (the position field of the input report), engine m.
	const FVector Feet = CharacterOwner->GetActorLocation() - FVector(0.0, 0.0, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const double X = Feet.X / 100.0;
	const double Y = -Feet.Y / 100.0;
	const std::optional<swc::water::Sample> Sample = Water->GetWaterAt(X, Y);
	const swc::water::DepthClass Before = WaterClass;
	WaterClass = swc::water::depth_class(Sample, static_cast<float>(Feet.Z / 100.0), &FeetDepth);
	bInWater = Sample.has_value() && Feet.Z / 100.0 >= Sample->floor;
	WaterSurface = Sample ? Sample->surface : 0.0;
	if (WaterClass >= swc::water::DepthClass::veryShallow && !IsSwimming()
		&& (Posture.posture() == swc::move::Posture::prone || Posture.desired() == swc::move::Posture::prone))
	{
		// Prone goes to crouch in the water (VeryShallow or deeper).
		Posture.force(swc::move::Posture::crouch);
		ApplyCapsule(swc::move::Posture::crouch);
	}
	if (WaterClass != Before)
	{
		static const TCHAR* Names[] = {TEXT("None"), TEXT("VeryShallow"), TEXT("Shallow"), TEXT("Deep")};
		UE_LOG(LogSwcBackend, Log, TEXT("move: water class %s (the feet %.2f m below the surface %.2f m)"), Names[static_cast<int32>(WaterClass)],
			FeetDepth, WaterSurface);
	}
}

bool USwcCharacterMovement::UpdateSwim()
{
	if (Move == ESwcMove::SwimExit)
	{
		// The exit action holds 0.5 s, then the wade (the ground states with the Wade conditionals of the data).
		if (StateTime < SwimExitTime)
		{
			return true;
		}
		SetMove(ESwcMove::Idle);
		return false;
	}
	if (Move == ESwcMove::Swim)
	{
		if (!bInWater || FeetDepth <= SwimExitDepth)
		{
			// The floor pushed the feet up (or the water ends): the exit, with gravity again.
			SetMovementMode(MOVE_Walking);
			SetMove(ESwcMove::SwimExit);
			UE_LOG(LogSwcBackend, Log, TEXT("move: the swim ends (the feet %.3f m below the surface)"), FeetDepth);
		}
		return true;
	}
	// The start: on the ground or in a fall (a jump goes to the fall first), when the feet reach the swim depth.
	if (!bInWater || FeetDepth < SwimFeetDepth || Move == ESwcMove::Jump || bActionMotion || MovementMode == MOVE_None)
	{
		return false;
	}
	ActiveTransition.reset();
	if (Posture.posture() != swc::move::Posture::stand || Posture.pending())
	{
		Posture.force(swc::move::Posture::stand);
		ApplyCapsule(swc::move::Posture::stand);
	}
	CancelAutoRun(TEXT("the swim"));
	bJumped = false;
	bStandingJump = false;
	bRootMotion = false;
	Velocity.Z = 0.0;
	SetMovementMode(MOVE_Flying);
	SetMove(ESwcMove::Swim);
	UE_LOG(LogSwcBackend, Log, TEXT("move: the swim starts (the feet %.3f m below the surface %.2f m)"), FeetDepth, WaterSurface);
	return true;
}

void USwcCharacterMovement::SwimVelocity(float DeltaTime)
{
	// Forward in the facing of the character (AlignVelocity), to 3.0 m/s with an input, else the stop of 130 m/s^2.
	const FVector Ahead = CharacterOwner != nullptr ? CharacterOwner->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	const double Current = FMath::Max(0.0, FVector::DotProduct(FVector(Velocity.X, Velocity.Y, 0.0), Ahead));
	const double Target = HasMoveInput() && Limits.can_nav() ? SwimSpeed : 0.0;
	const double Next = Current < Target ? FMath::Min(Current + SwimAccel * DeltaTime, Target) : FMath::Max(Current - SwimDecel * DeltaTime, Target);
	GroundVelocity = Ahead * Next;
	Velocity.X = GroundVelocity.X;
	Velocity.Y = GroundVelocity.Y;
	// The height: the feet back to 1.400 m below the surface (a floor above that height stops the move down).
	const double HalfHeight = CharacterOwner != nullptr && CharacterOwner->GetCapsuleComponent() != nullptr
		? CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0;
	const double FeetZ = (CharacterOwner != nullptr ? CharacterOwner->GetActorLocation().Z : 0.0) - HalfHeight;
	const double TargetZ = (WaterSurface - SwimFeetDepth) * 100.0;
	Velocity.Z = (TargetZ - FeetZ) * SwimReturnRate;
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
