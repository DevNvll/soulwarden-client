#include "SwcBackendSubsystem.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Camera/CameraActor.h"
#include "Character/SwcCharacterMovement.h"
#include "Character/SwcActions.h"
#include "Character/SwcLocomotion.h"
#include "Character/SwcPlayerActions.h"
#include "Combat/SwcAttackGraph.h"
#include "Combat/SwcBackendSupport.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "Character/SwcPlayerCharacter.h"
#include "Character/SwcPlayerLooks.h"
#include "Character/SwcRangedView.h"
#include "Character/SwcTestFloor.h"
#include "Interact/SwcInteractor.h"
#include "Persons/SwcPersonSubsystem.h"
#include "SwcDebugLines.h"
#include "World/SwcWorldStreaming.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Creatures/SwcCreatureSubsystem.h"
#include "Engine/AssetManager.h"
#include "Engine/CoreSettings.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/SwcLoginClient.h"
#include "Net/SwcRepConnection.h"
#include "Net/SwcSpace.h"
#include "Net/SwcSteamStub.h"
#include "Net/SwcTypeIndex.h"
#include "SwcCameraManager.h"
#include "SwcPlayerController.h"

DEFINE_LOG_CATEGORY(LogSwcBackend);

/** The player tables of the worker thread (USwcBackendSubsystem::StartTables). */
struct FSwcPlayerTables
{
	TSharedPtr<FSwcPlayerLooks> Looks;
	TSharedPtr<FSwcLocomotion> Locomotions[2];
	TSharedPtr<FSwcActions> Actions;
	TSharedPtr<FSwcInteractor> Interactor;
	/** The report of each table: a log line each on the game thread. */
	TArray<FString> Reports;
	double Seconds = 0.0;
};

namespace
{
using FPhase = swc::net::RepClient::Phase;

/** The fallback of the return to the menu (AST-203): 1 loads the level of the world again, as before the return that keeps the
 *  world (also the command-line option -SwcReturnReload). */
TAutoConsoleVariable<int32> CVarReturnReload(TEXT("swc.Menu.ReturnReload"), 0,
	TEXT("1: the return to the menu loads the level of the world again (the fallback); 0: the world stays (the default)."));

/** The spawn waits at most this time (from the in-game phase) for the assets of the look; then the rest loads at once. */
constexpr double PreloadLimit = 30.0;
/** The time after the spawn for the log of its longest frame. */
constexpr double SpawnFramesTime = 5.0;
/** The time of the async loader in each frame from the in-game phase to the spawn (the loading screen shows; the assets of
 *  the look and the cells of the place load): 50 ms in place of the 5 ms of the engine (s.AsyncLoadingTimeLimit), with the
 *  full time also when the loader waits for a read (s.AsyncLoadingUseFullTimeLimit). Measured by the world track: the cells
 *  of the place in 7.1 s and 7.7 s, against 9.8 s and 8.4 s with 50 ms only until the assets of the look were loaded.
 *  300 ms since 2026-10-20 (the loading work, docs/WORLD.md "The loading"): the uncooked client has no loading thread, so
 *  the loader works only in this slice of the game thread; the wait to the spawn 24.9 s -> 19.6 s, the assets of the look
 *  13.1 s -> 7.5 s (the loading screen then runs at about 3 frames per second). Also in the wait of a travel or a respawn
 *  (its loading screen). */
constexpr float SpawnLoadingTimeLimit = 300.0f;

/** @return The facing of the own entity in Unreal space: the rotation of the movement state turns north (engine +Y) about the vertical axis. */
FVector OwnForward(const swc::net::RepClient::OwnEntity& Own)
{
	const FQuat Rotation(Own.rotation.x, Own.rotation.y, Own.rotation.z, Own.rotation.w);
	const FVector North = Rotation.RotateVector(FVector(0.0, 1.0, 0.0));
	const swc::net::WorldPosition Ahead{Own.position.x + static_cast<float>(North.X), Own.position.y + static_cast<float>(North.Y), Own.position.z};
	return (SwcEngineToUnreal(Ahead) - SwcEngineToUnreal(Own.position)).GetSafeNormal();
}

/** The distance of the ground value of an input entry in a fall: 10 m at most (protocol notes 4.7). */
constexpr double MaxAirHeight = 1000.0;
/** The spawn height of the backend is the saved height plus a lift of 0.5 m (protocol notes 4.6): the test floor is
 *  0.5 m below the spawn, and the character drops to it as the retail client does. */
constexpr double SpawnLift = 50.0;

FString Text(const std::string& Value)
{
	return UTF8_TO_TCHAR(Value.c_str());
}

FString Hex(std::span<const std::uint8_t> Bytes)
{
	return Text(swc::net::hex_of(Bytes));
}

/** The inputs of the look: the appearance state and the paperdoll of the own entity. */
void LookInputs(const FSwcRepConnection::FSnapshot& Snapshot, TArray<uint8>& OutAppearance, TMap<uint32, uint32>& OutEquipment)
{
	if (Snapshot.Own.hasAppearance)
	{
		OutAppearance.Append(Snapshot.Own.appearance.data(), static_cast<int32>(Snapshot.Own.appearance.size()));
	}
	for (const auto& [Slot, Item] : Snapshot.Own.equipment)
	{
		OutEquipment.Add(Slot, Item);
	}
}
} // namespace

void USwcBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReadAdminToken();
	FString Value;
	if (FParse::Value(FCommandLine::Get(), TEXT("SwcHttp="), Value))
	{
		FString Host, Port;
		if (Value.Split(TEXT(":"), &Host, &Port))
		{
			HttpHost = Host;
			HttpPort = FCString::Atoi(*Port);
		}
	}
	// Safety rule of the project: the backend is on loopback only.
	if (HttpHost != TEXT("127.0.0.1") && HttpHost != TEXT("localhost"))
	{
		UE_LOG(LogSwcBackend, Error, TEXT("backend host %s is not loopback: the client uses 127.0.0.1"), *HttpHost);
		HttpHost = TEXT("127.0.0.1");
	}
	bPlayerRun = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_RUN")) != TEXT("development");
	FParse::Value(FCommandLine::Get(), TEXT("SwcQuitWhen="), QuitWhen);
	FParse::Value(FCommandLine::Get(), TEXT("SwcPlay="), PlayCharacter);
	FParse::Value(FCommandLine::Get(), TEXT("SwcStay="), StaySeconds);
	FParse::Value(FCommandLine::Get(), TEXT("SwcView="), View);
	float QuitAfter = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SwcQuitAfter="), QuitAfter) && QuitAfter > 0.0f)
	{
		QuitTimer = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
			[this](float)
			{
				Quit(true, TEXT("the time of -SwcQuitAfter ended"));
				return false;
			}), QuitAfter);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("SwcConnect")))
	{
		// The first tick of the engine: the HTTP module ticks from then on.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
			[this](float)
			{
				StartLogin();
				return false;
			}));
	}
}

void USwcBackendSubsystem::Deinitialize()
{
	SetSpawnLoading(false);
	FTSTicker::RemoveTicker(QuitTimer);
	FTSTicker::RemoveTicker(WorldTicker);
	if (Connection.IsValid())
	{
		// The carrier disconnect goes out, then the thread ends (the destructor waits for it).
		Connection->Leave(TEXT("the client stops"));
		Connection.Reset();
	}
	Login.Reset();
	Super::Deinitialize();
}

void USwcBackendSubsystem::LogEndpoints() const
{
	UE_LOG(LogSwcBackend, Log, TEXT("backend http://%s:%d"), *HttpHost, HttpPort);
}

void USwcBackendSubsystem::StartLogin()
{
	const FSwcSteamStub Identity = FSwcSteamStub::FromEnvironment();
	UE_LOG(LogSwcBackend, Log, TEXT("login: %s run"), bPlayerRun ? TEXT("player") : TEXT("development"));
	Login = MakeShared<FSwcLoginClient>(HttpHost, HttpPort, Identity);
	Login->Start([this](bool bSucceeded, const FString& Error) { OnCharacterList(bSucceeded, Error); });
}

void USwcBackendSubsystem::OnCharacterList(bool bSucceeded, const FString& Error)
{
	if (!bSucceeded)
	{
		LoginError = Error.IsEmpty() ? FString(TEXT("the login path failed")) : Error;
		if (!QuitWhen.IsEmpty())
		{
			Quit(false, TEXT("the login path failed"));
		}
		return;
	}
	const FSwcAccountSession& Session = Login->GetSession();
	for (const FSwcWorldInfo& World : Session.Worlds)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("login: world %s (%s), %s %s, version %s"), *World.Name, *World.WorldId, *World.Status,
			*World.Type, *World.Version);
	}
	UE_LOG(LogSwcBackend, Log, TEXT("login: persona %s has %d character(s)"), *Session.PersonaId, Session.Characters.Num());
	for (int32 Index = 0; Index < Session.Characters.Num(); ++Index)
	{
		const FSwcCharacterInfo& Character = Session.Characters[Index];
		// The menu document (stage F1): its status, the level of the screen, the body, and the count of the clothing entries.
		const swc::menu::FDocument& Menu = Character.Menu;
		UE_LOG(LogSwcBackend, Log, TEXT("login: character %d: %s, id %s, world %s, menu document %hs (level %lld, %hs, %d clothing entr%s)"),
			Index + 1, bPlayerRun ? TEXT("(name not in the log of a player run)") : *Character.Name, *Character.CharacterId,
			*Character.WorldId, swc::menu::StatusText(Character.MenuStatus), static_cast<long long>(Menu.DisplayLevel()),
			Menu.IsFemale() ? "female" : "male", static_cast<int32>(Menu.Equipment.size()), Menu.Equipment.size() == 1 ? TEXT("y") : TEXT("ies"));
	}
	if (QuitWhen == TEXT("characters"))
	{
		Quit(true, TEXT("the character list is complete"));
		return;
	}
	// The player tables load on a worker thread while the user selects a character (no read in the spawn frame).
	StartTables();
	if (!PlayCharacter.IsEmpty())
	{
		const FSwcCharacterInfo* Selected = Session.Characters.FindByPredicate(
			[this](const FSwcCharacterInfo& Character)
			{ return PlayCharacter == TEXT("first") || Character.CharacterId == PlayCharacter || (!bPlayerRun && Character.Name == PlayCharacter); });
		if (Selected == nullptr)
		{
			UE_LOG(LogSwcBackend, Error, TEXT("world: no character %s in the character list"), *PlayCharacter);
			Quit(false, TEXT("no character for -SwcPlay"));
			return;
		}
		EnterWorld(Selected->CharacterId);
		return;
	}
	// No -SwcPlay: the front end of the UI shows the list (stage F2 of docs/FRONTEND.md).
	bFrontEnd = true;
	UE_LOG(LogSwcBackend, Log, TEXT("login: the front end: the landing screen shows the character list"));
}

const FSwcAccountSession* USwcBackendSubsystem::GetAccountSession() const
{
	return Login.IsValid() && LoginError.IsEmpty() ? &Login->GetSession() : nullptr;
}

void USwcBackendSubsystem::DeleteCharacter(const FString& CharacterId, TFunction<void(bool bDeleted)> Done)
{
	if (!Login.IsValid() || Connection.IsValid())
	{
		Done(false);
		return;
	}
	Login->DeleteCharacter(CharacterId, [CharacterId, Done = MoveTemp(Done)](bool bDeleted, int32 Status)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("login: the delete of character %s: status %d%s"), *CharacterId, Status,
				bDeleted ? TEXT(" (deleted)") : TEXT(" (not deleted)"));
			Done(bDeleted);
		});
}

