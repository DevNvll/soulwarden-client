#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SwcBackendSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSwcBackend, Log, All);

/**
 * The connection to the Soulwarden backend. The client speaks the original wire protocol of the backend
 * (decision of 2026-10-18): the HTTP login path, then DTLS 1.2, the carrier, and the state bundles
 * (Soulwarden protocol notes, sections 2 to 4). Stage P0 has only the endpoints; the protocol is stage P3.
 */
UCLASS(Config = Game)
class SOULWARDENCLIENT_API USwcBackendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	void LogEndpoints() const;

	/** HTTP host and port of the backend (loopback only). The command line can set them: -SwcHttp=HOST:PORT. */
	UPROPERTY(Config)
	FString HttpHost = TEXT("127.0.0.1");

	UPROPERTY(Config)
	int32 HttpPort = 32200;
};
