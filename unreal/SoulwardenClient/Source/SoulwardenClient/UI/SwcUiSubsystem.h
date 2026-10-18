#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "SwcUi/Draw.h"
#include "SwcUi/Runtime.h"
#include "Net/Codec/SwcRepClient.h"

#include "SwcUiSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSwcUi, Log, All);

class FSwcUiResources;
class SSwcUiCanvas;

/**
 * The UI of the client (docs/UI.md): the runtime of the module SwcUi runs the original Lua scripts of the game on the
 * converted canvases (SWC_CONTENT/ui), with the bus emulation of unreal/SoulwardenClient/Lua. This subsystem starts it
 * with the game, gives it the world entry and the decoded states of the own entity (USwcBackendSubsystem::
 * GetLocalPlayer), ticks it, and draws its draw list with a Slate widget over the game viewport.
 *
 * Input (docs/UI.md): FSwcUiInputProcessor gives the keys and, while the UI has the cursor, the mouse to the runtime.
 * The cursor shows while a screen needs the pointer (Swc.HostCursor); then the game gets no mouse event.
 *
 * Command line:
 *   -SwcUi=off           no UI
 *   -SwcUiScreens=all    each converted screen loads (default: the screens of the first HUD and of stage P2.2)
 *   -SwcUiDrive=FILE     the harness input: a script of UI input events in the process (no input tool of the host).
 *                        Each line is "TIME COMMAND [VALUES]", the time in seconds from the spawn of the player:
 *                          key NAME            one press and release of a key (a CryInput name: escape, w, f10)
 *                          keydown NAME / keyup NAME
 *                          point ELEMENT [FX FY]   the pointer to an element of a shown canvas (fractions of its
 *                                              rect, default the middle), by its name
 *                          move FX FY          the pointer to fractions of the viewport
 *                          click / press / release   the left button at the pointer
 *                          drag ELEMENT FX0 FY0 FX1 FY1 SECONDS   press at one point of the element, move, release
 *                          wheel N             one wheel event of N notches
 *                          waitfor ELEMENT [SECONDS]   the clock stays at this step until a shown element has
 *                                              the name (default limit 30 s)
 *                          log TEXT            a line in the client log (a mark for a video)
 *                          end                 releases each held key and button ("the test input is released")
 *                        The harness draws a marker at its pointer.
 * The reports (the stub log, the error log, the emulated surface, the missing textures) go to SWC_WORK/logs/ui at the
 * stop of the client; the missing textures also to SWC_WORK/ui/wanted.json (tools/convert-ui.sh wanted).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcUiSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// The input of the UI (FSwcUiInputProcessor and the harness input).
	/** A key (a CryInput name) to the runtime. @return True when the UI used it (no game event). */
	bool UiKey(const FString& Name, bool bDown);
	/** @return False when the game must not get a key: each game map that binds it is off, or a text input edits. */
	bool UiKeyToGame(const FString& Name);
	/** @return True while the UI has the cursor (the pointer events go to the UI only). */
	bool UiCursor() const { return bCursorWanted; }
	/** The pointer at a position of the screen (Slate screen space). */
	void UiPointer(FVector2D ScreenPosition);
	bool UiButton(const TCHAR* Button, bool bDown);
	bool UiWheel(float Delta);

