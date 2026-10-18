#include "SwcBackendSubsystem.h"

#include "Containers/Ticker.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/SwcLoginClient.h"
#include "Net/SwcSteamStub.h"

DEFINE_LOG_CATEGORY(LogSwcBackend);

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
		if (QuitWhen == TEXT("characters"))
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
	}
}

void USwcBackendSubsystem::Quit(bool bSucceeded, const TCHAR* Cause) const
{
	UE_LOG(LogSwcBackend, Log, TEXT("client: stop (%s), status %d"), Cause, bSucceeded ? 0 : 1);
	FPlatformMisc::RequestExitWithStatus(false, bSucceeded ? 0 : 1, TEXT("USwcBackendSubsystem"));
}
