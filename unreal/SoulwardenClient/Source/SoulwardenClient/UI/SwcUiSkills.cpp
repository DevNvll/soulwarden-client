#include "UI/SwcUiSkills.h"

#include "SwcBackendSubsystem.h"
#include "SwcUi/Runtime.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcUiSkills, Log, All);

void FSwcUiSkills::Update(swc::ui::Runtime& Runtime, USwcBackendSubsystem& Backend)
{
	swc::net::RepClient::OwnEntity Own;
	if (!Backend.GetLocalPlayer(Own))
	{
		return;
	}
	using FArg = swc::ui::Runtime::Arg;
	if (Own.nodesRevision != SentNodesRevision && Own.nodesRevision != 0)
	{
		SentNodesRevision = Own.nodesRevision;
		std::string Text;
		for (const std::uint32_t Node : Own.learnedNodes)
		{
			Text += std::to_string(Node) + " ";
		}
		Runtime.call("HostLearnedNodes", {FArg::of(Text)});
		UE_LOG(LogSwcUiSkills, Log, TEXT("ui: %d learned node(s) of the weapon trees (the ProgressionPoint state)"), static_cast<int32>(Own.learnedNodes.size()));
	}
	if (Own.respecAnswers != SentRespecAnswers)
	{
		SentRespecAnswers = Own.respecAnswers;
		Runtime.call("HostRespecResult", {FArg::of(Own.lastRespecSuccess), FArg::of(static_cast<double>(Own.lastRespecTable))});
	}
}

bool FSwcUiSkills::HandleRequest(const FString& Request, USwcBackendSubsystem& Backend)
{
	TArray<FString> Parts;
	Request.ParseIntoArray(Parts, TEXT(" "));
	if (Parts.IsEmpty())
	{
		return false;
	}
	const auto Number = [&Parts](int32 Index) { return Parts.IsValidIndex(Index) ? FCString::Strtoui64(*Parts[Index], nullptr, 10) : 0ULL; };
	bool bSent = false;
	if (Parts[0] == TEXT("assign") && Parts.Num() == 4)
	{
		const uint32 Pool = static_cast<uint32>(Number(2));
		const uint32 Node = static_cast<uint32>(Number(3));
		bSent = Backend.QueueRepCall([Pool, Node](swc::net::RepClient& Client) { Client.assign_point(Pool, Node); });
	}
	else if (Parts[0] == TEXT("apply") && Parts.Num() == 1)
	{
		bSent = Backend.QueueRepCall([](swc::net::RepClient& Client) { Client.apply_points(); });
	}
	else if (Parts[0] == TEXT("bar") && Parts.Num() == 4)
	{
		const uint16 Index = static_cast<uint16>(Number(1));
		const uint32 Place = static_cast<uint32>(Number(2));
		const uint32 Ability = static_cast<uint32>(Number(3));
		bSent = Backend.QueueRepCall([Index, Place, Ability](swc::net::RepClient& Client) { Client.change_bar(Index, Place, Ability); });
	}
	else if (Parts[0] == TEXT("respec") && Parts.Num() == 2)
	{
		const uint16 Index = static_cast<uint16>(Number(1));
		bSent = Backend.QueueRepCall([Index](swc::net::RepClient& Client) { Client.respec_tree(Index); });
	}
	else
	{
		return false;
	}
	UE_LOG(LogSwcUiSkills, Log, TEXT("ui: the character window asks for \"%s\": %s"), *Request, bSent ? TEXT("sent") : TEXT("not sent (not in the world)"));
	return true;
}
