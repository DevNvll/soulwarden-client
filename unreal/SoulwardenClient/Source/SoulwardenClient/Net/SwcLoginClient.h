#pragma once

#include "CoreMinimal.h"
#include "Net/SwcSteamStub.h"
#include "Templates/SharedPointer.h"

class FJsonObject;

/** One character of the login information (GET /prod/game/getlogininfo, Soulwarden protocol notes 2). */
struct FSwcCharacterInfo
{
	FString CharacterId;
	FString Name;
	FString WorldId;
	bool bFtueCompleted = false;
	/** The menu document of the character (protocol notes 2.2.5). The menu body of a later stage reads it. */
	FString PublishedData;
};

/** One world of the login information. The client checks these values (protocol notes 2, client code 0x6423730). */
struct FSwcWorldInfo
{
	FString WorldId;
	FString Name;
	FString Status;
	FString Type;
	FString Version;
};

/**
 * The service hosts of the channel document: `publicApis[]` by `clientTag`, and the token service of
 * `platformMetadata` (protocol notes 2). The backend finds a route by the host of the request (header
 * X-Soulwarden-Host, as the game hook sends it), thus the client sends each request with its service host.
 */
struct FSwcServiceHosts
{
	FString Gateway;
	FString GatewayCf;
	FString TokenService;
};

/** The login token of the login queue reply (protocol notes 2.1 step 5). The registration request sends it again (3.4). */
struct FSwcLoginToken
{
	FString Signature;
	FString RepAddress;
	FString WorldId;
	FString CharacterId;
	FString PersonaId;
	uint32 SteamAppId = 0;
	FString SteamUserId;
	FString ChannelId;
	FString TicketId;
	/** The low 32 bits of the millisecond values of the reply (protocol notes 3.4). */
	uint32 IssueTime = 0;
	uint32 GenerateTime = 0;
	uint32 AccountAge = 0;
	uint32 TokenVersion = 0;
};

/** The state of the account after the login path. */
struct FSwcAccountSession
{
	FString AccessToken;
	FString PersonaId;
	FString AccountId;
	FSwcServiceHosts Hosts;
	/** The entitlement aliases of the account (protocol notes 4.29). */
	TArray<FString> Entitlements;
	/** The settings of the remote configuration documents (protocol notes 2.3). */
	TMap<FString, bool> RemoteSettings;
	TArray<FSwcWorldInfo> Worlds;
	TArray<FSwcCharacterInfo> Characters;
};

/**
 * The HTTP login path of the retail client (Soulwarden protocol notes 1 and 2), in its order:
 *
 *   GET  <channel host>/STEAM_APP_ID.1063730.json                                 the channel document
 *   POST <token service>/games/new-world/tokens                                    the account token (stub ticket)
 *   GET  <JavelinGatewayService-CF>/prod/credentials/omni                          the signing credentials
 *   POST client.entitlementservice.amazongames.com/players/{persona}/.../sync      the entitlement sync
 *   GET  client.entitlementservice.amazongames.com/players/{persona}/.../entitlements   the entitlement list
 *   GET  ags-javelin-remote-config.s3.amazonaws.com/applications/publicGameplay/...     the gameplay settings
 *   GET  <JavelinGatewayServiceV2>/prod/game/getlogininfo                          worlds and characters
 *
 * Each request goes to the backend on loopback (http://HOST:PORT) with the header X-Soulwarden-Host, as the game hook
 * sends the requests of the retail client. A log line has no token, no Steam id (only its last 4 digits), and no
 * persona name. The callbacks run on the game thread (the HTTP module of the engine).
 */
class FSwcLoginClient : public TSharedFromThis<FSwcLoginClient>
{
public:
	using FDone = TFunction<void(bool bSucceeded, const FString& Error)>;
	using FTokenDone = TFunction<void(bool bSucceeded, const FSwcLoginToken& Token, const FString& Error)>;

	FSwcLoginClient(const FString& InHttpHost, int32 InHttpPort, const FSwcSteamStub& InIdentity);

	/** Runs the login path to the character list. */
	void Start(FDone Done);

	/** POST /prod/game/login/queue/v2: the login token of a character (protocol notes 2.1 step 5). Needs a done Start. */
	void RequestLoginToken(const FString& CharacterId, FTokenDone Done);

	/**
	 * GET /v1/tls/ca.pem: the certificate of the backend for DTLS. The game hook reads it at this route and puts it in
	 * the trust list of the retail client (protocol notes 3.1). The route is a management route: no service host.
	 */
	void RequestBackendCertificate(TFunction<void(bool bSucceeded, const FString& Pem)> Done);

	const FSwcAccountSession& GetSession() const { return Session; }
	const FSwcSteamStub& GetIdentity() const { return Identity; }

	/** The channel id of the Steam application of the game. */
	static const TCHAR* ChannelId;

private:
	using FReply = TFunction<void(int32 Status, const FString& Body)>;

	void Send(const FString& Verb, const FString& ServiceHost, const FString& PathAndQuery, const TMap<FString, FString>& Headers,
		const FString& ContentType, const FString& Body, FReply OnReply);

	void RequestChannel();
	void RequestToken();
	void RequestCredentials();
	void RequestEntitlementSync();
	void RequestEntitlementList();
	void RequestRemoteConfiguration();
	void RequestLoginInfo();
	void Fail(const FString& Error);
	void Succeed();

	TMap<FString, FString> GatewayHeaders() const;
	TMap<FString, FString> PlayerServiceHeaders() const;

	FString HttpHost;
	int32 HttpPort = 0;
	FSwcSteamStub Identity;
	FSwcAccountSession Session;
	FDone OnDone;
};
