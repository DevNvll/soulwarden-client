#include "UI/SwcUiSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Creatures/SwcCreature.h"
#include "Interact/SwcInteractor.h"
#include "Character/SwcRangedView.h"
#include "Character/SwcPlayerActions.h"
#include "Character/SwcPlayerCharacter.h"
#include "Objects/SwcObjectSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "SwcCameraManager.h"
#include "Creatures/SwcCreatureSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "GameFramework/Pawn.h"
#include "SwcBackendSubsystem.h"
#include "Net/SwcSpace.h"
#include "Net/SwcLoginClient.h"
#include "Combat/SwcBackendSupport.h"
#include "Menu/SwcMenuPreview.h"
#include "SwcPlayerController.h"
#include "SwcDebugLines.h"
#include "UI/SwcUiInput.h"
#include "UI/SwcKeyBindings.h"
#include "UI/SwcUiSlate.h"

DEFINE_LOG_CATEGORY(LogSwcUi);

namespace
{
using FArg = swc::ui::Runtime::Arg;

/** The screens of the first HUD (stage 1.10) and of stage P2.2: the loader of the game loads each screen; only these
 *  load here. */
const char* const HudScreens[] = {
	"lyshineui/hud2024/vitals/vitals2024",
	"lyshineui/hud2024/weaponquickslots/weaponquickslots",
	"lyshineui/compass/compass",
	"lyshineui/hud2024/editmode/hudeditmode",
	"lyshineui/escapemenu/escapemenu",
	"lyshineui/popup/popup",
	// The front end (stage F2 of docs/FRONTEND.md): the landing screen of the main menu (a native load, swc/frontend.lua).
	"lyshineui/main menu/landingscreenv2",
	"lyshineui/respawn/respawn",
	// The world map (stage MVP-C): M opens the state Map (magicmap.luac and its layers; the map data of swc/worldmap.lua).
	"lyshineui/worldmap/magicmap",
	// The conversation screen (stage MVP-H): the Talk of a person (swc/conversation.lua, UI/SwcUiConversation).
	"lyshineui/conversation/conversationscreenv2",
	// The crafting screens (stage MVP-J): the state CraftingStation of a station use (swc/crafting.lua, UI/SwcUiCrafting).
	"lyshineui/crafting/craftingscreenv4",
	"lyshineui/stationproperties/stationproperties",
	// The character window (stage MVP-G): the attributes, the weapon mastery with its trees, the trade skills (swc/skills.lua).
	"lyshineui/skills/skills",
	"lyshineui/tooltip/tooltip",
	// The inventory (stage P2.4): the bag, the equipment, the weight of the action bar, the action menu of an item.
	"lyshineui/inventory/newinventory",
	"lyshineui/equipment/equipmentv3",
	"lyshineui/hud2024/actionbar/encumbrance",
	"lyshineui/flyoutmenu/flyoutmenu",
	// The panel of an open loot bag (the state Container).
	"lyshineui/container/catcontainer",
	// The world views (stage WF7): the markers (the AI plates), the prompt cards of the focus, the damage numbers, the loot ticker.
	"lyshineui/markers/markermanager",
	"lyshineui/markers/unifiedinteract",
	"lyshineui/hud/damagenumbers/damagenumbers",
	"lyshineui/hud2024/lootticker/lootticker2024",
	// The fight feedback (stage MVP-I): the hit indicator (the red edge and the arrow toward the source of a hit).
	"lyshineui/hitindicator/hitindicator",
	// The status tray of the own character (stage MVP-I).
	"lyshineui/statuseffects/statuseffectsv2",
	// The notifications (the minor messages of the inventory, the loot).
	"lyshineui/notifications/notification",
	// The options screen (stage P2.3): its categories, the key page (the rebinds go to the key table of the client).
	"lyshineui/options/options",
	// The fight HUD (stage P3.H): the consumable quick slots (3 to 6), the level and XP meter under the vitals, the reticle.
	"lyshineui/hud2024/consumablequickslots/consumablequickslots",
	"lyshineui/hud2024/xp/xp",
	"lyshineui/hud2024/reticles/reticlescanvas",
	// The help bar (AST-231, the old stage P6.9): the key hints of the screens ("ESC Back", "ESC Close"); the screens push their
	// actions to it (DynamicBus.HelpBar PushToStack, SetGlobalActionsToDefault).
	"lyshineui/helpbar/helpbar",
	// The chat widget (AST-231): its key hint "ENTER Chat" at the bottom left of the HUD and of the screens (the chat itself is not
	// loaded: no chat service).
	"lyshineui/hud/chat/chatwidget",
	// The quests (AST-236): the tracker of the HUD, the journal (J), and the banners (swc/quests.lua, UI/SwcUiQuests).
	"lyshineui/objectives/hud/objectiveshud",
	"lyshineui/objectives/journal/journalscreen",
	"lyshineui/banner/banner",
};

/** The world level name of the HUD scripts (the level crc tells them the front end and the tutorial levels). */
const char* const WorldLevelName = "newworld_vitaeeterna";

/** The first and the last weapon slot of the paperdoll (Soulwarden protocol notes 4.11). */
constexpr uint32 FirstWeaponSlot = 24;
constexpr uint32 LastWeaponSlot = 26;

std::string Utf8(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}

FString WorkDir()
{
	FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
	if (Work.IsEmpty())
	{
		Work = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("Games/soulwarden-client"));
	}
	return Work;
}
} // namespace

void USwcUiSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<USwcBackendSubsystem>();
	Collection.InitializeDependency<USwcKeyBindings>();
	FString Mode;
	FParse::Value(FCommandLine::Get(), TEXT("SwcUi="), Mode);
	if (GIsEditor || Mode == TEXT("off"))
	{
		return;
	}
	// A normal run shows no debug text (stage WF7): with no -SwcDebugLines the screen messages of the engine go off too (the warnings
	// of the renderer, and the map warnings: "Preparing Textures (N)" of the asset compile of an editor build), as in the matched
	// views (USwcMatchViewSubsystem). GAreScreenMessagesEnabled is the switch of the console command DisableAllScreenMessages.
	if (!SwcDebugLines() && GEngine != nullptr)
	{
		GEngine->bEnableOnScreenDebugMessages = false;
		GEngine->ClearOnScreenDebugMessages();
		GAreScreenMessagesEnabled = false;
	}
	Start();
}

void USwcUiSubsystem::Start()
{
	FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	if (Content.IsEmpty())
	{
		Content = FPaths::Combine(WorkDir(), TEXT("content"));
	}
	// The reports of the runtime (-SwcUiReports=DIR: a folder of its own; the default folder is common to the runs of all tracks).
	if (!FParse::Value(FCommandLine::Get(), TEXT("SwcUiReports="), ReportDir))
	{
		ReportDir = FPaths::Combine(WorkDir(), TEXT("logs/ui"));
	}
	IFileManager::Get().MakeDirectory(*ReportDir, true);
	ContentDir = Content;
	if (!StartRuntime())
	{
		return;
	}
	LoadItemCatalog();
	UE_LOG(LogSwcUi, Log, TEXT("ui: the runtime started (Lua 5.1.5, the original scripts of the game)"));
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USwcUiSubsystem::Tick));
	if (FSlateApplication::IsInitialized())
	{
		InputProcessor = MakeShared<FSwcUiInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
	}
	FString DrivePath;
	if (FParse::Value(FCommandLine::Get(), TEXT("SwcUiDrive="), DrivePath))
	{
		LoadDrive(DrivePath);
	}
	PostLoadMap = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &USwcUiSubsystem::OnPostLoadMap);
	// The return that keeps the world (AST-203): no level load, the backend tells the end of the session.
	if (USwcBackendSubsystem* Backend = GetGameInstance()->GetSubsystem<USwcBackendSubsystem>())
	{
		SessionEnd = Backend->OnWorldSessionEnd.AddUObject(this, &USwcUiSubsystem::OnWorldSessionEnd);
	}
}

void USwcUiSubsystem::OnWorldSessionEnd()
{
	OnPostLoadMap(nullptr);   // the same start of the screens as after the level load of a return
}

bool USwcUiSubsystem::StartRuntime()
{
	swc::ui::Runtime::Options Options;
	Options.contentDir = Utf8(FPaths::Combine(ContentDir, TEXT("ui")));
	Options.luaDir = Utf8(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Lua"))));
	Options.reportDir = Utf8(ReportDir);
	FString Screens;
	FParse::Value(FCommandLine::Get(), TEXT("SwcUiScreens="), Screens);
	if (Screens != TEXT("all"))
	{
		for (const char* Screen : HudScreens)
		{
			Options.allowedCanvases.emplace_back(Screen);
		}
	}
	if (!FPaths::DirectoryExists(UTF8_TO_TCHAR(Options.contentDir.c_str())))
	{
		UE_LOG(LogSwcUi, Warning, TEXT("ui: no converted UI content in %hs (tools/convert-ui.sh hud): no UI"), Options.contentDir.c_str());
		return false;
	}
	Runtime = MakeUnique<swc::ui::Runtime>(MoveTemp(Options));
	if (!Resources.IsValid())
	{
		Resources = MakeShared<FSwcUiResources>();
	}
	Runtime->set_text_measure(Resources.Get());
	std::string Error;
	if (!Runtime->start(&Error))
	{
		UE_LOG(LogSwcUi, Error, TEXT("ui: the Lua runtime did not start: %hs"), Error.c_str());
		FlushLog();
		Runtime.Reset();
		return false;
	}
	Runtime->call("Boot");
	// The rebinds that the client kept (UI/SwcKeyBindings: the file of the options screen) go to the key table of the scripts.
	if (const USwcKeyBindings* Keys = GetGameInstance()->GetSubsystem<USwcKeyBindings>())
	{
		Runtime->call("HostKeyBindings", {FArg::of(Utf8(Keys->OverrideLines()))});
	}
	// The loading screen of the game (globals.luac registers it) from the start to the spawn of the player.
	Runtime->call("HostLoading", {FArg::of(true), FArg::of(std::string(WorldLevelName)), FArg::of(std::string("eLoadingScreenIds_LEVEL1"))});
	bStartLoading = true;
	FlushLog();
	return true;
}

void USwcUiSubsystem::OnPostLoadMap(UWorld* LoadedWorld)
{
	// The return to the character selection (stage F6 of docs/FRONTEND.md): after a return (the world stays, or with the fallback
	// after the level load), the screens start again as at the start of the client (the start loading screen, then the landing
	// screen with a new list).
	const USwcBackendSubsystem* Backend = GetGameInstance()->GetSubsystem<USwcBackendSubsystem>();
	if (Backend == nullptr || Backend->GetMenuReturns() == SeenMenuReturns)
	{
		return;
	}
	SeenMenuReturns = Backend->GetMenuReturns();
	UE_LOG(LogSwcUi, Log, TEXT("ui: the return to the menu (%d): the screens start again"), SeenMenuReturns);
	if (Runtime.IsValid())
	{
		// The reports of the runtime that ends: a copy with the number of the return (the next runtime writes the same names).
		WriteReports();
		for (const TCHAR* Name : {TEXT("ui-errors.txt"), TEXT("ui-log.txt"), TEXT("ui-stubs.txt")})
		{
			const FString From = FPaths::Combine(ReportDir, Name);
			IFileManager::Get().Copy(*FPaths::Combine(ReportDir, FString::Printf(TEXT("%s-%d.%s"), *FPaths::GetBaseFilename(Name), SeenMenuReturns,
				*FPaths::GetExtension(Name))), *From);
		}
	}
	Runtime.Reset();
	ResetSession();
	DrawList = swc::ui::DrawList();
	StartRuntime();
}

void USwcUiSubsystem::ResetSession()
{
	bStartLoading = false;
	bMenuStarted = false;
	bMenuListPending = false;
	bMenuListAgain = false;
	bMenuLogin = false;
	SentMenuConnection = 0;
	MenuError.Reset();
	MenuErrorAt = 0.0;
	MenuSpawnTime = 0.0;
	MenuSmoothSince = -1.0;
	Preview.Reset();
	PreviewCharacter.Reset();
	BuiltCharacter.Reset();
	bPreviewDirty = false;
	MenuClips = {FString(), FString()};
	bErrorShown = false;
	bSentLoadingScreen = false;
	bLoadingForDeath = false;
	LoadingHoldUntil = 0.0;
	SentDeathNotices = 0;
	bCursorWanted = false;
	bKeybinding = false;
	bCursorApplied = false;
	SentDisplay = FIntPoint::ZeroValue;
	bSentAim = false;
	bWorldEntered = false;
	SentBag.Reset();
	SentPaperdoll.Reset();
	SentBagRevision = ~0u;
	SentPaperdollRevision = ~0u;
	SentEquipFails = 0;
	SentCoin = -1;
	SentLevel = -1;
	bSentSupport = false;
	SentAttributeRevision = ~0u;
	SentRankRevision = ~0u;
	SentNodesRevision = ~0u;
	SentChargeRevision = ~0u;
	SentAchievementRevision = ~0u;
	SentEntitlementRevision = ~0u;
	SentCraftRevision = ~0u;
	SentLoot.Reset();
	SentLootRevision = ~0u;
	SentNearBags.Reset();
	NextBagsTime = 0.0;
	SentOpenBag = 0;
	AskedOpenBag = 0;
	AskedOpenTime = 0.0;
	SentActiveSlot = -2;
	bSentSheathed = false;
	SentBars.Reset();
	SentCooldowns.Reset();
	SentItemCooldowns.Reset();
	SentYaw = 1000.0f;
	NextOcclusionTime = 0.0;
	bStandInsOff = false;
	SentDamageSerial = 0;
	SentOwnDamages = ~0u;
	SentHeals = ~0u;
}

void USwcUiSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(Ticker);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMap);
	if (USwcBackendSubsystem* Backend = GetGameInstance()->GetSubsystem<USwcBackendSubsystem>())
	{
		Backend->OnWorldSessionEnd.Remove(SessionEnd);
	}
	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}
	InputProcessor.Reset();
	if (Runtime.IsValid())
	{
		WriteReports();
	}
	if (bWidgetAttached && GEngine != nullptr && GEngine->GameViewport != nullptr && Widget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	}
	Widget.Reset();
	Runtime.Reset();
	Resources.Reset();
	Super::Deinitialize();
}

