#include "SwcCameraManager.h"

#include "Character/SwcCharacterMovement.h"
#include "Character/SwcPlayerActions.h"
#include "Character/SwcPlayerCharacter.h"
#include "Character/SwcSmooth.h"
#include "Character/SwcTestFloor.h"
#include "Environment/SwcMatchView.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SwcBackendSubsystem.h"

namespace
{
/** The FOV option of the player: 57 degrees (vertical [I]; option m_fov, C 0x457C016). Unreal keeps the vertical FOV
 *  (MaintainYFOV) with the horizontal FOV of a 16:9 view: 2 atan(tan(57 / 2) x 16 / 9) = 88.0 degrees. */
constexpr float PlayerFov = 57.0f;
constexpr float ReferenceAspect = 16.0f / 9.0f;
/** The zoom option m_cameraZoom: 2.0 m at the start (C 0x457BCF8). */
constexpr float DefaultZoom = 2.0f;
/** The pivot snaps to its target after a move of more than 2 m in one frame (C 0x1049C20: distance squared > 4). */
constexpr double PivotSnapDistance = 200.0;
/** The CameraLockComponent of the player slice: m_maxLockRange 50 m; the scans close (radius 10 m, max angle 95, weights:
 *  scan 1, angle 3, radius 1), mid (50 m, 90, scan 3, angle 10, radius 1), precision (length 50 m, width 1.5 m, scan 3,
 *  length 2, width 1); m_minCamPitchToLockedTarget -5, m_maxCamPitchToLockedTarget 10; m_targetSwitchInterpolationTime
 *  0.3 s; m_targetLOSLossTime 1 s; m_mouseMovementTrackingTime 0.25 s, m_mouseXMovementThreshold 700,
 *  m_mouseMovementTrackingIgnoreTime 0.2 s. */
constexpr double LockRange = 5000.0;
constexpr float LockPitchMin = -5.0f;
constexpr float LockPitchMax = 10.0f;
constexpr float LockSwitchTime = 0.3f;
constexpr float LockLineOfSightTime = 1.0f;
/** The part of the line of sight at the target that its own body can block (cm; an inference, P2.6). */
constexpr double LockSightBody = 150.0;
constexpr double LockMouseTime = 0.25;
constexpr float LockMouseThreshold = 700.0f;
constexpr double LockMouseIgnoreTime = 0.2;

/** The score of a candidate for the first lock: the smallest weighted cost of the scans that hold it (the weights of the
 *  slice; the combination of the scans is an inference). */
double LockCost(const FVector& Camera, const FVector& Forward, const FVector& Point)
{
	const FVector To = Point - Camera;
	const double Distance = To.Size();
	const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, To.GetSafeNormal()), -1.0, 1.0)));
	double Cost = TNumericLimits<double>::Max();
	if (Distance <= 1000.0 && Angle <= 95.0 / 2.0)
	{
		Cost = FMath::Min(Cost, (3.0 * Angle / 47.5 + 1.0 * Distance / 1000.0) / 1.0);
	}
	if (Distance <= 5000.0 && Angle <= 90.0 / 2.0)
	{
		Cost = FMath::Min(Cost, (10.0 * Angle / 45.0 + 1.0 * Distance / 5000.0) / 3.0);
	}
	const double Ahead = FVector::DotProduct(To, Forward);
	const double Side = (To - Forward * Ahead).Size();
	if (Ahead > 0.0 && Ahead <= 5000.0 && Side <= 75.0)
	{
		Cost = FMath::Min(Cost, (2.0 * Ahead / 5000.0 + 1.0 * Side / 75.0) / 3.0);
	}
	return Cost;
}
} // namespace

void ASwcCameraManager::AddZoom(float Notches)
{
	const FSwcCameraState& State = Data.State(StateName);
	if (State.MaxZoom > State.MinZoom)
	{
		// Wheel up is cam_zoom_in: the zoom less zoomAmount (1.0 m) for each notch, in MinZoom..MaxZoom (R1.4.3).
		SavedZoom = FMath::Clamp(SavedZoom - Notches * Data.ZoomAmount, State.MinZoom, State.MaxZoom);
		UE_LOG(LogSwcBackend, Log, TEXT("camera: zoom %.1f m (arm %.1f m)"), SavedZoom, State.Distance + SavedZoom);
	}
}

