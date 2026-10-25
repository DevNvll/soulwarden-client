#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SwcPlayerController.generated.h"

class ASwcCameraManager;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * The player controller: the keys of the movement and of the camera (Enhanced Input, the default profile of the retail
 * client; research feel-values.md R6), the mouse look with the sensitivity of the camera state (R1), the mouse wheel (the
 * zoom, or the next target of the target lock), the target lock (middle mouse, toggle), free look (Left Alt), and the
 * weapon keys (1, 2) and the sheathe key (~): the client draws and sheathes itself, as the retail client (docs/GEAR.md), and
 * the attack button (left mouse: attack_primary; its press, hold, and release go into the input report and to the attack
 * graph, docs/ACTIONS.md). The actions and the context are made in code: no input asset. The camera manager is
 * ASwcCameraManager.
 *
 * Command line: -SwcDrive=FILE plays a drive script through the same input actions (Enhanced Input injection; no input
 * tool of the host). Each line is "TIME COMMAND [VALUES]", the time in seconds from the possession of the character:
 *   move X Y         the move keys from now on (X right, Y forward; 0 0 releases them)
 *   walk 1|0         Left Ctrl down or up
 *   jump             one press of Space
 *   turn DEG_S       the mouse turns the camera at this yaw rate (degrees each second, positive to the right; 0 stops)
 *   pitch DEG_S      the same for the pitch (positive up)
 *   wheel N          one wheel event of N notches (positive: up, the camera nearer)
 *   lock             one press of the target lock key
 *   flick COUNTS     a fast mouse move on X in one frame
 *   look DX DY       a mouse move of the retail harness in one frame (counts; DY positive down)
 *   view HEADING PITCH   the camera of a retail sample (degrees: the heading clockwise from north, the pitch positive up)
 *   freelook 1|0     Left Alt down or up
 *   camera STATE     a camera state of gamecamera.xml in place of the state of the action list (none: the action list)
 *   key 1|2|sheathe  one press of the weapon key 1 or 2, or of the sheathe key
 *   attack 1|0       the attack button down or up
 *   click            the attack button down for one frame
 *   dodge            one press of the dodge key (left Shift)
 *   aim 1|0          the aim button (right mouse) down or up
 *   respawn          the development key F8: the give-up of the death screen (USwcBackendSubsystem::RequestRespawn)
 *   interact 1|0     the key E down or up (the prompt of an object, FSwcInteractor)
 * The run ends with -SwcQuitWhen=world and -SwcStay (USwcBackendSubsystem).
 */
UCLASS()
class SOULWARDENCLIENT_API ASwcPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASwcPlayerController();

	/** The aim of the character: the control rotation, or during free look the rotation of its start (the character does not
	 *  follow the camera then). The bend of the upper body to the aim reads it (usertest-1024 item 4). */
	FRotator GetAimRotation() const { return bFreeLook ? FreeLookRotation : GetControlRotation(); }

	/**
	 * A virtual input of a UI script with action bits for the input report (stage P3.H, K3): the quick slot uses of
	 * consumablequickslots.luac (SendVirtualInput Use_Quickslot_1 to 4: the bits 404, 117, 140, 513; protocol notes, the
	 * consumables). The press replaces the bits of the press before it, as a weapon key does: the motion carries one press
	 * (PressSerial and PressBits) for each input entry of the session, thus of two presses in one input step (1/30 s) only the
	 * last one reaches the server (a weapon key and a quick slot key at the same time lose the first one).
	 */
	void PressVirtualBits(const TArray<uint32>& Bits);

	virtual void PlayerTick(float DeltaTime) override;

	ASwcCameraManager* GetSwcCamera() const;

	/** The last key press with action bits for the input report: its serial and its bits. */
	uint32 GetPressSerial() const { return PressSerial; }
	const TArray<uint32>& GetPressBits() const { return PressBits; }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	/** Maps the keys of the play actions from the key table of the client (UI/SwcKeyBindings), again at each rebind. */
	void MapPlayKeys();
	virtual void OnPossess(APawn* InPawn) override;

