#include "Net/SwcLoginClient.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SwcBackendSubsystem.h"

const TCHAR* FSwcLoginClient::ChannelId = TEXT("STEAM_APP_ID.1063730");

namespace
{
/**
 * The host of the channel document. The backend route takes each host with "cloudfront.net" (channel_routes.cpp).
 * The host of the retail client is in the game executable; it is not copied into this repository.
 */
const TCHAR* const ChannelHost = TEXT("soulwarden-channel.cloudfront.net");
/** The token service of the account, when the channel document names none (protocol notes 1). */
const TCHAR* const DefaultTokenService = TEXT("tokenservice.amazongames.com");
const TCHAR* const EntitlementHost = TEXT("client.entitlementservice.amazongames.com");
/** The one remote configuration document of the backend (protocol notes 2.3): an exact host and path. */
const TCHAR* const RemoteConfigurationHost = TEXT("ags-javelin-remote-config.s3.amazonaws.com");
const TCHAR* const GameplayConfigurationPath = TEXT("/applications/publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.1063730/versionless");
/** The content type of a gateway request with a body (protocol notes 2.2.1). */
const TCHAR* const GatewayContentType = TEXT("application/x-amz-json-1.1");
/** The client has a limit of 10 seconds for a reply (protocol notes 2.2.3). */
constexpr float RequestTimeoutSeconds = 10.0f;

TSharedPtr<FJsonObject> ParseObject(const FString& Text)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Object))
	{
		return nullptr;
	}
	return Object;
}

FString WriteObject(const TSharedRef<FJsonObject>& Object)
{
	FString Text;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Object, Writer);
	return Text;
}

/** @return The host of an endpoint text: "https://host/", "host", or "host/path" give "host". */
FString HostOf(FString Endpoint)
{
	Endpoint.RemoveFromStart(TEXT("https://"));
	Endpoint.RemoveFromStart(TEXT("http://"));
	int32 Slash = INDEX_NONE;
	if (Endpoint.FindChar(TEXT('/'), Slash))
	{
		Endpoint.LeftInline(Slash);
	}
	return Endpoint;
}

/** @return The first 120 characters of a reply body, on one line, for a log line. */
FString Excerpt(const FString& Body)
{
	FString Text = Body.Left(120);
	Text.ReplaceCharInline(TEXT('\n'), TEXT(' '));
	Text.ReplaceCharInline(TEXT('\r'), TEXT(' '));
	return Body.Len() > 120 ? Text + TEXT("...") : Text;
}

uint32 Low32(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	double Value = 0.0;
	Object->TryGetNumberField(Field, Value);
	return static_cast<uint32>(static_cast<uint64>(static_cast<int64>(Value)) & 0xFFFFFFFFULL);
}
} // namespace

FSwcLoginClient::FSwcLoginClient(const FString& InHttpHost, int32 InHttpPort, const FSwcSteamStub& InIdentity)
	: HttpHost(InHttpHost), HttpPort(InHttpPort), Identity(InIdentity)
{
}

void FSwcLoginClient::Start(FDone Done)
{
	OnDone = MoveTemp(Done);
	Session = FSwcAccountSession();
	UE_LOG(LogSwcBackend, Log, TEXT("login: start, backend http://%s:%d, Steam id %s"), *HttpHost, HttpPort, *Identity.MaskedSteamId());
	RequestChannel();
}

void FSwcLoginClient::Send(const FString& Verb, const FString& ServiceHost, const FString& PathAndQuery,
	const TMap<FString, FString>& Headers, const FString& ContentType, const FString& Body, FReply OnReply)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(Verb);
	Request->SetURL(FString::Printf(TEXT("http://%s:%d%s"), *HttpHost, HttpPort, *PathAndQuery));
	// The header of the game hook: the backend finds the service of a request by this host (http_server.cpp).
	Request->SetHeader(TEXT("X-Soulwarden-Host"), ServiceHost);
	Request->SetHeader(TEXT("X-Soulwarden-Scheme"), TEXT("https"));
	for (const TPair<FString, FString>& Header : Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}
	if (!Body.IsEmpty())
	{
		Request->SetHeader(TEXT("Content-Type"), ContentType);
		Request->SetContentAsString(Body);
	}
	Request->SetTimeout(RequestTimeoutSeconds);
	int32 Query = INDEX_NONE;
	PathAndQuery.FindChar(TEXT('?'), Query);
	const FString Line = FString::Printf(TEXT("%s %s%s"), *Verb, *ServiceHost, *(Query == INDEX_NONE ? PathAndQuery : PathAndQuery.Left(Query)));
	TWeakPtr<FSwcLoginClient> WeakThis = AsShared();
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Line, OnReply = MoveTemp(OnReply)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			if (!WeakThis.IsValid())
			{
				return;
			}
			const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
			const FString Reply = Response.IsValid() ? Response->GetContentAsString() : FString();
			UE_LOG(LogSwcBackend, Log, TEXT("http: %s -> %d (%d bytes)"), *Line, Status, Reply.Len());
			OnReply(Status, Reply);
		});
	Request->ProcessRequest();
}