void USwcUiSubsystem::LoadItemCatalog()
{
	// The cap of the Azoth balance (the game data of the backend copy: crafting-data.json `currencies`, Azoth_Currency `cap`; stage
	// MVP-A). The file is large (the recipes): only the object of the currencies is parsed.
	{
		FString Crafting;
		const FString CraftingPath = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_GAMEDATA_DIR")), TEXT("crafting-data.json"));
		const int32 Key = FFileHelper::LoadFileToString(Crafting, *CraftingPath) ? Crafting.Find(TEXT("\"currencies\"")) : INDEX_NONE;
		const int32 Open = Key != INDEX_NONE ? Crafting.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Key) : INDEX_NONE;
		int32 Close = INDEX_NONE;
		for (int32 Index = Open, Depth = 0; Open != INDEX_NONE && Index < Crafting.Len(); ++Index)
		{
			Depth += Crafting[Index] == TEXT('{') ? 1 : Crafting[Index] == TEXT('}') ? -1 : 0;
			if (Depth == 0)
			{
				Close = Index;
				break;
			}
		}
		TSharedPtr<FJsonObject> Currencies;
		const TSharedPtr<FJsonObject>* Azoth = nullptr;
		if (Close != INDEX_NONE && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Crafting.Mid(Open, Close - Open + 1)), Currencies) &&
			Currencies.IsValid() && Currencies->TryGetObjectField(TEXT("Azoth_Currency"), Azoth) && (*Azoth)->HasTypedField<EJson::Number>(TEXT("cap")))
		{
			Runtime->call("HostAzothCap", {FArg::of((*Azoth)->GetNumberField(TEXT("cap")))});
		}
		else
		{
			UE_LOG(LogSwcUi, Warning, TEXT("ui: no Azoth cap in %s: the inventory shows the Azoth balance with no cap"), *CraftingPath);
		}
	}
	// The level cap of the character (the game data of the backend copy: progression-tables.json levelCap).
	{
		FString Progression;
		TSharedPtr<FJsonObject> Root;
		const FString ProgressionPath = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_GAMEDATA_DIR")), TEXT("progression-tables.json"));
		if (FFileHelper::LoadFileToString(Progression, *ProgressionPath) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Progression), Root) &&
			Root.IsValid() && Root->HasTypedField<EJson::Number>(TEXT("levelCap")))
		{
			Runtime->call("HostLevelCap", {FArg::of(Root->GetNumberField(TEXT("levelCap")))});
		}
	}
	// The item list of the wire (the backend copy): the item with index N is entry N - 1 (docs/CHARACTER.md).
	const FString Path = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_GAMEDATA_DIR")), TEXT("item-catalog.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogSwcUi, Warning, TEXT("ui: no item catalog at %s: the weapon slots show no item"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogSwcUi, Warning, TEXT("ui: the item catalog %s is not JSON"), *Path);
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Root->TryGetArrayField(TEXT("items"), Items))
	{
		ItemIds.Reserve(Items->Num());
		for (const TSharedPtr<FJsonValue>& Item : *Items)
		{
			const TArray<TSharedPtr<FJsonValue>>& Columns = Item->AsArray();
			ItemIds.Add(Columns.Num() > 0 ? Columns[0]->AsString() : FString());
		}
	}
	// The perk list of the wire (perk-data.json of the backend copy): the perk with index K + 1 is row K (protocol notes 4.23).
	FString PerkText;
	TSharedPtr<FJsonObject> PerkRoot;
	const FString PerkPath = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_GAMEDATA_DIR")), TEXT("perk-data.json"));
	const TArray<TSharedPtr<FJsonValue>>* Perks = nullptr;
	if (FFileHelper::LoadFileToString(PerkText, *PerkPath) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PerkText), PerkRoot)
		&& PerkRoot.IsValid() && PerkRoot->TryGetArrayField(TEXT("perks"), Perks))
	{
		PerkIds.Reserve(Perks->Num());
		for (const TSharedPtr<FJsonValue>& Perk : *Perks)
		{
			const TArray<TSharedPtr<FJsonValue>>& Columns = Perk->AsArray();
			PerkIds.Add(Columns.Num() > 0 ? Columns[0]->AsString() : FString());
		}
	}
	else
	{
		UE_LOG(LogSwcUi, Warning, TEXT("ui: no perk list at %s: the items show no perk"), *PerkPath);
	}
}

namespace
{
/** @return The text of an item instance id ({8-4-4-4-12} hexadecimal digits of the 16 bytes in their sequence), or empty for zero. */
FString InstanceText(const std::array<uint8, 16>& Instance)
{
	bool bZero = true;
	for (const uint8 Byte : Instance)
	{
		bZero = bZero && Byte == 0;
	}
	if (bZero)
	{
		return FString();
	}
	FString Hex;
	for (int32 Index = 0; Index < 16; ++Index)
	{
		if (Index == 4 || Index == 6 || Index == 8 || Index == 10)
		{
			Hex += TEXT("-");
		}
		Hex += FString::Printf(TEXT("%02X"), Instance[Index]);
	}
	return TEXT("{") + Hex + TEXT("}");
}

/** @return The 16 bytes of an instance id text of InstanceText (zero for a text that is not one). */
swc::net::ItemInstanceId ParseInstance(const FString& Text)
{
	swc::net::ItemInstanceId Instance{};
	FString Hex = Text.Replace(TEXT("{"), TEXT("")).Replace(TEXT("}"), TEXT("")).Replace(TEXT("-"), TEXT(""));
	if (Hex.Len() != 32)
	{
		return Instance;
	}
	for (int32 Index = 0; Index < 16; ++Index)
	{
		Instance[Index] = static_cast<uint8>(FParse::HexNumber(*Hex.Mid(Index * 2, 2)));
	}
	return Instance;
}
} // namespace

FString USwcUiSubsystem::CallItem(const char* Function, uint32 Slot, const soulwarden::backend::tests::WireItem* Item)
{
	const bool bItem = Item != nullptr && !Item->empty();
	const FString ItemId = bItem && ItemIds.IsValidIndex(static_cast<int32>(Item->index) - 1) ? ItemIds[Item->index - 1] : FString();
	FString Perks;
	if (bItem)
	{
		for (const auto Perk : Item->perks)
		{
			if (Perk > 0 && PerkIds.IsValidIndex(static_cast<int32>(Perk) - 1))
			{
				Perks += (Perks.IsEmpty() ? TEXT("") : TEXT(",")) + PerkIds[Perk - 1];
			}
		}
	}
	const FString Instance = bItem ? InstanceText(Item->instance) : FString();
	const double Quantity = bItem ? Item->quantity : 0;
	const double GearScore = bItem ? Item->gearScore : 0;
	const double Durability = bItem ? Item->durability : 0;
	const double MaxDurability = bItem ? Item->maxDurability : 0;
	const double Flags = bItem ? Item->flags : 0;
	const double Chrono = bItem ? Item->chrono : 0;
	const double Action = Item != nullptr ? Item->action : 0;
	Runtime->call(Function, {FArg::of(static_cast<double>(Slot)), FArg::of(Utf8(ItemId)), FArg::of(Quantity), FArg::of(GearScore),
							 FArg::of(Durability), FArg::of(MaxDurability), FArg::of(Utf8(Instance)), FArg::of(Flags), FArg::of(Chrono),
							 FArg::of(Action), FArg::of(Utf8(Perks))});
	return FString::Printf(TEXT("%s|%g|%g|%g|%g|%s|%g|%s"), *ItemId, Quantity, GearScore, Durability, MaxDurability, *Instance, Flags, *Perks);
}

void USwcUiSubsystem::FeedItems(const swc::net::RepClient::OwnEntity& Own)
{
	// The bag (the Container of slot 61): each slot whose values changed.
	if (Own.bagRevision != SentBagRevision)
	{
		SentBagRevision = Own.bagRevision;
		if (SentBag.Num() != static_cast<int32>(Own.bag.size()))
		{
			SentBag.SetNum(static_cast<int32>(Own.bag.size()));
			Runtime->call("HostBagSize", {FArg::of(static_cast<double>(Own.bag.size()))});
		}
		for (int32 Slot = 0; Slot < SentBag.Num(); ++Slot)
		{
			const auto& Item = Own.bag[static_cast<size_t>(Slot)];
			const FString ItemId = !Item.empty() && ItemIds.IsValidIndex(static_cast<int32>(Item.index) - 1) ? ItemIds[Item.index - 1] : FString();
			const FString Key = FString::Printf(TEXT("%s|%u|%u|%u|%u|%u|%s"), *ItemId, Item.empty() ? 0u : Item.quantity, Item.gearScore, Item.durability,
				Item.flags, Item.perks[0] + Item.perks[1] * 7 + Item.perks[2] * 13, *InstanceText(Item.instance));
			if (SentBag[Slot] != Key)
			{
				SentBag[Slot] = Key;
				CallItem("HostBagSlot", static_cast<uint32>(Slot), &Item);
			}
		}
	}
	// The items of the paperdoll (the owner view of slot 62): each slot that got or lost an item.
	if (Own.paperdollRevision != SentPaperdollRevision)
	{
		SentPaperdollRevision = Own.paperdollRevision;
		TSet<uint32> Slots;
		for (const auto& [Slot, Item] : Own.paperdollItems)
		{
			Slots.Add(Slot);
		}
		for (const auto& [Slot, Key] : SentPaperdoll)
		{
			Slots.Add(Slot);
		}
		for (const uint32 Slot : Slots)
		{
			const auto Found = Own.paperdollItems.find(Slot);
			const soulwarden::backend::tests::WireItem* Item = Found != Own.paperdollItems.end() ? &Found->second : nullptr;
			const FString ItemId = Item && ItemIds.IsValidIndex(static_cast<int32>(Item->index) - 1) ? ItemIds[Item->index - 1] : FString();
			const FString Key = Item ? FString::Printf(TEXT("%s|%u|%u|%u|%s"), *ItemId, Item->quantity, Item->gearScore, Item->durability, *InstanceText(Item->instance)) : FString();
			const FString* Sent = SentPaperdoll.Find(Slot);
			if (Sent == nullptr ? !Key.IsEmpty() : *Sent != Key)
			{
				CallItem("HostPaperdollItem", Slot, Item);
				if (Key.IsEmpty())
				{
					SentPaperdoll.Remove(Slot);
				}
				else
				{
					SentPaperdoll.Add(Slot, Key);
				}
			}
		}
	}
	// A refused request of the equipment (3626): the scripts show the slots again.
	if (Own.equipFails != SentEquipFails)
	{
		SentEquipFails = Own.equipFails;
		Runtime->call("HostEquipFail");
	}
	// The level of the screen (the wire level + 1) and the coin.
	if (Own.hasProgression && static_cast<int64>(Own.level) != SentLevel)
	{
		SentLevel = Own.level;
		Runtime->call("HostProgression", {FArg::of(static_cast<double>(Own.level) + 1.0), FArg::of(static_cast<double>(Own.experience))});
	}
	if (Own.hasCoin && static_cast<int64>(Own.coin) != SentCoin)
	{
		SentCoin = static_cast<int64>(Own.coin);
		Runtime->call("HostCoin", {FArg::of(static_cast<double>(Own.coin))});
	}
}

