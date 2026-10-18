#include "SwcPlayerController.h"

#include "Character/SwcCharacterMovement.h"
#include "Character/SwcPlayerActions.h"
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
#include "UI/SwcUiSubsystem.h"
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
	Weapon1Action = MakeAction(this, TEXT("IA_Weapon1"), EInputActionValueType::Boolean);
	Weapon2Action = MakeAction(this, TEXT("IA_Weapon2"), EInputActionValueType::Boolean);
	SheatheAction = MakeAction(this, TEXT("IA_Sheathe"), EInputActionValueType::Boolean);
	AttackAction = MakeAction(this, TEXT("IA_AttackPrimary"), EInputActionValueType::Boolean);
	DodgeAction = MakeAction(this, TEXT("IA_Dodge"), EInputActionValueType::Boolean);
	AimAction = MakeAction(this, TEXT("IA_Aim"), EInputActionValueType::Boolean);
	GiveUpAction = MakeAction(this, TEXT("IA_DevGiveUp"), EInputActionValueType::Boolean);
	InteractAction = MakeAction(this, TEXT("IA_Interact"), EInputActionValueType::Boolean);
	AbilityActions[0] = MakeAction(this, TEXT("IA_Ability1"), EInputActionValueType::Boolean);
	AbilityActions[1] = MakeAction(this, TEXT("IA_Ability2"), EInputActionValueType::Boolean);
	AbilityActions[2] = MakeAction(this, TEXT("IA_Ability3"), EInputActionValueType::Boolean);
	CrouchAction = MakeAction(this, TEXT("IA_CrouchToggle"), EInputActionValueType::Boolean);
	ProneAction = MakeAction(this, TEXT("IA_ProneToggle"), EInputActionValueType::Boolean);
	AutoRunAction = MakeAction(this, TEXT("IA_AutoRun"), EInputActionValueType::Boolean);
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
	MovementContext->MapKey(Weapon1Action, EKeys::One);
	MovementContext->MapKey(Weapon2Action, EKeys::Two);
	MovementContext->MapKey(SheatheAction, EKeys::Tilde);
	MovementContext->MapKey(AttackAction, EKeys::LeftMouseButton);
	MovementContext->MapKey(DodgeAction, EKeys::LeftShift);
	MovementContext->MapKey(AimAction, EKeys::RightMouseButton);
	// A development key (not a key of the retail client): the give-up of the death screen until the UI track has the screen.
	MovementContext->MapKey(GiveUpAction, EKeys::F8);
	// The key of the prompt of an object (the retail client: E, "E Ignite Fuse", "E Take All").
	MovementContext->MapKey(InteractAction, EKeys::E);
	// The keys of the bar of the default profile: Q, R, F (protocol notes 4.22 "The keys": the input numbers 402, 115, 139).
	MovementContext->MapKey(AbilityActions[0], EKeys::Q);
	MovementContext->MapKey(AbilityActions[1], EKeys::R);
	MovementContext->MapKey(AbilityActions[2], EKeys::F);
	// The keys of the posture and autorun of the default profile: C crouch_toggle, Z prone_toggle, `=` autorun (research
	// feel-values.md R6.3; p33-movement.md Q1, Q2).
	MovementContext->MapKey(CrouchAction, EKeys::C);
	MovementContext->MapKey(ProneAction, EKeys::Z);
	MovementContext->MapKey(AutoRunAction, EKeys::Equals);
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
		Enhanced->BindAction(Weapon1Action, ETriggerEvent::Started, this, &ASwcPlayerController::OnWeapon1);
		Enhanced->BindAction(Weapon2Action, ETriggerEvent::Started, this, &ASwcPlayerController::OnWeapon2);
		Enhanced->BindAction(SheatheAction, ETriggerEvent::Started, this, &ASwcPlayerController::OnSheathe);
		Enhanced->BindAction(AttackAction, ETriggerEvent::Started, this, &ASwcPlayerController::OnAttack);
		Enhanced->BindAction(AttackAction, ETriggerEvent::Completed, this, &ASwcPlayerController::OnAttackEnd);
		Enhanced->BindAction(DodgeAction, ETriggerEvent::Started, this, &ASwcPlayerController::OnDodge);
		Enhanced->BindAction(AimAction, ETriggerEvent::Started, this, &ASwcPlayerController::OnAim);
		Enhanced->BindAction(AimAction, ETriggerEvent::Completed, this, &ASwcPlayerController::OnAimEnd);
		Enhanced->BindAction(GiveUpAction, ETriggerEvent::Started, this, &ASwcPlayerController::OnGiveUp);
		Enhanced->BindAction(InteractAction, ETriggerEvent::Started, this, &ASwcPlayerController::OnInteract);
		Enhanced->BindAction(InteractAction, ETriggerEvent::Completed, this, &ASwcPlayerController::OnInteractEnd);
		for (int32 Place = 0; Place < 3; ++Place)
		{
			Enhanced->BindActionValueLambda(AbilityActions[Place], ETriggerEvent::Started, [this, Place](const FInputActionValue&) { OnAbilityKey(Place, true); });
			Enhanced->BindActionValueLambda(AbilityActions[Place], ETriggerEvent::Completed, [this, Place](const FInputActionValue&) { OnAbilityKey(Place, false); });
		}
		Enhanced->BindActionValueLambda(CrouchAction, ETriggerEvent::Started, [this](const FInputActionValue&) { OnMoveKey(TEXT("c")); });
		Enhanced->BindActionValueLambda(ProneAction, ETriggerEvent::Started, [this](const FInputActionValue&) { OnMoveKey(TEXT("z")); });
		Enhanced->BindActionValueLambda(AutoRunAction, ETriggerEvent::Started, [this](const FInputActionValue&) { OnMoveKey(TEXT("equals")); });
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
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()); Character != nullptr && !Character->GetActions()->IsInputLocked())
	{
		Character->GetSwcMovement()->RequestJump();
	}
}