TMap<FString, FString> FSwcLoginClient::GatewayHeaders() const
{
	return {{TEXT("x-nw-auth"), Session.AccessToken}};
}

TMap<FString, FString> FSwcLoginClient::PlayerServiceHeaders() const
{
	return {{TEXT("x-amzn-token"), Session.AccessToken}, {TEXT("x-amzn-playerid"), Identity.SteamId}};
}

void FSwcLoginClient::RequestChannel()
{
	Send(TEXT("GET"), ChannelHost, FString::Printf(TEXT("/%s.json"), ChannelId), {}, FString(), FString(),
		[this](int32 Status, const FString& Body)
		{
			const TSharedPtr<FJsonObject> Document = Status == 200 ? ParseObject(Body) : nullptr;
			if (!Document.IsValid())
			{
				Fail(FString::Printf(TEXT("channel document: status %d %s"), Status, *Excerpt(Body)));
				return;
			}
			// Region objects with publicApis[] (by clientTag) and platformMetadata.
			for (const auto& Member : Document->Values)
			{
				const TSharedPtr<FJsonObject>* Region = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Apis = nullptr;
				if (!Member.Value->TryGetObject(Region) || !(*Region)->TryGetArrayField(TEXT("publicApis"), Apis))
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& Value : *Apis)
				{
					const TSharedPtr<FJsonObject>* Api = nullptr;
					if (!Value->TryGetObject(Api))
					{
						continue;
					}
					const FString Tag = (*Api)->GetStringField(TEXT("clientTag"));
					const FString Host = HostOf((*Api)->GetStringField(TEXT("apiEndpoint")));
					if (Tag == TEXT("JavelinGatewayServiceV2"))
					{
						Session.Hosts.Gateway = Host;
					}
					else if (Tag == TEXT("JavelinGatewayService-CF"))
					{
						Session.Hosts.GatewayCf = Host;
					}
				}
			}
			const TSharedPtr<FJsonObject>* Metadata = nullptr;
			const TSharedPtr<FJsonObject>* Omni = nullptr;
			if (Document->TryGetObjectField(TEXT("platformMetadata"), Metadata)
				&& (*Metadata)->TryGetObjectField(FString::Printf(TEXT("OMNI.%s"), ChannelId), Omni))
			{
				Session.Hosts.TokenService = HostOf((*Omni)->GetStringField(TEXT("omniTokenUrl")));
			}
			if (Session.Hosts.TokenService.IsEmpty())
			{
				Session.Hosts.TokenService = DefaultTokenService;
			}
			if (Session.Hosts.Gateway.IsEmpty() || Session.Hosts.GatewayCf.IsEmpty())
			{
				Fail(TEXT("channel document: no JavelinGatewayServiceV2 or JavelinGatewayService-CF endpoint"));
				return;
			}
			UE_LOG(LogSwcBackend, Log, TEXT("login: channel: gateway %s, credentials %s, token service %s"),
				*Session.Hosts.Gateway, *Session.Hosts.GatewayCf, *Session.Hosts.TokenService);
			RequestToken();
		});
}

void FSwcLoginClient::RequestToken()
{
	// The body of the client (protocol notes 2.1 step 1). The ticket is the stub ticket of GetAuthTicketForWebApi.
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("platformIdentityType"), TEXT("steam"));
	const TSharedRef<FJsonObject> Auth = MakeShared<FJsonObject>();
	Auth->SetStringField(TEXT("ticket"), Identity.GetAuthTicketHex());
	Body->SetObjectField(TEXT("platformAuth"), Auth);
	Body->SetStringField(TEXT("fallbackToken"), FString());
	Body->SetObjectField(TEXT("attributionData"), MakeShared<FJsonObject>());
	Send(TEXT("POST"), Session.Hosts.TokenService, TEXT("/games/new-world/tokens"), {}, TEXT("application/json"), WriteObject(Body),
		[this](int32 Status, const FString& Reply)
		{
			const TSharedPtr<FJsonObject> Document = Status == 200 ? ParseObject(Reply) : nullptr;
			const TSharedPtr<FJsonObject>* Account = nullptr;
			if (!Document.IsValid() || !Document->TryGetStringField(TEXT("accessToken"), Session.AccessToken)
				|| !Document->TryGetObjectField(TEXT("account"), Account))
			{
				Fail(FString::Printf(TEXT("account token: status %d %s"), Status, *Excerpt(Reply)));
				return;
			}
			Session.PersonaId = (*Account)->GetStringField(TEXT("personaId"));
			Session.AccountId = (*Account)->GetStringField(TEXT("identityId"));
			UE_LOG(LogSwcBackend, Log, TEXT("login: account token for persona %s"), *Session.PersonaId);
			RequestCredentials();
		});
}