void USwcBackendSubsystem::RefreshCharacterList(TFunction<void(bool bSucceeded)> Done)
{
	if (!Login.IsValid())
	{
		Done(false);
		return;
	}
	Login->RefreshLoginInfo([Done = MoveTemp(Done)](bool bSucceeded, const FString& Error)
		{
			if (!bSucceeded)
			{
				UE_LOG(LogSwcBackend, Warning, TEXT("login: the refresh of the character list failed: %s"), *Error);
			}
			Done(bSucceeded);
		});
}

void USwcBackendSubsystem::EnterWorld(const FString& CharacterId)
{
	if (!Login.IsValid() || Connection.IsValid() || bEnteringWorld)
	{
		return;
	}
	FString Report;
	if (!FSwcTypeIndex::Check(Report))
	{
		UE_LOG(LogSwcBackend, Error, TEXT("world: %s"), *Report);
		Quit(false, TEXT("the type index does not agree with the backend"));
		return;
	}
	UE_LOG(LogSwcBackend, Log, TEXT("world: %s"), *Report);
	UE_LOG(LogSwcBackend, Log, TEXT("world: login queue for character %s"), *CharacterId);
	WorldCharacterId = CharacterId;
	// One login at a time (the PLAY of the front end); a failure in the front end keeps the client open (stage F5 of
	// docs/FRONTEND.md: the landing screen shows the error, and PLAY can try again).
	bEnteringWorld = true;
	EnterError.Reset();
	Login->RequestLoginToken(CharacterId,
		[this](bool bSucceeded, const FSwcLoginToken& Token, const FString& Error)
		{
			bEnteringWorld = false;
			if (!bSucceeded)
			{
				UE_LOG(LogSwcBackend, Error, TEXT("world: %s"), *Error);
				if (bFrontEnd)
				{
					EnterError = Error.IsEmpty() ? FString(TEXT("no login token")) : Error;
					return;
				}
				Quit(false, TEXT("no login token"));
				return;
			}
			UE_LOG(LogSwcBackend, Log, TEXT("world: login token, game server %s, token version %u"), *Token.RepAddress, Token.TokenVersion);
			Login->RequestBackendCertificate(
				[this, Token](bool bCertificate, const FString& Pem)
				{
					if (!bCertificate)
					{
						UE_LOG(LogSwcBackend, Error, TEXT("world: no certificate of the backend at /v1/tls/ca.pem"));
						Quit(false, TEXT("no certificate of the backend"));
						return;
					}
					StartConnection(Token, Pem);
				});
		});
}

void USwcBackendSubsystem::StartConnection(const FSwcLoginToken& Token, const FString& CertificatePem)
{
	FString Host, Port;
	if (!Token.RepAddress.Split(TEXT(":"), &Host, &Port, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		UE_LOG(LogSwcBackend, Error, TEXT("world: the game server address %s has no port"), *Token.RepAddress);
		Quit(false, TEXT("no game server address"));
		return;
	}
	FSwcRepConnection::FOptions Options;
	Options.Host = Host;
	Options.Port = FCString::Atoi(*Port);
	Options.CertificatePem = CertificatePem;
	swc::net::LoginToken& Wire = Options.Session.token;
	Wire.signature = TCHAR_TO_UTF8(*Token.Signature);
	Wire.repAddress = TCHAR_TO_UTF8(*Token.RepAddress);
	Wire.worldId = TCHAR_TO_UTF8(*Token.WorldId);
	Wire.characterId = TCHAR_TO_UTF8(*Token.CharacterId);
	Wire.personaId = TCHAR_TO_UTF8(*Token.PersonaId);
	Wire.steamAppId = Token.SteamAppId;
	Wire.steamUserId = TCHAR_TO_UTF8(*Token.SteamUserId);
	Wire.channelId = TCHAR_TO_UTF8(*Token.ChannelId);
	Wire.ticketId = TCHAR_TO_UTF8(*Token.TicketId);
	Wire.issueTime = Token.IssueTime;
	Wire.generateTime = Token.GenerateTime;
	Wire.accountAge = Token.AccountAge;
	Wire.tokenVersion = Token.TokenVersion;
	const FSwcSteamStub& Identity = Login->GetIdentity();
	Options.Session.steamId = TCHAR_TO_UTF8(*Identity.SteamId);
	Options.Session.ticketHex = TCHAR_TO_UTF8(*Identity.GetAuthTicketHex());
	// The records of the other entities go to the entity table of the creatures (TakeOtherUpdates).
	Options.Session.keepOtherRecords = true;
	// The attack input and the attack graph at the input steps of the session thread (stage P2.7, docs/ACTIONS.md).
	AttackDriver = std::make_shared<swc::combat::Driver>();
	// The supported-action list of the fixed backend (stage MVP-A): a key of an ability with no cast plan sends nothing.
	AttackDriver->set_supported_abilities(swc::combat::supported_abilities());
	Options.Session.inputStep = [Driver = AttackDriver](std::uint16_t Input, std::int64_t SteadyNs, std::vector<unsigned>& Bits, std::vector<swc::net::ShotRequest>& Shots)
	{
		std::vector<swc::combat::Shot> Made;
		Driver->step(Input, SteadyNs, Bits, &Made);
		for (const swc::combat::Shot& Shot : Made)
		{
			Shots.push_back({Shot.attack, Shot.row, Shot.charge, Shot.ammo, Shot.forward, Shot.up});
		}
	};
	Connection = MakeShared<FSwcRepConnection>(MoveTemp(Options));
	if (!Connection->Launch())
	{
		UE_LOG(LogSwcBackend, Error, TEXT("world: the connection thread did not start"));
		Connection.Reset();
		Quit(false, TEXT("no connection thread"));
		return;
	}
	WorldTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USwcBackendSubsystem::TickWorld));
}

bool USwcBackendSubsystem::TickWorld(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SwcBackend_TickWorld);   // the loading work: the time of the core tickers in a trace
	if (!Connection.IsValid())
	{
		return false;
	}
	for (const swc::net::RepClient::Event& Event : Connection->TakeEvents())
	{
		const FString Line = Text(Event.text);
		switch (Event.level)
		{
		case swc::net::RepClient::Event::Level::error:
			UE_LOG(LogSwcBackend, Error, TEXT("rep: %s"), *Line);
			break;
		case swc::net::RepClient::Event::Level::warning:
			UE_LOG(LogSwcBackend, Warning, TEXT("rep: %s"), *Line);
			break;
		default:
			UE_LOG(LogSwcBackend, Log, TEXT("rep: %s"), *Line);
			break;
		}
	}
	const FSwcRepConnection::FSnapshot Snapshot = Connection->GetSnapshot();
	// On the world level (World Partition), the character waits for the ground of its place (stage 1.3), and for the assets
	// of its look, which load in the background at the same time.
	double SpawnStart = 0.0;
	if (Snapshot.Phase == FPhase::inGame && !bInGame)
	{
		SetSpawnLoading(true);
		bCellsReady = bCellsReady || SwcWorldStreaming::IsReadyAt(GetGameInstance()->GetWorld(), SwcEngineToUnreal(Snapshot.Own.position), 90.0,
			OwnForward(Snapshot.Own));
		if (UpdatePreload() && bCellsReady)
		{
			SetSpawnLoading(false);
			bInGame = true;
			InGameSince = FPlatformTime::Seconds();
			SpawnStart = InGameSince;
			ShowOwnEntity();
			SwcWorldStreaming::Release();   // the character has its own streaming source
		}
	}
	if (bInGame && Snapshot.Own.lookRevision != LookRevision)
	{
		UpdateLook();
	}
	if (SpawnStart > 0.0)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("world: the spawn: %.3f s of work in its frame (the character, the look, the actions)"),
			FPlatformTime::Seconds() - SpawnStart);
	}
	else if (bInGame && !bFramesLogged)
	{
		// The frame of the spawn shows in the time step of the next frame.
		LongestFrame = FMath::Max(LongestFrame, DeltaTime);
		if (FPlatformTime::Seconds() - InGameSince >= SpawnFramesTime)
		{
			bFramesLogged = true;
			UE_LOG(LogSwcBackend, Log, TEXT("world: the longest frame of the first %.0f s after the spawn: %.3f s"), SpawnFramesTime, LongestFrame);
		}
	}
	if (bInGame && !bQuitting)
	{
		UpdatePlayer();
	}
	if (bInGame && QuitWhen == TEXT("world") && !bQuitting && FPlatformTime::Seconds() - InGameSince >= StaySeconds)
	{
		bQuitting = true;
		UE_LOG(LogSwcBackend, Log, TEXT("world: %.0f s in the world, %llu state bundles, %llu movement states, %llu input reports, %d other entities; the client leaves"),
			StaySeconds, static_cast<unsigned long long>(Snapshot.Counts.bundles), static_cast<unsigned long long>(Snapshot.Counts.movementStates),
			static_cast<unsigned long long>(Snapshot.Counts.inputReports), Snapshot.OtherEntities);
		// The values of the read path (the HUD of the UI track) at the leave: a check in the log.
		const swc::net::RepClient::OwnEntity& Own = Snapshot.Own;
		int32 Abilities = 0;
		for (const auto& Bar : Own.abilityBars)
		{
			Abilities += static_cast<int32>(Bar.abilities.size());
		}
		UE_LOG(LogSwcBackend, Log, TEXT("world: read path: health %.0f/%.0f, stamina %.0f/%.0f (state %d), mana %.0f/%.0f (state %d), dead %d, %d ability bar(s) with %d place(s), cooldowns %d/%d/%d, active map %llx, sheathe map %llx, server time %llu us, camera yaw %.1f"),
			Own.health, Own.healthMax, Own.hasStamina ? Own.stamina : Own.vitalsStamina, Own.hasStamina ? Own.staminaMax : Own.vitalsStaminaMax,
			Own.hasStamina ? 1 : 0, Own.hasMana ? Own.mana : Own.vitalsMana, Own.hasMana ? Own.manaMax : Own.vitalsManaMax, Own.hasMana ? 1 : 0,
			Own.dead ? 1 : 0, static_cast<int32>(Own.abilityBars.size()), Abilities, static_cast<int32>(Own.cooldowns[0].size()), static_cast<int32>(Own.cooldowns[1].size()),
			static_cast<int32>(Own.cooldowns[2].size()), static_cast<unsigned long long>(Own.activeMap), static_cast<unsigned long long>(Own.sheatheMap),
			static_cast<unsigned long long>(GetServerTimeMicroseconds()), GetCameraYawDegrees());
		if (AttackDriver)
		{
			const swc::combat::Driver::Snapshot Attack = AttackDriver->snapshot();
			UE_LOG(LogSwcBackend, Log, TEXT("world: attacks: %u press(es), %u hold step(s), %u release(s) in the input report; %u fragment visit(s) played, %u weapon switch(es), %u lost key(s)"),
				Attack.presses, Attack.holds, Attack.releases, Avatar.IsValid() ? Avatar->GetActions()->GetVisitCount() : 0u, Attack.switches,
				Attack.lostKeys);
			UE_LOG(LogSwcBackend, Log, TEXT("world: dodge: %u dodge bit(s), %u request(s): %u accepted, %u buffered, %u refused while a dodge plays, %u exhausted, %u no stamina, %u overburdened; stamina %.1f of %.1f%s; %u correction(s) from the server copy"),
				Attack.dodgeBits, Attack.requests, Attack.verdicts[0], Attack.verdicts[1], Attack.verdicts[2], Attack.verdicts[6], Attack.verdicts[7],
				Attack.verdicts[5], Attack.stamina, Attack.staminaMax, Attack.exhausted ? TEXT(", exhausted") : TEXT(""), Attack.corrections);
			if (Avatar.IsValid())
			{
				const USwcPlayerActions* Actions0 = Avatar->GetActions();
				UE_LOG(LogSwcBackend, Log, TEXT("world: melee: %u strike window(s) (%u with no row for the learned nodes), %u shape(s) to the contact query, %u contact(s); %llu contact(s) in list 3 of the input entries"),
					Actions0->GetStrikeCount(), Actions0->GetSkippedStrikes(), Actions0->GetShapeQueries(), Actions0->GetContactCount(),
					static_cast<unsigned long long>(Snapshot.Counts.meleeContacts));
			}
			UE_LOG(LogSwcBackend, Log, TEXT("world: abilities: %u press(es) of the keys of the bar: %u played, %u in the own cooldown, %u with no window, %u with no ability, %u refused by the load or a status, %u with no rule of the backend; %u shot(s) of a cast, %llu in list 2 of the input entries; %u end(s) of the server taken; %u collision step(s); %u dodge press(es) refused by the load or a status"),
				Attack.abilityPresses, Attack.pressVerdicts[0], Attack.pressVerdicts[2], Attack.pressVerdicts[3], Attack.pressVerdicts[1], Attack.pressVerdicts[4], Attack.pressVerdicts[5],
				Attack.shots, static_cast<unsigned long long>(Snapshot.Counts.shots), Attack.cooldownCorrections, Attack.collisions, Attack.limitedDodges);
			const USwcRangedView* RangedView = Avatar.IsValid() ? Avatar->GetRangedView() : nullptr;
			UE_LOG(LogSwcBackend, Log, TEXT("world: ranged: %u shot(s), %u cast(s), %u ability start(s), %u mana payment(s) (%u refused), mana %.1f of %.1f, %u correction(s); %u own projectile(s), %u hit(s); %u ground target(s); %u heal number(s); %u unknown leaf value(s) [%s], %u action(s) ended by the time rule"),
				Attack.ranged.shots, Attack.ranged.casts, Attack.ranged.abilities, Attack.ranged.payments, Attack.ranged.refusedPayments, Attack.mana, Attack.manaMax,
				Attack.manaCorrections, RangedView != nullptr ? RangedView->GetProjectiles() : 0u, RangedView != nullptr ? RangedView->GetProjectileHits() : 0u,
				RangedView != nullptr ? RangedView->GetTargetings() : 0u, RangedView != nullptr ? RangedView->GetHealNumbers() : 0u, Attack.ranged.unknown,
				UTF8_TO_TCHAR(Attack.ranged.unknownTypes.c_str()), Attack.ranged.stuck);
		}
		Connection->Leave(TEXT("the client leaves the world (-SwcQuitWhen=world)"));
	}
	if (Snapshot.bFinished)
	{
		const bool bLeft = bQuitting && bInGame;
		UE_LOG(LogSwcBackend, Log, TEXT("world: the connection ended (%s)"), *Snapshot.EndCause);
		Connection.Reset();
		if (!QuitWhen.IsEmpty())
		{
			Quit(bLeft, bLeft ? TEXT("the client left the world") : TEXT("the connection ended before the planned leave"));
		}
		else if (bFrontEnd)
		{
			// The return to the character selection (stage F6 of docs/FRONTEND.md): a planned leave, or a connection that ended with
			// no leave (its cause goes to the landing screen as the entry error).
			const FString Error = bReturning ? FString() : FString::Printf(TEXT("the connection ended: %s"), *Snapshot.EndCause);
			WorldTicker.Reset();   // this ticker ends with the false below
			if (!ReturnReloads())
			{
				EndWorldActors();   // the pawn and the view actor of the session (before ResetWorldSession forgets them)
			}
			ResetWorldSession();
			EnterError = Error;
			OpenMenuLevel();
		}
		return false;
	}
	return true;
}