void USwcUiSubsystem::FeedOwnerStates(const swc::net::RepClient::OwnEntity& Own)
{
	// The abilities with a cast plan of the backend (Combat/SwcBackendSupport.h): the HUD shows the other slots disabled.
	if (!bSentSupport)
	{
		bSentSupport = true;
		std::string Text;
		for (const std::uint32_t Id : swc::combat::supported_abilities())
		{
			Text += std::to_string(Id) + " ";
		}
		Runtime->call("HostSupportedAbilities", {FArg::of(Text)});
	}
	// The attributes (slot 27): the base value, the bonus, and the spent points of each CharacterAttributeType, and the free points.
	if (Own.hasAttributes && Own.attributeRevision != SentAttributeRevision)
	{
		SentAttributeRevision = Own.attributeRevision;
		const swc::net::RepClient::OwnEntity::Attributes& A = Own.attributes;
		std::vector<FArg> Args;
		for (const std::array<std::uint32_t, 5>* Part : {&A.base, &A.bonus, &A.spent})
		{
			for (const std::uint32_t Value : *Part)
			{
				Args.push_back(FArg::of(static_cast<double>(Value)));
			}
		}
		Args.push_back(FArg::of(static_cast<double>(A.unspent)));
		Runtime->call("HostAttributes", Args);
		UE_LOG(LogSwcUi, Log, TEXT("ui: attributes (base + bonus): CON %u+%u FOC %u+%u STR %u+%u DEX %u+%u INT %u+%u, %u free point(s)"),
			A.base[0], A.bonus[0], A.base[1], A.bonus[1], A.base[2], A.bonus[2], A.base[3], A.bonus[3], A.base[4], A.bonus[4], A.unspent);
	}
	// The ranks of the weapon masteries and the trade skills (slot 65): "id rank points" lines.
	if (Own.rankRevision != SentRankRevision && Own.rankRevision != 0)
	{
		SentRankRevision = Own.rankRevision;
		std::string Text;
		for (const auto& [Id, Rank] : Own.ranks)
		{
			Text += std::to_string(Id) + " " + std::to_string(Rank.rank) + " " + std::to_string(Rank.points) + "\n";
		}
		Runtime->call("HostRanks", {FArg::of(Text)});
		UE_LOG(LogSwcUi, Log, TEXT("ui: %d mastery and trade skill rank(s) (the CategoricalProgression state)"), static_cast<int32>(Own.ranks.size()));
	}
	// The point pools of the weapon trees (slot 67): "pool free" lines.
	if (Own.nodesRevision != SentNodesRevision && Own.nodesRevision != 0)
	{
		SentNodesRevision = Own.nodesRevision;
		std::string Text;
		for (const auto& [Pool, Free] : Own.pointPools)
		{
			Text += std::to_string(Pool) + " " + std::to_string(Free) + "\n";
		}
		Runtime->call("HostPointPools", {FArg::of(Text)});
	}
	// The heart rune charge (slot 48): the HUD fills the icon of the rune (Hud.LocalPlayer.Vitals.ChargePercent).
	if (Own.hasCharge && Own.chargeRevision != SentChargeRevision)
	{
		SentChargeRevision = Own.chargeRevision;
		Runtime->call("HostCharge", {FArg::of(static_cast<double>(Own.chargePercent))});
		UE_LOG(LogSwcUi, Log, TEXT("ui: the heart rune charge %u percent"), Own.chargePercent);
	}
	// The in-use state of a consumable (stage MVP-E; retail SetInUsePaperdollItem): the slot of the Use action of the character, 0 at its
	// end. consumablequickslots.luac dims the four quick slot keys while it lasts (Swc.HostConsumeSlot).
	{
		const APlayerController* Controller = GetGameInstance() != nullptr ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
		const ASwcPlayerCharacter* Character = Controller != nullptr ? Cast<ASwcPlayerCharacter>(Controller->GetPawn()) : nullptr;
		const uint32 Slot = Character != nullptr && Character->GetActions() != nullptr ? Character->GetActions()->GetConsumingSlot() : 0;
		if (Slot != SentConsumeSlot)
		{
			SentConsumeSlot = Slot;
			Runtime->call("HostConsumeSlot", {FArg::of(static_cast<double>(Slot))});
		}
	}
	// The statuses of the owner (slot 44, the full map; stage MVP-E): "index potency stacks" lines. The equipment screen takes the armor
	// parts of the statuses (swc/owner.lua Swc.HostStatuses); the server sends the map again at the end of a status.
	if (Own.statusRevision != SentStatusRevision)
	{
		SentStatusRevision = Own.statusRevision;
		std::string Text;
		for (const swc::net::RepClient::OwnEntity::Status& Status : Own.statuses)
		{
			Text += std::to_string(Status.index) + " " + std::to_string(Status.potency) + " " + std::to_string(Status.stacks) + "\n";
		}
		Runtime->call("HostStatuses", {FArg::of(Text)});
		// The status tray (stage MVP-I; swc/statustray.lua): "index potency stacks end" lines, the end in Unix seconds (0: no end).
		std::string Tray;
		for (const swc::net::RepClient::OwnEntity::Status& Status : Own.statuses)
		{
			Tray += std::to_string(Status.index) + " " + std::to_string(Status.potency) + " " + std::to_string(Status.stacks) + " "
				+ std::to_string(static_cast<double>(Status.wallEndNs) / 1e9) + "\n";
		}
		Runtime->call("HostStatusTray", {FArg::of(Tray)});
	}
	// The achievements of the character (slot 66) and the entitlements of the account (slot 19): the indexes of the set bits.
	if (Own.achievementRevision != SentAchievementRevision || Own.entitlementRevision != SentEntitlementRevision)
	{
		SentAchievementRevision = Own.achievementRevision;
		SentEntitlementRevision = Own.entitlementRevision;
		const auto Bits = [](const std::vector<std::uint8_t>& Bytes)
		{
			std::string Text;
			for (std::size_t Index = 0; Index < Bytes.size() * 8; ++Index)
			{
				if ((Bytes[Index >> 3] & (1u << (Index & 7))) != 0)
				{
					Text += std::to_string(Index) + " ";
				}
			}
			return Text;
		};
		Runtime->call("HostOwnership", {FArg::of(Bits(Own.achievements)), FArg::of(Bits(Own.entitlements)), FArg::of(Own.entitlementsReceived)});
	}
	// The craft state (slot 63) and the craft progress: UI/SwcUiCrafting (stage MVP-J).
}

void USwcUiSubsystem::FeedLoot(const swc::net::RepClient::OwnEntity& Own, USwcBackendSubsystem* Backend)
{
	// The items of the open bag (the selected Container of slot 76): each slot whose values changed.
	if (Own.selectedRevision != SentLootRevision)
	{
		SentLootRevision = Own.selectedRevision;
		if (SentLoot.Num() != static_cast<int32>(Own.selectedContainer.size()))
		{
			SentLoot.SetNum(static_cast<int32>(Own.selectedContainer.size()));
			Runtime->call("HostLootSize", {FArg::of(static_cast<double>(Own.selectedContainer.size()))});
			for (FString& Key : SentLoot)
			{
				Key.Reset();
			}
		}
		for (int32 Slot = 0; Slot < SentLoot.Num(); ++Slot)
		{
			const auto& Item = Own.selectedContainer[static_cast<size_t>(Slot)];
			const FString Key = Item.empty() ? FString() : FString::Printf(TEXT("%u|%u|%u|%s"), Item.index, Item.quantity, Item.gearScore, *InstanceText(Item.instance));
			if (SentLoot[Slot] != Key || Key.IsEmpty())
			{
				SentLoot[Slot] = Key;
				CallItem("HostLootSlot", static_cast<uint32>(Slot), &Item);
			}
		}
	}
	// The bags near the character (the trigger areas of the native code: a test of the place): the bags of the creature view in the
	// range of their Interact bound (m_interactRangeSq) with no Looted claim, each 0.25 s. The Enabled mark of the bound is not a
	// part of it (a use of the bag can change it).
	const double Now = FPlatformTime::Seconds();
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	USwcCreatureSubsystem* Creatures = World != nullptr ? World->GetSubsystem<USwcCreatureSubsystem>() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Creatures != nullptr && Pawn != nullptr && Now >= NextBagsTime)
	{
		NextBagsTime = Now + 0.25;
		TArray<FSwcLootBag> Bags;
		Creatures->GetLootBags(Pawn->GetNavAgentLocation(), Bags);
		FString Near;
		for (const FSwcLootBag& Bag : Bags)
		{
			const auto Claim = std::find_if(Own.lootClaims.begin(), Own.lootClaims.end(),
				[&Bag](const swc::net::LootClaim& Item) { return Item.actor == Bag.Actor; });
			const bool bLooted = Claim != Own.lootClaims.end() && Claim->state == swc::net::kLootLooted;
			const float DistanceSq = Bag.Distance * Bag.Distance;
			if (!bLooted && DistanceSq <= Bag.RangeSq)
			{
				// INTEREST:DISTANCESQ:KIND (the kind of the claim: 1 AILoot, 5 Discarded).
				Near += FString::Printf(TEXT("%s%u:%.1f:%u"), Near.IsEmpty() ? TEXT("") : TEXT(" "), Bag.Interest, DistanceSq,
					Claim != Own.lootClaims.end() ? Claim->kind : 1u);
			}
		}
		if (Near != SentNearBags)
		{
			SentNearBags = Near;
			Runtime->call("HostLootBags", {FArg::of(Utf8(Near))});
		}
	}
	// The use of the bag panel: its end (the Looted claim, the end of the bag, the close) closes the panel; an open with no use after
	// 1 s (the bag is not in range, a use runs) too.
	const uint32 Open = Backend->GetOpenLootBag();
	if (Open != SentOpenBag)
	{
		if (SentOpenBag != 0 && Open == 0)
		{
			UE_LOG(LogSwcUi, Log, TEXT("ui: the use of the bag %u ended: the panel closes"), SentOpenBag);
			Runtime->call("HostLootBagClosed", {FArg::of(static_cast<double>(SentOpenBag))});
		}
		SentOpenBag = Open;
		if (Open == AskedOpenBag)
		{
			AskedOpenBag = 0;
		}
	}
	if (AskedOpenBag != 0 && Now - AskedOpenTime > 1.0)
	{
		UE_LOG(LogSwcUi, Log, TEXT("ui: no use of the bag %u: the panel closes"), AskedOpenBag);
		Runtime->call("HostLootBagClosed", {FArg::of(static_cast<double>(AskedOpenBag))});
		AskedOpenBag = 0;
	}
}

void USwcUiSubsystem::HandleItemRequest(const FString& Request, USwcBackendSubsystem* Backend)
{
	TArray<FString> Parts;
	Request.ParseIntoArray(Parts, TEXT(" "));
	const auto Int = [&Parts](int32 Index) { return Parts.IsValidIndex(Index) ? FCString::Atoi(*Parts[Index]) : 0; };
	const FString& Verb = Parts.Num() > 0 ? Parts[0] : Request;
	bool bSent = false;
	if (Verb == TEXT("equip") && Parts.Num() >= 5)
	{
		bSent = Backend->EquipItem(Int(1), static_cast<uint32>(Int(2)), static_cast<uint32>(Int(3)), ParseInstance(Parts[4]));
	}
	else if (Verb == TEXT("unequip") && Parts.Num() >= 4)
	{
		bSent = Backend->UnequipItem(static_cast<uint32>(Int(1)), Int(2), static_cast<uint32>(Int(3)));
	}
	else if (Verb == TEXT("swap") && Parts.Num() >= 5)
	{
		bSent = Backend->SwapItems(static_cast<uint32>(Int(1)), static_cast<uint32>(Int(2)), ParseInstance(Parts[3]), ParseInstance(Parts[4]));
	}
	else if (Verb == TEXT("use") && Parts.Num() >= 2)
	{
		bSent = Backend->UseItem(static_cast<uint32>(Int(1)));
	}
	else if (Verb == TEXT("drop") && Parts.Num() >= 4)
	{
		bSent = Backend->DropItems({swc::net::DropItemEntry{static_cast<uint32>(Int(1)), ParseInstance(Parts[3]), static_cast<uint32>(Int(2))}});
	}
	else if (Verb == TEXT("dropequipped") && Parts.Num() >= 3)
	{
		bSent = Backend->DropPaperdollItem(static_cast<uint32>(Int(1)), static_cast<uint32>(Int(2)));
	}
	else if (Verb == TEXT("takeall"))
	{
		bSent = Backend->TakeAllItems();
	}
	else if (Verb == TEXT("take") && Parts.Num() >= 3)
	{
		bSent = Backend->TakeItem(ParseInstance(Parts[1]), static_cast<uint32>(Int(2)));
	}
	else if (Verb == TEXT("openloot") && Parts.Num() >= 2)
	{
		// The panel of a bag (OpenStorageOption): the interactor sends the request at its next update.
		Backend->OpenLootBag(static_cast<uint32>(Int(1)));
		AskedOpenBag = static_cast<uint32>(Int(1));
		AskedOpenTime = FPlatformTime::Seconds();
		bSent = true;
	}
	else if (Verb == TEXT("panelclosed"))
	{
		// The panel closed (catcontainer.luac InteractPanelClosed): the use of the bag ends (2473).
		bSent = Backend->GetOpenLootBag() != 0;
		Backend->CloseLootBag();
	}
	UE_LOG(LogSwcUi, Log, TEXT("ui: item request \"%s\": %s"), *Request, bSent ? TEXT("sent") : TEXT("not sent"));
}

void USwcUiSubsystem::AttachWidget()
{
	if (bWidgetAttached || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}
	Widget = SNew(SSwcUiCanvas, Resources.ToSharedRef());
	Widget->SetDrawList(&DrawList);
	GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(), 10);
	bWidgetAttached = true;
}

