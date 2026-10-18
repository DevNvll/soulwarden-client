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
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SwcBackendSubsystem.h"
#include "UI/SwcUiInput.h"
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
	"lyshineui/respawn/respawn",
	"lyshineui/tooltip/tooltip",
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
	FString Mode;
	FParse::Value(FCommandLine::Get(), TEXT("SwcUi="), Mode);
	if (GIsEditor || Mode == TEXT("off"))
	{
		return;
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
	ReportDir = FPaths::Combine(WorkDir(), TEXT("logs/ui"));
	IFileManager::Get().MakeDirectory(*ReportDir, true);

	swc::ui::Runtime::Options Options;
	Options.contentDir = Utf8(FPaths::Combine(Content, TEXT("ui")));
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
		return;
	}
	Runtime = MakeUnique<swc::ui::Runtime>(MoveTemp(Options));
	Resources = MakeShared<FSwcUiResources>();
	Runtime->set_text_measure(Resources.Get());
	std::string Error;
	if (!Runtime->start(&Error))
	{
		UE_LOG(LogSwcUi, Error, TEXT("ui: the Lua runtime did not start: %hs"), Error.c_str());
		FlushLog();
		Runtime.Reset();
		return;
	}
	Runtime->call("Boot");
	// The loading screen of the game (globals.luac registers it) from the start to the spawn of the player.
	Runtime->call("HostLoading", {FArg::of(true), FArg::of(std::string(WorldLevelName)), FArg::of(std::string("eLoadingScreenIds_LEVEL1"))});
	bStartLoading = true;
	FlushLog();
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
}

void USwcUiSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(Ticker);
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
	return CallInput({FArg::of(std::string("wheel")), FArg::of(static_cast<double>(Delta))});
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
	// The end of the loading screen of the start: the player has its character.
	if (bStartLoading && bWorldEntered && GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		UWorld* World = GEngine->GameViewport->GetWorld();
		APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		if (Controller != nullptr && Controller->GetPawn() != nullptr)
		{
			bStartLoading = false;
			Runtime->call("HostLoading", {FArg::of(false)});
		}
	}
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
	// starts it too (the error UI).
	if (Drive.IsEmpty() || !((bWorldEntered && !bStartLoading) || bErrorShown))
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
			DriveWaitStart = -1.0;
			++DriveNext;
			continue;
		}
		const FDriveStep& Step = Drive[DriveNext++];
		const auto Arg = [&Step](int32 I, float Default) { return Step.Args.IsValidIndex(I) ? FCString::Atof(*Step.Args[I]) : Default; };
		UE_LOG(LogSwcUi, Log, TEXT("ui drive: %.2f s %s %s"), Time, *Step.Command, *FString::Join(Step.Args, TEXT(" ")));
		if (Step.Command == TEXT("key") && Step.Args.Num() > 0)
		{
			DriveKey(Step.Args[0], true);
			DriveKeysUp.Add(Step.Args[0]);
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
		if (!bSentLoadingScreen)
		{
			bLoadingForDeath = false;
		}
	}

	// The paperdoll: the item id of each slot (the item index of the wire in the item catalog of the backend copy).
	for (const auto& [Slot, Index] : Own.equipment)
	{
		const FString ItemId = Index > 0 && ItemIds.IsValidIndex(static_cast<int32>(Index) - 1) ? ItemIds[Index - 1] : FString();
		const FString* Sent = SentEquipment.Find(Slot);
		if (Sent == nullptr || *Sent != ItemId)
		{
			SentEquipment.Add(Slot, ItemId);
			Runtime->call("HostEquipment", {FArg::of(static_cast<double>(Slot)), FArg::of(Utf8(ItemId))});
		}
	}
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

	// The compass: the camera yaw (0 north, positive west) and the position of the entity (engine meters).
	const float Yaw = Backend->GetCameraYawDegrees();
	if (!FMath::IsNearlyEqual(Yaw, SentYaw, 0.01f))
	{
		SentYaw = Yaw;
		Runtime->call("HostView", {FArg::of(static_cast<double>(Yaw)), FArg::of(Own.position.x), FArg::of(Own.position.y),
									FArg::of(Own.position.z)});
	}
	Runtime->call("HostCommit");
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