void USwcBackendSubsystem::ShowOwnEntity()
{
	const FSwcRepConnection::FSnapshot Snapshot = Connection->GetSnapshot();
	const swc::net::RepClient::OwnEntity& Own = Snapshot.Own;
	const FVector Feet = SwcEngineToUnreal(Own.position);
	// The movement state of the world entry is the first travel of the session (teleport number 1, protocol notes 4.16).
	SeenTravel = Own.travelRevision;
	UE_LOG(LogSwcBackend, Log, TEXT("world: own entity %s, character %s (%s), position %.2f %.2f %.2f m (Unreal %.0f %.0f %.0f cm), health %.0f of %.0f"),
		*Hex(Own.actor), *Text(Own.characterId), bPlayerRun ? TEXT("name not in the log of a player run") : *Text(Own.name),
		Own.position.x, Own.position.y, Own.position.z, Feet.X, Feet.Y, Feet.Z, Own.health, Own.healthMax);

	// The character of the user, and the camera of the retail client at the start of a session (or in front of the
	// character for a picture, -SwcView=front).
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}
	const FVector Forward = OwnForward(Own);
	// The level has no ground yet (the world level comes in stage 1.3): the test floor of the movement.
	bTestFloor = !SwcTestFloor::HasGround(World, Feet);
	if (bTestFloor)
	{
		SwcTestFloor::Spawn(World, Feet - FVector(0.0, 0.0, SpawnLift), Forward);
	}
	// The position field of the input reports moves the saved character of the backend: a player run sends it only on
	// the ground of the world, not on the test floor (docs/MOVEMENT.md).
	bSendPosition = !bPlayerRun || !bTestFloor;
	UE_LOG(LogSwcBackend, Log, TEXT("world: %s; the input reports %s"),
		bTestFloor ? TEXT("no ground below the spawn: the test floor of the movement") : TEXT("the level has ground below the spawn"),
		bSendPosition ? TEXT("have the position of the character") : TEXT("have the camera of a character at rest (a player run on the test floor)"));
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (ASwcPlayerCharacter* Character = World->SpawnActor<ASwcPlayerCharacter>(Feet, Forward.Rotation(), Parameters))
	{
		Character->SetActorLocation(Feet + FVector(0.0, 0.0, Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
		Avatar = Character;
		if (Controller != nullptr)
		{
			Controller->Possess(Character);
		}
		if (bTestFloor)
		{
			// The new floor is in the scene queries after the next physics step: the character waits for it.
			Character->GetCharacterMovement()->DisableMovement();
			bWaitForFloor = true;
		}
	}
	if (View == TEXT("front"))
	{
		// A picture of the character: the camera stands 3 m in front of the chest.
		const FVector Chest = Feet + FVector(0.0, 0.0, 120.0);
		const FVector Eye = Chest + Forward * 300.0 + FVector(0.0, 0.0, 30.0);
		if (ACameraActor* ViewActor = World->SpawnActor<ACameraActor>(Eye, (Chest - Eye).Rotation(), Parameters))
		{
			Camera = ViewActor;
			if (Controller != nullptr)
			{
				Controller->SetViewTarget(ViewActor);
			}
		}
	}
	if (GEngine != nullptr && SwcDebugLines())
	{
		GEngine->AddOnScreenDebugMessage(-1, 3600.0f, FColor::White,
			FString::Printf(TEXT("InGame: entity %s, position %.2f %.2f %.2f m, health %.0f/%.0f, permissions %02x %02x"),
				*Hex(std::span(Own.actor).first(8)), Own.position.x, Own.position.y, Own.position.z, Own.health, Own.healthMax, Own.dataBits,
				Own.moreDataBits));
	}
}

bool USwcBackendSubsystem::GetLocalPlayer(swc::net::RepClient::OwnEntity& Out) const
{
	if (!Connection.IsValid())
	{
		return false;
	}
	const FSwcRepConnection::FSnapshot Snapshot = Connection->GetSnapshot();
	if (Snapshot.Phase != FPhase::inGame)
	{
		return false;
	}
	Out = Snapshot.Own;
	// The stamina of the HUD is the prediction of the client (protocol notes 4.38: the state of the server does not change it).
	if (AttackDriver)
	{
		const swc::combat::Driver::Snapshot Attack = AttackDriver->snapshot();
		if (Attack.stepped && Attack.staminaMax > 0.0f)
		{
			Out.hasStamina = true;
			Out.stamina = Attack.stamina;
			Out.staminaMax = Attack.staminaMax;
		}
		// The mana meter follows the predicted copy of the client (stage P3.6; backend mana_messages.h: the meter of the retail client
		// follows its prediction, the mana state writes only the copy of the server).
		if (Attack.stepped && Attack.manaKnown)
		{
			Out.hasMana = true;
			Out.mana = Attack.mana;
			Out.manaMax = Attack.manaMax;
		}
		// The selected weapon of the HUD follows the weapon switch of the client (the driver). The backend switches at the same input
		// step, but its paperdoll state keeps the first weapon slot with an item as the active one (backend paperdoll_state.cpp: no
		// draw and no sheathe yet), so the HUD kept the bar of slot 24 after a switch to slot 25. The off hand of the weapon is active
		// with it; the weapon in the hand is out of its sheath while the character holds it drawn.
		if (Attack.stepped && (Attack.held == 24 || Attack.held == 25))
		{
			const uint32 Held = Attack.held;
			uint64 Active = 1ull << Held;
			if (const auto OffHand = Out.equipment.find(Held + 3); OffHand != Out.equipment.end() && OffHand->second != 0)
			{
				Active |= 1ull << (Held + 3);
			}
			const bool bDrawn = Avatar.IsValid() && Avatar->IsWeaponDrawn() && Avatar->GetActiveWeaponSlot() == Held;
			Out.activeMap = Active;
			Out.sheatheMap = bDrawn ? (Out.sheatheMap & ~Active) : (Out.sheatheMap | Active);
		}
	}
	return true;
}

bool USwcBackendSubsystem::TakeOtherUpdates(std::vector<swc::net::RepClient::OtherUpdate>& Out) const
{
	if (!Connection.IsValid())
	{
		return false;
	}
	Out = Connection->TakeOtherUpdates();
	return true;
}

void USwcBackendSubsystem::UpdateMovementLimits(const swc::net::RepClient::OwnEntity& Own)
{
	if (!Avatar.IsValid() || !Looks.IsValid())
	{
		return;
	}
	USwcCharacterMovement* Movement = Avatar->GetSwcMovement();
	const swc::move::Data* Rules = Movement->GetRules();
	swc::move::Limits Limits;
	// The bag weight (the rule of the backend Inventory::encumbrance): the weight of each bag slot times its quantity.
	if (Own.bagRevision != BagRevision)
	{
		BagRevision = Own.bagRevision;
		int64 Sum = 0;
		for (const soulwarden::backend::tests::WireItem& Item : Own.bag)
		{
			Sum += static_cast<int64>(Looks->ItemWeight(Item.index)) * Item.quantity;
		}
		BagWeight = static_cast<int32>(FMath::Min<int64>(Sum, MAX_int32));
	}
	Limits.bagWeight = BagWeight;
	Limits.bagLimit = Actions.IsValid() ? Actions->GetBagLimit() : 2000;
	// The equip load class (stage P2.5): light, medium, heavy, overburdened are Fast, Normal, Slow, Overburdened of the data.
	Limits.equipLoad = static_cast<swc::move::EquipLoad>(FMath::Min<uint8>(EquipLoadClass, 3));
	// The statuses of the status map with a movement column (a rule of this project: the backend never clears the permission bits).
	FString Statuses;
	if (Rules != nullptr)
	{
		for (const swc::net::RepClient::OwnEntity::Status& Status : Own.statuses)
		{
			const auto Row = Rules->statuses.find(Status.index);
			if (Row != Rules->statuses.end())
			{
				Limits.statuses.push_back(&Row->second);
				Statuses += FString::Printf(TEXT("%s%s"), Statuses.IsEmpty() ? TEXT("") : TEXT(", "), UTF8_TO_TCHAR(Row->second.id.c_str()));
			}
		}
	}
	// The speed table of the server: the key 0x35 (an absent key is 1.0).
	const auto Sprint = Own.speedTable.find(0x35);
	Limits.sprintMultiplier = Sprint != Own.speedTable.end() ? Sprint->second : 1.0f;
	if (AttackDriver)
	{
		const swc::combat::Driver::Snapshot Attack = AttackDriver->snapshot();
		Limits.exhausted = Attack.exhausted;
	}
	Movement->SetLimits(Limits);
	const swc::move::Limits& Merged = Movement->GetLimits();
	if (AttackDriver)
	{
		AttackDriver->set_action_limits(Merged.can_dodge(), Merged.can_use_abilities(), Merged.can_attack_from_rest());
		// The facts of the conditions of the ranged actions (stage P3.6): the statuses, the stun and the silence of the status columns, the
		// encumbered movement (IfEncumbered), the movement, a damage in this frame, the camera lock, the ground target.
		swc::combat::Driver::RangedFacts Facts;
		bool bSilenced = false;
		for (const swc::move::StatusRow* Row : Merged.statuses)
		{
			Facts.statuses.insert(TCHAR_TO_UTF8(*FString(UTF8_TO_TCHAR(Row->id.c_str())).ToLower()));
			bSilenced = bSilenced || Row->silenced;
		}
		Facts.stunned = Merged.stunned();
		Facts.silenced = bSilenced;
		Facts.encumbered = Merged.encumbered_movement();
		Facts.moving = Movement->GetHorizontalSpeed() > 0.1f;
		Facts.heightToGround = Movement->IsFalling() ? 1.0f : 0.0f;
		Facts.damaged = Own.health < LastHealth;
		LastHealth = Own.health;
		const APlayerController* Controller = Avatar->GetWorld() != nullptr ? Avatar->GetWorld()->GetFirstPlayerController() : nullptr;
		const ASwcCameraManager* CameraManager = Controller != nullptr ? Cast<ASwcCameraManager>(Controller->PlayerCameraManager) : nullptr;
		Facts.cameraLock = CameraManager != nullptr && CameraManager->IsLocked();
		Facts.groundValid = Avatar->GetRangedView() != nullptr && Avatar->GetRangedView()->IsGroundValid();
		AttackDriver->set_ranged_facts(Facts);
		// The mana of the server (stage P3.6): the mana state (slot 47) and the prediction sync 4289.
		if (Own.manaRevision != ManaRevision)
		{
			ManaRevision = Own.manaRevision;
			AttackDriver->set_server_mana(Own.mana, Own.manaMax, Own.manaDelay, Own.manaRate, Own.manaAtNs);
		}
		if (Own.manaSyncRevision != ManaSyncRevision)
		{
			ManaSyncRevision = Own.manaSyncRevision;
			AttackDriver->sync_mana(Own.manaSync.value, Own.manaSync.maximum, Own.manaSync.delay, Own.manaSync.factor, Own.manaSync.atNs);
		}
	}
	// The heal numbers (stage P3.6).
	if (USwcRangedView* RangedView = Avatar->GetRangedView())
	{
		RangedView->SetHeals(Own.healRevision, Own.heals);
	}
	const FString Text = FString::Printf(TEXT("bag %.1f of %.1f%s%s, equip load %s, statuses [%s], speed table %.2f; nav %d run %d sprint %d dodge %d jump %d abilities %d attack %d"),
		BagWeight / 10.0f, Limits.bagLimit / 10.0f, Merged.encumbered() ? TEXT(" (encumbered)") : TEXT(""),
		Merged.encumbered_movement() ? TEXT(" (the movement limit)") : TEXT(""), UTF8_TO_TCHAR(swc::move::equip_load_text(Merged.equipLoad)), *Statuses,
		Merged.speed_multiplier(), Merged.can_nav() ? 1 : 0, Merged.can_run() ? 1 : 0, Merged.can_sprint() ? 1 : 0, Merged.can_dodge() ? 1 : 0,
		Merged.can_jump() ? 1 : 0, Merged.can_use_abilities() ? 1 : 0, Merged.can_attack_from_rest() ? 1 : 0);
	if (Text != LastLimits)
	{
		LastLimits = Text;
		UE_LOG(LogSwcBackend, Log, TEXT("move: limits: %s"), *Text);
	}
}

void USwcBackendSubsystem::UpdateAbilities(const swc::net::RepClient::OwnEntity& Own, bool bServerClock, int64 ServerOffsetNs)
{
	// The bar of each weapon slot: the bar whose abilities the graph of the weapon has (the ability table of the weapon).
	std::array<std::array<uint32, 3>, 2> Bars{};
	for (uint32 Index = 0; Index < 2; ++Index)
	{
		const swc::combat::Graph* Graph = Avatar.IsValid() ? Avatar->GetActions()->GetDriverGraph(24 + Index) : nullptr;
		// A ranged weapon (stage P3.6): the bar whose abilities its program has.
		const swc::ranged::Program* Program = Avatar.IsValid() ? Avatar->GetActions()->GetRangedProgram(24 + Index) : nullptr;
		if (Graph == nullptr && Program == nullptr)
		{
			continue;
		}
		for (const swc::net::RepClient::OwnEntity::AbilityBar& Bar : Own.abilityBars)
		{
			const bool bOfWeapon = std::any_of(Bar.abilities.begin(), Bar.abilities.end(), [Graph, Program](std::uint32_t Id) {
				return Id != 0 && ((Graph != nullptr && Graph->ability(Id) != nullptr) || (Program != nullptr && Program->ability(Id) != nullptr));
			});
			if (bOfWeapon)
			{
				for (std::size_t Place = 0; Place < 3 && Place < Bar.abilities.size(); ++Place)
				{
					Bars[Index][Place] = Bar.abilities[Place];
				}
				break;
			}
		}
	}
	if (Bars != SentBars)
	{
		SentBars = Bars;
		AttackDriver->set_bars(Bars[0], Bars[1]);
		FString Text;
		for (uint32 Index = 0; Index < 2; ++Index)
		{
			const swc::combat::Graph* Graph = Avatar.IsValid() ? Avatar->GetActions()->GetDriverGraph(24 + Index) : nullptr;
			Text += FString::Printf(TEXT("%sslot %u:"), Index == 0 ? TEXT("") : TEXT("; "), 24 + Index);
			const swc::ranged::Program* Program = Avatar.IsValid() ? Avatar->GetActions()->GetRangedProgram(24 + Index) : nullptr;
			for (const uint32 Id : Bars[Index])
			{
				const swc::combat::Ability* Ability = Graph != nullptr && Id != 0 ? Graph->ability(Id) : nullptr;
				const swc::ranged::Ability* Ranged = Program != nullptr && Id != 0 ? Program->ability(Id) : nullptr;
				Text += FString::Printf(TEXT(" %s"), Ability != nullptr ? UTF8_TO_TCHAR(Ability->name.c_str())
						: Ranged != nullptr                         ? UTF8_TO_TCHAR(Ranged->id.c_str())
						: Id != 0                                   ? TEXT("(not in the graph)")
																	: TEXT("-"));
			}
		}
		UE_LOG(LogSwcBackend, Log, TEXT("ability: the bars of the weapons: %s"), *Text);
	}
	// The learned nodes of the weapon trees: the conditions of the transitions inside the abilities and the selections, and the
	// rows of the strikes.
	if (Avatar.IsValid())
	{
		Avatar->GetActions()->SetLearnedNodes(Own.nodesRevision, Own.learnedNodes);
	}
	// Stage MVP-E: the ability rows of the perks of the equipped items (IfAbilityActive External: the perk terms and the selections by a
	// perk, the artifact Whirlwind). The items of the backend rule (world/gear.cpp Gear::perks): the armor and the jewelry (0 to 7), the
	// heart rune (45), and the active weapon with its off-hand item (24 to 26, 27 to 29; the perks OnActive and OnUnsheathed count only
	// there; the client counts each perk of that weapon).
	if (!bPerkAbilitiesLoaded)
	{
		bPerkAbilitiesLoaded = true;
		FString Text;
		const FString Path = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_GAMEDATA_DIR")), TEXT("perk-data.json"));
		TSharedPtr<FJsonObject> Document;
		if (FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document) && Document.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
			if (Document->TryGetArrayField(TEXT("perks"), Rows))
			{
				for (const TSharedPtr<FJsonValue>& Row : *Rows)
				{
					TArray<uint32>& Out = PerkAbilities.AddDefaulted_GetRef();
					const TArray<TSharedPtr<FJsonValue>>& Columns = Row->AsArray();
					if (Columns.Num() > 8 && Columns[8]->Type == EJson::Array)
					{
						for (const TSharedPtr<FJsonValue>& Ability : Columns[8]->AsArray())
						{
							Out.Add(swc::combat::progression_id(TCHAR_TO_UTF8(*Ability->AsString())));
						}
					}
				}
			}
		}
		UE_LOG(LogSwcBackend, Log, TEXT("ability: %d perk(s) of the wire list with their ability rows (%s)"), PerkAbilities.Num(), *Path);
	}
	const uint64 PerkFactsKey = (static_cast<uint64>(Own.paperdollRevision) << 32) ^ ((Own.activeMap >> 24) & 0x7);
	if (Own.nodesRevision != NodesRevision || PerkFactsKey != SentPerkFactsKey)
	{
		NodesRevision = Own.nodesRevision;
		SentPerkFactsKey = PerkFactsKey;
		std::set<std::uint32_t> Perks;
		for (const auto& [Slot, Item] : Own.paperdollItems)
		{
			const uint32 Hand = (Own.activeMap >> 24) & 0x7;
			const bool bWeapon = Slot >= 24 && Slot <= 29;
			const uint32 Option = bWeapon ? (Slot - 24) % 3 : 0;
			if (!(Slot <= 7 || Slot == 45 || (bWeapon && (Hand & (1u << Option)) != 0)))
			{
				continue;
			}
			for (const std::uint32_t Perk : Item.perks)
			{
				if (Perk > 0 && PerkAbilities.IsValidIndex(static_cast<int32>(Perk) - 1))
				{
					for (const uint32 Ability : PerkAbilities[Perk - 1])
					{
						Perks.insert(Ability);
					}
				}
			}
		}
		AttackDriver->set_facts(std::set<std::uint32_t>(Own.learnedNodes.begin(), Own.learnedNodes.end()), Perks);
		UE_LOG(LogSwcBackend, Log, TEXT("ability: %d learned node(s) of the weapon trees (the ProgressionPoint state), %d ability row(s) of the equipped perks"),
			static_cast<int32>(Own.learnedNodes.size()), static_cast<int32>(Perks.size()));
	}
	// The hit counts of the tracked abilities (slot 46, fields 1 and 2): IfAbilityNumHits of the cast that plays.
	if (Own.hitRevision != HitRevision)
	{
		HitRevision = Own.hitRevision;
		AttackDriver->set_ability_hits(Own.abilityHits);
		UE_LOG(LogSwcBackend, Log, TEXT("ability: the hit counts of %d tracked abilit(ies) (the ability state, fields 1 and 2)"), static_cast<int32>(Own.abilityHits.size()));
	}
	// The cooldowns of the server: the end of the own cooldown of the client (Driver::set_server_cooldowns).
	std::vector<std::pair<std::uint32_t, std::uint64_t>> Cooldowns;
	for (const std::vector<swc::net::RepClient::OwnEntity::Cooldown>& Place : Own.cooldowns)
	{
		for (const swc::net::RepClient::OwnEntity::Cooldown& Entry : Place)
		{
			Cooldowns.emplace_back(Entry.ability, Entry.endUs);
		}
	}
	if (Cooldowns != SentCooldowns && bServerClock)
	{
		const int64 Steady = std::chrono::duration_cast<std::chrono::nanoseconds>(swc::net::Clock::now().time_since_epoch()).count();
		const uint64 ServerUs = static_cast<uint64>(Steady + ServerOffsetNs) / 1000;
		AttackDriver->set_server_cooldowns(Cooldowns, ServerUs, Steady);
		// The log: only a new entry or a new end.
		for (const auto& [Ability, EndUs] : Cooldowns)
		{
			const auto Before = std::find_if(SentCooldowns.begin(), SentCooldowns.end(), [Id = Ability](const auto& Entry) { return Entry.first == Id; });
			if (Before == SentCooldowns.end() || Before->second != EndUs)
			{
				const int64 LeftUs = static_cast<int64>(EndUs) - static_cast<int64>(ServerUs);
				UE_LOG(LogSwcBackend, Log, TEXT("ability: the cooldown state of the server: %08x ends in %.2f s%s"), Ability, static_cast<double>(LeftUs) / 1e6,
					Before == SentCooldowns.end() ? TEXT("") : TEXT(" (a new end)"));
			}
		}
		SentCooldowns = Cooldowns;
		if (Cooldowns.empty())
		{
			UE_LOG(LogSwcBackend, Log, TEXT("ability: the cooldown state of the server: no cooldown (the client keeps its own cooldowns)"));
		}
	}
}

