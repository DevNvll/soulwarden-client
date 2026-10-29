#include "UI/SwcUiBoards.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SwcBackendSubsystem.h"
#include "SwcUi/Runtime.h"

#include <cstdlib>
#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogSwcUiBoards, Log, All);

namespace
{
std::string HexOf(const swc::net::ActorId& Actor)
{
	static const char* Digits = "0123456789ABCDEF";
	std::string Out;
	for (const std::uint8_t Byte : Actor)
	{
		Out += Digits[Byte >> 4];
		Out += Digits[Byte & 15];
	}
	return Out;
}

/** The CRC-32 of the text with the letters A to Z in lower case (the backend gamedata::progression_id; the rank key of a trade skill). */
uint32 ProgressionId(const std::string& Text)
{
	uint32 Crc = 0xFFFFFFFFu;
	for (char Letter : Text)
	{
		if (Letter >= 'A' && Letter <= 'Z')
		{
			Letter = static_cast<char>(Letter - 'A' + 'a');
		}
		Crc ^= static_cast<uint8>(Letter);
		for (int32 Bit = 0; Bit < 8; ++Bit)
		{
			Crc = (Crc >> 1) ^ (0xEDB88320u & (0u - (Crc & 1u)));
		}
	}
	return ~Crc;
}
}

bool FSwcUiBoards::LoadTown(const FString& ContentDir)
{
	if (bTownTried)
	{
		return Town != nullptr;
	}
	bTownTried = true;
	// The town part of the mission data (tools/convert-missions.sh): rows [crc, project, enabled, territories, exclude, minLevel, maxLevel,
	// tradeskill, tradeskillLevel, conditional], projects [crc, type, level], buckets {key, missions}.
	const FString Path = FPaths::Combine(ContentDir, TEXT("ui/gamedata/missiontown.json"));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogSwcUiBoards, Warning, TEXT("ui: boards: no town mission data %s (tools/convert-missions.sh): the town board has no offers"), *Path);
		return false;
	}
	const TSharedPtr<FJsonObject>* TownObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("town"), TownObject))
	{
		UE_LOG(LogSwcUiBoards, Warning, TEXT("ui: boards: %s has no town part"), *Path);
		return false;
	}
	auto Data = std::make_unique<swc::boards::TownData>();
	for (const TSharedPtr<FJsonValue>& Value : (*TownObject)->GetArrayField(TEXT("rows")))
	{
		const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
		if (Row.Num() < 10)
		{
			continue;
		}
		swc::boards::MissionRow Each;
		Each.crc = static_cast<uint32>(Row[0]->AsNumber());
		Each.project = static_cast<uint32>(Row[1]->AsNumber());
		Each.town = true;
		Each.enabled = Row[2]->AsBool();
		for (const TSharedPtr<FJsonValue>& Id : Row[3]->AsArray())
		{
			Each.territories.push_back(static_cast<uint16>(Id->AsNumber()));
		}
		Each.excludeTerritories = Row[4]->AsBool();
		Each.minLevel = static_cast<uint32>(Row[5]->AsNumber());
		Each.maxLevel = static_cast<uint32>(Row[6]->AsNumber());
		Each.tradeskill = TCHAR_TO_UTF8(*Row[7]->AsString());
		Each.tradeskillLevel = static_cast<uint32>(Row[8]->AsNumber());
		Each.conditional = TCHAR_TO_UTF8(*Row[9]->AsString());
		Data->rows[Each.crc] = std::move(Each);
	}
	for (const TSharedPtr<FJsonValue>& Value : (*TownObject)->GetArrayField(TEXT("projects")))
	{
		const TArray<TSharedPtr<FJsonValue>>& Project = Value->AsArray();
		if (Project.Num() >= 3)
		{
			Data->projects.push_back({static_cast<uint32>(Project[0]->AsNumber()), std::string(TCHAR_TO_UTF8(*Project[1]->AsString())),
				static_cast<uint32>(Project[2]->AsNumber())});
		}
	}
	for (const TSharedPtr<FJsonValue>& Value : (*TownObject)->GetArrayField(TEXT("buckets")))
	{
		const TSharedPtr<FJsonObject> Bucket = Value->AsObject();
		swc::boards::Bucket Each;
		Each.key = static_cast<uint8>(Bucket->GetNumberField(TEXT("key")));
		for (const TSharedPtr<FJsonValue>& Mission : Bucket->GetArrayField(TEXT("missions")))
		{
			Each.missions.push_back(static_cast<uint32>(Mission->AsNumber()));
		}
		Data->buckets.push_back(std::move(Each));
	}
	UE_LOG(LogSwcUiBoards, Log, TEXT("ui: boards: the town mission data: %d rows, %d projects, %d buckets"), static_cast<int32>(Data->rows.size()),
		static_cast<int32>(Data->projects.size()), static_cast<int32>(Data->buckets.size()));
	Town = std::move(Data);
	return true;
}