void ASwcPlayerController::OnWalk(const FInputActionValue&)
{
	bWalkHeld = true;
	// Left Ctrl is also self_target in the default profile (stage P3.6): the self cast of a heal (bit 430).
	if (ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetActions()->SetSelfTarget(true);
	}
}

void ASwcPlayerController::OnWalkEnd(const FInputActionValue&)
{
	bWalkHeld = false;
	if (ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetActions()->SetSelfTarget(false);
	}
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

void ASwcPlayerController::UseWeapon(uint32 Slot, const TArray<uint32>& Bits)
{
	// The weapon keys reach the server in the input report (Use_MainHandSlot_N and quickslot-weaponN; protocol notes 4.15).
	// The client draws the weapon itself (the action lists of Unsheathe_MainHand_N; the backend has no draw, protocol notes
	// 4.11); a key of the other weapon switches at the step of the attack driver (docs/ACTIONS.md).
	PressBits = Bits;
	++PressSerial;
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetActions()->OnWeaponKey(Slot);
	}
}

void ASwcPlayerController::OnWeapon1(const FInputActionValue&)
{
	UseWeapon(24, {9, 40});
}

void ASwcPlayerController::OnWeapon2(const FInputActionValue&)
{
	UseWeapon(25, {355, 306});
}

void ASwcPlayerController::OnSheathe(const FInputActionValue&)
{
	// The sheathe key changes the active weapon between the hand and its sheath (its bit is not known: no bit).
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetActions()->OnSheatheKey();
	}
}

void ASwcPlayerController::OnAttack(const FInputActionValue&)
{
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetActions()->OnAttackButton(true);
	}
}

void ASwcPlayerController::OnAttackEnd(const FInputActionValue&)
{
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetActions()->OnAttackButton(false);
	}
}