bool USwcUiSubsystem::Tick(float DeltaTime)
{
	if (!Runtime.IsValid())
	{
		return false;
	}
	AttachWidget();
	if (Widget.IsValid())
	{
		const FVector2f Size = Widget->GetLastSize();
		Runtime->set_viewport(FMath::Max(Size.X, 1.0f), FMath::Max(Size.Y, 1.0f));
		// The viewport of the runtime is in Slate units (after the DPI scale); the options screen shows the window in pixels.
		const FVector2D Pixels = Widget->GetLastGeometry().GetAbsoluteSize();
		const FIntPoint Display(FMath::RoundToInt(Pixels.X), FMath::RoundToInt(Pixels.Y));
		if (Display.X > 0 && Display.Y > 0 && Display != SentDisplay)
		{
			SentDisplay = Display;
			Runtime->call("HostDisplay", {FArg::of(static_cast<double>(Display.X)), FArg::of(static_cast<double>(Display.Y))});
		}
	}
	FeedLocalPlayer();
	TickDrive();
	Runtime->tick(FMath::Min(DeltaTime, 0.25f));
	UpdateLoadingAndRequests();
	UpdateCursor();
	Runtime->build_draw_list(DrawList);
	if (Widget.IsValid())
	{
		Widget->Invalidate(EInvalidateWidgetReason::Paint);
	}
	FlushLog();
	const double Now = FPlatformTime::Seconds();
	if (Now - LastStatusTime > 30.0)
	{
		// The reports also each 30 s: a stop by tools/dev-stop.sh does not wait for Deinitialize.
		if (LastStatusTime > 0.0)
		{
			std::string Error;
			Runtime->write_reports(&Error);
			WriteDrawList();
		}
		LastStatusTime = Now;
		const swc::ui::Runtime::Counts& Counts = Runtime->counts();
		UE_LOG(LogSwcUi, Log, TEXT("ui: %llu ticks, %llu Lua errors, %llu stub names (%llu calls), %d draw items, %d textures"),
			static_cast<unsigned long long>(Counts.ticks), static_cast<unsigned long long>(Counts.luaErrors),
			static_cast<unsigned long long>(Counts.stubNames), static_cast<unsigned long long>(Counts.stubCalls),
			static_cast<int32>(DrawList.items.size()), Resources.IsValid() ? Resources->TextureCount() : 0);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------- input

bool USwcUiSubsystem::CallInput(const std::vector<FArg>& Args)
{
	if (!Runtime.IsValid())
	{
		return false;
	}
	std::string Result;
	Runtime->call("HostInput", Args, &Result);
	return Result == "true";
}

bool USwcUiSubsystem::UiKey(const FString& Name, bool bDown)
{
	return CallInput({FArg::of(std::string("key")), FArg::of(Utf8(Name)), FArg::of(bDown)});
}

bool USwcUiSubsystem::UiKeyToGame(const FString& Name)
{
	if (!Runtime.IsValid())
	{
		return true;
	}
	std::string Result;
	Runtime->call("HostKeyToGame", {FArg::of(Utf8(Name))}, &Result);
	return Result != "false";
}

void USwcUiSubsystem::UiPointer(FVector2D ScreenPosition)
{
	if (Widget.IsValid())
	{
		UiPointerLocal(Widget->GetLastGeometry().AbsoluteToLocal(FVector2f(ScreenPosition)));
	}
}

void USwcUiSubsystem::UiPointerLocal(FVector2f Local)
{
	CallInput({FArg::of(std::string("move")), FArg::of(static_cast<double>(Local.X)), FArg::of(static_cast<double>(Local.Y))});
}

bool USwcUiSubsystem::UiButton(const TCHAR* Button, bool bDown)
{
	return CallInput({FArg::of(std::string("button")), FArg::of(Utf8(Button)), FArg::of(bDown)});
}

bool USwcUiSubsystem::UiWheel(float Delta)
{
	// The unit of CryInput: 120 for each step of the wheel (Slate gives 1); the scroll boxes and the actions of the wheel use it.
	return CallInput({FArg::of(std::string("wheel")), FArg::of(static_cast<double>(Delta) * 120.0)});
}

void USwcUiSubsystem::UpdateCursor()
{
	std::string Result;
	Runtime->call("HostCursor", {}, &Result);
	bCursorWanted = Result == "true";
	if (bCursorWanted == bCursorApplied || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}
	UWorld* World = GEngine->GameViewport->GetWorld();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr)
	{
		return;
	}
	bCursorApplied = bCursorWanted;
	// The cursor of a screen that needs the pointer (UiJavelinCanvasComponent requiresMouse, AddMouseOwner): the
	// viewport gives up the mouse capture; the input pre-processor keeps the mouse events from the game.
	Controller->SetShowMouseCursor(bCursorWanted);
	if (bCursorWanted)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Controller->SetInputMode(Mode);
	}
	else
	{
		Controller->SetInputMode(FInputModeGameOnly());
		CallInput({FArg::of(std::string("move")), FArg::of(-1.0), FArg::of(-1.0)});
	}
	UE_LOG(LogSwcUi, Log, TEXT("ui: the cursor %s"), bCursorWanted ? TEXT("shows (a screen needs the pointer)") : TEXT("hides (the game has the mouse)"));
}

void USwcUiSubsystem::UpdateLoadingAndRequests()
{
	USwcBackendSubsystem* Backend = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	if (Backend == nullptr)
	{
		return;
	}
	// A refused or failed connection of the login path: the error popup of the game (UIError_login_unable_to_connect).
	if (!bErrorShown && !Backend->GetLoginError().IsEmpty())
	{
		bErrorShown = true;
		bStartLoading = false;
		UE_LOG(LogSwcUi, Log, TEXT("ui: the login path failed (%s): the error UI shows"), *Backend->GetLoginError());
		Runtime->call("HostConnectionError");
	}
	// The end of the loading screen of the start: the player has its character (the player character of the client, not the
	// default pawn of the map: the spawn waits for the cells of the place).
	if (bStartLoading && bWorldEntered && GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		UWorld* World = GEngine->GameViewport->GetWorld();
		APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		if (Controller != nullptr && Cast<ASwcPlayerCharacter>(Controller->GetPawn()) != nullptr)
		{
			bStartLoading = false;
			Runtime->call("HostLoading", {FArg::of(false)});
		}
	}
	UpdateMenu(Backend);
	Conversation.Update(*Runtime, GEngine != nullptr && GEngine->GameViewport != nullptr ? GEngine->GameViewport->GetWorld() : nullptr, *Backend);
	CraftingScreen.Update(*Runtime, *Backend);
	Skills.Update(*Runtime, *Backend);
	// The requests of the scripts: a respawn of the respawn screen (respawn.luac TriggerRespawn).
	for (int32 Guard = 0; Guard < 8; ++Guard)
	{
		std::string Request;
		Runtime->call("TakeHostRequest", {}, &Request);
		if (Request.empty())
		{
			break;
		}
		if (Request.rfind("respawn ", 0) == 0)
		{
			// "respawn RANDOM HOMEID": the row "Random Location" has random 1 and no home id (the backend has no home point).
			TArray<FString> Parts;
			FString(UTF8_TO_TCHAR(Request.c_str())).ParseIntoArray(Parts, TEXT(" "));
			const bool bRandom = Parts.Num() < 2 || Parts[1] == TEXT("1");
			const bool bSent = Backend->RequestRespawn(bRandom, Parts.Num() > 2 ? Parts[2] : FString());
			if (bSent)
			{
				// The retail client shows the loading screen of the death at the request.
				bLoadingForDeath = true;
				LoadingHoldUntil = FPlatformTime::Seconds() + 1.0;
			}
			UE_LOG(LogSwcUi, Log, TEXT("ui: the respawn screen asks for a respawn (%hs): %s"), Request.c_str(), bSent ? TEXT("sent") : TEXT("not sent"));
		}
		else if (FSwcUiConversation::HandleRequest(UTF8_TO_TCHAR(Request.c_str()), *Backend))
		{
		}
		else if (FSwcUiQuests::HandleRequest(UTF8_TO_TCHAR(Request.c_str()), *Backend))
		{
		}
		else if (FSwcUiCrafting::HandleRequest(UTF8_TO_TCHAR(Request.c_str()), *Backend))
		{
		}
		else if (FSwcUiSkills::HandleRequest(UTF8_TO_TCHAR(Request.c_str()), *Backend))
		{
		}
		else if (Request.rfind("travel ", 0) == 0)
		{
			// "travel shrine POI X Y Z" (swc/worldmap.lua: the Yes of the shrine popup of the map): the travel command of the
			// backend to the place at the landmark position (USwcBackendSubsystem::RequestTravel).
			TArray<FString> Parts;
			FString(UTF8_TO_TCHAR(Request.c_str())).ParseIntoArray(Parts, TEXT(" "));
			const bool bSent = Parts.Num() >= 6 && Backend->RequestTravel(FVector(FCString::Atod(*Parts[3]), FCString::Atod(*Parts[4]),
				FCString::Atod(*Parts[5])));
			UE_LOG(LogSwcUi, Log, TEXT("ui: the map asks for a travel (%hs): %s"), Request.c_str(), bSent ? TEXT("sent") : TEXT("not sent"));
		}
		else if (Request.rfind("menu ", 0) == 0)
		{
			HandleMenuRequest(UTF8_TO_TCHAR(Request.c_str()), Backend);
		}
		else if (Request.rfind("rebind", 0) == 0 || Request.rfind("keybinding ", 0) == 0)
		{
			HandleKeyRequest(UTF8_TO_TCHAR(Request.c_str()));
		}
		else if (Request.rfind("press ", 0) == 0)
		{
			// A virtual input of a script with its action bits (swc/items.lua SendVirtualInput: the quick slot keys), in the
			// next input entry (K3: ASwcPlayerController::PressVirtualBits).
			TArray<FString> Parts;
			FString(UTF8_TO_TCHAR(Request.c_str())).ParseIntoArray(Parts, TEXT(" "));
			TArray<uint32> Bits;
			for (int32 Index = 1; Index < Parts.Num(); ++Index)
			{
				Bits.Add(static_cast<uint32>(FCString::Atoi(*Parts[Index])));
			}
			ASwcPlayerController* Player = GetGameInstance() != nullptr ? Cast<ASwcPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()) : nullptr;
			if (Player != nullptr && Bits.Num() > 0)
			{
				Player->PressVirtualBits(Bits);
			}
			UE_LOG(LogSwcUi, Log, TEXT("ui: a virtual input of a script (%hs): %s"), Request.c_str(), Player != nullptr ? TEXT("pressed") : TEXT("no player"));
		}
		else
		{
			HandleItemRequest(UTF8_TO_TCHAR(Request.c_str()), Backend);
		}
	}
}

void USwcUiSubsystem::UpdateMenu(USwcBackendSubsystem* Backend)
{
	UpdatePreview(Backend);
	if (bMenuLogin)
	{
		// The login of PLAY (stage F5): the connection states of the table of landingscreenv2.luac (3 QueueGameLogin while the
		// login queue request runs, 10 WaitingForREPConnection after the token, 14 InGame), or the error of the request.
		const FString& Error = Backend->GetEnterError();
		if (!Error.IsEmpty())
		{
			bMenuLogin = false;
			SentMenuConnection = 0;
			Runtime->call("HostMenuLoginFailed", {FArg::of(std::string("@mm_loginservices_UnableToConnectError"))});
			UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: the login failed (%s): the landing screen shows the error"), *Error);
			return;
		}
		const int32 Status = Backend->HasConnection() ? 10 : 3;
		if (Status != SentMenuConnection)
		{
			SentMenuConnection = Status;
			Runtime->call("HostMenuConnection", {FArg::of(static_cast<double>(Status))});
			if (Status == 10)
			{
				// The token came: the menu ends, the loading screen of the world shows until the possession of the character.
				bMenuLogin = false;
				EndPreview();
				Runtime->call("HostMenuEnd");
				Runtime->call("HostLoading", {FArg::of(true), FArg::of(std::string(WorldLevelName)), FArg::of(std::string("eLoadingScreenIds_LEVEL1"))});
				bStartLoading = true;
				UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: the login token came; the loading screen of the world shows"));
			}
		}
		return;
	}
	// The error of a world connection that ended with no leave (stage F6): the landing screen shows MainMenu.LastError only more
	// than 5 s after its init (landingscreenv2.luac), so the error goes 5.5 s after the menu start.
	if (bMenuStarted && !MenuError.IsEmpty() && FPlatformTime::Seconds() >= MenuErrorAt)
	{
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: the world connection ended (%s): the landing screen shows the error"), *MenuError);
		MenuError.Reset();
		Runtime->call("HostMenuLoginFailed", {FArg::of(std::string("@mm_connectionfailed"))});
	}
	if (bMenuStarted || !Backend->IsFrontEnd() || Backend->GetAccountSession() == nullptr)
	{
		return;
	}
	// The scene first, under the start loading screen: the preview actor, its scene record, and the menu mode of the environment.
	// The landing screen starts when the meshes of the scene are placed (an asynchronous load of about 10 s; else the landing
	// showed over an empty view, run f6-return6) and after 0.5 s of frames shorter than 0.25 s: the first frames of the
	// environment of a new level are long (the sky mesh, the local lights: one frame of 10 s after a return to the menu, run
	// f6-return3; a limit of 0.1 s held it for 30 s while the clients of other tracks used the shared machine, run f6-return5).
	// At most 30 s after the spawn.
	const double Now = FPlatformTime::Seconds();
	if (MenuSpawnTime <= 0.0)
	{
		SpawnPreview();
		MenuSpawnTime = Now;
		MenuSmoothSince = -1.0;
		return;
	}
	if (Now - MenuSpawnTime < 30.0)
	{
		if (FApp::GetDeltaTime() > 0.25 || (Preview.IsValid() && !Preview->IsScenePlaced()))
		{
			MenuSmoothSince = -1.0;
			return;
		}
		MenuSmoothSince = MenuSmoothSince < 0.0 ? Now : MenuSmoothSince;
		if (Now - MenuSmoothSince < 0.5)
		{
			return;
		}
	}
	UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: the menu scene is ready after %.1f s"), Now - MenuSpawnTime);
	bMenuStarted = true;
	MenuError = Backend->GetEnterError();
	MenuErrorAt = FPlatformTime::Seconds() + 5.5;
	const FSwcAccountSession& Session = *Backend->GetAccountSession();
	// The loading screen of the start ends: the landing screen of the game shows (the native load of the main menu).
	bStartLoading = false;
	Runtime->call("HostLoading", {FArg::of(false)});
	if (!LastSelected.IsEmpty())
	{
		Runtime->call("HostMenuSelected", {FArg::of(Utf8(LastSelected))});
	}
	Runtime->call("HostMenuStart");
	for (int32 Index = 0; Index < Session.Regions.Num(); ++Index)
	{
		const FSwcRegionInfo& Region = Session.Regions[Index];
		Runtime->call("HostMenuRegion", {FArg::of(Utf8(Region.RegionId)), FArg::of(Utf8(Region.Name)), FArg::of(Utf8(Region.AwsRegion)),
										  FArg::of(Index == 0)});
	}
	// The login path is complete: the account token and the entitlement list came (FE9: the account event, not the UI load).
	Runtime->call("HostMenuAccount", {FArg::of(true), FArg::of(true)});
	// The menu scene is stage F4: no level loads now, thus its load is complete (the landing screen plays its queued camera
	// sequence at this event).
	Runtime->call("HostMenuLevelLoaded");
	UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: the landing screen (%d character(s), %d world(s), %d region(s))"), Session.Characters.Num(),
		Session.Worlds.Num(), Session.Regions.Num());
}