private:
	bool Tick(float DeltaTime);
	void Start();
	void AttachWidget();
	void FeedLocalPlayer();
	void FlushLog();
	void WriteReports();
	void WriteDrawList();
	void LoadItemCatalog();
	void UpdateCursor();
	bool CallInput(const std::vector<swc::ui::Runtime::Arg>& Args);
	void UiPointerLocal(FVector2f Local);
	void LoadDrive(const FString& Path);
	void TickDrive();
	void ReleaseDriveInput();
	/** A key of the harness input as a key of the keyboard (Slate): the UI first, then the game when the UI does not keep it. */
	void DriveKey(const FString& Name, bool bDown);
	bool DrivePointAt(const FString& Element, float FractionX, float FractionY);
	/** The loading screen of the start, the error UI of the login path, and the requests of the scripts (a respawn). */
	void UpdateLoadingAndRequests();
	/** The bag, the items of the paperdoll, the level, and the coin of the read path (stage P2.4; docs/UI.md). */
	void FeedItems(const swc::net::RepClient::OwnEntity& Own);
	/** The loot bags near the character, the items of the open bag (the selected Container, slot 76), and the end of the use of
	 *  the bag panel (stage P2.4; docs/UI.md). */
	void FeedLoot(const swc::net::RepClient::OwnEntity& Own, class USwcBackendSubsystem* Backend);
	/** One item to the runtime (HostBagSlot or HostPaperdollItem). @return The key of the values (a change test). */
	FString CallItem(const char* Function, uint32 Slot, const soulwarden::backend::tests::WireItem* Item);
	/** The UI in the world views (stage WF7; docs/UI.md): the view projection of the camera, the plates of the creature view
	 *  (USwcCreatureSubsystem::GetPlates), and the occlusion tests of the markers; the simple stand-ins of the other tracks go off. */
	void FeedWorld(const swc::net::RepClient::OwnEntity& Own);
	/** The damage numbers (the creature view, message 3601) and the heal numbers of the own entity for the damage number canvas. */
	void FeedNumbers(const swc::net::RepClient::OwnEntity& Own, class USwcCreatureSubsystem* Creatures, const FVector& Eye, const FRotator& View);
	/** A request of the inventory scripts ("equip BAG SLOT QUANTITY INSTANCE", "use SLOT", ...) to the backend subsystem. */
	void HandleItemRequest(const FString& Request, class USwcBackendSubsystem* Backend);

	TUniquePtr<swc::ui::Runtime> Runtime;
	TSharedPtr<FSwcUiResources> Resources;
	TSharedPtr<SSwcUiCanvas> Widget;
	swc::ui::DrawList DrawList;
	FTSTicker::FDelegateHandle Ticker;
	TSharedPtr<class FSwcUiInputProcessor> InputProcessor;
	bool bStartLoading = false;
	bool bErrorShown = false;
	bool bSentLoadingScreen = false;
	bool bLoadingForDeath = false;
	double LoadingHoldUntil = 0.0;
	uint32 SentDeathNotices = 0;
	bool bCursorWanted = false;
	bool bCursorApplied = false;

	struct FDriveStep
	{
		double Time = 0.0;
		FString Command;
		TArray<FString> Args;
	};
	TArray<FDriveStep> Drive;
	int32 DriveNext = 0;
	double DriveStart = -1.0;
	double DriveWaitStart = -1.0;
	TSet<FString> DriveKeysDown;
	TArray<FString> DriveKeysUp;   // the key releases of the next frame (a "key" press lasts one frame)
	bool bDriveButtonDown = false;
	bool bDriveReleaseNext = false;
	struct FDriveDrag
	{
		bool bActive = false;
		FString Element;
		FVector2f From = FVector2f::ZeroVector;
		FVector2f To = FVector2f::ZeroVector;
		double Start = 0.0;
		double Duration = 0.0;
	} DriveDrag;
	FString ReportDir;
	bool bWorldEntered = false;
	bool bWidgetAttached = false;
	bool bLogStarted = false;
	double LastStatusTime = 0.0;

	// The values that the runtime has (a change goes to the runtime).
	TArray<FString> ItemIds;
	TArray<FString> PerkIds;
	TArray<FString> SentBag;
	TMap<uint32, FString> SentPaperdoll;
	uint32 SentBagRevision = ~0u;
	uint32 SentPaperdollRevision = ~0u;
	uint32 SentEquipFails = 0;
	int64 SentCoin = -1;
	int64 SentLevel = -1;
	/** The bag panel: the items of the open bag, the bags near the character ("INTEREST:DISTANCESQ ..."), the bag of the panel use,
	 *  and the open that the scripts asked for (its bag and its time: no use after 1 s closes the panel). */
	TArray<FString> SentLoot;
	uint32 SentLootRevision = ~0u;
	FString SentNearBags;
	double NextBagsTime = 0.0;
	uint32 SentOpenBag = 0;
	uint32 AskedOpenBag = 0;
	double AskedOpenTime = 0.0;
	int32 SentActiveSlot = -2;
	bool bSentSheathed = false;
	TMap<uint32, TArray<uint32>> SentBars;
	TMap<uint32, uint64> SentCooldowns;
	float SentYaw = 1000.0f;
	/** The world views: the time of the next occlusion test of the markers, and the switches of the stand-ins. */
	double NextOcclusionTime = 0.0;
	bool bStandInsOff = false;
	/** The numbers that went to the UI: the serial of the damage events, the counts of the own damages and heals. */
	uint32 SentDamageSerial = 0;
	uint32 SentOwnDamages = ~0u;
	uint32 SentHeals = ~0u;
};
