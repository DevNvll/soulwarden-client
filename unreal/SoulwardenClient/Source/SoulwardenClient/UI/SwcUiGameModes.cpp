#include "UI/SwcUiGameModes.h"

#include "SwcBackendSubsystem.h"
#include "SwcUi/Runtime.h"
#include "Net/Codec/SwcRepClient.h"

#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogSwcUiGameModes, Log, All);

void FSwcUiGameModes::Update(swc::ui::Runtime& Runtime, USwcBackendSubsystem& Backend)
{
	using FArg = swc::ui::Runtime::Arg;
	swc::net::RepClient::OwnEntity Own;
	const bool bOwn = Backend.GetLocalPlayer(Own);
	if (bOwn && Own.gameModeRevision != SentRevision)
	{
		SentRevision = Own.gameModeRevision;
		std::string Lines;
		for (const swc::net::GameModeEntry& Entry : Own.gameModes)
		{
			Lines += "M " + std::to_string(Entry.mode) + " " + std::to_string(Entry.map) + " " + std::to_string(Entry.enteredNs / 1000000000ULL) + "\n";
		}
		const bool bCanQuit = swc::net::has_game_mode_flag(Own.gameModeFlags, swc::net::kGameModeFlagCanQuit);
		Lines += bCanQuit ? "Q 1\n" : "Q 0\n";
		Runtime.call("HostGameModes", {FArg::of(Lines)});
		UE_LOG(LogSwcUiGameModes, Log, TEXT("ui: game modes: %d active mode(s)%s, can quit %d (revision %u)"), static_cast<int32>(Own.gameModes.size()),
			Own.gameModes.empty() ? TEXT("") : *FString::Printf(TEXT(" (%08X)"), Own.gameModes.front().mode), bCanQuit ? 1 : 0, Own.gameModeRevision);
	}
	uint32 Mode = 0;
	const bool bOpen = Backend.GetDungeonEntrance(Mode);
	if (bShown && (!bOpen || Mode != ShownMode))
	{
		bShown = false;
		Runtime.call("HostDungeon", {FArg::of(std::string("close"))});
		Backend.SetDungeonScreenShown(false);
		UE_LOG(LogSwcUiGameModes, Log, TEXT("ui: game modes: the entrance window closes (the entrance use ended)"));
	}
	// The results after the open of the window: a result of an earlier use is no answer of this window.
	if (bOwn && Own.dungeonResults != SentResults)
	{
		const bool bFirst = SentResults == ~0u;
		SentResults = Own.dungeonResults;
		if (!bFirst && bShown)
		{
			Runtime.call("HostDungeonResult", {FArg::of(static_cast<double>(Own.lastDungeonResult.result))});
			UE_LOG(LogSwcUiGameModes, Log, TEXT("ui: game modes: the result of the entrance %u (3094)"), Own.lastDungeonResult.result);
		}
	}
	if (!bOpen || bShown)
	{
		return;
	}
	ShownMode = Mode;
	std::string Result;
	const bool bCalled = Runtime.call("HostDungeon", {FArg::of(std::string("open")), FArg::of(std::to_string(Mode))}, &Result);
	if (!bCalled || Result != "ok")
	{
		UE_LOG(LogSwcUiGameModes, Warning, TEXT("ui: game modes: the entrance window did not open (%s): the client ends the entrance use"),
			bCalled ? UTF8_TO_TCHAR(Result.c_str()) : TEXT("the call of Swc.HostDungeon failed"));
		Backend.EndDungeonEntrance();
		return;
	}
	bShown = true;
	Backend.SetDungeonScreenShown(true);
	UE_LOG(LogSwcUiGameModes, Log, TEXT("ui: game modes: the entrance window opens (mode %08X)"), Mode);
}

bool FSwcUiGameModes::HandleRequest(const FString& Request, USwcBackendSubsystem& Backend)
{
	if (!Request.StartsWith(TEXT("dungeon ")) && !Request.StartsWith(TEXT("gamemode ")))
	{
		return false;
	}
	TArray<FString> Parts;
	Request.ParseIntoArray(Parts, TEXT(" "));
	bool bSent = false;
	if (Parts.Num() == 3 && Parts[0] == TEXT("dungeon") && Parts[1] == TEXT("enter"))
	{
		// The difficulty of the window: a mutation level (0 for a story instance; the backend has no mutation).
		bSent = Backend.EnterDungeon();
	}
	else if (Parts.Num() == 2 && Parts[0] == TEXT("dungeon") && Parts[1] == TEXT("end"))
	{
		Backend.EndDungeonEntrance();
		bSent = true;
	}
	else if (Parts.Num() == 3 && Parts[0] == TEXT("gamemode") && Parts[1] == TEXT("event"))
	{
		const uint32 Event = static_cast<uint32>(FCString::Strtoui64(*Parts[2], nullptr, 10));
		bSent = Event == swc::net::kGameModeLeaveEvent
			&& Backend.QueueRepCall([Event](swc::net::RepClient& Client) { Client.game_mode_event(Event); });
	}
	UE_LOG(LogSwcUiGameModes, Log, TEXT("ui: game modes: the request \"%s\": %s"), *Request, bSent ? TEXT("sent") : TEXT("not sent"));
	return true;
}