void USwcUiSubsystem::SpawnPreview()
{
	// The character of the menu (stage F3): its actor at the place of the Player Entity of the NightHaven scene (game 3031.3987,
	// 2047.3601, 84.0722 m; the scene itself is stage F4), and its camera as the view of the player.
	if (UWorld* World = GEngine != nullptr && GEngine->GameViewport != nullptr ? GEngine->GameViewport->GetWorld() : nullptr)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASwcMenuPreview* Actor = World->SpawnActor<ASwcMenuPreview>(FVector(303139.87, -204736.01, 8407.22), FRotator::ZeroRotator, Parameters);
		Preview = Actor;
		if (Actor != nullptr)
		{
			// The scene record (stage F4): the Player Entity, MainCamera, and the camera sequences of the NightHaven scene.
			FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
			Content = Content.IsEmpty() ? FPaths::Combine(WorkDir(), TEXT("content")) : Content;
			Actor->LoadScene(FPaths::Combine(Content, TEXT("frontend/nighthaven.json")));
			MenuClips = FindMenuClips(FPaths::Combine(Content, TEXT("player/imported.json")));
		}
		if (APlayerController* Controller = World->GetFirstPlayerController(); Controller != nullptr && Actor != nullptr)
		{
			Controller->SetViewTarget(Actor);
		}
	}
}

void USwcUiSubsystem::SendMenuList(USwcBackendSubsystem* Backend)
{
	const FSwcAccountSession* Session = Backend->GetAccountSession();
	if (Session == nullptr)
	{
		return;
	}
	Runtime->call("HostMenuListBegin");
	for (const FSwcWorldInfo& World : Session->Worlds)
	{
		Runtime->call("HostMenuWorld", {FArg::of(Utf8(World.WorldId)), FArg::of(Utf8(World.Name)), FArg::of(Utf8(World.PublicName)),
										 FArg::of(Utf8(World.WorldSet)), FArg::of(Utf8(World.Status)), FArg::of(Utf8(World.Type)),
										 FArg::of(Utf8(World.Version)), FArg::of(static_cast<double>(World.PublicStatusCode)),
										 FArg::of(static_cast<double>(World.MaxAccountCharacters)), FArg::of(World.bIsFull),
										 FArg::of(World.bIsRecommended), FArg::of(Utf8(World.TransferToRegion)),
										 FArg::of(static_cast<double>(World.QueueSize)), FArg::of(static_cast<double>(World.QueueWaitTimeSec)),
										 FArg::of(Utf8(World.PopulationStatus))});
	}
	const FDateTime Now = FDateTime::UtcNow();
	for (const FSwcCharacterInfo& Character : Session->Characters)
	{
		// The seconds since the last change of the row (the last played character of GetLastPlayedCharacterIndex); 0: not known.
		FDateTime Modified;
		double Elapsed = 0.0;
		if (FDateTime::ParseIso8601(*Character.ModifiedDate, Modified))
		{
			Elapsed = FMath::Max(1.0, (Now - Modified).GetTotalSeconds());
		}
		const bool bMenu = Character.MenuStatus == swc::menu::Status::Ok;
		const swc::menu::FDocument& Menu = Character.Menu;
		Runtime->call("HostMenuCharacter", {FArg::of(Utf8(Character.CharacterId)), FArg::of(Utf8(Character.PersonaId)),
											 FArg::of(Utf8(Character.WorldId)), FArg::of(Utf8(Character.Name)),
											 FArg::of(bMenu ? static_cast<double>(Menu.CurrentLevel) : 0.0), FArg::of(bMenu ? Menu.Currency : 0.0),
											 FArg::of(Menu.Position[0]), FArg::of(Menu.Position[1]), FArg::of(Menu.Position[2]), FArg::of(Elapsed),
											 FArg::of(Character.bFtueCompleted), FArg::of(Character.bMustRename), FArg::of(Character.bNeedsTransfer),
											 FArg::of(Character.bIsFreshStart), FArg::of(Character.bIsNameLatent)});
	}
	for (const FSwcNameReservation& Name : Session->NameReservations)
	{
		Runtime->call("HostMenuName", {FArg::of(Utf8(Name.Name)), FArg::of(Utf8(Name.WorldId)), FArg::of(Utf8(Name.Region))});
	}
	Runtime->call("HostMenuListEnd", {FArg::of(static_cast<double>(Session->MaxChannelCharacters))});
	UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: the character list to the landing screen (%d character(s))"), Session->Characters.Num());
}

void USwcUiSubsystem::UpdatePreview(USwcBackendSubsystem* Backend)
{
	if (!Preview.IsValid())
	{
		return;
	}
	if (Widget.IsValid())
	{
		const FVector2f Size = Widget->GetLastSize();
		Preview->UpdateFov(Size.Y > 0.0f ? Size.X / Size.Y : 0.0f);
	}
	if (!bPreviewDirty)
	{
		return;
	}
	const FSwcPlayerLooks* Looks = Backend->GetPlayerLooks();
	const FSwcAccountSession* Session = Backend->GetAccountSession();
	if (Looks == nullptr || Session == nullptr)
	{
		return;
	}
	bPreviewDirty = false;
	const FSwcCharacterInfo* Character = Session->Characters.FindByPredicate(
		[this](const FSwcCharacterInfo& Info) { return Info.CharacterId == PreviewCharacter; });
	if (Character == nullptr || Character->MenuStatus != swc::menu::Status::Ok)
	{
		// No character or no valid menu document: no body (no stale parts of the character before).
		Preview->Clear();
		BuiltCharacter = PreviewCharacter;
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: no body for %s (%hs)"), *PreviewCharacter,
			Character != nullptr ? swc::menu::StatusText(Character->MenuStatus) : "not in the list");
		return;
	}
	// The appearance names and the armor appearance ids of the menu document (the keys hat, shirt, gloves, pants, boots are the
	// paperdoll slots 0 to 4; an other key is no part of the menu look: research frontend-native.md FE5).
	TArray<FString> Names;
	for (const std::string& Name : Character->Menu.Appearance)
	{
		Names.Add(UTF8_TO_TCHAR(Name.c_str()));
	}
	static const char* const Keys[] = {"hat", "shirt", "gloves", "pants", "boots"};
	TMap<uint32, uint32> Armor;
	TArray<FString> Other;
	for (const swc::menu::FEquipment& Entry : Character->Menu.Equipment)
	{
		int32 Slot = INDEX_NONE;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Keys); ++Index)
		{
			Slot = Entry.Slot == Keys[Index] ? Index : Slot;
		}
		if (Slot == INDEX_NONE)
		{
			Other.Add(UTF8_TO_TCHAR(Entry.Slot.c_str()));
			continue;
		}
		if (!Entry.ItemName.empty() || (Entry.bHasDyes && (Entry.Dyes[0] | Entry.Dyes[1] | Entry.Dyes[2] | Entry.Dyes[3]) != 0))
		{
			UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: %hs: an item name or a dye that the menu look does not apply yet"), Entry.Slot.c_str());
		}
		Armor.Add(static_cast<uint32>(Slot), Entry.AppearanceId);
	}
	const FSwcPlayerLook Look = Looks->ResolveMenu(Names, Armor);
	for (const FString& Note : Look.Notes)
	{
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: look: %s"), *Note);
	}
	if (Other.Num() > 0)
	{
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: look: map keys with no part of the menu look: %s"), *FString::Join(Other, TEXT(", ")));
	}
	// The appearance preflight of the menu (AST-215): each part of the selected character is imported, or a clear warning.
	FString Preflight;
	if (Looks->Preflight(Look, Preflight))
	{
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: appearance preflight (menu) of %s: %s"), *PreviewCharacter, *Preflight);
	}
	else
	{
		UE_LOG(LogSwcUi, Warning, TEXT("ui: the front end: appearance preflight (menu) of %s: %s"), *PreviewCharacter, *Preflight);
	}
	const int32 Wanted = Looks->WriteWanted(Look);
	UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: the look of %s: %s, %d part(s), %d armor row(s), %d new wanted part(s)"), *PreviewCharacter,
		Look.bFemale ? TEXT("female") : TEXT("male"), Look.Parts.Num(), Armor.Num(), Wanted);
	// The idle of the Player Entity: the menu clip unarmed_idle_loadingscreen (SimpleAnimationComponent; tools/convert-frontend.sh
	// clips), else the idle of the world.
	const FString& MenuClip = MenuClips[Look.bFemale ? 1 : 0];
	Preview->ShowLook(Look, Looks->BaseMesh(Look.bFemale), MenuClip.IsEmpty() ? Looks->IdleAnimation(Look.bFemale) : MenuClip);
	BuiltCharacter = PreviewCharacter;
}

TArray<FString> USwcUiSubsystem::FindMenuClips(const FString& ImportedPath)
{
	// imported.json: the assets of the clip packs "clips|male|menu" and "clips|female|menu" (AnimSequence object paths).
	TArray<FString> Out = {FString(), FString()};
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	const TSharedPtr<FJsonObject>* Assets = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *ImportedPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc)
		|| !Doc.IsValid() || !Doc->TryGetObjectField(TEXT("assets"), Assets))
	{
		return Out;
	}
	for (int32 Gender = 0; Gender < 2; ++Gender)
	{
		const TSharedPtr<FJsonObject>* Pack = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Clips = nullptr;
		if ((*Assets)->TryGetObjectField(Gender == 0 ? TEXT("clips|male|menu") : TEXT("clips|female|menu"), Pack)
			&& (*Pack)->TryGetArrayField(TEXT("AnimSequence"), Clips))
		{
			// The pack has the one menu clip (the import names its asset after the pack), else the clip of that name.
			for (const TSharedPtr<FJsonValue>& Clip : *Clips)
			{
				if (Clips->Num() == 1 || Clip->AsString().Contains(TEXT("unarmed_idle_loadingscreen")))
				{
					Out[Gender] = Clip->AsString();
				}
			}
		}
	}
	UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: the menu clip: male %s, female %s"), Out[0].IsEmpty() ? TEXT("no") : TEXT("yes"),
		Out[1].IsEmpty() ? TEXT("no") : TEXT("yes"));
	return Out;
}

void USwcUiSubsystem::EndPreview()
{
	if (Preview.IsValid())
	{
		Preview->Destroy();
	}
	Preview.Reset();
	bPreviewDirty = false;
}

/** The requests of the landing screen (swc/frontend.lua): "menu list" (GetLoginInfoLists: a refresh of the list), "menu preview
 *  ID" (SetVisibleCharacter), "menu show 1|0" (the preview character shows or hides), "menu login ID TYPE" (RequestLogin),
 *  "menu quit DESTINATION" (RequestDisconnect: the desktop, or the return to the character selection), "menu delete ID"
 *  (DeactivateCharacter, stage F8), "menu loading" and "menu intro" (later stages). */