void FSwcLoginClient::RequestCredentials()
{
	Send(TEXT("GET"), Session.Hosts.GatewayCf, TEXT("/prod/credentials/omni"),
		{{TEXT("authorization"), FString::Printf(TEXT("Bearer %s"), *Session.AccessToken)}}, FString(), FString(),
		[this](int32 Status, const FString& Reply)
		{
			const TSharedPtr<FJsonObject> Document = Status == 200 ? ParseObject(Reply) : nullptr;
			if (!Document.IsValid() || !Document->HasField(TEXT("accessKeyId")))
			{
				Fail(FString::Printf(TEXT("credentials: status %d %s"), Status, *Excerpt(Reply)));
				return;
			}
			RequestEntitlementSync();
		});
}

void FSwcLoginClient::RequestEntitlementSync()
{
	// The client sends the sync and then the list, after the token and before the character list (protocol notes 4.29).
	// A reply that is not 200 gives no error on the screen of the retail client: the path continues.
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Types;
	for (const TCHAR* Type : {TEXT("FirstTimeLogin"), TEXT("PrimeGamingSync"), TEXT("CodeRedemptionSync"), TEXT("TwitchDropsSync"), TEXT("SteamRetailSync")})
	{
		Types.Add(MakeShared<FJsonValueString>(Type));
	}
	Body->SetArrayField(TEXT("syncTypes"), Types);
	Send(TEXT("POST"), EntitlementHost,
		FString::Printf(TEXT("/players/%s/games/new-world/platforms/steam/entitlements/sync"), *Session.PersonaId),
		PlayerServiceHeaders(), TEXT("application/json"), WriteObject(Body),
		[this](int32, const FString&) { RequestEntitlementList(); });
}

void FSwcLoginClient::RequestEntitlementList()
{
	Send(TEXT("GET"), EntitlementHost, FString::Printf(TEXT("/players/%s/games/new-world/platforms/steam/entitlements"), *Session.PersonaId),
		PlayerServiceHeaders(), FString(), FString(),
		[this](int32 Status, const FString& Reply)
		{
			const TSharedPtr<FJsonObject> Document = Status == 200 ? ParseObject(Reply) : nullptr;
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (Document.IsValid() && Document->TryGetArrayField(TEXT("entitlements"), List))
			{
				for (const TSharedPtr<FJsonValue>& Value : *List)
				{
					const TSharedPtr<FJsonObject>* Entry = nullptr;
					if (Value->TryGetObject(Entry))
					{
						Session.Entitlements.Add((*Entry)->GetStringField(TEXT("alias")));
					}
				}
			}
			UE_LOG(LogSwcBackend, Log, TEXT("login: %d entitlement(s)"), Session.Entitlements.Num());
			RequestRemoteConfiguration();
		});
}

void FSwcLoginClient::RequestRemoteConfiguration()
{
	// The retail client asks for 12 documents before the main menu; the backend has only this one (protocol notes 2.3).
	// A setting is the JSON value true or false; the reply 404 (NoSuchKey) leaves each setting at its default.
	Send(TEXT("GET"), RemoteConfigurationHost, GameplayConfigurationPath, {}, FString(), FString(),
		[this](int32 Status, const FString& Reply)
		{
			const TSharedPtr<FJsonObject> Document = Status >= 200 && Status < 300 ? ParseObject(Reply) : nullptr;
			if (Document.IsValid())
			{
				for (const auto& Member : Document->Values)
				{
					bool bValue = false;
					if (Member.Value->TryGetBool(bValue))
					{
						const FString Name(Member.Key.ToView());
						Session.RemoteSettings.Add(Name, bValue);
						UE_LOG(LogSwcBackend, Log, TEXT("login: remote setting %s = %s"), *Name, bValue ? TEXT("true") : TEXT("false"));
					}
				}
			}
			RequestLoginInfo();
		});
}

