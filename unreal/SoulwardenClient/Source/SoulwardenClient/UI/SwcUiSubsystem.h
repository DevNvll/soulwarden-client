#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "SwcUi/Draw.h"
#include "SwcUi/Runtime.h"

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
 * Command line:
 *   -SwcUi=off           no UI
 *   -SwcUiScreens=all    each converted screen loads (default: the screens of the first HUD, stage 1.10)
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

private:
	bool Tick(float DeltaTime);
	void Start();
	void AttachWidget();
	void FeedLocalPlayer();
	void FlushLog();
	void WriteReports();
	void LoadItemCatalog();

	TUniquePtr<swc::ui::Runtime> Runtime;
	TSharedPtr<FSwcUiResources> Resources;
	TSharedPtr<SSwcUiCanvas> Widget;
	swc::ui::DrawList DrawList;
	FTSTicker::FDelegateHandle Ticker;
	FString ReportDir;
	bool bWorldEntered = false;
	bool bWidgetAttached = false;
	bool bLogStarted = false;
	double LastStatusTime = 0.0;

	// The values that the runtime has (a change goes to the runtime).
	TArray<FString> ItemIds;
	TMap<uint32, FString> SentEquipment;
	int32 SentActiveSlot = -2;
	bool bSentSheathed = false;
	TMap<uint32, TArray<uint32>> SentBars;
	TMap<uint32, uint64> SentCooldowns;
	float SentYaw = 1000.0f;
};
