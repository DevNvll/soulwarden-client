#include "Net/SwcSteamStub.h"

#include "HAL/PlatformMisc.h"

namespace
{
/** The fallback identity of the Soulwarden game hook: Steam id 0x0110000100A207A1 and the name "Soulwarden". */
const TCHAR* const FallbackSteamId = TEXT("76561197970884513");
const TCHAR* const FallbackPersonaName = TEXT("Soulwarden");
/** A persona name of Steam has 32 characters at most, and a character has 4 bytes at most in UTF-8. */
constexpr int32 PersonaNameLimit = 32 * 4;

FString HexOf(const uint8* Data, int32 Size)
{
	static const TCHAR* Digits = TEXT("0123456789abcdef");
	FString Text;
	Text.Reserve(Size * 2);
	for (int32 Index = 0; Index < Size; ++Index)
	{
		Text.AppendChar(Digits[Data[Index] >> 4]);
		Text.AppendChar(Digits[Data[Index] & 15]);
	}
	return Text;
}

FString HexOfUtf8(const FString& Text, int32 Limit)
{
	const FTCHARToUTF8 Utf8(*Text);
	return HexOf(reinterpret_cast<const uint8*>(Utf8.Get()), FMath::Min(Utf8.Length(), Limit));
}
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
	const FString Ticket = FString::Printf(TEXT("SOULWARDEN-LOCAL-TICKET;1;%s;%s"), *SteamId, *HexOfUtf8(PersonaName, PersonaNameLimit));
	const FTCHARToUTF8 Utf8(*Ticket);
	return HexOf(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

FString FSwcSteamStub::MaskedSteamId() const
{
	return FString::Printf(TEXT("...%s%s"), *SteamId.Right(4), bFallback ? TEXT(" (fallback identity)") : TEXT(""));
}