void ASwcCameraManager::SetSceneView(bool bOn, const FVector& Location, const FVector& LookAt, float VerticalFov)
{
	if (bOn != bScene)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("camera: %s"), bOn ? *FString::Printf(TEXT("the cut to the camera of the conversation at %.2f %.2f %.2f m, FOV %.0f"),
			Location.X / 100.0, -Location.Y / 100.0, Location.Z / 100.0, VerticalFov) : TEXT("the cut back to the state camera"));
	}
	bScene = bOn;
	if (bOn)
	{
		SceneLocation = Location;
		SceneRotation = (LookAt - Location).Rotation();
		SceneFov = VerticalFov;
	}
}

void ASwcCameraManager::SetForcedState(const FString& State)
{
	ForcedState = State == TEXT("none") ? FString() : State;
}

bool ASwcCameraManager::AllowsFreeLook() const
{
	return Data.State(StateName).bAllowFreeLook && !IsLocked();
}

void ASwcCameraManager::SelectState(const ASwcPlayerCharacter& Character)
{
	// The entry of the camera action list (R1.4.1): the first true condition wins.
	FString Entry;
	// The base name of the movement fragment and the posture tag (stage P3.3: the entries of crouch and prone, MidCameraCrouch,
	// MidCameraProne, LockedCameraCrouch, AimCameraCrouch of the camera action list).
	const FString Fragment = Character.GetSwcMovement()->GetBaseFragment();
	const FString Posture = UTF8_TO_TCHAR(swc::move::posture_tag(Character.GetSwcMovement()->GetPosture()));
	// DisableCameraLock in the aim and in the death (the action Death): they end the lock.
	if (IsLocked() && Character.GetActions() != nullptr && Character.GetActions()->IsAiming())
	{
		Unlock(TEXT("the aim"));
	}
	if (IsLocked() && (bDead || (Character.GetActions() != nullptr && Character.GetActions()->IsInputLocked())))
	{
		Unlock(TEXT("the death"));
	}
	if (bDead)
	{
		Entry = TEXT("DeathCam");
	}
	else if (IsLocked())
	{
		Entry = TEXT("Locked_Cam+") + Posture;
	}
	else if (Character.GetActions() != nullptr && Character.GetActions()->IsAiming())
	{
		Entry = TEXT("Aim+") + Posture;
	}
	else if (Fragment == TEXT("Sprint") || Fragment == TEXT("Sprint_Start"))
	{
		Entry = TEXT("Dynamic+Sprint+") + Posture;
	}
	else if (Fragment == TEXT("Idle"))
	{
		Entry = TEXT("Dynamic+Idle+") + Posture;
	}
	else if (Fragment == TEXT("Strafe"))
	{
		Entry = TEXT("Dynamic+Nav+") + Posture;
	}
	else
	{
		Entry = TEXT("Dynamic+") + Posture;
	}
	FString Camera = TEXT("MidCamera");
	float SmoothTime = 0.3f;
	Data.Activation(Entry, Camera, SmoothTime);
	if (!ForcedState.IsEmpty())
	{
		Camera = ForcedState;
	}
	if (Camera == StateName)
	{
		return;
	}
	const FSwcCameraState& Next = Data.State(Camera);
	const bool bFirst = StateName.IsEmpty();
	// The blend time: the FromTransition of the new state for the old state, else the SmoothTime of the entry (R1.4.2 [I]).
	if (const float* Transition = Next.Transitions.Find(StateName))
	{
		SmoothTime = *Transition;
	}
	UE_LOG(LogSwcBackend, Log, TEXT("camera: %s -> %s (%s), blend %.2f s"), bFirst ? TEXT("start") : *StateName, *Camera,
		ForcedState.IsEmpty() ? *Entry : TEXT("forced by the drive"), bFirst ? 0.0f : SmoothTime);
	StateName = Camera;
	BlendTime = SmoothTime;
	const float VerticalFov = Next.bUserFov ? PlayerFov : Next.Fov;
	Distance.Set(Next.Distance, bFirst);
	OffsetSide.Set(Next.Offset.X, bFirst);
	OffsetForward.Set(Next.Offset.Y, bFirst);
	OffsetUp.Set(Next.Offset.Z, bFirst);
	Fov.Set(VerticalFov, bFirst);
	PitchDown.Set(Next.PitchDown, bFirst);
	PitchUp.Set(Next.PitchUp, bFirst);
	HorizontalSensitivity.Set(Next.HorizontalSensitivity, bFirst);
	VerticalSensitivity.Set(Next.VerticalSensitivity, bFirst);
	bAds = Next.bAds;
	if (bFirst)
	{
		Zoom = FMath::Clamp(SavedZoom, Next.MinZoom, Next.MaxZoom);
	}
}

