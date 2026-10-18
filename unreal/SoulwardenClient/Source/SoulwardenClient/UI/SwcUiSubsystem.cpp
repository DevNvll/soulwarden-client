#include "UI/SwcUiSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
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
#include "UI/SwcUiSlate.h"

DEFINE_LOG_CATEGORY(LogSwcUi);

namespace
{
using FArg = swc::ui::Runtime::Arg;

/** The screens of the first HUD (stage 1.10): the loader of the game loads each screen; only these load here. */
const char* const HudScreens[] = {
	"lyshineui/hud2024/vitals/vitals2024",
	"lyshineui/hud2024/weaponquickslots/weaponquickslots",
	"lyshineui/compass/compass",
	"lyshineui/hud2024/editmode/hudeditmode",
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
	FlushLog();
	LoadItemCatalog();
	UE_LOG(LogSwcUi, Log, TEXT("ui: the runtime started (Lua 5.1.5, the original scripts of the game)"));
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USwcUiSubsystem::Tick));
}

void USwcUiSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(Ticker);
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
	Runtime->tick(FMath::Min(DeltaTime, 0.25f));
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
	// The missing textures for the converter (tools/convert-ui.sh wanted).
	const FString Wanted = FPaths::Combine(WorkDir(), TEXT("ui/wanted.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Wanted), true);
	IFileManager::Get().Copy(*Wanted, *FPaths::Combine(ReportDir, TEXT("wanted.json")));
	const swc::ui::Runtime::Counts& Counts = Runtime->counts();
	UE_LOG(LogSwcUi, Log, TEXT("ui: reports in %s: %llu Lua errors, %llu stub names (%llu calls)"), *ReportDir,
		static_cast<unsigned long long>(Counts.luaErrors), static_cast<unsigned long long>(Counts.stubNames),
		static_cast<unsigned long long>(Counts.stubCalls));
}
