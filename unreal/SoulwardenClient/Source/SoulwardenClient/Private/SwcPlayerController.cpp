#include "SwcPlayerController.h"

#include "Character/SwcCharacterMovement.h"
#include "Character/SwcPlayerCharacter.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "SwcBackendSubsystem.h"
#include "SwcCameraManager.h"

namespace
{
/** The mouse look: degrees = mouse counts x 0.5 (rotateyaw / rotatepitch, defaultprofile.inputbindings) x 0.05 (option
 *  m_cameraSensitivityHorizontal / Vertical) x the sensitivity of the camera state = 0.025 degrees each count in
 *  MidCamera, no smoothing (research feel-values.md R1.4.4). */
constexpr float LookDegreesPerCount = 0.5f * 0.05f;

UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
{
	UInputAction* Action = NewObject<UInputAction>(Outer, Name);
	Action->ValueType = Type;
	return Action;
}

/** A key of a 2D action: the swizzle moves a 1D key to Y; the negate turns its sign. */
void MapAxisKey(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key, bool bToY, bool bNegative)
{
	FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
	if (bToY)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		Mapping.Modifiers.Add(Swizzle);
	}
	if (bNegative)
	{
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}
}
} // namespace

ASwcPlayerController::ASwcPlayerController()
{
	PlayerCameraManagerClass = ASwcCameraManager::StaticClass();
}

ASwcCameraManager* ASwcPlayerController::GetSwcCamera() const
{
	return Cast<ASwcCameraManager>(PlayerCameraManager);
}

void ASwcPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (USwcBackendSubsystem* Backend = GameInstance->GetSubsystem<USwcBackendSubsystem>())
		{
			Backend->LogEndpoints();
		}
	}
	if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Input->AddMappingContext(MovementContext, 1);
	}
	SetInputMode(FInputModeGameOnly());
	FString DrivePath;
	if (FParse::Value(FCommandLine::Get(), TEXT("SwcDrive="), DrivePath))
	{
		LoadDrive(DrivePath);
	}
}

void ASwcPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	// The actions of the maps "movement", "camera", and "combat" of the default profile (R6.3, R6.4).
	MoveAction = MakeAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	JumpAction = MakeAction(this, TEXT("IA_Traverse"), EInputActionValueType::Boolean);
	WalkAction = MakeAction(this, TEXT("IA_Walk"), EInputActionValueType::Boolean);
	ZoomAction = MakeAction(this, TEXT("IA_Zoom"), EInputActionValueType::Axis1D);
	LockAction = MakeAction(this, TEXT("IA_TargetLock"), EInputActionValueType::Boolean);
	FreeLookAction = MakeAction(this, TEXT("IA_FreeLook"), EInputActionValueType::Boolean);
	MovementContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Movement"));
	MapAxisKey(MovementContext, MoveAction, EKeys::W, true, false);
	MapAxisKey(MovementContext, MoveAction, EKeys::S, true, true);
	MapAxisKey(MovementContext, MoveAction, EKeys::A, false, true);
	MapAxisKey(MovementContext, MoveAction, EKeys::D, false, false);
	MovementContext->MapKey(LookAction, EKeys::Mouse2D);
	MovementContext->MapKey(JumpAction, EKeys::SpaceBar);
	MovementContext->MapKey(WalkAction, EKeys::LeftControl);
	MovementContext->MapKey(ZoomAction, EKeys::MouseWheelAxis);
	MovementContext->MapKey(LockAction, EKeys::MiddleMouseButton);
	MovementContext->MapKey(FreeLookAction, EKeys::LeftAlt);
	if (UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(InputComponent))
	{
		Enhanced->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASwcPlayerController::OnMove);
		Enhanced->BindAction(MoveAction, ETriggerEvent::Completed, this, &ASwcPlayerController::OnMoveEnd);
		Enhanced->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASwcPlayerController::OnLook);
		Enhanced->BindAction(JumpAction, ETriggerEvent::Started, this, &ASwcPlayerController::OnJump);
		Enhanced->BindAction(WalkAction, ETriggerEvent::Started, this, &ASwcPlayerController::OnWalk);
		Enhanced->BindAction(WalkAction, ETriggerEvent::Completed, this, &ASwcPlayerController::OnWalkEnd);
		Enhanced->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ASwcPlayerController::OnZoom);
		Enhanced->BindAction(LockAction, ETriggerEvent::Started, this, &ASwcPlayerController::OnLock);
		Enhanced->BindAction(FreeLookAction, ETriggerEvent::Started, this, &ASwcPlayerController::OnFreeLook);
		Enhanced->BindAction(FreeLookAction, ETriggerEvent::Completed, this, &ASwcPlayerController::OnFreeLookEnd);
	}
	else
	{
		UE_LOG(LogSwcBackend, Error, TEXT("input: the input component is not an Enhanced Input component (Config/DefaultInput.ini)"));
	}
}

void ASwcPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (InPawn != nullptr)
	{
		// The camera starts behind the character, level (the start of a session of the retail client, protocol notes 4.7).
		SetControlRotation(FRotator(0.0, InPawn->GetActorRotation().Yaw, 0.0));
		if (!Drive.IsEmpty())
		{
			DriveStart = GetWorld()->GetTimeSeconds();
			UE_LOG(LogSwcBackend, Log, TEXT("drive: %d step(s) from now"), Drive.Num());
		}
	}
}

void ASwcPlayerController::OnMove(const FInputActionValue& Value)
{
	MoveInput = Value.Get<FVector2D>();
}

void ASwcPlayerController::OnMoveEnd(const FInputActionValue&)
{
	MoveInput = FVector2D::ZeroVector;
}

void ASwcPlayerController::OnLook(const FInputActionValue& Value)
{
	const FVector2D Counts = Value.Get<FVector2D>();
	ASwcCameraManager* Camera = GetSwcCamera();
	if (Camera != nullptr && Camera->IsLocked())
	{
		// The target lock turns the camera; a fast mouse move switches the target.
		Camera->AddLockMouse(Counts);
		return;
	}
	const FVector2D Sensitivity = Camera != nullptr ? Camera->GetSensitivity() : FVector2D(1.0, 1.0);
	// RotationInput has no legacy input scale; the camera manager clamps the pitch (the pitch limits of the camera state).
	RotationInput.Yaw += Counts.X * LookDegreesPerCount * Sensitivity.X;
	RotationInput.Pitch += Counts.Y * LookDegreesPerCount * Sensitivity.Y;
}

void ASwcPlayerController::OnJump(const FInputActionValue&)
{
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetSwcMovement()->RequestJump();
	}
}

void ASwcPlayerController::OnWalk(const FInputActionValue&)
{
	bWalkHeld = true;
}

void ASwcPlayerController::OnWalkEnd(const FInputActionValue&)
{
	bWalkHeld = false;
}

void ASwcPlayerController::OnZoom(const FInputActionValue& Value)
{
	ASwcCameraManager* Camera = GetSwcCamera();
	const float Notches = Value.Get<float>();
	if (Camera == nullptr || Notches == 0.0f)
	{
		return;
	}
	// The wheel: the zoom, or the next target while the target lock is on (camera_lock_next_target_*).
	if (Camera->IsLocked())
	{
		Camera->CycleTarget(Notches > 0.0f ? 1 : -1);
	}
	else
	{
		Camera->AddZoom(Notches);
	}
}

void ASwcPlayerController::OnLock(const FInputActionValue&)
{
	// camera_lock_toggle: the slice default of CameraLock_InputMode_PressAndHold is false (toggle).
	if (ASwcCameraManager* Camera = GetSwcCamera())
	{
		Camera->ToggleLock();
	}
}

void ASwcPlayerController::OnFreeLook(const FInputActionValue&)
{
	// Free look (Left Alt): the press keeps the camera direction, the release sets it again (C 0x1046420); in between
	// the character does not turn to the camera.
	const ASwcCameraManager* Camera = GetSwcCamera();
	if (Camera != nullptr && Camera->AllowsFreeLook())
	{
		bFreeLook = true;
		FreeLookRotation = GetControlRotation();
	}
}

void ASwcPlayerController::OnFreeLookEnd(const FInputActionValue&)
{
	if (bFreeLook)
	{
		bFreeLook = false;
		SetControlRotation(FreeLookRotation);
	}
}

