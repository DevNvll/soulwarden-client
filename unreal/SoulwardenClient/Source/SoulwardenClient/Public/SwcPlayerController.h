#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SwcPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * The player controller: the keys of the movement and the mouse look (Enhanced Input, the default profile of the retail
 * client; research feel-values.md R6), and the pitch limits of the free camera (R1). The actions and the context are
 * made in code: no input asset.
 *
 * Command line: -SwcDrive=FILE plays a drive script through the same input actions (Enhanced Input injection; no input
 * tool of the host). Each line is "TIME COMMAND [VALUES]", the time in seconds from the possession of the character:
 *   move X Y      the move keys from now on (X right, Y forward; 0 0 releases them)
 *   walk 1|0      Left Ctrl down or up
 *   jump          one press of Space
 *   turn DEG_S    the mouse turns the camera at this yaw rate (degrees each second, positive to the right; 0 stops)
 *   pitch DEG_S   the same for the pitch (positive up)
 * The run ends with -SwcQuitWhen=world and -SwcStay (USwcBackendSubsystem).
 */
UCLASS()
class SOULWARDENCLIENT_API ASwcPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void PlayerTick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

private:
	struct FDriveStep
	{
		float Time = 0.0f;
		FString Command;
		FVector2D Value = FVector2D::ZeroVector;
	};

	void OnMove(const FInputActionValue& Value);
	void OnMoveEnd(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJump(const FInputActionValue& Value);
	void OnWalk(const FInputActionValue& Value);
	void OnWalkEnd(const FInputActionValue& Value);
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

	FVector2D MoveInput = FVector2D::ZeroVector;
	bool bWalkHeld = false;

	TArray<FDriveStep> Drive;
	int32 DriveNext = 0;
	double DriveStart = -1.0;
	FVector2D DriveTurn = FVector2D::ZeroVector;
};
