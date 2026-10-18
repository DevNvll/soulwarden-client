#pragma once

#include "CoreMinimal.h"

/**
 * The three Steam functions of the retail client, as stubs (Soulwarden protocol notes 2.1). The client has no Steam
 * library: the run script reads the Steam id and the persona name of the last Steam login (as the Soulwarden launcher
 * does) and gives them to the client in its environment (SWC_STEAM_ID, SWC_PERSONA_NAME). The values never go into a
 * file. A log line shows only the last 4 digits of the Steam id (MaskedSteamId), and never the persona name.
 *
 * The backend does not check a ticket (stub authentication): the ticket is the local ticket of the Soulwarden game hook.
 *
 *   ticket      := "SOULWARDEN-LOCAL-TICKET" ";" "1" ";" steam-id ";" persona-hex
 *   steam-id    := 17 decimal digits, high 32 bits 0x01100001 (an individual account, public universe)
 *   persona-hex := lower-case hexadecimal text of the UTF-8 persona name (128 bytes at most)
 *
 * With no Steam id in the environment the client uses the fallback identity of the hook.
 */
struct SOULWARDENCLIENT_API FSwcSteamStub
{
	/** ISteamUser::GetSteamID: decimal text of the Steam id. */
	FString SteamId;
	/** ISteamFriends::GetPersonaName: UTF-8 text. */
	FString PersonaName;
	/** True when the environment had no Steam id (the fallback identity). */
	bool bFallback = false;

	/** Reads SWC_STEAM_ID and SWC_PERSONA_NAME. A value that is not a Steam id gives the fallback identity. */
	static FSwcSteamStub FromEnvironment();

	/** @return True for 17 decimal digits whose high 32 bits are 0x01100001. */
	static bool IsSteamId(const FString& Text);

	/** ISteamUser::GetAuthTicketForWebApi: the ticket bytes as lower-case hexadecimal text (client function 0x5A871A0). */
	FString GetAuthTicketHex() const;

	/** @return "...1234": the last 4 digits of the Steam id, for a log line. */
	FString MaskedSteamId() const;
};