void USwcUiSubsystem::HandleMenuRequest(const FString& Request, USwcBackendSubsystem* Backend)
{
	TArray<FString> Parts;
	Request.ParseIntoArray(Parts, TEXT(" "));
	const FString Kind = Parts.IsValidIndex(1) ? Parts[1] : FString();
	if (Kind == TEXT("list"))
	{
		// One refresh at a time: a request during a refresh gets the next list too.
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: menu list (a refresh of getlogininfo)%s"), bMenuListPending ? TEXT(", after the one that runs") : TEXT(""));
		if (bMenuListPending)
		{
			bMenuListAgain = true;
			return;
		}
		bMenuListPending = true;
		TWeakObjectPtr<USwcUiSubsystem> Self(this);
		TWeakObjectPtr<USwcBackendSubsystem> WeakBackend(Backend);
		Backend->RefreshCharacterList([Self, WeakBackend](bool bSucceeded)
			{
				if (!Self.IsValid() || !WeakBackend.IsValid() || !Self->Runtime)
				{
					return;
				}
				Self->bMenuListPending = false;
				// A failed refresh keeps the list of the session (the script waits for its three flags).
				Self->SendMenuList(WeakBackend.Get());
				if (Self->bMenuListAgain)
				{
					Self->bMenuListAgain = false;
					Self->HandleMenuRequest(TEXT("menu list"), WeakBackend.Get());
				}
			});
	}
	else if (Kind == TEXT("preview"))
	{
		// SetVisibleCharacter: the body of the selected character (UpdatePreview, when the look tables are there). The landing screen
		// selects the same character again each 30 s (and after each list): the body of that character stays.
		const FString Id = Parts.IsValidIndex(2) ? Parts[2] : FString();
		LastSelected = Id.IsEmpty() ? LastSelected : Id;
		if (Id == BuiltCharacter && Id == PreviewCharacter && !bPreviewDirty)
		{
			return;
		}
		PreviewCharacter = Id;
		bPreviewDirty = true;
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: %s"), *Request);
	}
	else if (Kind == TEXT("sequence") || Kind == TEXT("sequence-stop"))
	{
		// The camera sequences of the landing screen (CinematicRequestBus): the camera tracks of MainCamera (stage F4).
		const FString Name = Parts.IsValidIndex(2) ? Parts[2] : FString();
		bool bPlayed = false;
		if (Preview.IsValid())
		{
			if (Kind == TEXT("sequence"))
			{
				bPlayed = Preview->PlaySequence(Name);
			}
			else
			{
				Preview->StopSequence(Name);
			}
		}
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: %s%s"), *Request, Kind == TEXT("sequence") && !bPlayed ? TEXT(" (no camera track)") : TEXT(""));
	}
	else if (Kind == TEXT("show"))
	{
		if (Preview.IsValid())
		{
			Preview->SetBodyVisible(Parts.IsValidIndex(2) && Parts[2] == TEXT("1"));
		}
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: %s"), *Request);
	}
	else if (Kind == TEXT("delete"))
	{
		// "menu delete ID": DeactivateCharacter of the landing screen after its hold-to-accept popup (stage F8). The id must be a
		// character of the current account list (the backend also refuses a character of an other account: 404). The result goes
		// to the script (DeactivateCharacterResult), which asks for its list again.
		const FString CharacterId = Parts.IsValidIndex(2) ? Parts[2] : FString();
		const FSwcAccountSession* Session = Backend->GetAccountSession();
		const bool bKnown = Session != nullptr
			&& Session->Characters.ContainsByPredicate([&CharacterId](const FSwcCharacterInfo& Character) { return Character.CharacterId == CharacterId; });
		if (!bKnown || bMenuLogin || Backend->IsEnteringWorld() || Backend->HasConnection())
		{
			UE_LOG(LogSwcUi, Warning, TEXT("ui: the front end: no delete of %s (%s)"), *CharacterId,
				!bKnown ? TEXT("not in the account list") : TEXT("a login runs"));
			Runtime->call("HostMenuDeleteResult", {FArg::of(Utf8(CharacterId)), FArg::of(false)});
			return;
		}
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: the delete of %s"), *CharacterId);
		TWeakObjectPtr<USwcUiSubsystem> Self(this);
		Backend->DeleteCharacter(CharacterId, [Self, CharacterId, Generation = SeenMenuReturns](bool bDeleted)
			{
				USwcUiSubsystem* Ui = Self.Get();
				if (Ui == nullptr || !Ui->Runtime.IsValid() || Ui->SeenMenuReturns != Generation)
				{
					return;   // the screens started again in the meantime (a return to the menu): the old result is not shown
				}
				Ui->Runtime->call("HostMenuDeleteResult", {FArg::of(Utf8(CharacterId)), FArg::of(bDeleted)});
			});
	}
	else if (Kind == TEXT("login"))
	{
		// "menu login ID TYPE": the PLAY of the landing screen. The id must be a character of the current account list; one login
		// at a time. The world type (OpenWorld or FTUE) is not in the login queue request (the native RequestLogin sends an empty
		// world id: research character-creation.md 4.1).
		const FString CharacterId = Parts.IsValidIndex(2) ? Parts[2] : FString();
		const FSwcAccountSession* Session = Backend->GetAccountSession();
		const bool bKnown = Session != nullptr
			&& Session->Characters.ContainsByPredicate([&CharacterId](const FSwcCharacterInfo& Character) { return Character.CharacterId == CharacterId; });
		if (bMenuLogin || Backend->IsEnteringWorld() || Backend->HasConnection())
		{
			UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: a login runs; PLAY again is ignored"));
		}
		else if (!bKnown)
		{
			UE_LOG(LogSwcUi, Warning, TEXT("ui: the front end: the character %s is not in the account list: no login"), *CharacterId);
			Runtime->call("HostMenuLoginFailed", {FArg::of(std::string("@mm_loginservices_UnableToConnectError"))});
		}
		else
		{
			bMenuLogin = true;
			SentMenuConnection = 0;
			UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: PLAY with the character %s (%s)"), *CharacterId,
				Parts.IsValidIndex(3) ? *Parts[3] : TEXT(""));
			Backend->EnterWorld(CharacterId);
		}
	}
	else if (Kind == TEXT("quit"))
	{
		// RequestDisconnect(destination): eExitGameDestination_Desktop 0 quits; MainMenu 1 and AttractScreen 2 (this client has
		// no attract screen) go back to the character selection from the world (stage F6).
		const int32 Destination = Parts.IsValidIndex(2) ? FCString::Atoi(*Parts[2]) : 0;
		if (Destination != 0 && Backend != nullptr && Backend->HasConnection())
		{
			UE_LOG(LogSwcUi, Log, TEXT("ui: the return to the character selection (the destination %d)"), Destination);
			Runtime->call("HostLoading", {FArg::of(true), FArg::of(std::string(WorldLevelName)), FArg::of(std::string("eLoadingScreenIds_LEVEL1"))});
			Backend->ReturnToMenu();
			return;
		}
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: quit to the desktop"));
		FPlatformMisc::RequestExit(false, TEXT("the front end: quit"));
	}
	else
	{
		UE_LOG(LogSwcUi, Log, TEXT("ui: the front end: %s (not in this stage)"), *Request);
	}
}

/** The key requests of the options screen (swc/keys.lua): "rebind MAP ACTION KEY" ("-": no key), "rebind-save",
 *  "rebind-reset", "keybinding 1|0" (the key page listens for a key: the mouse buttons and the wheel go as keys). */
void USwcUiSubsystem::HandleKeyRequest(const FString& Request)
{
	USwcKeyBindings* Keys = GetGameInstance()->GetSubsystem<USwcKeyBindings>();
	TArray<FString> Parts;
	Request.ParseIntoArray(Parts, TEXT(" "));
	if (Parts[0] == TEXT("keybinding"))
	{
		bKeybinding = Parts.Num() > 1 && Parts[1] == TEXT("1");
		UE_LOG(LogSwcUi, Log, TEXT("ui: the key page %s"), bKeybinding ? TEXT("listens for a key") : TEXT("ends its listen"));
	}
	else if (Keys == nullptr)
	{
		UE_LOG(LogSwcUi, Warning, TEXT("ui: no key table: %s"), *Request);
	}
	else if (Parts[0] == TEXT("rebind") && Parts.Num() == 4)
	{
		Keys->Rebind(Parts[1], Parts[2], Parts[3] == TEXT("-") ? FString(TEXT(" ")) : Parts[3]);
	}
	else if (Parts[0] == TEXT("rebind-save"))
	{
		Keys->Save();
	}
	else if (Parts[0] == TEXT("rebind-reset"))
	{
		Keys->Reset();
	}
}

// -------------------------------------------------------------------------------------------------------- harness input

void USwcUiSubsystem::LoadDrive(const FString& Path)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		UE_LOG(LogSwcUi, Warning, TEXT("ui drive: the script %s did not load"), *Path);
		return;
	}
	for (const FString& Raw : Lines)
	{
		FString Line = Raw.TrimStartAndEnd();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			continue;
		}
		TArray<FString> Parts;
		Line.ParseIntoArrayWS(Parts);
		if (Parts.Num() < 2)
		{
			continue;
		}
		FDriveStep Step;
		Step.Time = FCString::Atod(*Parts[0]);
		Step.Command = Parts[1].ToLower();
		for (int32 I = 2; I < Parts.Num(); ++I)
		{
			Step.Args.Add(Parts[I]);
		}
		if (Step.Command == TEXT("log"))
		{
			Step.Args = {Line.Mid(Line.Find(Parts[1]) + Parts[1].Len()).TrimStartAndEnd()};
		}
		Drive.Add(MoveTemp(Step));
	}
	Drive.StableSort([](const FDriveStep& A, const FDriveStep& B) { return A.Time < B.Time; });
	UE_LOG(LogSwcUi, Log, TEXT("ui drive: %d step(s) from %s (from the spawn of the player or the error of the login path)"), Drive.Num(), *Path);
}

bool USwcUiSubsystem::DrivePointAt(const FString& Element, float FractionX, float FractionY)
{
	std::string Result;
	Runtime->call("HostPointOf", {FArg::of(Utf8(Element)), FArg::of(static_cast<double>(FractionX)), FArg::of(static_cast<double>(FractionY))}, &Result);
	float X = 0.0f;
	float Y = 0.0f;
	if (Result.empty() || sscanf(Result.c_str(), "%f %f", &X, &Y) != 2)
	{
		UE_LOG(LogSwcUi, Warning, TEXT("ui drive: no shown element %s"), *Element);
		return false;
	}
	UiPointerLocal(FVector2f(X, Y));
	if (Widget.IsValid())
	{
		Widget->SetPointerMarker(true, FVector2f(X, Y));
	}
	return true;
}

void USwcUiSubsystem::DriveKey(const FString& Name, bool bDown)
{
	const FKey Key = FSwcUiInputProcessor::KeyOf(Name);
	if (!Key.IsValid() || !FSlateApplication::IsInitialized() || !InputProcessor.IsValid())
	{
		UiKey(Name, bDown);
		return;
	}
	// The same path as a key of the keyboard: the input pre-processor of the UI, then the game viewport (the player
	// controller) when the UI does not keep the key.
	FSlateApplication& Slate = FSlateApplication::Get();
	const FKeyEvent Event(Key, Slate.GetModifierKeys(), 0, false, 0, 0);
	if (bDown)
	{
		Slate.ProcessKeyDownEvent(Event);
	}
	else
	{
		Slate.ProcessKeyUpEvent(Event);
	}
}

void USwcUiSubsystem::ReleaseDriveInput()
{
	for (const FString& Key : DriveKeysUp)
	{
		DriveKey(Key, false);
	}
	DriveKeysUp.Reset();
	for (const FString& Key : DriveKeysDown)
	{
		DriveKey(Key, false);
	}
	DriveKeysDown.Reset();
	if (bDriveButtonDown)
	{
		UiButton(TEXT("left"), false);
		bDriveButtonDown = false;
	}
	DriveDrag.bActive = false;
	if (Widget.IsValid())
	{
		Widget->SetPointerMarker(false, FVector2f::ZeroVector);
	}
}

void USwcUiSubsystem::TickDrive()
{
	// The clock of the drive starts at the spawn of the player (the end of the loading screen of the start), as the
	// drive of the player controller (-SwcDrive) starts at the possession: the two scripts agree. A failed login path
	// starts it too (the error UI), and the start of the front end (the landing screen, stage F2).
	if (Drive.IsEmpty() || !((bWorldEntered && !bStartLoading) || bErrorShown || bMenuStarted))
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (DriveStart < 0.0)
	{
		DriveStart = Now;
		if (InputProcessor.IsValid())
		{
			InputProcessor->bLogKeys = true;
		}
	}
	const double Time = Now - DriveStart;
	// A "key" press lasts one frame; a click releases at the next frame.
	for (const FString& Key : DriveKeysUp)
	{
		DriveKey(Key, false);
	}
	DriveKeysUp.Reset();
	if (bDriveReleaseNext)
	{
		UiButton(TEXT("left"), false);
		bDriveButtonDown = false;
		bDriveReleaseNext = false;
	}
	if (DriveDrag.bActive)
	{
		const float T = DriveDrag.Duration > 0.0 ? FMath::Clamp(static_cast<float>((Time - DriveDrag.Start) / DriveDrag.Duration), 0.0f, 1.0f) : 1.0f;
		const FVector2f F = FMath::Lerp(DriveDrag.From, DriveDrag.To, T);
		DrivePointAt(DriveDrag.Element, F.X, F.Y);
		if (T >= 1.0f)
		{
			UiButton(TEXT("left"), false);
			bDriveButtonDown = false;
			DriveDrag.bActive = false;
		}
	}
	while (DriveNext < Drive.Num() && Drive[DriveNext].Time <= Time)
	{
		// waitfor ELEMENT [SECONDS]: the clock of the drive stays at this step until a shown element has the name.
		if (Drive[DriveNext].Command == TEXT("waitfor") && Drive[DriveNext].Args.Num() > 0)
		{
			const FDriveStep& Wait = Drive[DriveNext];
			std::string Point;
			Runtime->call("HostPointOf", {FArg::of(Utf8(Wait.Args[0])), FArg::of(0.5), FArg::of(0.5)}, &Point);
			if (DriveWaitStart < 0.0)
			{
				DriveWaitStart = Now;
			}
			const double Limit = Wait.Args.IsValidIndex(1) ? FCString::Atod(*Wait.Args[1]) : 30.0;
			if (Point.empty() && Now - DriveWaitStart < Limit)
			{
				DriveStart = Now - Wait.Time;
				return;
			}
			UE_LOG(LogSwcUi, Log, TEXT("ui drive: %.2f s waitfor %s: %s after %.1f s"), Wait.Time, *Wait.Args[0],
				Point.empty() ? TEXT("not shown (the limit)") : TEXT("shown"), Now - DriveWaitStart);
			// The clock goes on from the time of the wait (also when the element shows at once after a pause of the drive, as
			// the level load of a return to the menu): the next steps start in the next frame with that clock.
			DriveStart = Now - Wait.Time;
			DriveWaitStart = -1.0;
			++DriveNext;
			return;
		}
		const FDriveStep& Step = Drive[DriveNext++];
		const auto Arg = [&Step](int32 I, float Default) { return Step.Args.IsValidIndex(I) ? FCString::Atof(*Step.Args[I]) : Default; };
		UE_LOG(LogSwcUi, Log, TEXT("ui drive: %.2f s %s %s"), Time, *Step.Command, *FString::Join(Step.Args, TEXT(" ")));
		if (Step.Command == TEXT("key") && Step.Args.Num() > 0)
		{
			DriveKey(Step.Args[0], true);
			DriveKeysUp.Add(Step.Args[0]);
		}
		else if (Step.Command == TEXT("text") && Step.Args.Num() > 0)
		{
			// The text of the arguments (one space between them) into the text input that edits (the search of the crafting screen).
			CallInput({FArg::of(std::string("text")), FArg::of(Utf8(FString::Join(Step.Args, TEXT(" "))))});
		}
		else if (Step.Command == TEXT("keydown") && Step.Args.Num() > 0)
		{
			DriveKey(Step.Args[0], true);
			DriveKeysDown.Add(Step.Args[0]);
		}
		else if (Step.Command == TEXT("keyup") && Step.Args.Num() > 0)
		{
			DriveKey(Step.Args[0], false);
			DriveKeysDown.Remove(Step.Args[0]);
		}
		else if (Step.Command == TEXT("point") && Step.Args.Num() > 0)
		{
			DrivePointAt(Step.Args[0], Arg(1, 0.5f), Arg(2, 0.5f));
		}
		else if (Step.Command == TEXT("move"))
		{
			const FVector2f Size = Widget.IsValid() ? Widget->GetLastSize() : FVector2f(1920.0f, 1080.0f);
			const FVector2f P(Arg(0, 0.5f) * Size.X, Arg(1, 0.5f) * Size.Y);
			UiPointerLocal(P);
			if (Widget.IsValid())
			{
				Widget->SetPointerMarker(true, P);
			}
		}
		else if (Step.Command == TEXT("click"))
		{
			UiButton(TEXT("left"), true);
			bDriveButtonDown = true;
			bDriveReleaseNext = true;
		}
		else if (Step.Command == TEXT("press"))
		{
			UiButton(TEXT("left"), true);
			bDriveButtonDown = true;
		}
		else if (Step.Command == TEXT("release"))
		{
			UiButton(TEXT("left"), false);
			bDriveButtonDown = false;
		}
		else if (Step.Command == TEXT("drag") && Step.Args.Num() >= 6)
		{
			DriveDrag.Element = Step.Args[0];
			DriveDrag.From = FVector2f(Arg(1, 0.5f), Arg(2, 0.5f));
			DriveDrag.To = FVector2f(Arg(3, 0.5f), Arg(4, 0.5f));
			DriveDrag.Duration = Arg(5, 1.0f);
			DriveDrag.Start = Time;
			if (DrivePointAt(DriveDrag.Element, DriveDrag.From.X, DriveDrag.From.Y))
			{
				UiButton(TEXT("left"), true);
				bDriveButtonDown = true;
				DriveDrag.bActive = true;
			}
		}
		else if (Step.Command == TEXT("wheel"))
		{
			UiWheel(Arg(0, 1.0f));
		}
		else if (Step.Command == TEXT("log"))
		{
			UE_LOG(LogSwcUi, Log, TEXT("ui drive: mark: %s"), Step.Args.Num() > 0 ? *Step.Args[0] : TEXT(""));
		}
		else if (Step.Command == TEXT("draw") && Step.Args.Num() > 0)
		{
			// The draw list of this frame (REPORTS/draw-NAME.json; a software picture: swc_convert.uipreview).
			const FString File = FPaths::Combine(ReportDir, FString::Printf(TEXT("draw-%s.json"), *Step.Args[0]));
			FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(swc::ui::draw_list_json(DrawList).c_str())), *File,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogSwcUi, Log, TEXT("ui drive: the draw list of the frame: %s"), *File);
		}
		else if (Step.Command == TEXT("end"))
		{
			ReleaseDriveInput();
			UE_LOG(LogSwcUi, Log, TEXT("ui drive: the test input is released (no held key, no held button)"));
		}
		else
		{
			UE_LOG(LogSwcUi, Warning, TEXT("ui drive: unknown command %s"), *Step.Command);
		}
	}
}

