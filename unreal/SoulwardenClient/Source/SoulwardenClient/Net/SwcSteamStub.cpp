#include "Net/SwcSteamStub.h"

#include "HAL/PlatformMisc.h"
#include "Net/Codec/SwcRepMessages.h"

namespace
{
/** The fallback identity of the Soulwarden game hook: Steam id 0x0110000100A207A1 and the name "Soulwarden". */
const TCHAR* const FallbackSteamId = TEXT("76561197970884513");
const TCHAR* const FallbackPersonaName = TEXT("Soulwarden");
} // namespace

FSwcSteamStub FSwcSteamStub::FromEnvironment()
{
	FSwcSteamStub Stub;
	Stub.SteamId = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_STEAM_ID"));
	Stub.PersonaName = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_PERSONA_NAME"));
	if (!IsSteamId(Stub.SteamId))
	{
		Stub.SteamId = FallbackSteamId;
		Stub.PersonaName = FallbackPersonaName;
		Stub.bFallback = true;
	}
	else if (Stub.PersonaName.IsEmpty())
	{
		// The launcher of Soulwarden gives this name to an account with no persona name.
		Stub.PersonaName = FallbackPersonaName;
	}
	return Stub;
}

bool FSwcSteamStub::IsSteamId(const FString& Text)
{
	if (Text.Len() != 17)
	{
		return false;
	}
	for (const TCHAR Character : Text)
	{
		if (!FChar::IsDigit(Character))
		{
			return false;
		}
	}
	const uint64 Value = FCString::Strtoui64(*Text, nullptr, 10);
	return (Value >> 32) == 0x01100001ULL;
}

FString FSwcSteamStub::GetAuthTicketHex() const
{
	// One writer for the ticket: the codec (tools/test-net.sh compares it with the ticket of the backend tests).
	const std::string Ticket = swc::net::local_ticket_hex(TCHAR_TO_UTF8(*SteamId), TCHAR_TO_UTF8(*PersonaName));
	return UTF8_TO_TCHAR(Ticket.c_str());
}

FString FSwcSteamStub::MaskedSteamId() const
{
	return FString::Printf(TEXT("...%s%s"), *SteamId.Right(4), bFallback ? TEXT(" (fallback identity)") : TEXT(""));
}