void ASwcCameraManager::UpdateZ(const FVector& TargetPivot, const ASwcPlayerCharacter& Character, const FSwcCameraState& State,
	float DeltaTime)
{
	// The pivot height follows its target with a rate from a strength curve of the height gap (C 0x1049C20; the input of the
	// curve, the gap in m, is an inference): the default curve, the airborne curve in the air, the aiming curve in an aim
	// state, with a blend of 0.3 s back to the default curve.
	if (!bHasPivot || !State.bZSmoothing || FVector::DistSquared(TargetPivot, LastTargetPivot) > FMath::Square(PivotSnapDistance))
	{
		SmoothedZ = TargetPivot.Z;
		bHasPivot = true;
		LastTargetPivot = TargetPivot;
		return;
	}
	const int32 Optional = Character.GetSwcMovement()->IsFalling() || Character.GetSwcMovement()->IsFlying() ? 1
		: StateName.StartsWith(TEXT("Aim")) ? 2 : 0;
	if (Optional != 0)
	{
		OptionalCurve = Optional;
		OptionalTimer = Data.ZCurveBlendOut;
	}
	else
	{
		OptionalTimer = FMath::Max(0.0f, OptionalTimer - DeltaTime);
	}
	const float Gap = static_cast<float>((TargetPivot.Z - SmoothedZ) / 100.0);
	float Strength = SwcCurveAt(Data.ZCurveDefault, Gap);
	if (OptionalTimer > 0.0f && OptionalCurve != 0)
	{
		const float Other = SwcCurveAt(OptionalCurve == 1 ? Data.ZCurveAirborne : Data.ZCurveAiming, Gap);
		Strength = FMath::Lerp(Strength, Other, Data.ZCurveBlendOut > 0.0f ? OptionalTimer / Data.ZCurveBlendOut : 1.0f);
	}
	if (Strength == 0.0f)
	{
		SmoothedZ = TargetPivot.Z;
	}
	else
	{
		SmoothedZ += (TargetPivot.Z - SmoothedZ) * FMath::Min(1.0f, Strength * DeltaTime);
	}
	// The limits: above the target 0.2 x the distance x a scalar of the FOV (40 degrees: 0.7, 70: 1.2); below the target
	// the pivot height less 1.1 x the near plane (the fields of the lower limit are an inference).
	const double Upper = TargetPivot.Z + Data.ZUpperGradient * (Distance.Value + Zoom) * SwcCurveAt(Data.ZFovScalar, Fov.Value) * 100.0;
	const double Lower = TargetPivot.Z - (OffsetUp.Value - 1.1f * State.NearPlane) * 100.0;
	SmoothedZ = FMath::Clamp(SmoothedZ, Lower, Upper);
	LastTargetPivot = TargetPivot;
}

const FSwcLockCandidate* ASwcCameraManager::FindCandidate(uint32 Id) const
{
	return Candidates.FindByPredicate([Id](const FSwcLockCandidate& Candidate) { return Candidate.Id == Id; });
}

void ASwcCameraManager::Unlock(const TCHAR* Cause)
{
	if (LockId != 0)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("camera: target lock off (%s)"), Cause);
	}
	LockId = 0;
	LockMouse.Reset();
}

