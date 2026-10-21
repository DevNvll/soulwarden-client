// The travel of the world map (stage MVP-C, Linear AST-213; docs/UI.md "The world map and the travel"). The backend has no
// fast travel of the game (no discovery, no fee, no inn recall); it has the admin command "travel" of its tool window, for the
// characters of the account of the token of the start (Soulwarden docs/systems/tool-window-and-admin.md). The session gives
// the same random token to the backend (SOULWARDEN_ADMIN_TOKEN) and to the client (SWC_ADMIN_TOKEN; tools/lib/session.sh).
// The original shrine flow of the map (fasttravelicon.luac) gives the position of the shrine landmark; the client takes the
// place of the backend place list (GET /admin/v1/catalog/places) at that position and sends the command. The backend then
// moves the character as for each travel: the loading screen (3951), the movement state with a new teleport number, and 3954;
// the client keeps its loading gate until the cells of the place are there (USwcBackendSubsystem::Travel).

#include "SwcBackendSubsystem.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Net/SwcRepConnection.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
/** The header of the token of the admin routes (Soulwarden server/http/admin_access.h). */
const TCHAR* const AdminHeader = TEXT("X-Soulwarden-Admin");
/** The distance from the landmark of the map to the place of the backend (both come from the same region data: 0 m). */
constexpr double PlaceMatchMeters = 5.0;
} // namespace

void USwcBackendSubsystem::ReadAdminToken()
{
	// The token is a secret of one session: it leaves the environment after the read, and no log line has it.
	AdminToken = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_ADMIN_TOKEN"));
	FPlatformMisc::SetEnvironmentVar(TEXT("SWC_ADMIN_TOKEN"), TEXT(""));
	UE_LOG(LogSwcBackend, Log, TEXT("travel: %s"), AdminToken.Len() >= 32
		? TEXT("the travel of the map is on (the admin routes of the session)")
		: TEXT("the travel of the map is off: the session gave no admin token (SWC_ADMIN_TOKEN)"));
	if (AdminToken.Len() < 32)
	{
		AdminToken.Reset();
	}
}

bool USwcBackendSubsystem::RequestTravel(const FVector& LandmarkMeters)
{
	if (AdminToken.IsEmpty() || WorldCharacterId.IsEmpty() || !Connection.IsValid() || !bInGame || bTravelRequest || bTravelWait)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("travel: no request now (%s)"), AdminToken.IsEmpty() ? TEXT("no admin token")
			: !Connection.IsValid() || !bInGame ? TEXT("not in the world") : TEXT("a travel runs"));
		return false;
	}
	if (Connection->GetSnapshot().Own.dead)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("travel: no request now (the character is dead)"));
		return false;
	}
	bTravelRequest = true;
	if (TravelPlaces.Num() > 0)
	{
		SendTravel(LandmarkMeters);
		return true;
	}
	// The place list of the backend: once for each start (the places do not change after the start of the backend).
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("GET"));
	Request->SetURL(FString::Printf(TEXT("http://%s:%d/admin/v1/catalog/places"), *HttpHost, HttpPort));
	Request->SetHeader(AdminHeader, AdminToken);
	Request->SetTimeout(10.0f);
	TWeakObjectPtr<USwcBackendSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, LandmarkMeters](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			USwcBackendSubsystem* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
			TSharedPtr<FJsonObject> Root;
			if (Status == 200)
			{
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Root);
			}
			const TArray<TSharedPtr<FJsonValue>>* Places = nullptr;
			if (Root.IsValid() && Root->TryGetArrayField(TEXT("places"), Places))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Places)
				{
					const TSharedPtr<FJsonObject>* Place = nullptr;
					const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
					if (Value->TryGetObject(Place) && (*Place)->TryGetArrayField(TEXT("position"), Position) && Position->Num() == 3)
					{
						Self->TravelPlaces.Add({(*Place)->GetStringField(TEXT("id")),
							FVector((*Position)[0]->AsNumber(), (*Position)[1]->AsNumber(), (*Position)[2]->AsNumber())});
					}
				}
			}
			UE_LOG(LogSwcBackend, Log, TEXT("travel: the place list of the backend: status %d, %d places"), Status, Self->TravelPlaces.Num());
			if (Self->TravelPlaces.Num() == 0)
			{
				Self->bTravelRequest = false;
				return;
			}
			Self->SendTravel(LandmarkMeters);
		});
	Request->ProcessRequest();
	return true;
}

void USwcBackendSubsystem::SendTravel(const FVector& LandmarkMeters)
{
	const FTravelPlace* Best = nullptr;
	double BestDistance = PlaceMatchMeters;
	for (const FTravelPlace& Place : TravelPlaces)
	{
		const double Distance = FVector::Dist(Place.Position, LandmarkMeters);
		if (Distance <= BestDistance)
		{
			Best = &Place;
			BestDistance = Distance;
		}
	}
	if (Best == nullptr)
	{
		UE_LOG(LogSwcBackend, Warning, TEXT("travel: the backend has no place within %.0f m of the landmark %.1f %.1f %.1f"),
			PlaceMatchMeters, LandmarkMeters.X, LandmarkMeters.Y, LandmarkMeters.Z);
		bTravelRequest = false;
		return;
	}
	TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("place"), Best->Id);
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("command"), TEXT("travel"));
	Body->SetStringField(TEXT("character"), WorldCharacterId);
	Body->SetObjectField(TEXT("args"), Args);
	FString Text;
	FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Text));
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(FString::Printf(TEXT("http://%s:%d/admin/v1/commands"), *HttpHost, HttpPort));
	Request->SetHeader(AdminHeader, AdminToken);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Text);
	Request->SetTimeout(10.0f);
	const FString PlaceId = Best->Id;
	UE_LOG(LogSwcBackend, Log, TEXT("travel: the request to the place %s (%.1f m from the landmark)"), *PlaceId, BestDistance);
	TWeakObjectPtr<USwcBackendSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, PlaceId](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			USwcBackendSubsystem* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bTravelRequest = false;
			const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
			UE_LOG(LogSwcBackend, Log, TEXT("travel: the backend answered %d for the place %s%s%s"), Status, *PlaceId,
				Status == 200 ? TEXT("") : TEXT(": "), Status == 200 || !Response.IsValid() ? TEXT("") : *Response->GetContentAsString().Left(200));
		});
	Request->ProcessRequest();
}