private:
	struct FDriveStep
	{
		float Time = 0.0f;
		FString Command;
		FString Text;
		FVector2D Value = FVector2D::ZeroVector;
		/** The events of a "raw" step (AST-268). */
		int32 Events = 1;
	};

	void OnMove(const FInputActionValue& Value);
	void OnMoveEnd(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJump(const FInputActionValue& Value);
	void OnWalk(const FInputActionValue& Value);
	void OnWalkEnd(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void OnLock(const FInputActionValue& Value);
	void OnFreeLook(const FInputActionValue& Value);
	void OnFreeLookEnd(const FInputActionValue& Value);
	void OnWeapon1(const FInputActionValue& Value);
	void OnWeapon2(const FInputActionValue& Value);
	void OnSheathe(const FInputActionValue& Value);
	void OnAttack(const FInputActionValue& Value);
	void OnAttackEnd(const FInputActionValue& Value);
	void OnDodge(const FInputActionValue& Value);
	void OnAim(const FInputActionValue& Value);
	void OnAimEnd(const FInputActionValue& Value);
	void OnGiveUp(const FInputActionValue& Value);
	void OnInteract(const FInputActionValue& Value);
	void OnInteractEnd(const FInputActionValue& Value);
	void OnInteractSecond(const FInputActionValue& Value);
	void OnInteractSecondEnd(const FInputActionValue& Value);
	/** The keys of the bar (Q, R, F: the places 0 to 2; stage P3.5). */
	void OnAbilityKey(int32 Place, bool bDown);
	/** The keys of the movement of stage P3.3: C crouch, Z prone, `=` autorun (a key name of the UI rule). */
	void OnMoveKey(const TCHAR* Key);
	void UseWeapon(uint32 Slot, const TArray<uint32>& Bits);
	void LoadDrive(const FString& Path);
	void TickDrive(float DeltaTime);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MovementContext;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> WalkAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ZoomAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LockAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FreeLookAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Weapon1Action;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Weapon2Action;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SheatheAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AttackAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DodgeAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AimAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> GiveUpAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractSecondAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AbilityActions[3];
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ProneAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AutoRunAction;
	/** The release time of a key of the bar that a drive script pressed (its drive time; below 0: up). */
	double DriveAbilityUp[3] = {-1.0, -1.0, -1.0};
	/** A click of the drive script: the button goes up at the next frame. */
	bool bDriveClick = false;
	/** The look sensitivity options of the options screen (AST-255): H, V and the ADS factors, read again each 0.5 s. */
	FVector2D LookOption = FVector2D(0.05, 0.05);
	FVector2D LookAdsOption = FVector2D(1.0, 1.0);
	double LookOptionsReadAt = -1.0;
	/** The mouse log of swc.MouseLog (AST-268): the counts and the turn of the look action since the last line. */
	FVector2D MouseLogCounts = FVector2D::ZeroVector;
	FVector2D MouseLogTurn = FVector2D::ZeroVector;
	FVector2D MouseLogFrameCounts = FVector2D::ZeroVector;
	int32 MouseLogEvents = 0;
	int32 MouseLogFrames = 0;
	double MouseLogAt = -1.0;
	void TickMouseLog();
	uint32 PressSerial = 0;
	TArray<uint32> PressBits;

	FVector2D MoveInput = FVector2D::ZeroVector;
	bool bWalkHeld = false;
	bool bFreeLook = false;
	FRotator FreeLookRotation = FRotator::ZeroRotator;

	TArray<FDriveStep> Drive;
	int32 DriveNext = 0;
	double DriveStart = -1.0;
	FVector2D DriveTurn = FVector2D::ZeroVector;
	/** The raw mouse turn of the drive (counts each second, AST-268) and its fraction that is not sent yet. */
	double DriveRawTurn = 0.0;
	double DriveRawLeft = 0.0;
};