void ASwcPlayerController::PlayerTick(float DeltaTime)
{
	TickDrive(DeltaTime);
	Super::PlayerTick(DeltaTime);
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		const double Yaw = bFreeLook ? FreeLookRotation.Yaw : GetControlRotation().Yaw;
		Character->GetSwcMovement()->SetInput(MoveInput, Yaw, bWalkHeld);
	}
}

void ASwcPlayerController::LoadDrive(const FString& Path)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		UE_LOG(LogSwcBackend, Error, TEXT("drive: no file %s"), *Path);
		return;
	}
	for (const FString& Line : Lines)
	{
		TArray<FString> Words;
		Line.TrimStartAndEnd().ParseIntoArrayWS(Words);
		if (Words.Num() < 2 || Words[0].StartsWith(TEXT("#")))
		{
			continue;
		}
		FDriveStep& Step = Drive.AddDefaulted_GetRef();
		Step.Time = FCString::Atof(*Words[0]);
		Step.Command = Words[1];
		Step.Text = Words.Num() > 2 ? Words[2] : FString();
		Step.Value.X = Words.Num() > 2 ? FCString::Atof(*Words[2]) : 0.0;
		Step.Value.Y = Words.Num() > 3 ? FCString::Atof(*Words[3]) : 0.0;
	}
	Drive.StableSort([](const FDriveStep& A, const FDriveStep& B) { return A.Time < B.Time; });
	UE_LOG(LogSwcBackend, Log, TEXT("drive: %d step(s) in %s"), Drive.Num(), *Path);
}

void ASwcPlayerController::TickDrive(float DeltaTime)
{
	UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (DriveStart < 0.0 || Input == nullptr)
	{
		return;
	}
	const double Time = GetWorld()->GetTimeSeconds() - DriveStart;
	while (DriveNext < Drive.Num() && Drive[DriveNext].Time <= Time)
	{
		const FDriveStep& Step = Drive[DriveNext++];
		UE_LOG(LogSwcBackend, Log, TEXT("drive: %.2f s %s %s"), Time, *Step.Command, *Step.Text);
		if (Step.Command == TEXT("move"))
		{
			if (Step.Value.IsNearlyZero())
			{
				Input->StopContinuousInputInjectionForAction(MoveAction);
			}
			else
			{
				Input->StartContinuousInputInjectionForAction(MoveAction, FInputActionValue(Step.Value), {}, {});
			}
		}
		else if (Step.Command == TEXT("walk") || Step.Command == TEXT("freelook"))
		{
			UInputAction* Action = Step.Command == TEXT("walk") ? WalkAction.Get() : FreeLookAction.Get();
			if (Step.Value.X != 0.0)
			{
				Input->StartContinuousInputInjectionForAction(Action, FInputActionValue(true), {}, {});
			}
			else
			{
				Input->StopContinuousInputInjectionForAction(Action);
			}
		}
		else if (Step.Command == TEXT("jump"))
		{
			Input->InjectInputForAction(JumpAction, FInputActionValue(true), {}, {});
		}
		else if (Step.Command == TEXT("lock"))
		{
			Input->InjectInputForAction(LockAction, FInputActionValue(true), {}, {});
		}
		else if (Step.Command == TEXT("wheel"))
		{
			Input->InjectInputForAction(ZoomAction, FInputActionValue(static_cast<float>(Step.Value.X)), {}, {});
		}
		else if (Step.Command == TEXT("turn"))
		{
			DriveTurn.X = Step.Value.X;
		}
		else if (Step.Command == TEXT("pitch"))
		{
			DriveTurn.Y = Step.Value.X;
		}
		else if (Step.Command == TEXT("flick"))
		{
			// A fast mouse move: these counts in one frame.
			Input->InjectInputForAction(LookAction, FInputActionValue(FVector2D(Step.Value.X, 0.0)), {}, {});
		}
		else if (Step.Command == TEXT("camera"))
		{
			if (ASwcCameraManager* Camera = GetSwcCamera())
			{
				Camera->SetForcedState(Step.Text);
			}
		}
	}
	if (!DriveTurn.IsNearlyZero())
	{
		// The mouse counts of this frame for the rate of the script (with the sensitivity 1 of MidCamera).
		Input->InjectInputForAction(LookAction, FInputActionValue(DriveTurn * DeltaTime / LookDegreesPerCount), {}, {});
	}
}
