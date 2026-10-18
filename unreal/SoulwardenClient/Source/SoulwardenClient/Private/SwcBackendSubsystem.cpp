#include "SwcBackendSubsystem.h"

#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
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

DEFINE_LOG_CATEGORY(LogSwcBackend);

namespace
{
using FPhase = swc::net::RepClient::Phase;

/** The camera of a character at rest at the start of a session (protocol notes 4.7): north, no pitch, 4.0 m behind,
 *  0.5 m to the right, 1.65 m above the feet. */
constexpr float CameraBehind = 4.0f;
constexpr float CameraRight = 0.5f;
constexpr float CameraUp = 1.65f;

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
			[this](const FSwcCharacterInfo& Character) { return PlayCharacter == TEXT("first") || Character.CharacterId == PlayCharacter; });
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
	if (Snapshot.Phase == FPhase::inGame && !bInGame)
	{
		bInGame = true;
		InGameSince = FPlatformTime::Seconds();
		ShowOwnEntity();
	}
	if (bInGame && QuitWhen == TEXT("world") && !bQuitting && FPlatformTime::Seconds() - InGameSince >= StaySeconds)
	{
		bQuitting = true;
		UE_LOG(LogSwcBackend, Log, TEXT("world: %.0f s in the world, %llu state bundles, %llu movement states, %llu input reports, %d other entities; the client leaves"),
			StaySeconds, static_cast<unsigned long long>(Snapshot.Counts.bundles), static_cast<unsigned long long>(Snapshot.Counts.movementStates),
			static_cast<unsigned long long>(Snapshot.Counts.inputReports), Snapshot.OtherEntities);
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

	// A marker of the size of a character at its feet, and the camera of the retail client at the start of a session.
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}
	const swc::net::WorldPosition CameraPosition{Own.position.x + CameraRight, Own.position.y - CameraBehind, Own.position.z + CameraUp};
	const FVector Eye = SwcEngineToUnreal(CameraPosition);
	const FVector Chest = Feet + FVector(0.0, 0.0, 120.0);
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AStaticMeshActor* Body = World->SpawnActor<AStaticMeshActor>(Feet + FVector(0.0, 0.0, 90.0), FRotator::ZeroRotator, Parameters))
	{
		Body->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Body->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
		Body->SetActorScale3D(FVector(0.6, 0.6, 1.8));
		Marker = Body;
	}
	if (ACameraActor* View = World->SpawnActor<ACameraActor>(Eye, (Chest - Eye).Rotation(), Parameters))
	{
		Camera = View;
		if (APlayerController* Controller = World->GetFirstPlayerController())
		{
			Controller->SetViewTarget(View);
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

void USwcBackendSubsystem::Quit(bool bSucceeded, const TCHAR* Cause)
{
	UE_LOG(LogSwcBackend, Log, TEXT("client: stop (%s), status %d"), Cause, bSucceeded ? 0 : 1);
	FPlatformMisc::RequestExitWithStatus(false, bSucceeded ? 0 : 1, TEXT("USwcBackendSubsystem"));
}