void FSwcUiBoards::SendOffers(swc::ui::Runtime& Runtime, USwcBackendSubsystem& Backend, const swc::net::RepClient::OwnEntity& Own,
	const swc::net::RepClient::OtherEntity& Board, bool bForce)
{
	using FArg = swc::ui::Runtime::Arg;
	std::string TerritoryText;
	Runtime.call("HostBoardTerritory", {}, &TerritoryText);
	const uint16 Territory = static_cast<uint16>(std::strtoul(TerritoryText.c_str(), nullptr, 10));
	const uint64 ServerNs = Backend.GetServerTimeMicroseconds() * 1000ULL;
	const std::string CharacterId = TCHAR_TO_UTF8(*Backend.GetWorldCharacterId());
	// The level of the filter: the level of the screen (the wire level plus 1; backend TownInputs::level).
	const uint32 Level = Own.level + 1;
	const uint32 Seed = swc::boards::town_seed(CharacterId, Territory, ServerNs);
	const FString Key = FString::Printf(TEXT("%u %u %u %u %u %u"), Territory, Seed, Board.boardRevision, Own.providerRevision, Own.rankRevision, Level);
	if (!bForce && Key == OffersKey)
	{
		return;
	}
	OffersKey = Key;
	std::vector<std::uint32_t> Projects;
	for (const swc::net::RepClient::OtherEntity::BoardProject& Project : Board.boardProjects)
	{
		Projects.push_back(Project.project);
	}
	const swc::boards::TownEntry* Entry = nullptr;
	swc::boards::TownEntry Stored;
	for (const swc::net::RepClient::OwnEntity::BoardTown& Each : Own.boardTowns)
	{
		if (Each.territory == Territory)
		{
			Stored.seed = Each.seed;
			Stored.unavailable = Each.unavailable;
			Entry = &Stored;
		}
	}
	swc::boards::TownCharacter Character;
	Character.rank = [&Own](const std::string& Tradeskill)
	{
		const auto Found = Own.ranks.find(ProgressionId(Tradeskill));
		return Found != Own.ranks.end() ? static_cast<std::uint32_t>(Found->second.rank) : 0u;
	};
	// No town row has a conditional (the mission data of the backend copy): a conditional row is no candidate.
	Character.conditional = nullptr;
	std::string Lines = "S " + std::to_string(Seed) + " " + std::to_string(swc::boards::seconds_to_next_seed(ServerNs)) + "\n";
	int32 Count = 0;
	if (Town != nullptr && Territory != 0)
	{
		const swc::boards::TownOffers Offers = swc::boards::town_offers(*Town, CharacterId, Territory, ServerNs, Projects, Level, Entry, &Character);
		for (const swc::boards::TownOffer& Offer : Offers.offers)
		{
			Lines += "O " + std::to_string(Offer.mission) + " " + std::to_string(Offer.bucket) + " " + std::to_string(Offer.project) + " "
				+ (Offer.available ? "1" : "0") + "\n";
			++Count;
		}
	}
	Runtime.call("HostBoardOffers", {FArg::of(Lines)});
	UE_LOG(LogSwcUiBoards, Log, TEXT("ui: boards: %d town offer(s) of the territory %u (seed %u, %d project(s), level %u)"), Count, Territory, Seed,
		static_cast<int32>(Projects.size()), Level);
}