void USwcUiSubsystem::FeedLocalPlayer()
{
	USwcBackendSubsystem* Backend = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	swc::net::RepClient::OwnEntity Own;
	if (Backend == nullptr || !Backend->GetLocalPlayer(Own))
	{
		return;
	}
	if (!bWorldEntered)
	{
		bWorldEntered = true;
		UE_LOG(LogSwcUi, Log, TEXT("ui: the world entry: the screen loader of the game starts"));
		Runtime->call("EnterWorld", {FArg::of(std::string(WorldLevelName))});
		Runtime->call("PlayerSpawned");
	}
	// The vitals: the stamina and mana states when the backend sent them, else the values of the vitals state.
	const float Stamina = Own.hasStamina ? Own.stamina : Own.vitalsStamina;
	const float StaminaMax = Own.hasStamina ? Own.staminaMax : Own.vitalsStaminaMax;
	const float Mana = Own.hasMana ? Own.mana : Own.vitalsMana;
	const float ManaMax = Own.hasMana ? Own.manaMax : Own.vitalsManaMax;
	Runtime->call("HostVitals", {FArg::of(static_cast<double>(Own.health)), FArg::of(static_cast<double>(Own.healthMax)),
								  FArg::of(static_cast<double>(Stamina)), FArg::of(static_cast<double>(StaminaMax)),
								  FArg::of(static_cast<double>(Mana)), FArg::of(static_cast<double>(ManaMax)), FArg::of(Own.dead)});

	// The life of the character (stage P2.10): each death notice (message 3950) with its time to the respawn screen, and
	// the loading screen of the client (from the respawn request 1938 or message 3951 to message 3954).
	if (Own.deathNotices != SentDeathNotices)
	{
		if (Own.deathNotices > SentDeathNotices)
		{
			Runtime->call("HostDeathNotice", {FArg::of(static_cast<double>(Own.respawnScreenTime))});
			UE_LOG(LogSwcUi, Log, TEXT("ui: a death notice: the respawn screen in %.1f s"), Own.respawnScreenTime);
		}
		SentDeathNotices = Own.deathNotices;
	}
	// The loading screen of a respawn or a travel: from the request (or message 3951) while the flag lasts, while the cells
	// of the new place load, and at least 1 s from a respawn request (the flag of a respawn can end in the same frame).
	const bool bWantLoading = !bStartLoading && (Own.loadingScreen || Backend->IsWaitingForCells() || FPlatformTime::Seconds() < LoadingHoldUntil);
	if (bWantLoading != bSentLoadingScreen)
	{
		bSentLoadingScreen = bWantLoading;
		Runtime->call("HostLoading", {FArg::of(bSentLoadingScreen), FArg::of(std::string(WorldLevelName)),
										FArg::of(std::string(bLoadingForDeath ? "eLoadingScreenIds_DEATH" : "eLoadingScreenIds_LEVEL1"))});
		UE_LOG(LogSwcUi, Log, TEXT("ui: the loading screen of %s %s"), bLoadingForDeath ? TEXT("a respawn") : TEXT("a travel"),
			bSentLoadingScreen ? TEXT("shows") : TEXT("ends"));
		if (bSentLoadingScreen && !bLoadingForDeath)
		{
			Runtime->call("HostTravelStarts", {});
		}
		if (!bSentLoadingScreen)
		{
			bLoadingForDeath = false;
		}
	}

	// The bag, the paperdoll items (the item index of the wire in the item catalog of the backend copy), the level, the coin.
	FeedItems(Own);
	FeedOwnerStates(Own);
	CraftingScreen.Feed(*Runtime, Own);
	Quests.Feed(*Runtime, Own, *Backend);
	// The place cues 2297 and 6416 of the points of interest (AST-241): from the feet of the character.
	if (UWorld* World = GetGameInstance()->GetWorld())
	{
		const APlayerController* Controller = World->GetFirstPlayerController();
		const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
		if (Pawn != nullptr)
		{
			Quests.FeedPlaces(SwcUnrealToEngine(Pawn->GetNavAgentLocation()), Own, *Backend, ContentDir, FPlatformTime::Seconds());
		}
	}
	FeedLoot(Own, Backend);
	FeedWorld(Own);
	int32 Active = -1;
	for (uint32 Slot = FirstWeaponSlot; Slot <= LastWeaponSlot; ++Slot)
	{
		if ((Own.activeMap >> Slot) & 1ull)
		{
			Active = static_cast<int32>(Slot);
			break;
		}
	}
	const bool bSheathed = Active >= 0 && ((Own.sheatheMap >> Active) & 1ull) != 0;
	if (Active != SentActiveSlot || bSheathed != bSentSheathed)
	{
		SentActiveSlot = Active;
		bSentSheathed = bSheathed;
		Runtime->call("HostActiveWeapon", {FArg::of(static_cast<double>(Active)), FArg::of(bSheathed)});
	}

	// The ability bars (one for each ability table) and the cooldowns of the server clock.
	for (const swc::net::RepClient::OwnEntity::AbilityBar& Bar : Own.abilityBars)
	{
		TArray<uint32> Abilities;
		for (int32 Place = 0; Place < 3; ++Place)
		{
			Abilities.Add(Place < static_cast<int32>(Bar.abilities.size()) ? Bar.abilities[Place] : 0u);
		}
		const TArray<uint32>* Sent = SentBars.Find(Bar.table);
		if (Sent == nullptr || *Sent != Abilities)
		{
			SentBars.Add(Bar.table, Abilities);
			Runtime->call("HostAbilityBar", {FArg::of(static_cast<double>(Bar.table)), FArg::of(static_cast<double>(Abilities[0])),
											  FArg::of(static_cast<double>(Abilities[1])), FArg::of(static_cast<double>(Abilities[2]))});
		}
	}
	const uint64 NowUs = Backend->GetServerTimeMicroseconds();
	TSet<uint32> Seen;
	for (const std::vector<swc::net::RepClient::OwnEntity::Cooldown>& Place : Own.cooldowns)
	{
		for (const swc::net::RepClient::OwnEntity::Cooldown& Cooldown : Place)
		{
			Seen.Add(Cooldown.ability);
			const uint64* Sent = SentCooldowns.Find(Cooldown.ability);
			if (Sent != nullptr && *Sent == Cooldown.endUs)
			{
				continue;
			}
			SentCooldowns.Add(Cooldown.ability, Cooldown.endUs);
			// An indefinite cooldown (all ones) shows as a long one.
			const double Left = Cooldown.endUs == ~0ull ? 3600.0 : (Cooldown.endUs > NowUs ? static_cast<double>(Cooldown.endUs - NowUs) / 1e6 : 0.0);
			Runtime->call("HostCooldown", {FArg::of(static_cast<double>(Cooldown.ability)), FArg::of(Left), FArg::of(0.0)});
		}
	}
	for (auto It = SentCooldowns.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			Runtime->call("HostCooldown", {FArg::of(static_cast<double>(It.Key())), FArg::of(0.0), FArg::of(0.0)});
			It.RemoveCurrent();
		}
	}
	// The cooldowns of the consumables (field 3 of the cooldown state: the CRC-32 of the lower-case item id and the end): the
	// quick slots show their fill and count (stage P3.H).
	TSet<uint32> SeenItems;
	for (const std::pair<std::uint32_t, std::uint64_t>& Item : Own.itemCooldowns)
	{
		SeenItems.Add(Item.first);
		const uint64* Sent = SentItemCooldowns.Find(Item.first);
		if (Sent != nullptr && *Sent == Item.second)
		{
			continue;
		}
		SentItemCooldowns.Add(Item.first, Item.second);
		const double Left = Item.second > NowUs ? static_cast<double>(Item.second - NowUs) / 1e6 : 0.0;
		UE_LOG(LogSwcUi, Log, TEXT("ui: the cooldown of the item %u: %.2f s"), Item.first, Left);
		Runtime->call("HostItemCooldown", {FArg::of(static_cast<double>(Item.first)), FArg::of(Left)});
	}
	for (auto It = SentItemCooldowns.CreateIterator(); It; ++It)
	{
		if (!SeenItems.Contains(It.Key()))
		{
			Runtime->call("HostItemCooldown", {FArg::of(static_cast<double>(It.Key())), FArg::of(0.0)});
			It.RemoveCurrent();
		}
	}

	// The compass: the camera yaw (0 north, positive west) and the position of the entity (engine meters).
	const float Yaw = Backend->GetCameraYawDegrees();
	if (!FMath::IsNearlyEqual(Yaw, SentYaw, 0.01f))
	{
		SentYaw = Yaw;
		Runtime->call("HostView", {FArg::of(static_cast<double>(Yaw)), FArg::of(Own.position.x), FArg::of(Own.position.y),
									FArg::of(Own.position.z)});
	}
	// The aim of the player (the camera state AimCamera): the bow reticle takes the spread of its bars from it (swc/reticle.lua).
	const APlayerController* Player = GetGameInstance() != nullptr ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	const ASwcCameraManager* Camera = Player != nullptr ? Cast<ASwcCameraManager>(Player->PlayerCameraManager) : nullptr;
	const bool bAim = Camera != nullptr && Camera->GetStateName().StartsWith(TEXT("Aim"));
	if (bAim != bSentAim)
	{
		bSentAim = bAim;
		Runtime->call("HostAim", {FArg::of(bAim)});
	}
	Runtime->call("HostCommit");
}