void FSwcLoginClient::RequestLoginInfo()
{
	Send(TEXT("GET"), Session.Hosts.Gateway, FString::Printf(TEXT("/prod/game/getlogininfo?channelId=%s"), ChannelId),
		GatewayHeaders(), FString(), FString(),
		[this](int32 Status, const FString& Reply)
		{
			const TSharedPtr<FJsonObject> Document = Status == 200 ? ParseObject(Reply) : nullptr;
			const TSharedPtr<FJsonObject>* List = nullptr;
			if (!Document.IsValid() || !Document->TryGetObjectField(TEXT("LoginInfoList"), List))
			{
				Fail(FString::Printf(TEXT("login information: status %d %s"), Status, *Excerpt(Reply)));
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>* Worlds = nullptr;
			if ((*List)->TryGetArrayField(TEXT("Worlds"), Worlds))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Worlds)
				{
					const TSharedPtr<FJsonObject>* World = nullptr;
					if (Value->TryGetObject(World))
					{
						FSwcWorldInfo& Info = Session.Worlds.AddDefaulted_GetRef();
						Info.WorldId = (*World)->GetStringField(TEXT("WorldId"));
						Info.Name = (*World)->GetStringField(TEXT("WorldName"));
						Info.Status = (*World)->GetStringField(TEXT("WorldStatus"));
						Info.Type = (*World)->GetStringField(TEXT("WorldType"));
						Info.Version = (*World)->GetStringField(TEXT("WorldVersion"));
					}
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Characters = nullptr;
			if ((*List)->TryGetArrayField(TEXT("Characters"), Characters))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Characters)
				{
					const TSharedPtr<FJsonObject>* Character = nullptr;
					if (Value->TryGetObject(Character))
					{
						FSwcCharacterInfo& Info = Session.Characters.AddDefaulted_GetRef();
						Info.CharacterId = (*Character)->GetStringField(TEXT("CharacterId"));
						Info.Name = (*Character)->GetStringField(TEXT("Name"));
						Info.WorldId = (*Character)->GetStringField(TEXT("WorldId"));
						(*Character)->TryGetBoolField(TEXT("FtueCompleted"), Info.bFtueCompleted);
						(*Character)->TryGetStringField(TEXT("PublishedData"), Info.PublishedData);
					}
				}
			}
			Succeed();
		});
}

void FSwcLoginClient::RequestLoginToken(const FString& CharacterId, FTokenDone Done)
{
	const TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("CharacterId"), CharacterId);
	Request->SetStringField(TEXT("WorldId"), FString());
	Request->SetStringField(TEXT("SteamAuthTicket"), Identity.GetAuthTicketHex());
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("LoginQueueRequest"), Request);
	Send(TEXT("POST"), Session.Hosts.Gateway, TEXT("/prod/game/login/queue/v2?tokenVersion=10"), GatewayHeaders(), GatewayContentType,
		WriteObject(Body),
		[Done = MoveTemp(Done)](int32 Status, const FString& Reply)
		{
			FSwcLoginToken Token;
			const TSharedPtr<FJsonObject> Document = Status == 200 ? ParseObject(Reply) : nullptr;
			const TSharedPtr<FJsonObject>* Response = nullptr;
			const TSharedPtr<FJsonObject>* Fields = nullptr;
			if (!Document.IsValid() || !Document->TryGetObjectField(TEXT("LoginQueueResponse"), Response)
				|| !(*Response)->TryGetObjectField(TEXT("Token"), Fields))
			{
				Done(false, Token, FString::Printf(TEXT("login queue: status %d %s"), Status, *Excerpt(Reply)));
				return;
			}
			const TSharedPtr<FJsonObject>& Object = *Fields;
			Token.Signature = Object->GetStringField(TEXT("Signature"));
			Token.RepAddress = Object->GetStringField(TEXT("RepAddress"));
			Token.WorldId = Object->GetStringField(TEXT("WorldId"));
			Token.CharacterId = Object->GetStringField(TEXT("CharacterId"));
			Token.PersonaId = Object->GetStringField(TEXT("PersonaId"));
			Token.SteamAppId = Low32(Object, TEXT("SteamAppId"));
			Token.SteamUserId = Object->GetStringField(TEXT("SteamUserId"));
			Token.ChannelId = Object->GetStringField(TEXT("ChannelId"));
			Token.TicketId = Object->GetStringField(TEXT("TicketId"));
			Token.IssueTime = Low32(Object, TEXT("IssueTime"));
			Token.GenerateTime = Low32(Object, TEXT("GenerateTime"));
			Token.AccountAge = Low32(Object, TEXT("AccountAge"));
			Token.TokenVersion = Low32(Object, TEXT("TokenVersion"));
			Done(true, Token, FString());
		});
}

void FSwcLoginClient::Fail(const FString& Error)
{
	UE_LOG(LogSwcBackend, Error, TEXT("login: failed: %s"), *Error);
	if (OnDone)
	{
		FDone Done = MoveTemp(OnDone);
		Done(false, Error);
	}
}

void FSwcLoginClient::Succeed()
{
	if (OnDone)
	{
		FDone Done = MoveTemp(OnDone);
		Done(true, FString());
	}
}
