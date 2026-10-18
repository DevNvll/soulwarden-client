#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Net/Codec/SwcRepClient.h"

class FRunnableThread;

/**
 * The connection to the game server of the backend on its own thread (Soulwarden protocol notes 3): one UDP socket on
 * loopback, DTLS 1.2 (the OpenSSL of the engine; the cipher TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384; the client trusts
 * only the certificate of the backend, as the game hook makes the retail client do), and the session (swc::net::
 * RepClient). The thread keeps the carrier alive (acknowledgements, keep-alives, input reports) also while the game
 * thread loads a level. The game thread reads the events and a snapshot of the session.
 *
 * The client sends no DTLS close notice at the end (the retail client sends only the carrier disconnect).
 */
class FSwcRepConnection : public FRunnable
{
public:
	struct FOptions
	{
		/** The address of the game server (the RepAddress of the login token): loopback only. */
		FString Host;
		int32 Port = 0;
		/** The certificate of the backend (PEM, GET /v1/tls/ca.pem). */
		FString CertificatePem;
		swc::net::RepClient::Options Session;
	};

	struct FSnapshot
	{
		swc::net::RepClient::Phase Phase = swc::net::RepClient::Phase::idle;
		swc::net::RepClient::OwnEntity Own;
		swc::net::RepClient::Counts Counts;
		int32 OtherEntities = 0;
		bool bMovementStale = false;
		/** The server clock: server nanoseconds = the steady clock of the client in nanoseconds + ServerOffsetNs. */
		bool bServerClock = false;
		int64 ServerOffsetNs = 0;
		FString EndCause;
		/** True when the thread ended (the session ended, or the transport failed). */
		bool bFinished = false;
	};

	explicit FSwcRepConnection(FOptions InOptions);
	virtual ~FSwcRepConnection() override;

	/** Starts the thread. */
	bool Launch();
	/** Asks the thread to leave the world: the carrier disconnect goes out, then the thread ends. */
	void Leave(const FString& Cause);

	/** @return The log lines of the thread since the last call (with their level). */
	std::vector<swc::net::RepClient::Event> TakeEvents();
	FSnapshot GetSnapshot() const;
	/** The player of the game thread for the next input reports (swc::net::RepClient::set_motion). */
	void SetMotion(const std::optional<swc::net::RepClient::Motion>& InMotion);

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	void Post(swc::net::RepClient::Event::Level Level, const FString& Text);
	void Publish(const swc::net::RepClient& Client, bool bFinished);

	FOptions Options;
	FRunnableThread* Thread = nullptr;
	FThreadSafeBool bStopRequested = false;
	mutable FCriticalSection Lock;
	/** A std::vector: an event has a std::string, which TArray must not move as raw bytes. */
	std::vector<swc::net::RepClient::Event> Events;
	FSnapshot Snapshot;
	FString LeaveCause;
	std::optional<swc::net::RepClient::Motion> Motion;
};
