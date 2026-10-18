#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Character/SwcCharacterMovement.h"
#include "Net/Codec/SwcRepClient.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SwcBackendSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSwcBackend, Log, All);

class AActor;
class ASwcPlayerCharacter;
class FSwcLocomotion;
class FSwcLoginClient;
class FSwcPlayerLooks;
class FSwcRepConnection;
struct FSwcLoginToken;

/**
 * The connection to the Soulwarden backend. The client speaks the original wire protocol of the backend
 * (decision of 2026-10-18): the HTTP login path, then DTLS 1.2, the carrier, and the state bundles
 * (Soulwarden protocol notes, sections 2 to 4). docs/NETWORK.md has the client side of each contract.
 *
 * Command line (the run scripts give it; the editor does not connect):
 *   -SwcConnect                the login path starts with the game
 *   -SwcHttp=HOST:PORT         the HTTP listener of the backend (loopback only)
 *   -SwcPlay=first|ID|NAME     after the character list: the world entry with the first character, the character ID, or
 *                              (development runs) the character NAME
 *   -SwcQuitWhen=characters    the client stops after the character list (status 0), or after a failure (status 1)
 *   -SwcQuitWhen=world         the client stays -SwcStay seconds in the world (default 10), leaves, and stops (status 0)
 *   -SwcQuitAfter=SECONDS      the client stops after this time
 *   -SwcView=front             the camera stands in front of the character (pictures of the character)
 *   -SwcDrive=FILE             a drive script of the player controller (ASwcPlayerController)
 * Environment: SWC_STEAM_ID and SWC_PERSONA_NAME (the Steam stubs), SWC_RUN (player or development).
 */
UCLASS(Config = Game)
class SOULWARDENCLIENT_API USwcBackendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void LogEndpoints() const;

	/** Runs the HTTP login path to the character list. */
	void StartLogin();

	/** Enters the world with a character of the character list: the login token, then the game server. */
	void EnterWorld(const FString& CharacterId);

	/**
	 * The read path of the states of the own entity (the HUD of the UI track reads it): a copy of the decoded own entity
	 * (health, stamina, mana, the dead mark, the ability bars, the cooldowns, the paperdoll with its sheathe and active
	 * maps, the appearance, the position). docs/NETWORK.md has the slot, the field, and the unit of each value.
	 * @return True while the client is in the world (phase InGame).
	 */
	bool GetLocalPlayer(swc::net::RepClient::OwnEntity& Out) const;

	/** @return The server clock now in microseconds (the clock of the cooldown ends), or 0 before the registration reply. */
	uint64 GetServerTimeMicroseconds() const;

	/**
	 * @return The yaw of the camera of the player in degrees, in the convention of the input report (protocol notes 4.7):
	 *         0 = north (engine +Y), positive = to the left (west), -180 to 180.
	 */
	float GetCameraYawDegrees() const;

	/** HTTP host and port of the backend (loopback only). The command line can set them: -SwcHttp=HOST:PORT. */
	UPROPERTY(Config)
	FString HttpHost = TEXT("127.0.0.1");

	UPROPERTY(Config)
	int32 HttpPort = 32200;

private:
	void OnCharacterList(bool bSucceeded, const FString& Error);
	void StartConnection(const FSwcLoginToken& Token, const FString& CertificatePem);
	bool TickWorld(float DeltaTime);
	void ShowOwnEntity();
	void UpdateLook();
	/** Gives the movement its permissions and the connection the player of this frame (the input report). */
	void UpdatePlayer();
	void Quit(bool bSucceeded, const TCHAR* Cause);

	TSharedPtr<FSwcLoginClient> Login;
	TSharedPtr<FSwcRepConnection> Connection;
	/** A player run shows no character name of the user in the log (the rule of AGENTS.md). */
	bool bPlayerRun = true;
	FString QuitWhen;
	FString PlayCharacter;
	float StaySeconds = 10.0f;
	double InGameSince = 0.0;
	bool bInGame = false;
	bool bQuitting = false;
	FTSTicker::FDelegateHandle QuitTimer;
	FTSTicker::FDelegateHandle WorldTicker;
	TWeakObjectPtr<ASwcPlayerCharacter> Avatar;
	TWeakObjectPtr<AActor> Camera;
	TSharedPtr<FSwcPlayerLooks> Looks;
	/** The locomotion data of each gender (index 0 male, 1 female), read at the first use. */
	TSharedPtr<FSwcLocomotion> Locomotions[2];
	/** The level has no ground at the spawn: the test floor of the movement (Character/SwcTestFloor). */
	bool bTestFloor = false;
	bool bWaitForFloor = false;
	/** The input reports have the position of the character (docs/MOVEMENT.md): not in a player run on the test floor. */
	bool bSendPosition = false;
	/** The movement state of the last log line. */
	ESwcMove LastMove = ESwcMove::Idle;
	uint32 LookRevision = MAX_uint32;
	FString View;
};
