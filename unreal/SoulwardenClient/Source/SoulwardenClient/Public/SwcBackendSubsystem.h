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

/** The end of a world session on a return to the menu that keeps the world (the subsystems clear the actors of the session). */
DECLARE_MULTICAST_DELEGATE(FSwcWorldSessionEnd);

namespace swc::combat
{
class Driver;
}

class AActor;
class ASwcPlayerCharacter;
class FSwcActions;
class FSwcLocomotion;
class FSwcLoginClient;
struct FSwcAccountSession;
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
	/** The front end (stage F2 of docs/FRONTEND.md): true after a character list with no -SwcPlay; the landing screen of the UI
	 *  then shows the list, and its Play calls EnterWorld. */
	bool IsFrontEnd() const { return bFrontEnd; }
	/** @return The account of the login path (the worlds, the characters, the regions), or nullptr before the character list. */
	const FSwcAccountSession* GetAccountSession() const;
	/** The refresh of the landing screen: GET getlogininfo again (FSwcLoginClient::RefreshLoginInfo). */
	void RefreshCharacterList(TFunction<void(bool bSucceeded)> Done);
	/** Deletes a character of the account (stage F8: the landing screen). Done gets true when the backend deleted it. */
	void DeleteCharacter(const FString& CharacterId, TFunction<void(bool bDeleted)> Done);
	/** @return True from EnterWorld to the reply of the login queue (the login token). */
	bool IsEnteringWorld() const { return bEnteringWorld; }
	/**
	 * Leaves the world and goes back to the character selection (stage F6 of docs/FRONTEND.md; RequestDisconnect with the
	 * destination MainMenu): the carrier disconnect, then the level again with the front end. In the front end, a world
	 * connection that ends with no leave goes back too, and its cause is the entry error of the landing screen.
	 */
	void ReturnToMenu();
	/** @return The count of the returns to the menu (the UI starts its screens again after each one). */
	int32 GetMenuReturns() const { return MenuReturns; }
	/** A return to the menu that keeps the world (the default; swc.Menu.ReturnReload 1 or -SwcReturnReload: the level again):
	 *  the subsystems with actors of the session clear them, the UI starts its screens again. Broadcast after the count of
	 *  the return goes up. */
	FSwcWorldSessionEnd OnWorldSessionEnd;
	/** @return True while the client has a connection to the game server (after the login token). */
	bool HasConnection() const { return Connection.IsValid(); }
	/** @return The error of the last EnterWorld of the front end (no login token), else empty. */
	const FString& GetEnterError() const { return EnterError; }
	/** @return The look tables of the player (the menu preview of stage F3), or null while their worker task runs (no wait). */
	const FSwcPlayerLooks* GetPlayerLooks();
	/** @return True while the cells of a new place load after a travel or a respawn (the UI keeps its loading screen). */
	bool IsWaitingForCells() const { return bTravelWait; }
	/**
	 * The travel of the world map (stage MVP-C, Linear AST-213; Private/SwcBackendTravel.cpp): the admin command "travel" of the
	 * backend (the routes /admin/v1 with the token of the session, SWC_ADMIN_TOKEN) to the backend place nearest to a landmark
	 * of the map (game meters; a shrine of the map is a place of the backend place list). The backend then moves the character
	 * (the movement state with a new teleport number, messages 3951 and 3954): the usual travel path and its loading gate.
	 * @return False when no request went out (no token, not in the world, dead, a travel that runs, no place within 5 m).
	 */
	bool RequestTravel(const FVector& LandmarkMeters);
	/** The key E of the interactions (stage P2.10): down or up. */
	void SetInteractKey(bool bDown) { bInteractKey = bDown; }
	/** The key T: the second option of the card of an object (stage MVP-F). */
	void SetInteractSecondKey(bool bDown) { bInteractSecondKey = bDown; }
	/** The move keys of the player (any one down; the raw keys, also while the movement is locked): a new one ends a gather. */
	void SetMoveKeys(bool bDown) { bMoveKeys = bDown; }
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
	/** The conversation screen of the UI ends the conversation (its Back key; its Next key at the last line): 6705, then 2473, then the
	 *  finished topics (5397) when the list has one (AST-248). */
	void EndConversation(const std::vector<uint32>& FinishedTopics = {});
	/** The conversation screen sends the topic of a selection (3158; AST-248). False with no world connection. */
	bool ConversationTopic(uint32 Topic);
	/** The InteractTags of the Active quest tasks of the character (AST-241; UI/SwcUiQuests from swc/quests.lua): the focus of the quest
	 *  objects. */
	void SetQuestTags(const TSet<FString>& Tags);
	/** @return The Talk key of the open conversation scene (an npc object key, or "person:" + a key of persons.json), empty with no
	 *  scene (the conversation screen of the UI: UI/SwcUiConversation). */
	FString GetConversationSceneKey() const;
	/** @return True when the call goes to the client of the session (in the world): the edits of the trees and the bars of the
	 *  character window (UI/SwcUiSkills). */
	bool QueueRepCall(std::function<void(swc::net::RepClient&)> Call);
	/** The conversation screen shows or hides (UI/SwcUiConversation): while it shows, E is its Next key, not the end. */
	void SetConversationScreenShown(bool bShown);
	/** The crafting station of the use whose crafting screen shows (stage MVP-J, docs/CRAFTING.md): its Assembly key ("Blacksmith5"),
	 *  the fee flag, and its name key. @return False with no open station. */
	bool GetCraftingStation(FString& OutKey, bool& bOutFee, FString& OutName) const;
	/** The crafting screen ends the station use (its Escape, its exit): 2473. */
	void EndCraftingStation();
	/** The craft request of the crafting screen (2997; protocol notes 4.50): the recipe CRC, the count, the claims of the fee and of the
	 *  gear score, the perk items, the upgrade level, and the CRC of the selected item of each ingredient (0: a currency). @return False
	 *  with no connection or no open station. */
	/** The quest calls of the UI (AST-236): 2709, 1458, 2617, 3836, 5600. False with no world connection. */
	bool EnableObjectives(const std::vector<bool>& Flags);
	bool UpdateTrackedObjectives(const std::vector<uint64>& Instances);
	bool NotifyOpenUiScreen(uint32 Screen);
	bool AbandonObjective(uint64 Instance);
	bool ResetObjective(uint64 Instance);
	bool RequestCraft(uint32 Recipe, uint32 Quantity, uint32 Fee, uint16 GearScore, const TArray<uint32>& PerkItems, uint8 Upgrade,
		const TArray<uint32>& Items);

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
	/** The travel of the world map (SwcBackendTravel.cpp): the token of the admin routes, the character of the world, the places. */
	struct FTravelPlace
	{
		FString Id;
		FVector Position = FVector::ZeroVector;
	};
	void ReadAdminToken();
	void SendTravel(const FVector& LandmarkMeters);
	FString AdminToken;
	FString WorldCharacterId;
	TArray<FTravelPlace> TravelPlaces;
	bool bTravelRequest = false;

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
	/** Sets the state of the world session back to the start (each member below that one world entry changes), so that the
	 *  next entry starts as the first one. A new member of the world session goes into this function too. */
	void ResetWorldSession();
	/** The return to the menu: keeps the world (the pawn and the view actor go, OnWorldSessionEnd), or with the fallback loads the
	 *  level of the world again (its actors and its world subsystems start new). */
	void OpenMenuLevel();
	/** @return True when the return to the menu loads the level again (swc.Menu.ReturnReload 1 or -SwcReturnReload). */
	bool ReturnReloads() const;
	/** The return that keeps the world: the pawn of the player and the view actor of the session go. */
	void EndWorldActors();
	/** A load at the place failed after its retry (SwcWorldStreaming::HasFailed): back to the menu with the reason. */
	void FailLoad(const FString& Reason);
	bool bReturning = false;
	bool bLoadFailed = false;
	FString LoadError;   // the entry error of a failed load (the landing screen shows it after the return)
	int32 MenuReturns = 0;

	TSharedPtr<FSwcLoginClient> Login;
	FString LoginError;
	TSharedPtr<FSwcRepConnection> Connection;
	/** A player run shows no character name of the user in the log (the rule of AGENTS.md). */
	bool bPlayerRun = true;
	FString QuitWhen;
	FString PlayCharacter;
	/** A character list with no -SwcPlay: the front end of the UI (IsFrontEnd). */
	bool bFrontEnd = false;
	/** EnterWorld: the login queue request runs; the error of a failed request of the front end. */
	bool bEnteringWorld = false;
	FString EnterError;
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
	bool bInteractSecondKey = false;
	bool bMoveKeys = false;
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
	uint32 HitRevision = 0;
	/** Stage MVP-E: the ability rows of each perk of the wire list (perk-data.json of the backend copy: the perk with index K + 1 is
	 *  row K), as progression ids; the key of the perk facts that the driver has (the paperdoll revision and the active map). */
	TArray<TArray<uint32>> PerkAbilities;
	bool bPerkAbilitiesLoaded = false;
	/** AST-230: the status map of the owner that the status effects of the body show. */
	uint32 StatusFxRevision = MAX_uint32;
	uint64 SentPerkFactsKey = ~0ull;
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
	/** The last camera sample of -SwcMoveSamples (the pitch limits of stage P3.G): its rotation. */
	FRotator LastCameraSample = FRotator::ZeroRotator;
	uint32 LookRevision = MAX_uint32;
	/** The travel revision of the own entity that the character took, and the wait for the cells of a new place. */
	uint32 SeenTravel = 0;
	bool bTravelWait = false;
	double TravelSince = 0.0;
	FVector TravelFeet = FVector::ZeroVector;
	FVector TravelForward = FVector::ForwardVector;
	FString View;
};