void ASwcCameraManager::ToggleLock()
{
	if (IsLocked())
	{
		Unlock(TEXT("the lock key"));
		return;
	}
	const APawn* Pawn = PCOwner != nullptr ? PCOwner->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		return;
	}
	// DisableCameraLock of the camera action list: no new lock in the aim, or in an attack before its mark CanTransition.
	if (const ASwcPlayerCharacter* Player = Cast<ASwcPlayerCharacter>(Pawn); Player != nullptr && !Player->GetActions()->IsLockAllowed())
	{
		UE_LOG(LogSwcBackend, Log, TEXT("camera: target lock: not now (the aim, or an attack before its mark CanTransition)"));
		return;
	}
	const FVector Camera = GetCameraLocation();
	const FVector Forward = GetCameraRotation().Vector();
	double Best = TNumericLimits<double>::Max();
	const FSwcLockCandidate* Chosen = nullptr;
	for (const FSwcLockCandidate& Candidate : Candidates)
	{
		const FVector Point = Candidate.LockPoint;
		if (FVector::Dist(Point, Pawn->GetActorLocation()) > LockRange)
		{
			continue;
		}
		const double Cost = LockCost(Camera, Forward, Point);
		if (Cost < Best)
		{
			Best = Cost;
			Chosen = &Candidate;
		}
	}
	if (Chosen == nullptr)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("camera: target lock: no target in %.0f m in front of the camera"), LockRange / 100.0);
		return;
	}
	LockId = Chosen->Id;
	LockPoint = Chosen->LockPoint;
	LockPointRate = FVector::ZeroVector;
	LockStart = LastRotation.Quaternion();
	LockBlend = 0.0f;
	LockBlendRate = 0.0f;
	LineOfSightLost = 0.0f;
	UE_LOG(LogSwcBackend, Log, TEXT("camera: target lock on entity %u at %.1f m"), LockId, FVector::Dist(LockPoint, Pawn->GetActorLocation()) / 100.0);
}

void ASwcCameraManager::CycleTarget(int32 Direction)
{
	const APawn* Pawn = PCOwner != nullptr ? PCOwner->GetPawn() : nullptr;
	const FSwcLockCandidate* Current = FindCandidate(LockId);
	if (!IsLocked() || Pawn == nullptr || Current == nullptr)
	{
		return;
	}
	// The next target: the smallest turn of the camera yaw to the side of the input.
	const FVector Camera = GetCameraLocation();
	const double CurrentYaw = (Current->Position - Camera).Rotation().Yaw;
	double Best = TNumericLimits<double>::Max();
	const FSwcLockCandidate* Chosen = nullptr;
	for (const FSwcLockCandidate& Candidate : Candidates)
	{
		if (Candidate.Id == LockId || FVector::Dist(Candidate.Position, Pawn->GetActorLocation()) > LockRange)
		{
			continue;
		}
		const double Delta = FMath::FindDeltaAngleDegrees(CurrentYaw, (Candidate.Position - Camera).Rotation().Yaw) * Direction;
		if (Delta > 0.0 && Delta < Best)
		{
			Best = Delta;
			Chosen = &Candidate;
		}
	}
	if (Chosen != nullptr)
	{
		LockId = Chosen->Id;
		LockStart = LastRotation.Quaternion();
		LockBlend = 0.0f;
		LockBlendRate = 0.0f;
		LineOfSightLost = 0.0f;
		UE_LOG(LogSwcBackend, Log, TEXT("camera: target lock switch to entity %u (to the %s)"), LockId, Direction > 0 ? TEXT("right") : TEXT("left"));
	}
}

void ASwcCameraManager::AddLockMouse(const FVector2D& Counts)
{
	// A fast mouse move switches the target: more than 700 counts on X in 0.25 s, then no switch for 0.2 s.
	const double Now = GetWorld()->GetTimeSeconds();
	LockMouse.Add(TPair<double, float>(Now, static_cast<float>(Counts.X)));
	LockMouse.RemoveAll([Now](const TPair<double, float>& Entry) { return Now - Entry.Key > LockMouseTime; });
	if (Now < LockMouseIgnoreUntil)
	{
		return;
	}
	float Sum = 0.0f;
	for (const TPair<double, float>& Entry : LockMouse)
	{
		Sum += Entry.Value;
	}
	if (FMath::Abs(Sum) > LockMouseThreshold)
	{
		LockMouse.Reset();
		LockMouseIgnoreUntil = Now + LockMouseIgnoreTime;
		CycleTarget(Sum > 0.0f ? 1 : -1);
	}
}