uint64 USwcBackendSubsystem::GetServerTimeMicroseconds() const
{
	if (!Connection.IsValid())
	{
		return 0;
	}
	const FSwcRepConnection::FSnapshot Snapshot = Connection->GetSnapshot();
	if (!Snapshot.bServerClock)
	{
		return 0;
	}
	const int64 Steady = std::chrono::duration_cast<std::chrono::nanoseconds>(swc::net::Clock::now().time_since_epoch()).count();
	return static_cast<uint64>(Steady + Snapshot.ServerOffsetNs) / 1000;
}

float USwcBackendSubsystem::GetCameraYawDegrees() const
{
	const UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr || Controller->PlayerCameraManager == nullptr)
	{
		return 0.0f;
	}
	// The Unreal forward vector in the engine space (Unreal Y = -engine Y), then the angle from north to the left.
	const FVector Forward = Controller->PlayerCameraManager->GetCameraRotation().Vector();
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-Forward.X, -Forward.Y)));
}

void USwcBackendSubsystem::StartTables()
{
	if (TablesTask.IsValid())
	{
		return;
	}
	TablesTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []
		{
			// Only files and JSON here (no UObject): the assets of the tables load on the game thread (UpdatePreload).
			const double Start = FPlatformTime::Seconds();
			TSharedPtr<FSwcPlayerTables> Tables = MakeShared<FSwcPlayerTables>();
			FString Report;
			Tables->Looks = MakeShared<FSwcPlayerLooks>();
			if (!Tables->Looks->Load(Report))
			{
				Tables->Looks.Reset();
			}
			Tables->Reports.Add(Report);
			for (int32 Gender = 0; Gender < 2; ++Gender)
			{
				Report.Reset();
				Tables->Locomotions[Gender] = MakeShared<FSwcLocomotion>();
				if (!Tables->Locomotions[Gender]->Load(Gender == 1, Report))
				{
					Tables->Locomotions[Gender].Reset();
				}
				Tables->Reports.Add(Report);
			}
			Report.Reset();
			Tables->Actions = MakeShared<FSwcActions>();
			if (!Tables->Actions->Load(Report))
			{
				Tables->Actions.Reset();
			}
			Tables->Reports.Add(Report);
			Report.Reset();
			Tables->Interactor = MakeShared<FSwcInteractor>();
			if (!Tables->Interactor->Load(Report))
			{
				Tables->Interactor.Reset();
			}
			Tables->Reports.Add(Report);
			Tables->Seconds = FPlatformTime::Seconds() - Start;
			return Tables;
		},
		UE::Tasks::ETaskPriority::BackgroundNormal);
}