void USwcUiSubsystem::FeedNumbers(const swc::net::RepClient::OwnEntity& Own, USwcCreatureSubsystem* Creatures, const FVector& Eye, const FRotator& View)
{
	UWorld* World = GetGameInstance()->GetWorld();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	const FVector Feet = Pawn != nullptr ? Pawn->GetNavAgentLocation() : Eye;
	auto Send = [this](const TCHAR* Kind, uint32 Interest, const FVector& Point, uint32 Flags, double DistanceSq, uint32 Vitals, const FString& Entries)
	{
		const FString Line = FString::Printf(TEXT("%s\t%u\t%.1f\t%.1f\t%.1f\t%u\t%.2f\t%s\t%s"), Kind, Interest, Point.X, Point.Y, Point.Z, Flags, DistanceSq,
			Vitals != 0 ? *FString::Printf(TEXT("%u"), Vitals) : TEXT(""), *Entries);
		Runtime->call("HostDamage", {FArg::of(Utf8(Line))});
	};
	// The damage numbers of the player (2071): the point at the creature, its vitals row (the weakness of defenses.json).
	TArray<FSwcDamageEvent> Events;
	const uint32 Newest = Creatures->GetDamageEvents(SentDamageSerial, Events);
	for (const FSwcDamageEvent& Event : Events)
	{
		FString Entries;
		for (const TPair<uint8, float>& Entry : Event.Entries)
		{
			Entries += FString::Printf(TEXT("%s%u:%.2f:1"), Entries.IsEmpty() ? TEXT("") : TEXT(","), Entry.Key, Entry.Value);
		}
		uint32 Vitals = 0;
		const auto Found = Creatures->GetTable().entities().find(Event.Interest);
		if (Found != Creatures->GetTable().entities().end())
		{
			Vitals = Found->second.vitalsId;
		}
		Send(TEXT("enemy"), Event.Interest, Event.Point, Event.Flags, FVector::DistSquared(Feet, Event.Point) / 10000.0, Vitals, Entries);
	}
	SentDamageSerial = Newest;
	// The numbers of the own character: 1.6 m above the feet and 0.5 m to the right of the view (the Player settings of
	// damagenumber.luac: ZOffsetBase 1.6, HorizontalOffsetBase 0.5).
	const FVector Right = FRotationMatrix(FRotator(0.0, View.Yaw, 0.0)).GetScaledAxis(EAxis::Y);
	const FVector OwnPoint = Feet + FVector(0.0, 0.0, 160.0) + Right * 50.0;
	if (SentOwnDamages == ~0u)
	{
		SentOwnDamages = Own.damageRevision;
		SentHeals = Own.healRevision;
	}
	const uint32 NewDamages = FMath::Min<uint32>(Own.damageRevision - SentOwnDamages, static_cast<uint32>(Own.damages.size()));
	for (std::size_t Index = Own.damages.size() - NewDamages; Index < Own.damages.size(); ++Index)
	{
		FString Entries;
		for (const swc::net::DamageTaken::Entry& Entry : Own.damages[Index].message.entries)
		{
			Entries += FString::Printf(TEXT("%s%u:%.2f:%.3f"), Entries.IsEmpty() ? TEXT("") : TEXT(","), Entry.type, Entry.amount, Entry.part > 0.0f ? Entry.part : 1.0f);
		}
		Send(TEXT("player"), 0, OwnPoint, Own.damages[Index].message.flags, 0.0, 0, Entries);
		// The hit indicator (swc/hitindicator.lua): the part of the maximum health, the place of the source, and the place of the feet of
		// the avatar (engine meters; the own entity place of the server and the place of HostView can be seconds old, and a creature
		// bites from 1.5 m).
		const swc::net::DamageTaken& Hit = Own.damages[Index].message;
		if ((Hit.flags & swc::net::DamageTaken::kHasPlace) != 0)
		{
			const swc::net::WorldPosition FeetEngine = SwcUnrealToEngine(Feet);
			Runtime->call("HostHit", {FArg::of(static_cast<double>(Hit.part)), FArg::of(static_cast<double>(Hit.from[0])),
				FArg::of(static_cast<double>(Hit.from[1])), FArg::of(static_cast<double>(Hit.from[2])),
				FArg::of(static_cast<double>(FeetEngine.x)), FArg::of(static_cast<double>(FeetEngine.y)),
				FArg::of(static_cast<double>(FeetEngine.z))});
		}
		else
		{
			Runtime->call("HostHit", {FArg::of(static_cast<double>(Hit.part))});
		}
	}
	SentOwnDamages = Own.damageRevision;
	const uint32 NewHeals = FMath::Min<uint32>(Own.healRevision - SentHeals, static_cast<uint32>(Own.heals.size()));
	for (std::size_t Index = Own.heals.size() - NewHeals; Index < Own.heals.size(); ++Index)
	{
		if (Own.heals[Index].healed == 0)
		{
			// A heal has no damage type: the Standard row (3) of the damage types; the script shows "+N" with no icon.
			Send(TEXT("heal"), 0, OwnPoint, 0, 0.0, 0, FString::Printf(TEXT("3:%.2f:1"), Own.heals[Index].amount));
		}
	}
	SentHeals = Own.healRevision;
}

void USwcUiSubsystem::FeedWorld(const swc::net::RepClient::OwnEntity& Own)
{
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	ULocalPlayer* LocalPlayer = Controller != nullptr ? Controller->GetLocalPlayer() : nullptr;
	UGameViewportClient* ViewportClient = World != nullptr ? World->GetGameViewport() : nullptr;
	USwcCreatureSubsystem* Creatures = World != nullptr ? World->GetSubsystem<USwcCreatureSubsystem>() : nullptr;
	if (LocalPlayer == nullptr || ViewportClient == nullptr || ViewportClient->Viewport == nullptr || Creatures == nullptr || !Widget.IsValid())
	{
		return;
	}
	if (!bStandInsOff)
	{
		// The original plates and numbers of the UI replace the simple stand-ins of the creature view.
		bStandInsOff = true;
		Creatures->SetUiPlates(true);
		USwcRangedView::SetUiHealNumbers(true);
		if (USwcObjectSubsystem* Objects = World->GetSubsystem<USwcObjectSubsystem>())
		{
			Objects->SetUiLootNotice(true);
		}
		UE_LOG(LogSwcUi, Log, TEXT("ui: the world views: the plates, the damage and heal numbers, and the loot ticker of the UI replace the stand-ins"));
	}
	// The view projection of the frame (Unreal row vectors: clip = (x, y, z, 1) * M), the size of the viewport of the UI, the camera.
	FSceneViewProjectionData Projection;
	if (!LocalPlayer->GetProjectionData(ViewportClient->Viewport, Projection))
	{
		return;
	}
	const FMatrix ViewProjection = Projection.ComputeViewProjectionMatrix();
	const FVector2f Size = Widget->GetLastSize();
	std::vector<FArg> View;
	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 Column = 0; Column < 4; ++Column)
		{
			View.push_back(FArg::of(static_cast<double>(ViewProjection.M[Row][Column])));
		}
	}
	View.push_back(FArg::of(static_cast<double>(FMath::Max(Size.X, 1.0f))));
	View.push_back(FArg::of(static_cast<double>(FMath::Max(Size.Y, 1.0f))));
	View.push_back(FArg::of(Projection.ViewOrigin.X));
	View.push_back(FArg::of(Projection.ViewOrigin.Y));
	View.push_back(FArg::of(Projection.ViewOrigin.Z));
	Runtime->call("HostViewProjection", View);
	FeedNumbers(Own, Creatures, Projection.ViewOrigin, Controller->PlayerCameraManager != nullptr ? Controller->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator);
	// The plates: one line for each creature (swc/markers.lua parse_plates).
	TArray<FSwcCreaturePlate> Plates;
	Creatures->GetPlates(Plates);
	FString Lines;
	for (const FSwcCreaturePlate& Plate : Plates)
	{
		FString Statuses;
		for (const FSwcPlateStatus& Status : Plate.Statuses)
		{
			Statuses += FString::Printf(TEXT("%s%s*%u"), Statuses.IsEmpty() ? TEXT("") : TEXT(","), *Status.Id, Status.Stacks);
		}
		Lines += FString::Printf(TEXT("%u\t%llu\t%.1f\t%.1f\t%.1f\t%s\t%s\t%s\t%s\t%u\t%d\t%.1f\t%.1f\t%d\t%.2f\t%d\t%d\t%d\t%.2f\t%d\t%s\n"),
			Plate.Interest, static_cast<unsigned long long>(Plate.Serial), Plate.Feet.X, Plate.Feet.Y, Plate.Feet.Z, *Plate.Name.Replace(TEXT("\t"), TEXT(" ")),
			*Plate.MarkerType, *Plate.CreatureType, *Plate.CreatureTypeOverride, Plate.Level, Plate.bHidesLevel ? 1 : 0, Plate.Health, Plate.HealthMax,
			Plate.bDead ? 1 : 0, Plate.DeadSeconds, Plate.bImmune ? 1 : 0, Plate.bNeverShowHealthBar ? 1 : 0, Plate.bForceDisplayHealthBar ? 1 : 0,
			Plate.Distance, Plate.bShown ? 1 : 0, *Statuses);
	}
	Runtime->call("HostPlates", {FArg::of(Utf8(Lines))});
	// The prompt of the focus (FSwcInteractor::GetPrompt): one line for swc/interact.lua (Swc.HostPrompt).
	USwcBackendSubsystem* Backend = GetGameInstance()->GetSubsystem<USwcBackendSubsystem>();
	FSwcInteractPrompt Prompt;
	FString PromptLine(TEXT("none"));
	if (Backend != nullptr && Backend->GetInteractPrompt(Prompt))
	{
		static const TCHAR* const Kinds[] = {TEXT("none"), TEXT("bag"), TEXT("object"), TEXT("fixture")};
		FString Options;
		for (const TPair<FString, float>& Option : Prompt.Options)
		{
			Options += FString::Printf(TEXT("%s%s:%.2f"), Options.IsEmpty() ? TEXT("") : TEXT(","), *Option.Key, Option.Value);
		}
		const FString Id = Prompt.Kind == FSwcInteractPrompt::EKind::Object ? Prompt.ObjectKey : FString::FromInt(static_cast<int32>(Prompt.Interest));
		PromptLine = FString::Printf(TEXT("%s\t%s\t%s\t%s\t%.1f\t%.1f\t%.1f\t%s\t%.3f\t%d\t%s\t%s\t%s\t%d\t%d\t%s\t%.2f\t%d\t%d"), Kinds[static_cast<int32>(Prompt.Kind)], *Id,
			*Prompt.ObjectKind.ToString(), *Prompt.Name.Replace(TEXT("\t"), TEXT(" ")), Prompt.Place.X, Prompt.Place.Y, Prompt.Place.Z, *Prompt.Phase,
			Prompt.Progress, Prompt.bToolMissing ? 1 : 0, *Prompt.GatheringType, *Prompt.RefusalKey, *Options, Prompt.bRankMissing ? 1 : 0, Prompt.RequiredRank,
			*Prompt.Title.Replace(TEXT("\t"), TEXT(" ")), Prompt.WorkTime, Prompt.Impacts, Prompt.bQuestObject ? 1 : 0);
	}
	Runtime->call("HostPrompt", {FArg::of(Utf8(PromptLine))});
	// The occlusion of the markers (markerdata.luac: allowWorldOcclusion, occlusionInterval 0.33 s): a line from the camera to the point
	// of each marker; a hit of the world before the point hides it (the creatures and the pawn do not hide a marker).
	const double Now = FPlatformTime::Seconds();
	if (Now < NextOcclusionTime)
	{
		return;
	}
	NextOcclusionTime = Now + 0.33;
	std::string Points;
	Runtime->call("HostMarkerPoints", {}, &Points);
	TArray<FString> Parts;
	FString(UTF8_TO_TCHAR(Points.c_str())).ParseIntoArray(Parts, TEXT(" "));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SwcMarkerOcclusion), false, Controller->GetPawn());
	FString Result;
	for (int32 Index = 0; Index + 3 < Parts.Num(); Index += 4)
	{
		const FVector Point(FCString::Atod(*Parts[Index + 1]), FCString::Atod(*Parts[Index + 2]), FCString::Atod(*Parts[Index + 3]));
		FHitResult Hit;
		bool bHidden = World->LineTraceSingleByChannel(Hit, Projection.ViewOrigin, Point, ECC_Visibility, Params);
		if (bHidden && Hit.GetActor() != nullptr && Hit.GetActor()->IsA<ASwcCreature>())
		{
			bHidden = false;
		}
		Result += FString::Printf(TEXT("%s:%d "), *Parts[Index], bHidden ? 1 : 0);
	}
	Runtime->call("HostMarkerOcclusion", {FArg::of(Utf8(Result))});
}

void USwcUiSubsystem::FlushLog()
{
	if (!Runtime.IsValid())
	{
		return;
	}
	// Each line goes to the log file of the UI (SWC_WORK/logs/ui/ui-log.txt). The client log gets the errors and the notes
	// of the runtime; the stubs, the log lines of the scripts, and the screens of later stages only at the Verbose level.
	FString Lines;
	for (const std::string& Line : Runtime->take_log())
	{
		Lines += UTF8_TO_TCHAR(Line.c_str());
		Lines += TEXT("\n");
		if (Line.rfind("error ", 0) == 0)
		{
			UE_LOG(LogSwcUi, Warning, TEXT("ui: %hs"), Line.c_str());
		}
		else if (Line.rfind("stub ", 0) == 0 || Line.rfind("script: ", 0) == 0 || Line.find("is not a screen of this stage") != std::string::npos)
		{
			UE_LOG(LogSwcUi, Verbose, TEXT("ui: %hs"), Line.c_str());
		}
		else
		{
			UE_LOG(LogSwcUi, Log, TEXT("ui: %hs"), Line.c_str());
		}
	}
	if (!Lines.IsEmpty() && !ReportDir.IsEmpty())
	{
		FFileHelper::SaveStringToFile(Lines, *FPaths::Combine(ReportDir, TEXT("ui-log.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), bLogStarted ? FILEWRITE_Append : FILEWRITE_None);
		bLogStarted = true;
	}
}

void USwcUiSubsystem::WriteDrawList()
{
	// The draw list of the last frame (a software picture: python3 -B -m swc_convert.uipreview draw.json OUT.png).
	FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(swc::ui::draw_list_json(DrawList).c_str())),
		*FPaths::Combine(ReportDir, TEXT("draw.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void USwcUiSubsystem::WriteReports()
{
	Runtime->call("Report");
	FlushLog();
	std::string Error;
	if (!Runtime->write_reports(&Error))
	{
		UE_LOG(LogSwcUi, Warning, TEXT("ui: %hs"), Error.c_str());
		return;
	}
	WriteDrawList();
	// The missing textures for the converter (tools/convert-ui.sh wanted).
	const FString Wanted = FPaths::Combine(WorkDir(), TEXT("ui/wanted.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Wanted), true);
	IFileManager::Get().Copy(*Wanted, *FPaths::Combine(ReportDir, TEXT("wanted.json")));
	const swc::ui::Runtime::Counts& Counts = Runtime->counts();
	UE_LOG(LogSwcUi, Log, TEXT("ui: reports in %s: %llu Lua errors, %llu stub names (%llu calls)"), *ReportDir,
		static_cast<unsigned long long>(Counts.luaErrors), static_cast<unsigned long long>(Counts.stubNames),
		static_cast<unsigned long long>(Counts.stubCalls));
}