FRotator ASwcCameraManager::UpdateLock(const FVector& Pivot, const ASwcPlayerCharacter& Character, float DeltaTime)
{
	const FSwcLockCandidate* Target = FindCandidate(LockId);
	if (Target == nullptr)
	{
		Unlock(TEXT("the target is gone"));
		return LastRotation;
	}
	const FVector Point = Target->LockPoint;
	if (FVector::Dist(Point, Character.GetActorLocation()) > LockRange)
	{
		Unlock(TEXT("the target is farther than 50 m"));
		return LastRotation;
	}
	// The line of sight from the camera; the test floor is not world geometry.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SwcLockSight), false, &Character);
	TArray<AActor*> Floor;
	SwcTestFloor::Actors(GetWorld(), Floor);
	Query.AddIgnoredActors(Floor);
	FHitResult Hit;
	// A hit near the lock point is the body of the target itself (the creatures have models and collision from stage 1.9): only
	// a hit farther than LockSightBody from the point blocks the sight.
	if (GetWorld()->LineTraceSingleByChannel(Hit, GetCameraLocation(), Point, ECC_Visibility, Query)
		&& FVector::Dist(Hit.ImpactPoint, Point) > LockSightBody)
	{
		LineOfSightLost += DeltaTime;
		if (LineOfSightLost >= LockLineOfSightTime)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("camera: the sight to the target meets %s (%s) at %.1f m, %.1f m before the target"),
				Hit.GetActor() != nullptr ? *Hit.GetActor()->GetName() : TEXT("no actor"), Hit.GetComponent() != nullptr ? *Hit.GetComponent()->GetName() : TEXT("no component"),
				Hit.Distance / 100.0f, FVector::Dist(Hit.ImpactPoint, Point) / 100.0);
			Unlock(TEXT("no line of sight for 1 s"));
			return LastRotation;
		}
	}
	else
	{
		LineOfSightLost = 0.0f;
	}
	// The camera looks at the smoothed point (lookAtPositionSmoothingTime 0.1 s), the pitch to the target from -5 to 10
	// degrees, with a blend of 0.3 s after a lock or a switch.
	SwcSmoothCD(LockPoint, LockPointRate, Point, Data.LookAtSmoothingTime, DeltaTime);
	FRotator Look = (LockPoint - Pivot).Rotation();
	Look.Pitch = FMath::Clamp(Look.Pitch, LockPitchMin, LockPitchMax);
	if (Target->bLimitPitch)
	{
		// The pitch limits of the observer of the target (its CameraLockTargetComponent): both ranges apply (an inference).
		Look.Pitch = FMath::Clamp(Look.Pitch, Target->PitchMin, Target->PitchMax);
	}
	Look.Roll = 0.0;
	SwcSmoothCD(LockBlend, LockBlendRate, 1.0f, LockSwitchTime, DeltaTime);
	const FRotator Result = FQuat::Slerp(LockStart, Look.Quaternion(), FMath::Clamp(LockBlend, 0.0f, 1.0f)).Rotator();
	return FRotator(Result.Pitch, Result.Yaw, 0.0);
}

void ASwcCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(OutVT.Target);
	if (Character == nullptr || PCOwner == nullptr)
	{
		Super::UpdateViewTarget(OutVT, DeltaTime);
		return;
	}
	if (!bLoaded)
	{
		bLoaded = true;
		FString Report;
		Data.Load(Report);
		UE_LOG(LogSwcBackend, Log, TEXT("%s"), *Report);
		SavedZoom = DefaultZoom;
	}
	SelectState(*Character);
	const FSwcCameraState& State = Data.State(StateName);
	for (FBlend* Value : {&Distance, &OffsetSide, &OffsetForward, &OffsetUp, &Fov, &PitchDown, &PitchUp, &HorizontalSensitivity,
			 &VerticalSensitivity})
	{
		SwcSmoothCD(Value->Value, Value->Rate, Value->Target, BlendTime, DeltaTime);
	}
	// The zoom: the option clamped to the zoom range of the state, smoothed with zoomLag (0.1 s) or ZoomLagOverride.
	const float ZoomTarget = FMath::Clamp(SavedZoom, State.MinZoom, State.MaxZoom);
	SwcSmoothCD(Zoom, ZoomRate, ZoomTarget, State.ZoomLag > -1.5f ? State.ZoomLag : Data.ZoomLag, DeltaTime);

	// The pivot: the feet, the height and the forward offset of the state, the Z smoothing, then the side offset in the
	// yaw frame of the camera (R1.5).
	FRotator Rotation = PCOwner->GetControlRotation();
	const FVector Feet = Character->GetFeetLocation();
	const FRotator YawFrame(0.0, Rotation.Yaw, 0.0);
	const FVector TargetPivot = Feet + FVector(0.0, 0.0, OffsetUp.Value * 100.0) + YawFrame.Vector() * (OffsetForward.Value * 100.0);
	UpdateZ(TargetPivot, *Character, State, DeltaTime);
	FVector Pivot(TargetPivot.X, TargetPivot.Y, SmoothedZ);
	Pivot += FRotationMatrix(YawFrame).GetUnitAxis(EAxis::Y) * (OffsetSide.Value * 100.0);
	if (IsLocked())
	{
		Rotation = UpdateLock(Pivot, *Character, DeltaTime);
		if (IsLocked())
		{
			PCOwner->SetControlRotation(Rotation);
		}
	}
	LastRotation = Rotation;

	// The collision cast: a sphere of 0.084 m from the pivot less 0.049 m to the desired distance and 0.04 m more, against
	// the static world (ECC_Camera: the characters do not block it). A pull-in has the DistanceDampening of the state (0:
	// at once); the push-out after a hit has offCollisionSmoothTime (0.35 s).
	const FVector Back = -Rotation.Vector();
	const float Desired = (Distance.Value + Zoom) * 100.0f;
	float Allowed = Desired;
	if (State.bCollides)
	{
		const float Start = Data.FocusCastRadius * 50.0f;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(SwcCamera), false, Character);
		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(Hit, Pivot + Back * Start, Pivot + Back * (Desired + Data.CastExtension * 100.0f), FQuat::Identity,
				ECC_Camera, FCollisionShape::MakeSphere(Data.CastRadius * 100.0f), Query))
		{
			Allowed = FMath::Min(Desired, static_cast<float>(FVector::Dist(Pivot, Hit.Location)));
		}
	}
	const bool bHit = Allowed < Desired - 1.0f;
	if (Allowed < CurrentDistance - 0.1f)
	{
		SwcSmoothCD(CurrentDistance, DistanceRate, Allowed, State.DistanceDampening, DeltaTime);
	}
	else if (bPushOut)
	{
		SwcSmoothCD(CurrentDistance, DistanceRate, Allowed, Data.OffCollisionSmoothTime, DeltaTime);
		if (!bHit && FMath::Abs(Allowed - CurrentDistance) <= 0.1f)
		{
			bPushOut = false;
			UE_LOG(LogSwcBackend, Log, TEXT("camera: out again at %.2f m, %.2f s after the last hit"), CurrentDistance / 100.0f,
				GetWorld()->GetTimeSeconds() - LastHitTime);
		}
	}
	else
	{
		CurrentDistance = Allowed;
		DistanceRate = 0.0f;
	}
	if (bHit)
	{
		// A hit: the way back out is smoothed.
		if (!bPushOut)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("camera: the collision cast hits: %.2f m in place of %.2f m"), Allowed / 100.0f, Desired / 100.0f);
		}
		bPushOut = true;
		LastHitTime = GetWorld()->GetTimeSeconds();
	}

	OutVT.POV.Location = bScene ? SceneLocation : Pivot + Back * CurrentDistance;
	OutVT.POV.Rotation = bScene ? SceneRotation : Rotation;
	OutVT.POV.FOV = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(bScene ? SceneFov : Fov.Value) * 0.5f) * ReferenceAspect));
	OutVT.POV.AspectRatio = ReferenceAspect;
	OutVT.POV.bConstrainAspectRatio = false;
	OutVT.POV.AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV;
	FSwcViewOverride::Apply(OutVT.POV);   // a matched view of the fidelity checks (Environment/SwcMatchView.h)
	ViewPitchMin = PitchDown.Value;
	ViewPitchMax = PitchUp.Value;

	if (bDrawCandidates)
	{
		for (const FSwcLockCandidate& Candidate : Candidates)
		{
			if (FVector::Dist(Candidate.Position, Character->GetActorLocation()) <= LockRange)
			{
				const bool bLocked = Candidate.Id == LockId;
				DrawDebugSphere(GetWorld(), Candidate.LockPoint, bLocked ? 40.0f : 25.0f, 10,
					bLocked ? FColor::Red : FColor(200, 200, 200), false, -1.0f, 0, bLocked ? 3.0f : 1.0f);
			}
		}
	}
}