const FSwcPlayerLooks* USwcBackendSubsystem::GetPlayerLooks()
{
	if (!bTablesTaken && (!TablesTask.IsValid() || !TablesTask.IsCompleted()))
	{
		return nullptr;
	}
	return TakeTables() ? Looks.Get() : nullptr;
}

bool USwcBackendSubsystem::TakeTables()
{
	if (!bTablesTaken)
	{
		StartTables();
		const double Start = FPlatformTime::Seconds();
		const TSharedPtr<FSwcPlayerTables> Tables = TablesTask.GetResult();
		bTablesTaken = true;
		for (const FString& Report : Tables->Reports)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("character: %s"), *Report);
		}
		UE_LOG(LogSwcBackend, Log, TEXT("character: the player tables: %.2f s on a worker thread, %.3f s of wait on the game thread"),
			Tables->Seconds, FPlatformTime::Seconds() - Start);
		Looks = Tables->Looks;
		Locomotions[0] = Tables->Locomotions[0];
		Locomotions[1] = Tables->Locomotions[1];
		Actions = Tables->Actions;
		Interactor = Tables->Interactor;
	}
	return Looks.IsValid();
}

bool USwcBackendSubsystem::UpdatePreload()
{
	const double Now = FPlatformTime::Seconds();
	PreloadSince = PreloadSince > 0.0 ? PreloadSince : Now;
	if (PreloadState == EPreload::Waiting && TablesTask.IsValid() && TablesTask.IsCompleted())
	{
		const FSwcRepConnection::FSnapshot Snapshot = Connection->GetSnapshot();
		if (!Snapshot.Own.hasAppearance)
		{
			// No appearance yet: wait for it while the cells load, then go on with no preload.
			if (bCellsReady)
			{
				PreloadState = EPreload::Done;
			}
		}
		else if (!TakeTables())
		{
			PreloadState = EPreload::Done;
		}
		else
		{
			TArray<uint8> Appearance;
			TMap<uint32, uint32> Equipment;
			LookInputs(Snapshot, Appearance, Equipment);
			const FSwcPlayerLook Look = Looks->Resolve(Appearance, Equipment);
			TSet<FString> Unique;
			TArray<FSoftObjectPath> Paths;
			const auto Add = [&Unique, &Paths](const FString& Path)
			{
				bool bKnown = false;
				Unique.Add(Path, &bKnown);
				if (!Path.IsEmpty() && !bKnown)
				{
					Paths.Emplace(Path);
				}
			};
			Add(Looks->BaseMesh(Look.bFemale));
			Add(Looks->IdleAnimation(Look.bFemale));
			for (const FSwcLookPart& Part : Look.Parts)
			{
				Add(Part.MeshPath);
			}
			for (const FSwcGearPart& Part : Look.Gear)
			{
				Add(Part.MeshPath);
			}
			if (const TSharedPtr<FSwcLocomotion>& Locomotion = Locomotions[Look.bFemale ? 1 : 0])
			{
				for (const FSwcClip& Clip : Locomotion->Clips)
				{
					Add(Clip.AssetPath);
				}
			}
			if (Actions.IsValid())
			{
				TArray<FString> Clips;
				Actions->ClipAssets(Look.bFemale, Clips);
				for (const FString& Clip : Clips)
				{
					Add(Clip);
				}
			}
			Preload = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority);
			PreloadState = Preload.IsValid() ? EPreload::Loading : EPreload::Done;
			UE_LOG(LogSwcBackend, Log, TEXT("character: %d asset(s) of the look load in the background (the meshes, the clips of the movement and of the actions)"),
				Paths.Num());
		}
	}
	if (PreloadState == EPreload::Loading && (Preload->HasLoadCompleted() || Preload->WasCanceled()))
	{
		PreloadState = EPreload::Done;
		UE_LOG(LogSwcBackend, Log, TEXT("character: the assets of the look are loaded, %.1f s after the in-game phase"), Now - PreloadSince);
	}
	if (PreloadState != EPreload::Done && Now - PreloadSince >= PreloadLimit)
	{
		PreloadState = EPreload::Done;
		UE_LOG(LogSwcBackend, Warning, TEXT("character: the assets of the look are not loaded after %.0f s: the spawn goes on, the rest loads at once"), PreloadLimit);
	}
	return PreloadState == EPreload::Done;
}

void USwcBackendSubsystem::SetSpawnLoading(bool bOn)
{
	if (bOn == bSpawnLoading)
	{
		return;
	}
	bSpawnLoading = bOn;
	if (bOn)
	{
		SavedLoadingTimeLimit = GAsyncLoadingTimeLimit;
		SavedLoadingFullTime = GAsyncLoadingUseFullTimeLimit;
		GAsyncLoadingTimeLimit = FMath::Max(GAsyncLoadingTimeLimit, SpawnLoadingTimeLimit);
		GAsyncLoadingUseFullTimeLimit = 1;
	}
	else
	{
		// The time of the loader goes back to the value of the engine for the play.
		GAsyncLoadingTimeLimit = SavedLoadingTimeLimit;
		GAsyncLoadingUseFullTimeLimit = SavedLoadingFullTime;
	}
	UE_LOG(LogSwcBackend, Log, TEXT("world: the async loader has %.0f ms in each frame%s"), GAsyncLoadingTimeLimit,
		bOn ? TEXT(" during the loading screen (the cells of the place and the assets of the look load)") : TEXT(" again (the end of the screen)"));
}

