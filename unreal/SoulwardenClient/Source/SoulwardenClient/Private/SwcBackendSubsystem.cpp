#include "SwcBackendSubsystem.h"

#include "Camera/CameraActor.h"
#include "Character/SwcCharacterMovement.h"
#include "Character/SwcActions.h"
#include "Character/SwcLocomotion.h"
#include "Character/SwcPlayerActions.h"
#include "Combat/SwcAttackGraph.h"
#include "Character/SwcPlayerCharacter.h"
#include "Character/SwcPlayerLooks.h"
#include "Character/SwcTestFloor.h"
#include "World/SwcWorldStreaming.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
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

namespace
{
using FPhase = swc::net::RepClient::Phase;

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
} // namespace

void USwcBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
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
		UE_LOG(LogSwcBackend, Log, TEXT("login: character %d: %s, id %s, world %s, menu document %s"), Index + 1,
			bPlayerRun ? TEXT("(name not in the log of a player run)") : *Character.Name, *Character.CharacterId, *Character.WorldId,
			Character.PublishedData.IsEmpty() ? TEXT("no") : TEXT("yes"));
	}
	if (QuitWhen == TEXT("characters"))
	{
		Quit(true, TEXT("the character list is complete"));
		return;
	}
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
	}
}

void USwcBackendSubsystem::EnterWorld(const FString& CharacterId)
{
	if (!Login.IsValid() || Connection.IsValid())
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
	Login->RequestLoginToken(CharacterId,
		[this](bool bSucceeded, const FSwcLoginToken& Token, const FString& Error)
		{
			if (!bSucceeded)
			{
				UE_LOG(LogSwcBackend, Error, TEXT("world: %s"), *Error);
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
	Options.Session.inputStep = [Driver = AttackDriver](std::uint16_t Input, std::int64_t SteadyNs, std::vector<unsigned>& Bits)
	{
		Driver->step(Input, SteadyNs, Bits);
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

bool USwcBackendSubsystem::TickWorld(float)
{
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
	// On the world level (World Partition), the character waits for the ground of its place (stage 1.3).
	if (Snapshot.Phase == FPhase::inGame && !bInGame &&
		SwcWorldStreaming::IsReadyAt(GetGameInstance()->GetWorld(), SwcEngineToUnreal(Snapshot.Own.position)))
	{
		bInGame = true;
		InGameSince = FPlatformTime::Seconds();
		ShowOwnEntity();
		SwcWorldStreaming::Release();   // the character has its own streaming source
	}
	if (bInGame && Snapshot.Own.lookRevision != LookRevision)
	{
		UpdateLook();
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
		return false;
	}
	return true;
}

void USwcBackendSubsystem::ShowOwnEntity()
{
	const FSwcRepConnection::FSnapshot Snapshot = Connection->GetSnapshot();
	const swc::net::RepClient::OwnEntity& Own = Snapshot.Own;
	const FVector Feet = SwcEngineToUnreal(Own.position);
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
	// The facing of the entity: the rotation of the movement state turns north (engine +Y) about the vertical axis.
	const FQuat Rotation(Own.rotation.x, Own.rotation.y, Own.rotation.z, Own.rotation.w);
	const FVector North = Rotation.RotateVector(FVector(0.0, 1.0, 0.0));
	const swc::net::WorldPosition Ahead{Own.position.x + static_cast<float>(North.X), Own.position.y + static_cast<float>(North.Y), Own.position.z};
	const FVector Forward = (SwcEngineToUnreal(Ahead) - Feet).GetSafeNormal();
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
	if (GEngine != nullptr)
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

void USwcBackendSubsystem::UpdateLook()
{
	if (!Connection.IsValid() || !Avatar.IsValid())
	{
		return;
	}
	const FSwcRepConnection::FSnapshot Snapshot = Connection->GetSnapshot();
	LookRevision = Snapshot.Own.lookRevision;
	if (!Looks.IsValid())
	{
		Looks = MakeShared<FSwcPlayerLooks>();
		FString Report;
		const bool bLoaded = Looks->Load(Report);
		UE_LOG(LogSwcBackend, Log, TEXT("character: %s"), *Report);
		if (!bLoaded)
		{
			return;
		}
	}
	TArray<uint8> Appearance;
	if (Snapshot.Own.hasAppearance)
	{
		Appearance.Append(Snapshot.Own.appearance.data(), static_cast<int32>(Snapshot.Own.appearance.size()));
	}
	TMap<uint32, uint32> Equipment;
	for (const auto& [Slot, Item] : Snapshot.Own.equipment)
	{
		Equipment.Add(Slot, Item);
	}
	const FSwcPlayerLook Look = Looks->Resolve(Appearance, Equipment);
	TSharedPtr<FSwcLocomotion>& Locomotion = Locomotions[Look.bFemale ? 1 : 0];
	if (!Locomotion.IsValid())
	{
		Locomotion = MakeShared<FSwcLocomotion>();
		FString Report;
		const bool bLoaded = Locomotion->Load(Look.bFemale, Report);
		UE_LOG(LogSwcBackend, Log, TEXT("character: %s"), *Report);
		if (!bLoaded)
		{
			Locomotion.Reset();
		}
	}
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
	if (!Actions.IsValid())
	{
		Actions = MakeShared<FSwcActions>();
		FString Report;
		const bool bLoaded = Actions->Load(Report);
		UE_LOG(LogSwcBackend, Log, TEXT("character: %s"), *Report);
		if (!bLoaded)
		{
			Actions.Reset();
		}
	}
	if (Actions.IsValid())
	{
		Avatar->GetActions()->Setup(Actions, Look.bFemale, AttackDriver);
		Avatar->GetActions()->OnGearChanged();
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
	Movement->SetPermissions(Snapshot.Own.dataBits, Snapshot.Own.moreDataBits);
	if (ASwcCameraManager* CameraManager = Cast<ASwcCameraManager>(Controller->PlayerCameraManager))
	{
		// The camera: the dead mark (DeathCamera) and the targets of the target lock.
		CameraManager->SetDead(Snapshot.Own.dead);
		CameraManager->SetDrawCandidates(!bPlayerRun);
		TArray<FSwcLockCandidate> Candidates;
		for (const TPair<uint32, swc::net::WorldPosition>& Other : Snapshot.Others)
		{
			Candidates.Add({Other.Key, SwcEngineToUnreal(Other.Value)});
		}
		CameraManager->SetLockCandidates(MoveTemp(Candidates));
	}
	if (bWaitForFloor && SwcTestFloor::HasGround(Character->GetWorld(), Character->GetFeetLocation()))
	{
		bWaitForFloor = false;
		Movement->SetMovementMode(MOVE_Falling);
		UE_LOG(LogSwcBackend, Log, TEXT("world: the test floor is in the scene: the character drops to it"));
	}
	if (!bSendPosition)
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
	if (Movement->IsFalling())
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, Feet, Feet - FVector(0.0, 0.0, MaxAirHeight), ECC_WorldStatic);
		Motion.airHeight = static_cast<float>((bHit ? Hit.Distance : MaxAirHeight) / 100.0);
	}
	Connection->SetMotion(Motion);
	if (Movement->GetMoveState() != LastMove)
	{
		LastMove = Movement->GetMoveState();
		UE_LOG(LogSwcBackend, Log, TEXT("move: %s (fragment %s), %.2f m/s, feet %.3f %.3f %.3f m"), *UEnum::GetValueAsString(LastMove),
			*Movement->GetFragment(), Movement->GetHorizontalSpeed(), Motion.character.x, Motion.character.y, Motion.character.z);
	}
	if (!bPlayerRun && GEngine != nullptr)
	{
		// The movement state for the videos of a development run.
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0x5357434D), 1.0f, FColor::Yellow,
			FString::Printf(TEXT("move %s, %s, %.2f m/s, feet %.2f %.2f %.2f m, yaw %.1f, camera %s %.2f m"),
				*UEnum::GetValueAsString(Movement->GetMoveState()), *Movement->GetFragment(), Movement->GetHorizontalSpeed(), Motion.character.x,
				Motion.character.y, Motion.character.z, FMath::RadiansToDegrees(Motion.yaw), *GetCameraStateName(),
				FVector::Dist(Controller->PlayerCameraManager->GetCameraLocation(), Feet) / 100.0));
	}
}

FString USwcBackendSubsystem::GetCameraStateName() const
{
	const UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	const ASwcCameraManager* CameraManager = Controller != nullptr ? Cast<ASwcCameraManager>(Controller->PlayerCameraManager) : nullptr;
	return CameraManager != nullptr ? CameraManager->GetStateName() : FString();
}

void USwcBackendSubsystem::Quit(bool bSucceeded, const TCHAR* Cause)
{
	UE_LOG(LogSwcBackend, Log, TEXT("client: stop (%s), status %d"), Cause, bSucceeded ? 0 : 1);
	FPlatformMisc::RequestExitWithStatus(false, bSucceeded ? 0 : 1, TEXT("USwcBackendSubsystem"));
}