void ASwcPlayerController::OnDodge(const FInputActionValue&)
{
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetActions()->OnDodgeKey();
	}
}

void ASwcPlayerController::OnAim(const FInputActionValue&)
{
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetActions()->OnAimButton(true);
	}
}

void ASwcPlayerController::OnAimEnd(const FInputActionValue&)
{
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetActions()->OnAimButton(false);
	}
}

void ASwcPlayerController::OnGiveUp(const FInputActionValue&)
{
	// A development key that no action map binds: it does nothing while the UI has the cursor (a screen of the UI, the cursor
	// owner of stage P2.2), as the keys of the game maps.
	if (const USwcUiSubsystem* Ui = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USwcUiSubsystem>() : nullptr; Ui != nullptr && Ui->UiCursor())
	{
		UE_LOG(LogSwcBackend, Log, TEXT("life: F8 while the UI has the cursor: no respawn request"));
		return;
	}
	if (USwcBackendSubsystem* Backend = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USwcBackendSubsystem>() : nullptr)
	{
		Backend->RequestRespawn();
	}
}

void ASwcPlayerController::OnInteract(const FInputActionValue&)
{
	// The key E of the game map "player" (the pre-processor of the UI keeps a real key from the game while that map is off; an
	// injected key of a drive script asks the same rule).
	if (USwcUiSubsystem* Ui = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USwcUiSubsystem>() : nullptr; Ui != nullptr && !Ui->UiKeyToGame(TEXT("e")))
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: E while a screen of the UI holds the key: no use"));
		return;
	}
	if (USwcBackendSubsystem* Backend = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USwcBackendSubsystem>() : nullptr)
	{
		Backend->SetInteractKey(true);
	}
	// A use of an object ends autorun (the interaction kinds of the deactivation of AutoRun; stage P3.3).
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetSwcMovement()->CancelAutoRun(TEXT("a use of an object"));
	}
}

void ASwcPlayerController::OnInteractEnd(const FInputActionValue&)
{
	if (USwcBackendSubsystem* Backend = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USwcBackendSubsystem>() : nullptr)
	{
		Backend->SetInteractKey(false);
	}
}

void ASwcPlayerController::OnMoveKey(const TCHAR* Key)
{
	if (USwcUiSubsystem* Ui = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USwcUiSubsystem>() : nullptr; Ui != nullptr && !Ui->UiKeyToGame(Key))
	{
		UE_LOG(LogSwcBackend, Log, TEXT("move: %s while a screen of the UI holds the key: nothing"), Key);
		return;
	}
	const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn());
	if (Character == nullptr || Character->GetActions()->IsInputLocked())
	{
		return;
	}
	USwcCharacterMovement* Movement = Character->GetSwcMovement();
	if (FCString::Strcmp(Key, TEXT("equals")) == 0)
	{
		Movement->ToggleAutoRun();
	}
	else
	{
		Movement->TogglePosture(FCString::Strcmp(Key, TEXT("z")) == 0);
	}
}

void ASwcPlayerController::OnAbilityKey(int32 Place, bool bDown)
{
	// The keys of the game map "player": the pre-processor of the UI keeps a real key from the game while that map is off; an
	// injected key of a drive script asks the same rule. A release always goes (the key is up).
	static const TCHAR* const Keys[] = {TEXT("q"), TEXT("r"), TEXT("f")};
	if (bDown)
	{
		if (USwcUiSubsystem* Ui = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USwcUiSubsystem>() : nullptr; Ui != nullptr && !Ui->UiKeyToGame(Keys[Place]))
		{
			UE_LOG(LogSwcBackend, Log, TEXT("ability: %s while a screen of the UI holds the key: no press"), Keys[Place]);
			return;
		}
	}
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		Character->GetActions()->OnAbilityKey(static_cast<uint32>(Place), bDown);
	}
}