void USwcBackendSubsystem::UpdateLook()
{
	if (!Connection.IsValid() || !Avatar.IsValid())
	{
		return;
	}
	const FSwcRepConnection::FSnapshot Snapshot = Connection->GetSnapshot();
	LookRevision = Snapshot.Own.lookRevision;
	if (!TakeTables())
	{
		return;
	}
	TArray<uint8> Appearance;
	TMap<uint32, uint32> Equipment;
	LookInputs(Snapshot, Appearance, Equipment);
	const FSwcPlayerLook Look = Looks->Resolve(Appearance, Equipment);
	const TSharedPtr<FSwcLocomotion>& Locomotion = Locomotions[Look.bFemale ? 1 : 0];
	Avatar->ApplyLook(Look, Looks->BaseMesh(Look.bFemale), Looks->IdleAnimation(Look.bFemale), Locomotion);
	// The weapon state of the paperdoll state at the start (protocol notes 4.11: the active weapon is the first weapon
	// slot of activeMap, sheatheMap has the sheathe bit of each weapon); then the keys of the client change it.
	const uint64 ActiveMap = Snapshot.Own.activeMap;
	const uint64 SheatheMap = Snapshot.Own.sheatheMap;
	uint32 Active = Avatar->HasWeapon(Avatar->GetActiveWeaponSlot()) ? Avatar->GetActiveWeaponSlot() : 0;
	bool bDrawn = Active != 0 && Avatar->IsWeaponDrawn();
	if (Active == 0)
	{
		for (uint32 Slot = 24; Slot <= 26 && Active == 0; ++Slot)
		{
			Active = ((ActiveMap >> Slot) & 1) != 0 && Avatar->HasWeapon(Slot) ? Slot : 0;
		}
		for (uint32 Slot = 24; Slot <= 26 && Active == 0; ++Slot)
		{
			Active = Avatar->HasWeapon(Slot) ? Slot : 0;
		}
		bDrawn = Active != 0 && ((SheatheMap >> Active) & 1) == 0;
	}
	Avatar->SetWeaponState(Active, bDrawn);
	// The actions of the weapons: the stances, the draw and the sheathe, the attacks (stage P2.7).
	if (Actions.IsValid())
	{
		Avatar->GetActions()->Setup(Actions, Look.bFemale, AttackDriver);
		Avatar->GetRangedView()->Setup(Actions, AttackDriver);
		// The melee contacts of the strikes: the hit volumes of the creatures (stage P2.8, docs/CREATURES.md).
		Avatar->GetActions()->SetContactQuery([Creatures = TWeakObjectPtr<USwcCreatureSubsystem>(Avatar->GetWorld()->GetSubsystem<USwcCreatureSubsystem>())](
			const FSwcMeleeShape& Shape, std::vector<swc::net::MeleeContactItem>& Out) { if (Creatures.IsValid()) { Creatures->QueryMeleeContacts(Shape, Out); } });
		Avatar->GetActions()->OnGearChanged();
		// The stamina (stage P2.5): the data at the first look, and the equip load class of the armor (slots 0 to 4).
		if (AttackDriver)
		{
			if (!bStaminaSet)
			{
				bStaminaSet = true;
				AttackDriver->set_stamina(Actions->GetStaminaData());
			}
			TArray<FString> Armor;
			for (uint32 Slot = 0; Slot <= 4; ++Slot)
			{
				if (const uint32* Item = Equipment.Find(Slot); Item != nullptr && *Item != 0)
				{
					Armor.Add(Looks->ItemId(*Item));
				}
			}
			float Load = 0.0f;
			const swc::combat::LoadClass Class = Actions->LoadClassOf(Armor, Load);
			EquipLoadClass = static_cast<uint8>(Class);
			AttackDriver->set_load(Class);
			static const TCHAR* const ClassNames[] = {TEXT("light"), TEXT("medium"), TEXT("heavy"), TEXT("overburdened")};
			UE_LOG(LogSwcBackend, Log, TEXT("character: equip load %.1f of %d armor item(s): %s"), Load / 10.0f, Armor.Num(),
				ClassNames[static_cast<int32>(Class)]);
		}
	}
	FString Bytes;
	for (const uint8 Value : Appearance)
	{
		Bytes += FString::Printf(TEXT("%d "), Value);
	}
	UE_LOG(LogSwcBackend, Log, TEXT("character: %s, appearance [%s], %d equipped slot(s), %d of %d part(s) drawn"),
		Look.bFemale ? TEXT("female") : TEXT("male"), *Bytes.TrimEnd(), Equipment.Num(), Avatar->PartCount(), Look.Parts.Num());
	for (const FSwcLookPart& Part : Look.Parts)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("character: slot %s: %s with %s%s"), *Part.Slot.ToString(), *Part.Skin, *Part.Material,
			Part.MeshPath.IsEmpty() ? TEXT(" (not imported)") : TEXT(""));
	}
	for (const FSwcGearPart& Part : Look.Gear)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("character: gear slot %u: %s%s (%s: sheath %s on %s, hand %s on %s)%s"), Part.PaperdollSlot, *Part.Item,
			Part.bAccessory ? TEXT(" accessory") : TEXT(""), *Part.EquipType, *Part.Sheathed.Name, *Part.Sheathed.Bone.ToString(), *Part.Hand.Name,
			*Part.Hand.Bone.ToString(), Part.MeshPath.IsEmpty() ? TEXT(" (not imported)") : TEXT(""));
	}
	if (!Look.Gear.IsEmpty())
	{
		UE_LOG(LogSwcBackend, Log, TEXT("character: weapon slot %u %s, active map %llx, sheathe map %llx"), Active,
			Active == 0 ? TEXT("(none)") : bDrawn ? TEXT("in the hand") : TEXT("in its sheath"), static_cast<unsigned long long>(ActiveMap),
			static_cast<unsigned long long>(SheatheMap));
	}
	for (const FString& Note : Look.Notes)
	{
		UE_LOG(LogSwcBackend, Warning, TEXT("character: %s"), *Note);
	}
	// The appearance preflight of the world entry (AST-215): each part of the character is imported, or a clear warning.
	FString Preflight;
	if (Looks->Preflight(Look, Preflight))
	{
		UE_LOG(LogSwcBackend, Log, TEXT("character: appearance preflight (world): %s"), *Preflight);
	}
	else
	{
		UE_LOG(LogSwcBackend, Warning, TEXT("character: appearance preflight (world): %s"), *Preflight);
	}
	if (const int32 Wanted = Looks->WriteWanted(Look); Wanted > 0)
	{
		UE_LOG(LogSwcBackend, Warning, TEXT("character: %d part(s) are not imported; they are in the wanted list: run tools/convert-player.sh wanted, then tools/import-player.sh"),
			Wanted);
	}
}