void FSwcUiBoards::Update(swc::ui::Runtime& Runtime, USwcBackendSubsystem& Backend, const FString& ContentDir)
{
	using FArg = swc::ui::Runtime::Arg;
	swc::net::ActorId Actor{};
	const bool bOpen = Backend.GetMissionBoard(Actor);
	if (bShown && (!bOpen || Actor != ShownActor))
	{
		bShown = false;
		Runtime.call("HostBoard", {FArg::of(std::string("close"))});
		Backend.SetBoardScreenShown(false);
		UE_LOG(LogSwcUiBoards, Log, TEXT("ui: boards: the board screen closes (the board use ended)"));
	}
	if (!bOpen)
	{
		return;
	}
	swc::net::RepClient::OwnEntity Own;
	swc::net::RepClient::OtherEntity Board;
	swc::net::RepClient::OtherEntity Claims;
	if (!Backend.GetLocalPlayer(Own) || !Backend.GetBoardStates(Actor, Board, Claims))
	{
		return;
	}
	if (Board.boardRevision != SentBoardRevision || Claims.boardRevision != SentClaimRevision || !bShown)
	{
		SentBoardRevision = Board.boardRevision;
		SentClaimRevision = Claims.boardRevision;
		std::string Lines;
		const uint64 ServerNs = Backend.GetServerTimeMicroseconds() * 1000ULL;
		for (const swc::net::RepClient::OtherEntity::BoardProject& Project : Board.boardProjects)
		{
			// The end of the project as seconds from now (the screen counts on the wall clock of the UI: TimeHelperFunctions ServerNow).
			const uint64 SecondsLeft = Project.endNs > ServerNs ? (Project.endNs - ServerNs) / 1000000000ULL : 0;
			Lines += "P " + std::to_string(Project.key) + " " + std::to_string(Project.project) + " " + std::to_string(SecondsLeft) + " "
				+ std::to_string(Project.progress) + "\n";
		}
		for (size_t Index = 0; Index < Claims.claimKeys.size(); ++Index)
		{
			const swc::net::RepClient::OtherEntity::ClaimSummary* Summary = Index < Claims.claimSummaries.size() ? &Claims.claimSummaries[Index] : nullptr;
			Lines += "C " + std::to_string(Claims.claimKeys[Index]) + " " + std::to_string(Summary != nullptr ? Summary->level : 0) + "\n";
			if (Summary == nullptr)
			{
				continue;
			}
			for (const auto& [Category, UpgradeLevel] : Summary->upgrades)
			{
				Lines += "U " + std::to_string(Category) + " " + std::to_string(UpgradeLevel) + "\n";
			}
			for (const swc::net::RepClient::OtherEntity::ClaimActive& Active : Summary->active)
			{
				Lines += "A " + std::to_string(Active.project) + " " + std::to_string(Active.type) + " " + std::to_string(Active.state) + "\n";
			}
		}
		Runtime.call("HostBoardStates", {FArg::of(Lines)});
	}
	LoadTown(ContentDir);
	SendOffers(Runtime, Backend, Own, Board, !bShown);
	if (bShown)
	{
		return;
	}
	ShownActor = Actor;
	std::string Result;
	const bool bCalled = Runtime.call("HostBoard", {FArg::of(std::string("open")), FArg::of(HexOf(Actor))}, &Result);
	if (!bCalled || Result != "ok")
	{
		// The screen did not open: the client ends the board use, so that the player is not stuck (as AST-287 for the stations).
		UE_LOG(LogSwcUiBoards, Warning, TEXT("ui: boards: the board screen did not open (%s): the client ends the board use"),
			bCalled ? UTF8_TO_TCHAR(Result.c_str()) : TEXT("the call of Swc.HostBoard failed"));
		Backend.EndMissionBoard();
		return;
	}
	bShown = true;
	Backend.SetBoardScreenShown(true);
	UE_LOG(LogSwcUiBoards, Log, TEXT("ui: boards: the board screen opens (board %hs)"), HexOf(Actor).c_str());
}

bool FSwcUiBoards::HandleRequest(const FString& Request, USwcBackendSubsystem& Backend)
{
	if (!Request.StartsWith(TEXT("board ")))
	{
		return false;
	}
	TArray<FString> Parts;
	Request.ParseIntoArray(Parts, TEXT(" "));
	const auto Number = [&Parts](int32 Index) { return Parts.IsValidIndex(Index) ? FCString::Strtoui64(*Parts[Index], nullptr, 10) : 0ULL; };
	bool bSent = false;
	if (Parts.Num() == 2 && Parts[1] == TEXT("end"))
	{
		Backend.EndMissionBoard();
		bSent = true;
	}
	else if (Parts.Num() == 4 && Parts[1] == TEXT("accept"))
	{
		// RequestCommunityGoalSelection(mission, project or 0): 3819.
		const uint32 Mission = static_cast<uint32>(Number(2));
		const uint32 Project = static_cast<uint32>(Number(3));
		bSent = Mission != 0 && Backend.QueueRepCall([Mission, Project](swc::net::RepClient& Client) { Client.select_community_goal(Mission, Project); });
	}
	else if (Parts.Num() == 3 && Parts[1] == TEXT("select"))
	{
		// RequestMissionSelectionById of the faction board: the index of the offer in the provider state (3816).
		const uint8 Index = static_cast<uint8>(Number(2));
		bSent = Backend.QueueRepCall([Index](swc::net::RepClient& Client) { Client.select_objective(Index); });
	}
	else if (Parts.Num() == 3 && Parts[1] == TEXT("complete"))
	{
		// RequestMissionCompletion(instance): 2461.
		const uint64 Instance = Number(2);
		bSent = Instance != 0 && Backend.QueueRepCall([Instance](swc::net::RepClient& Client) { Client.complete_mission(Instance); });
	}
	UE_LOG(LogSwcUiBoards, Log, TEXT("ui: boards: the request \"%s\": %s"), *Request, bSent ? TEXT("sent") : TEXT("not sent"));
	return true;
}