void ASwcPlayerController::PlayerTick(float DeltaTime)
{
	TickDrive(DeltaTime);
	Super::PlayerTick(DeltaTime);
	if (const ASwcPlayerCharacter* Character = Cast<ASwcPlayerCharacter>(GetPawn()))
	{
		const double Yaw = bFreeLook ? FreeLookRotation.Yaw : GetControlRotation().Yaw;
		// The death, a loading screen, and the respawn before its mark lock the movement keys (stage P2.10).
		Character->GetSwcMovement()->SetInput(Character->GetActions()->IsInputLocked() ? FVector2D::ZeroVector : MoveInput, Yaw, bWalkHeld);
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
	if (bDriveClick)
	{
		bDriveClick = false;
		Input->StopContinuousInputInjectionForAction(AttackAction);
	}
	for (int32 Place = 0; Place < 3; ++Place)
	{
		if (DriveAbilityUp[Place] >= 0.0 && Time >= DriveAbilityUp[Place])
		{
			DriveAbilityUp[Place] = -1.0;
			Input->StopContinuousInputInjectionForAction(AbilityActions[Place]);
		}
	}
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
		else if (Step.Command == TEXT("key"))
		{
			UInputAction* Action = Step.Text == TEXT("1") ? Weapon1Action.Get() : Step.Text == TEXT("2") ? Weapon2Action.Get() : SheatheAction.Get();
			Input->InjectInputForAction(Action, FInputActionValue(true), {}, {});
		}
		else if (Step.Command == TEXT("attack") || Step.Command == TEXT("click"))
		{
			if (Step.Command == TEXT("click") || Step.Value.X != 0.0)
			{
				Input->StartContinuousInputInjectionForAction(AttackAction, FInputActionValue(true), {}, {});
				bDriveClick = Step.Command == TEXT("click");
			}
			else
			{
				Input->StopContinuousInputInjectionForAction(AttackAction);
			}
		}
		else if (Step.Command == TEXT("aim"))
		{
			if (Step.Value.X != 0.0)
			{
				Input->StartContinuousInputInjectionForAction(AimAction, FInputActionValue(true), {}, {});
			}
			else
			{
				Input->StopContinuousInputInjectionForAction(AimAction);
			}
		}
		else if (Step.Command == TEXT("interact"))
		{
			if (Step.Value.X != 0.0)
			{
				Input->StartContinuousInputInjectionForAction(InteractAction, FInputActionValue(true), {}, {});
			}
			else
			{
				Input->StopContinuousInputInjectionForAction(InteractAction);
			}
		}
		else if (Step.Command == TEXT("respawn"))
		{
			Input->InjectInputForAction(GiveUpAction, FInputActionValue(true), {}, {});
		}
		else if (Step.Command == TEXT("dodge"))
		{
			Input->InjectInputForAction(DodgeAction, FInputActionValue(true), {}, {});
		}
		else if (Step.Command == TEXT("crouch") || Step.Command == TEXT("prone") || Step.Command == TEXT("autorun"))
		{
			UInputAction* Action = Step.Command == TEXT("crouch") ? CrouchAction.Get() : Step.Command == TEXT("prone") ? ProneAction.Get() : AutoRunAction.Get();
			Input->InjectInputForAction(Action, FInputActionValue(true), {}, {});
		}
		else if (Step.Command == TEXT("ability"))
		{
			// "TIME ability q|r|f [SECONDS]": a press of a key of the bar, up after SECONDS (default 0.1 s: a tap of 3 input steps).
			const int32 Place = Step.Text == TEXT("q") ? 0 : Step.Text == TEXT("r") ? 1 : Step.Text == TEXT("f") ? 2 : INDEX_NONE;
			if (Place != INDEX_NONE)
			{
				Input->StartContinuousInputInjectionForAction(AbilityActions[Place], FInputActionValue(true), {}, {});
				DriveAbilityUp[Place] = Time + (Step.Value.Y > 0.0 ? Step.Value.Y : 0.1);
			}
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