void USwcBackendSubsystem::UpdatePlayer()
{
	if (!Connection.IsValid() || !Avatar.IsValid())
	{
		return;
	}
	const ASwcPlayerCharacter* Character = Avatar.Get();
	USwcCharacterMovement* Movement = Character->GetSwcMovement();
	const UWorld* World = Character->GetWorld();
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Movement == nullptr || Controller == nullptr || Controller->PlayerCameraManager == nullptr)
	{
		return;
	}
	const FSwcRepConnection::FSnapshot Snapshot = Connection->GetSnapshot();
	if (Snapshot.Own.travelRevision != SeenTravel)
	{
		SeenTravel = Snapshot.Own.travelRevision;
		Travel(Snapshot.Own);
	}
	if (bTravelWait)
	{
		UpdateTravel();
	}
	Movement->SetPermissions(Snapshot.Own.dataBits, Snapshot.Own.moreDataBits);
	// The life of the character (stage P2.10): the dead mark of the vitals state and the loading screen of a respawn or a far
	// travel (docs/ACTIONS.md "The death and the respawn").
	Avatar->GetActions()->SetLife(Snapshot.Own.dead, Snapshot.Own.loadingScreen || bTravelWait);
	if (ASwcCameraManager* CameraManager = Cast<ASwcCameraManager>(Controller->PlayerCameraManager))
	{
		// The camera: the dead mark (DeathCamera) and the targets of the target lock: the living creatures at their shown feet
		// (USwcCreatureSubsystem, stage P2.6); with no creature view the entities of the backend.
		CameraManager->SetDead(Snapshot.Own.dead);
		CameraManager->SetDrawCandidates(false);
		TArray<FSwcLockCandidate> Candidates;
		if (const USwcCreatureSubsystem* Creatures = World->GetSubsystem<USwcCreatureSubsystem>())
		{
			TArray<FSwcCreatureTarget> Targets;
			Creatures->GetTargets(Targets);
			for (const FSwcCreatureTarget& Target : Targets)
			{
				if (!Target.bDead)
				{
					// The lock point of the slice of the creature (its CameraLockTargetComponent), with the pitch limits of its observer.
					FSwcLockCandidate Candidate{Target.Interest, Target.Position, Target.LockPoint};
					FSwcLockPoint Point;
					if (Creatures->GetLockPoint(Target.Interest, Point))
					{
						Candidate.LockPoint = Point.Point;
						Candidate.bLimitPitch = Point.bLimitPitch;
						Candidate.PitchMin = Point.PitchMin;
						Candidate.PitchMax = Point.PitchMax;
					}
					Candidates.Add(Candidate);
				}
			}
		}
		else
		{
			for (const TPair<uint32, swc::net::WorldPosition>& Other : Snapshot.Others)
			{
				// No creature view: 1.0 m above the feet (an inference).
				Candidates.Add({Other.Key, SwcEngineToUnreal(Other.Value), SwcEngineToUnreal(Other.Value) + FVector(0.0, 0.0, 100.0)});
			}
		}
		CameraManager->SetLockCandidates(MoveTemp(Candidates));
	}
	if (bWaitForFloor && SwcTestFloor::HasGround(Character->GetWorld(), Character->GetFeetLocation()))
	{
		bWaitForFloor = false;
		Movement->SetMovementMode(MOVE_Falling);
		UE_LOG(LogSwcBackend, Log, TEXT("world: the test floor is in the scene: the character drops to it"));
	}
	if (!bSendPosition || bTravelWait)
	{
		Connection->SetMotion(std::nullopt);
		return;
	}
	// The player of this frame, in the engine space of the wire (protocol notes 4.7).
	swc::net::RepClient::Motion Motion;
	FVector Feet = Character->GetFeetLocation();
	if (Movement->IsMovingOnGround() && Movement->CurrentFloor.bBlockingHit)
	{
		// The capsule of the engine floats 1.9 to 2.4 cm above its floor: the feet of the character are on the floor.
		Feet.Z -= Movement->CurrentFloor.FloorDist;
	}
	Motion.character = SwcUnrealToEngine(Feet);
	Motion.camera = SwcUnrealToEngine(Controller->PlayerCameraManager->GetCameraLocation());
	Motion.yaw = FMath::DegreesToRadians(GetCameraYawDegrees());
	Motion.pitch = -FMath::DegreesToRadians(static_cast<float>(FRotator::NormalizeAxis(Controller->PlayerCameraManager->GetCameraRotation().Pitch)));
	Motion.forward = Movement->IsForwardHeld();
	if (const ASwcPlayerController* Player = Cast<ASwcPlayerController>(Controller))
	{
		Motion.pressSerial = Player->GetPressSerial();
		for (const uint32 Bit : Player->GetPressBits())
		{
			Motion.pressBits.push_back(Bit);
		}
	}
	// The depth class of the feet (stage WF5; mask bit 3), and in a swim the water below the feet (the ground distance of the retail
	// client: set3-swim/swim-heights.json, water_below_feet_m).
	Motion.depthClass = static_cast<uint8>(Movement->GetWaterClass());
	Character->GetActions()->SetSwimming(Movement->IsSwimming());
	if (Movement->IsFalling() || Movement->IsSwimming())
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, Feet, Feet - FVector(0.0, 0.0, MaxAirHeight), ECC_WorldStatic);
		Motion.airHeight = static_cast<float>((bHit ? Hit.Distance : MaxAirHeight) / 100.0);
	}
	// The aim point of the camera for the direction of the shot items (stage P3.6).
	if (FVector Aim; Character->GetRangedView() != nullptr && Character->GetRangedView()->GetAimPoint(Aim))
	{
		Motion.hasAimPoint = true;
		Motion.aimPoint = SwcUnrealToEngine(Aim);
	}
	Connection->SetMotion(Motion);
	// The interactions (stage P2.10): the focus at the feet, the key E, the use, and the replies of the session. The death and a
	// loading screen end a use (DisableInteractor of the action Death); a timed use plays the action DelayedInteraction.
	if (Interactor.IsValid())
	{
		Interactor->SetKey(bInteractKey);
		// A swim blocks the interactions (only Reaction, Death, and Respawn can start in a swim: player.grid; research wf5-swim).
		Interactor->Update(Snapshot, *Connection, Motion.character, Snapshot.Own.dead || Snapshot.Own.loadingScreen || bTravelWait || Movement->IsSwimming(),
			FPlatformTime::Seconds(), World->GetSubsystem<USwcCreatureSubsystem>());
		Avatar->GetActions()->SetTimedUse(Interactor->IsTimedUse(), Interactor->GetUseKind(), Interactor->GetUseTool(), Interactor->GetUsePlace());
		// The Talk (stage P4.8, docs/PERSONS.md): the input lock until the answer 6709, the camera of the line (a cut), and the
		// HidePlayerAvatar mark of its preset.
		const swc::interact::Conversation& Talk = Interactor->GetConversation();
		Avatar->GetActions()->SetConversation(Talk.locked());
		USwcPersonSubsystem* Persons = World->GetSubsystem<USwcPersonSubsystem>();
		if (Persons != nullptr)
		{
			// The clips of the talk timelines of the person: the greet and the interact loop in the scene, the bye at its end.
			Persons->SetTalking(Interactor->GetConversationKey(), Talk.scene());
		}
		const FSwcPerson* Person = Talk.scene() && Persons != nullptr ? Persons->FindByInteraction(Interactor->GetConversationKey()) : nullptr;
		const FSwcPerson::FLine* Line = Person != nullptr && Person->Line.bValid ? &Person->Line : nullptr;
		if (Talk.scene() && Line == nullptr && ConversationLine.IsEmpty())
		{
			ConversationLine = TEXT("?");
			UE_LOG(LogSwcBackend, Warning, TEXT("interact: no line and no camera for the person of %s (persons.json)"), *Interactor->GetConversationKey());
		}
		if (ASwcCameraManager* CameraManager = Cast<ASwcCameraManager>(Controller->PlayerCameraManager))
		{
			CameraManager->SetSceneView(Line != nullptr, Line != nullptr ? Line->Camera : FVector::ZeroVector, Line != nullptr ? Line->LookAt : FVector::ZeroVector,
				Line != nullptr ? Line->Fov : 0.0f);
		}
		// HidePlayerAvatar hides the character; a two-shot preset shows the dummy of the player in its place.
		if (const bool bHide = Line != nullptr && Line->bHidePlayer && !Line->bDummy; Avatar->IsHidden() != bHide)
		{
			Avatar->SetActorHiddenInGame(bHide);
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the character is %s (HidePlayerAvatar of the camera of the line)"), bHide ? TEXT("hidden") : TEXT("shown again"));
		}
		const bool bDummy = Line != nullptr && Line->bDummy;
		if (bDummy)
		{
			Avatar->SetDummy(&Line->DummyFeet, Line->DummyYaw, Line->bDummyArmed);
		}
		if (bDummy != bDummyShown)
		{
			bDummyShown = bDummy;
			if (!bDummy)
			{
				Avatar->SetDummy(nullptr);
			}
			UE_LOG(LogSwcBackend, Log, TEXT("interact: %s"), bDummy ? *FString::Printf(TEXT("the dummy of the player at %.2f %.2f %.2f m, yaw %.1f"), Line->DummyFeet.X / 100.0,
				-Line->DummyFeet.Y / 100.0, Line->DummyFeet.Z / 100.0, Line->DummyYaw) : TEXT("the body goes back to the character"));
		}
		if (Line != nullptr && ConversationLine.IsEmpty())
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the conversation with %s (%s)"), *Person->Name, *Person->NpcId);
		}
		ConversationLine = Line != nullptr ? FString::Printf(TEXT("%s: %s"), *Line->Speaker, *Line->Text) : Talk.scene() ? ConversationLine : FString();
	}
	// The server copy of the stamina (the Stamina state): the client keeps its prediction (Driver::set_server_stamina).
	if (AttackDriver && Snapshot.Own.hasStamina && Snapshot.Own.staminaRevision != StaminaRevision)
	{
		StaminaRevision = Snapshot.Own.staminaRevision;
		AttackDriver->set_server_stamina(Snapshot.Own.stamina, Snapshot.Own.staminaRegenDelay, Snapshot.Own.staminaRegenMultiplier, Snapshot.Own.staminaAtNs);
	}
	if (AttackDriver)
	{
		UpdateAbilities(Snapshot.Own, Snapshot.bServerClock, Snapshot.ServerOffsetNs);
	}
	UpdateMovementLimits(Snapshot.Own);
	// The melee contacts of this frame: list 3 of the next input entry (stage P2.8).
	if (std::vector<swc::net::MeleeContactItem> Contacts = Avatar->GetActions()->TakeContacts(); !Contacts.empty())
	{
		Connection->QueueContacts(Contacts);
	}
	// The camera with -SwcMoveSamples (stage P3.G, the pitch limits and the arm of the retail camera take): a line when the view turns.
	static const bool bCameraSamples = FCString::Strfind(FCommandLine::Get(), TEXT("-SwcMoveSamples=")) != nullptr;
	if (!bPlayerRun && bCameraSamples && Controller->PlayerCameraManager != nullptr)
	{
		const FRotator CameraView = Controller->PlayerCameraManager->GetCameraRotation();
		if (!CameraView.Equals(LastCameraSample, 0.2))
		{
			LastCameraSample = CameraView;
			UE_LOG(LogSwcBackend, Log, TEXT("camera: sample pitch %.2f yaw %.2f, %.2f m from the feet, state %s (world %.3f s)"), FRotator::NormalizeAxis(CameraView.Pitch),
				FRotator::NormalizeAxis(CameraView.Yaw + 90.0), FVector::Dist(Controller->PlayerCameraManager->GetCameraLocation(), Feet) / 100.0,
				*GetCameraStateName(), World->GetTimeSeconds());
		}
	}
	if (Movement->GetMoveState() != LastMove)
	{
		LastMove = Movement->GetMoveState();
		UE_LOG(LogSwcBackend, Log, TEXT("move: %s (fragment %s), %.2f m/s, feet %.3f %.3f %.3f m"), *UEnum::GetValueAsString(LastMove),
			*Movement->GetFragment(), Movement->GetHorizontalSpeed(), Motion.character.x, Motion.character.y, Motion.character.z);
	}
	// A speed sample each second while the character moves (development runs: the speed checks of stage P3.3);
	// -SwcMoveSamples=SECONDS sets the interval (the time and distance checks of stage P3.G); also in the air (a standing jump).
	if (!bPlayerRun && (Movement->GetHorizontalSpeed() > 0.05f || FMath::Abs(Movement->Velocity.Z) > 5.0 || Movement->IsFalling())
		&& World->GetTimeSeconds() >= NextMoveSample)
	{
		static const double SampleInterval = []
		{
			double Value = 1.0;
			FParse::Value(FCommandLine::Get(), TEXT("-SwcMoveSamples="), Value);
			return FMath::Clamp(Value, 0.02, 10.0);
		}();
		NextMoveSample = World->GetTimeSeconds() + SampleInterval;
		UE_LOG(LogSwcBackend, Log, TEXT("move: sample %s, %s, %.3f m/s, feet %.3f %.3f %.3f m, water class %d depth %.3f m (world %.3f s)"),
			*UEnum::GetValueAsString(LastMove), *Movement->GetMotionName(), Movement->GetHorizontalSpeed(), Motion.character.x, Motion.character.y,
			Motion.character.z, static_cast<int32>(Movement->GetWaterClass()), Movement->GetWaterDepth(), World->GetTimeSeconds());
	}
	if (!bPlayerRun && GEngine != nullptr && SwcDebugLines())
	{
		// The movement state for the videos of a development run.
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0x5357434D), 1.0f, FColor::Yellow,
			FString::Printf(TEXT("move %s, %s (%s%s), %.2f m/s, feet %.2f %.2f %.2f m, yaw %.1f, camera %s %.2f m"),
				*UEnum::GetValueAsString(Movement->GetMoveState()), *Movement->GetFragment(), *Movement->GetMotionName(),
				Movement->IsAutoRun() ? TEXT(", autorun") : TEXT(""), Movement->GetHorizontalSpeed(), Motion.character.x,
				Motion.character.y, Motion.character.z, FMath::RadiansToDegrees(Motion.yaw), *GetCameraStateName(),
				FVector::Dist(Controller->PlayerCameraManager->GetCameraLocation(), Feet) / 100.0));
		if (AttackDriver)
		{
			// The stamina of the client and the last dodge request (stage P2.5).
			const swc::combat::Driver::Snapshot Attack = AttackDriver->snapshot();
			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0x53574353), 1.0f, FColor::Cyan,
				FString::Printf(TEXT("stamina %.1f of %.1f%s%s, dodge %u, last request: %s"), Attack.stamina, Attack.staminaMax,
					Attack.exhausted ? TEXT(", exhausted") : TEXT(""), Attack.winded > 0.0f ? TEXT(", winded") : TEXT(""),
					Attack.dodge ? Attack.dodge->number : 0u, Attack.lastVerdict ? UTF8_TO_TCHAR(swc::combat::dodge_verdict_text(*Attack.lastVerdict)) : TEXT("none")));
		}
		// The life (stage P2.10): the death screen of the UI track comes later; a development key sends the give-up.
		const swc::net::RepClient::OwnEntity& Own = Snapshot.Own;
		FString Life = FString::Printf(TEXT("health %.0f of %.0f"), Own.health, Own.healthMax);
		if (Own.dead && Own.deathNotices > 0)
		{
			const int64 Steady = std::chrono::duration_cast<std::chrono::nanoseconds>(swc::net::Clock::now().time_since_epoch()).count();
			const double Left = Own.respawnScreenTime - static_cast<double>(Steady - Own.deathNoticeNs) / 1e9;
			Life += Left > 0.0 ? FString::Printf(TEXT(", YOU DIED: the respawn screen in %.0f s"), FMath::CeilToDouble(Left))
							   : FString(TEXT(", the respawn screen: F8 (development) sends the give-up"));
		}
		if (Own.loadingScreen)
		{
			Life += TEXT(", loading screen");
		}
		if (bTravelWait)
		{
			Life += TEXT(", the cells of the place load");
		}
		if (Interactor.IsValid())
		{
			const FString Prompt = Interactor->DebugText(FPlatformTime::Seconds());
			if (!Prompt.IsEmpty())
			{
				Life += TEXT(", ") + Prompt;
			}
			if (!ConversationLine.IsEmpty())
			{
				Life += TEXT("\n") + ConversationLine;
			}
		}
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0x5357434C), 1.0f, Own.dead ? FColor::Red : FColor::Green, Life);
	}
}

void USwcBackendSubsystem::Travel(const swc::net::RepClient::OwnEntity& Own)
{
	ASwcPlayerCharacter* Character = Avatar.Get();
	UWorld* World = Character != nullptr ? Character->GetWorld() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Character == nullptr || Controller == nullptr)
	{
		return;
	}
	// The client copies the movement state of the server to its own state (0x486EA80): the place, the rotation, and the look,
	// with no step between the two places (protocol notes 4.16). The rotation of a travel is the identity: the character and
	// the camera look north.
	const FVector Feet = SwcEngineToUnreal(Own.position);
	const FQuat Rotation(Own.rotation.x, Own.rotation.y, Own.rotation.z, Own.rotation.w);
	const FVector North = Rotation.RotateVector(FVector(0.0, 1.0, 0.0));
	const swc::net::WorldPosition Ahead{Own.position.x + static_cast<float>(North.X), Own.position.y + static_cast<float>(North.Y), Own.position.z};
	const FVector Forward = (SwcEngineToUnreal(Ahead) - Feet).GetSafeNormal();
	const double Distance = FVector::Dist(Character->GetFeetLocation(), Feet) / 100.0;
	// The cells of the new place load first, as at the spawn (the ground check of the place must not meet the character): the
	// character waits at its old place with no movement, and the input reports have no position field in that time.
	USwcCharacterMovement* Movement = Character->GetSwcMovement();
	Movement->StopMovementImmediately();
	Movement->DisableMovement();
	bTravelWait = true;
	SetSpawnLoading(true);   // the loading screen of the travel: the cells of the new place load as at the world entry
	TravelSince = FPlatformTime::Seconds();
	TravelFeet = Feet;
	TravelForward = Forward;
	UE_LOG(LogSwcBackend, Log, TEXT("world: a travel of %.1f m to %.2f %.2f %.2f m (teleport number %u): the cells of the place load"),
		Distance, Own.position.x, Own.position.y, Own.position.z, static_cast<unsigned>(Own.teleportId & 0x0F));
}

