#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Character/SwcCharacterMovement.h"
#include "Net/Codec/SwcRepClient.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tasks/Task.h"

#include <memory>

#include "SwcBackendSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSwcBackend, Log, All);

namespace swc::combat
{
class Driver;
}

class AActor;
class ASwcPlayerCharacter;
class FSwcActions;
class FSwcLocomotion;
class FSwcLoginClient;
class FSwcInteractor;
struct FSwcInteractPrompt;
class FSwcPlayerLooks;
class FSwcRepConnection;
struct FStreamableHandle;
struct FSwcLoginToken;
struct FSwcPlayerTables;

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
	 * The read path of the other entities (the entity table of the creatures, Creatures/SwcCreatureSubsystem): the records
	 * and the stops of the interest numbers that are not 1 since the last call, in the order of the bundles.
	 * @return False when the client has no connection (the table then clears itself).
	 */
	bool TakeOtherUpdates(std::vector<swc::net::RepClient::OtherUpdate>& Out) const;
	/**
	 * The give-up of the death screen (stage P2.10): the respawn request of the retail client (1938, "Random Location"), only for a
	 * dead character after the time of its death notice and with no loading screen. Until the death screen of the UI exists, the
	 * development key F8 and the drive command "respawn" call it. @return False when no request went out.
	 */
	bool RequestRespawn();
	/** The respawn request of the original respawn screen (LocalPlayerUIRequestsBus.RequestRespawn(random, homeId) of respawn.luac, a
	 *  host request of the UI): random true for the row "Random Location" (an empty home id). The checks of RequestRespawn(). */
	bool RequestRespawn(bool bRandom, const FString& HomeId);
	/** The requests of the inventory screens (stage P2.4, host requests of the UI; protocol notes 4.10, 4.11, 4.23, 4.42): equip,
	 *  unequip (bag slot -1: the first free slot), swap two paperdoll slots, use a bag item, drop bag items or a paperdoll item,
	 *  and take the items of the open bag. The answer is the new states (GetLocalPlayer), or message 3626 for a refused request
	 *  of the equipment (OwnEntity::equipFails). @return False when no request went out (no connection, not in the world). */
	bool EquipItem(int32 BagSlot, uint32 PaperdollSlot, uint32 Quantity, const swc::net::ItemInstanceId& Instance);
	bool UnequipItem(uint32 PaperdollSlot, int32 BagSlot, uint32 Quantity);
	bool SwapItems(uint32 SlotTo, uint32 SlotFrom, const swc::net::ItemInstanceId& InstanceTo, const swc::net::ItemInstanceId& InstanceFrom);
	bool UseItem(uint32 BagSlot);
	bool DropItems(const std::vector<swc::net::DropItemEntry>& Entries);
	bool DropPaperdollItem(uint32 PaperdollSlot, uint32 Quantity);
	bool TakeAllItems();
	bool TakeItem(const swc::net::ItemInstanceId& Instance, uint32 Quantity);
	/** @return The error of the login path when it failed (a refused connection), else empty: the UI shows its error popup. */
	const FString& GetLoginError() const { return LoginError; }
	/** @return True while the cells of a new place load after a travel or a respawn (the UI keeps its loading screen). */
	bool IsWaitingForCells() const { return bTravelWait; }
	/** The key E of the interactions (stage P2.10): down or up. */
	void SetInteractKey(bool bDown) { bInteractKey = bDown; }
	/** The bag panel of the UI (stage P2.4): the open of a loot bag of the creature view (its interest) with OpenStorageOption, the
	 *  close of the panel (2473), and the bag of the panel use (0: none). FSwcInteractor does the use. */
	void OpenLootBag(uint32 Interest);
	void CloseLootBag();
	uint32 GetOpenLootBag() const;
	/** The prompt of the focus for the UI (stage WF7; FSwcInteractor::GetPrompt). @return False with no focus. */
	bool GetInteractPrompt(FSwcInteractPrompt& Out) const;
	/** The line of the conversation of the player ("SPEAKER: TEXT"; stage P4.8, docs/PERSONS.md), empty with no conversation: the
	 *  conversation screen of the UI comes later. */
	const FString& GetConversationLine() const { return ConversationLine; }
	/** The conversation screen of the UI ends the conversation (its Back key; its Next key at the last line): 6705, then 2473. */
	void EndConversation();

	/**
	 * @return The yaw of the camera of the player in degrees, in the convention of the input report (protocol notes 4.7):
	 *         0 = north (engine +Y), positive = to the left (west), -180 to 180.
	 */
	float GetCameraYawDegrees() const;

	/** @return The camera state of the player (gamecamera.xml), or an empty text. */
	FString GetCameraStateName() const;

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
	/** Starts the read of the player tables on a worker thread (once). */
	void StartTables();
	/** Takes the player tables of the worker (it waits for the worker when it has not finished). @return False with no looks. */
	bool TakeTables();
	/**
	 * Before the spawn: when the tables and the appearance of the own entity are there, the assets of the look (the meshes,
	 * the clips of the movement and of the actions) start to load in the background, while the cells of the place load.
	 * @return True when the spawn can go on (the assets are loaded, there is nothing to load, or the time limit came).
	 */
	bool UpdatePreload();
	/** The time of the async loader from the in-game phase to the spawn (on), and the time of the engine again (off). */
	void SetSpawnLoading(bool bOn);
	/** Gives the movement its permissions and the connection the player of this frame (the input report). */
	void UpdatePlayer();
	/** A travel of the server (a movement state with a new teleport number): the character goes to its place, then waits for the
	 *  cells of that place (stage P2.10, protocol notes 4.16). */
	void Travel(const swc::net::RepClient::OwnEntity& Own);
	void UpdateTravel();
	/**
	 * The keys of the bar (stage P3.5, docs/ACTIONS.md): the bar of each weapon slot (the bar of the Ability state whose abilities
	 * are abilities of the graph of the weapon), the learned nodes (slot 67), and the cooldowns of the server (slot 45: the end of
	 * the own cooldown of the client) to the driver. `ServerOffsetNs`: server ns = steady ns + offset (no clock: no cooldown).
	 */
	void UpdateAbilities(const swc::net::RepClient::OwnEntity& Own, bool bServerClock, int64 ServerOffsetNs);
	/**
	 * The limits of the movement (stage P3.3, docs/MOVEMENT.md "The limits"): the movement columns of the statuses of the status map
	 * of the owner (slot 44), the speed table (slot 43, key 0x35), the bag weight (the bag of slot 61 with the item weights of the
	 * catalog), and the equip load class, for the movement and for the actions of the driver.
	 */
	void UpdateMovementLimits(const swc::net::RepClient::OwnEntity& Own);
	void Quit(bool bSucceeded, const TCHAR* Cause);

	TSharedPtr<FSwcLoginClient> Login;
	FString LoginError;
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
	/**
	 * The player tables (the looks with the item catalog, the locomotion of each gender, the actions of the weapons) are read
	 * on a worker thread from the character list on: the spawn frame does not read them (docs/CHARACTER.md, "The spawn").
	 */
	UE::Tasks::TTask<TSharedPtr<FSwcPlayerTables>> TablesTask;
	bool bTablesTaken = false;
	TSharedPtr<FSwcPlayerLooks> Looks;
	/** The locomotion data of each gender (index 0 male, 1 female). */
	TSharedPtr<FSwcLocomotion> Locomotions[2];
	/** The actions of the weapons (actions.json) and the attack driver of the session (stage P2.7). */
	TSharedPtr<FSwcActions> Actions;
	/** The interactions of the player with the objects of the server (stage P2.10) and the key E. */
	TSharedPtr<FSwcInteractor> Interactor;
	bool bInteractKey = false;
	FString ConversationLine;
	bool bDummyShown = false;
	/** The assets of the look of the character, loaded in the background before the spawn (the handle keeps them). */
	TSharedPtr<FStreamableHandle> Preload;
	enum class EPreload : uint8 { Waiting, Loading, Done };
	EPreload PreloadState = EPreload::Waiting;
	double PreloadSince = 0.0;
	/** The async loader has more time in each frame from the in-game phase to the spawn; its values of the engine before. */
	bool bSpawnLoading = false;
	float SavedLoadingTimeLimit = 5.0f;
	int32 SavedLoadingFullTime = 0;
	/** The cells of the place are loaded (SwcWorldStreaming::IsReadyAt returned true). */
	bool bCellsReady = false;
	/** The frames after the spawn: the longest frame of the first seconds (the log of the spawn hitch). */
	float LongestFrame = 0.0f;
	bool bFramesLogged = false;
	std::shared_ptr<swc::combat::Driver> AttackDriver;
	bool bStaminaSet = false;
	uint32 StaminaRevision = 0;
	/** The bars, the revision of the learned nodes, and the cooldown entries (ability, end in server us) that the driver has. */
	std::array<std::array<uint32, 3>, 2> SentBars{};
	uint32 NodesRevision = 0;
	std::vector<std::pair<std::uint32_t, std::uint64_t>> SentCooldowns;
	/** The movement limits (stage P3.3): the bag weight and its revision, the equip load class, the log of the last limits. */
	uint32 BagRevision = MAX_uint32;
	int32 BagWeight = 0;
	uint8 EquipLoadClass = 0;
	FString LastLimits;
	/** The mana revisions of the read path that went to the driver, and the health of the last frame (stage P3.6). */
	uint32 ManaRevision = MAX_uint32;
	uint32 ManaSyncRevision = 0;
	float LastHealth = 0.0f;
	/** The level has no ground at the spawn: the test floor of the movement (Character/SwcTestFloor). */
	bool bTestFloor = false;
	bool bWaitForFloor = false;
	/** The input reports have the position of the character (docs/MOVEMENT.md): not in a player run on the test floor. */
	bool bSendPosition = false;
	/** The movement state of the last log line. */
	ESwcMove LastMove = ESwcMove::Idle;
	/** The game time of the next speed sample of a development run (stage P3.3). */
	double NextMoveSample = 0.0;
	uint32 LookRevision = MAX_uint32;
	/** The travel revision of the own entity that the character took, and the wait for the cells of a new place. */
	uint32 SeenTravel = 0;
	bool bTravelWait = false;
	double TravelSince = 0.0;
	FVector TravelFeet = FVector::ZeroVector;
	FVector TravelForward = FVector::ForwardVector;
	FString View;
};
