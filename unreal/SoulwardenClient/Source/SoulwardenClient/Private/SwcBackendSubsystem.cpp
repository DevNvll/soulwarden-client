#include "SwcBackendSubsystem.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
}

void USwcBackendSubsystem::LogEndpoints() const
{
	UE_LOG(LogSwcBackend, Log, TEXT("backend http://%s:%d (the protocol is stage P3)"), *HttpHost, HttpPort);
}