void USwcBackendSubsystem::UpdateTravel()
{
	ASwcPlayerCharacter* Character = Avatar.Get();
	UWorld* World = Character != nullptr ? Character->GetWorld() : nullptr;
	if (World == nullptr || !SwcWorldStreaming::IsReadyAt(World, TravelFeet, 90.0, TravelForward))
	{
		return;
	}
	bTravelWait = false;
	SetSpawnLoading(false);
	SwcWorldStreaming::Release();
	const bool bGround = SwcTestFloor::HasGround(World, TravelFeet);
	if (!bGround)
	{
		// No ground at the new place (a region that is not in the level): the test floor of the movement there.
		SwcTestFloor::Spawn(World, TravelFeet - FVector(0.0, 0.0, SpawnLift), TravelForward);
		bWaitForFloor = true;
	}
	// The client copies the movement state of the server to its own state (0x486EA80): the place, the rotation, and the look,
	// with no step between the two places (protocol notes 4.16).
	Character->SetActorLocationAndRotation(TravelFeet + FVector(0.0, 0.0, Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()),
		TravelForward.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
	if (APlayerController* Controller = World->GetFirstPlayerController())
	{
		Controller->SetControlRotation(FRotator(0.0, TravelForward.Rotation().Yaw, 0.0));
	}
	if (bGround)
	{
		Character->GetSwcMovement()->SetMovementMode(MOVE_Falling);
	}
	UE_LOG(LogSwcBackend, Log, TEXT("world: the character is at the place of the travel, %.1f s after it (%s)"), FPlatformTime::Seconds() - TravelSince,
		bGround ? TEXT("the place has ground: it drops to it") : TEXT("no ground: the test floor of the movement"));
}

bool USwcBackendSubsystem::RequestRespawn()
{
	return RequestRespawn(true, FString());
}

bool USwcBackendSubsystem::RequestRespawn(bool bRandom, const FString& HomeId)
{
	if (!Connection.IsValid() || !bInGame)
	{
		return false;
	}
	const FSwcRepConnection::FSnapshot Snapshot = Connection->GetSnapshot();
	// The death screen shows its button "Respawn" only after the time of the death notice (protocol notes 4.18).
	const int64 Steady = std::chrono::duration_cast<std::chrono::nanoseconds>(swc::net::Clock::now().time_since_epoch()).count();
	const double Since = Snapshot.Own.deathNotices > 0 ? static_cast<double>(Steady - Snapshot.Own.deathNoticeNs) / 1e9 : 0.0;
	if (!Snapshot.Own.dead || Snapshot.Own.loadingScreen || Snapshot.Own.deathNotices == 0 || Since < Snapshot.Own.respawnScreenTime)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("life: no respawn request now (%s)"), !Snapshot.Own.dead ? TEXT("the character lives")
			: Snapshot.Own.loadingScreen ? TEXT("a loading screen shows") : TEXT("the respawn screen is not there yet"));
		return false;
	}
	Connection->RequestRespawn(bRandom, HomeId);
	UE_LOG(LogSwcBackend, Log, TEXT("life: the respawn request (%s), %.1f s after the death notice"),
		bRandom ? TEXT("Random Location") : *FString::Printf(TEXT("the home point %s"), *HomeId), Since);
	return true;
}

bool USwcBackendSubsystem::EquipItem(int32 BagSlot, uint32 PaperdollSlot, uint32 Quantity, const swc::net::ItemInstanceId& Instance)
{
	if (!Connection.IsValid() || !bInGame)
	{
		return false;
	}
	Connection->EquipItem(BagSlot, PaperdollSlot, Quantity, Instance);
	return true;
}

bool USwcBackendSubsystem::UnequipItem(uint32 PaperdollSlot, int32 BagSlot, uint32 Quantity)
{
	if (!Connection.IsValid() || !bInGame)
	{
		return false;
	}
	Connection->UnequipItem(PaperdollSlot, BagSlot, Quantity);
	return true;
}

bool USwcBackendSubsystem::SwapItems(uint32 SlotTo, uint32 SlotFrom, const swc::net::ItemInstanceId& InstanceTo, const swc::net::ItemInstanceId& InstanceFrom)
{
	if (!Connection.IsValid() || !bInGame)
	{
		return false;
	}
	Connection->SwapItems(SlotTo, SlotFrom, InstanceTo, InstanceFrom);
	return true;
}

bool USwcBackendSubsystem::UseItem(uint32 BagSlot)
{
	if (!Connection.IsValid() || !bInGame)
	{
		return false;
	}
	Connection->UseItem(BagSlot);
	return true;
}

bool USwcBackendSubsystem::DropItems(const std::vector<swc::net::DropItemEntry>& Entries)
{
	if (!Connection.IsValid() || !bInGame || Entries.empty())
	{
		return false;
	}
	Connection->DropItems(Entries);
	return true;
}

bool USwcBackendSubsystem::DropPaperdollItem(uint32 PaperdollSlot, uint32 Quantity)
{
	if (!Connection.IsValid() || !bInGame)
	{
		return false;
	}
	Connection->DropPaperdollItem(PaperdollSlot, Quantity);
	return true;
}

void USwcBackendSubsystem::OpenLootBag(uint32 Interest)
{
	if (Interactor.IsValid())
	{
		Interactor->OpenBag(Interest);
	}
}

void USwcBackendSubsystem::EndConversation()
{
	if (Interactor.IsValid())
	{
		Interactor->EndConversation();
	}
}

void USwcBackendSubsystem::CloseLootBag()
{
	if (Interactor.IsValid())
	{
		Interactor->CloseBag();
	}
}

uint32 USwcBackendSubsystem::GetOpenLootBag() const
{
	return Interactor.IsValid() ? Interactor->GetOpenBag() : 0;
}

bool USwcBackendSubsystem::GetInteractPrompt(FSwcInteractPrompt& Out) const
{
	if (!Interactor.IsValid())
	{
		Out = FSwcInteractPrompt();
		return false;
	}
	return Interactor->GetPrompt(FPlatformTime::Seconds(), Out);
}

bool USwcBackendSubsystem::TakeAllItems()
{
	if (!Connection.IsValid() || !bInGame)
	{
		return false;
	}
	Connection->TakeAllItems();
	return true;
}

bool USwcBackendSubsystem::TakeItem(const swc::net::ItemInstanceId& Instance, uint32 Quantity)
{
	if (!Connection.IsValid() || !bInGame)
	{
		return false;
	}
	Connection->TakeItem(Instance, Quantity);
	return true;
}

FString USwcBackendSubsystem::GetCameraStateName() const
{
	const UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	const ASwcCameraManager* CameraManager = Controller != nullptr ? Cast<ASwcCameraManager>(Controller->PlayerCameraManager) : nullptr;
	return CameraManager != nullptr ? CameraManager->GetStateName() : FString();
}

void USwcBackendSubsystem::ReturnToMenu()
{
	if (!bFrontEnd || !Connection.IsValid() || bReturning)
	{
		return;
	}
	bReturning = true;
	UE_LOG(LogSwcBackend, Log, TEXT("world: the return to the character selection: the carrier disconnect"));
	Connection->Leave(TEXT("the player goes back to the character selection"));
}

void USwcBackendSubsystem::ResetWorldSession()
{
	SetSpawnLoading(false);
	Connection.Reset();
	bEnteringWorld = false;
	EnterError.Reset();
	InGameSince = 0.0;
	bInGame = false;
	bQuitting = false;
	Avatar.Reset();
	Camera.Reset();
	bInteractKey = false;
	ConversationLine.Reset();
	bDummyShown = false;
	if (Preload.IsValid())
	{
		Preload->CancelHandle();
	}
	Preload.Reset();
	PreloadState = EPreload::Waiting;
	PreloadSince = 0.0;
	bCellsReady = false;
	LongestFrame = 0.0f;
	bFramesLogged = false;
	AttackDriver.reset();
	bStaminaSet = false;
	StaminaRevision = 0;
	SentBars = {};
	NodesRevision = 0;
	HitRevision = 0;
	SentCooldowns.clear();
	BagRevision = MAX_uint32;
	BagWeight = 0;
	EquipLoadClass = 0;
	LastLimits.Reset();
	ManaRevision = MAX_uint32;
	ManaSyncRevision = 0;
	LastHealth = 0.0f;
	bTestFloor = false;
	bWaitForFloor = false;
	bSendPosition = false;
	LastMove = ESwcMove::Idle;
	NextMoveSample = 0.0;
	LastCameraSample = FRotator::ZeroRotator;
	LookRevision = MAX_uint32;
	SeenTravel = 0;
	bTravelWait = false;
	TravelSince = 0.0;
	TravelFeet = FVector::ZeroVector;
	TravelForward = FVector::ForwardVector;
	bReturning = false;
}

void USwcBackendSubsystem::OpenMenuLevel()
{
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		Quit(false, TEXT("no world for the return to the menu"));
		return;
	}
	++MenuReturns;
	if (!ReturnReloads())
	{
		// The world stays (AST-203: the level again took 16 s cooked and 81 s in the editor binary): the subsystems clear the
		// actors of the session, and the UI starts its screens and the menu scene again in this world.
		UE_LOG(LogSwcBackend, Log, TEXT("world: the return to the menu (%d): the world stays%s"), MenuReturns,
			EnterError.IsEmpty() ? TEXT("") : TEXT(" with the entry error"));
		OnWorldSessionEnd.Broadcast();
		return;
	}
	const FString Map = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	UE_LOG(LogSwcBackend, Log, TEXT("world: the return to the menu (%d): the level %s again%s"), MenuReturns, *Map,
		EnterError.IsEmpty() ? TEXT("") : TEXT(" with the entry error"));
	UGameplayStatics::OpenLevel(World, FName(*Map));
}

bool USwcBackendSubsystem::ReturnReloads() const
{
	static const bool bOption = FParse::Param(FCommandLine::Get(), TEXT("SwcReturnReload"));
	return bOption || CVarReturnReload.GetValueOnGameThread() != 0;
}

void USwcBackendSubsystem::EndWorldActors()
{
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller != nullptr && Controller->GetPawn() != nullptr)
	{
		Controller->UnPossess();
	}
	if (Avatar.IsValid())
	{
		Avatar->Destroy();
	}
	if (Camera.IsValid())
	{
		Camera->Destroy();
	}
}

void USwcBackendSubsystem::Quit(bool bSucceeded, const TCHAR* Cause)
{
	UE_LOG(LogSwcBackend, Log, TEXT("client: stop (%s), status %d"), Cause, bSucceeded ? 0 : 1);
	FPlatformMisc::RequestExitWithStatus(false, bSucceeded ? 0 : 1, TEXT("USwcBackendSubsystem"));
}
