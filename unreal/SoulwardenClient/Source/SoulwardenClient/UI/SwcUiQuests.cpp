#include "UI/SwcUiQuests.h"

#include "SwcBackendSubsystem.h"
#include "SwcUi/Runtime.h"

#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogSwcUiQuests, Log, All);

void FSwcUiQuests::Reset()
{
	bEnabled = false;
	bRestored = false;
	SentRevision = 0;
	SentReadyRevision = 0;
	bSentAny = false;
}

void FSwcUiQuests::Feed(swc::ui::Runtime& Runtime, const swc::net::RepClient::OwnEntity& Own, USwcBackendSubsystem& Backend)
{
	using FArg = swc::ui::Runtime::Arg;
	if (!Own.hasMetadata)
	{
		return;
	}
	if (Own.group != Group)
	{
		// A new character or a new world session: the calls and the state start again.
		Reset();
		Group = Own.group;
	}
	if (!bEnabled)
	{
		// The auto-pin flags of 2709 (the options Hud.LocalPlayer.Options.Misc.EnableAutoPinningObjectives.*; the retail default: each type
		// on, CommunityGoal off: live check L1 of stage 1.8a).
		std::vector<bool> Flags(swc::net::kAutoPinFlags, true);
		Flags[swc::net::kCommunityGoalType] = false;
		bEnabled = Backend.EnableObjectives(Flags);
	}
	if (Own.questRevision != SentRevision)
	{
		SentRevision = Own.questRevision;
		std::string Lines;
		for (const auto& Active : Own.questActive)
		{
			Lines += "A " + std::to_string(Active.instance) + " " + std::to_string(Active.objective) + " " + std::to_string(Active.originTerritory) + " "
				+ std::to_string(Active.parent) + "\n";
		}
		for (const auto& Task : Own.questTasks)
		{
			Lines += "T " + std::to_string(Task.instance) + " " + std::to_string(Task.index) + " " + std::to_string(Task.task) + " "
				+ std::to_string(Task.progress) + " " + std::to_string(Task.state) + "\n";
		}
		for (const std::uint64_t Instance : Own.questTracked)
		{
			Lines += "K " + std::to_string(Instance) + "\n";
		}
		for (const auto& Start : Own.questStarts)
		{
			Lines += "S " + std::to_string(Start.instance) + " " + std::to_string(Start.index) + " " + std::to_string(Start.startNs) + "\n";
		}
		for (const std::uint64_t Instance : Own.questCompleted)
		{
			Lines += "C " + std::to_string(Instance) + "\n";
		}
		Runtime.call("HostQuests", {FArg::of(Lines)});
		bSentAny = true;
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: %d active, %d task element(s), %d tracked"), static_cast<int32>(Own.questActive.size()),
			static_cast<int32>(Own.questTasks.size()), static_cast<int32>(Own.questTracked.size()));
	}
	if (Own.pinReadyRevision != SentReadyRevision)
	{
		SentReadyRevision = Own.pinReadyRevision;
		Runtime.call("HostQuestPinReady", {FArg::of(Own.pinReady)});
	}
	if (!bRestored && Own.pinReady && bSentAny)
	{
		// The pinned list of the state goes back to the backend once after the readiness.
		bRestored = Backend.UpdateTrackedObjectives(std::vector<uint64>(Own.questTracked.begin(), Own.questTracked.end()));
	}
}

bool FSwcUiQuests::HandleRequest(const FString& Request, USwcBackendSubsystem& Backend)
{
	if (!Request.StartsWith(TEXT("quest ")))
	{
		return false;
	}
	TArray<FString> Parts;
	Request.ParseIntoArray(Parts, TEXT(" "));
	const auto Number = [](const FString& Text) { return static_cast<uint64>(FCString::Strtoui64(*Text, nullptr, 10)); };
	if (Parts.Num() >= 2 && Parts[1] == TEXT("pins"))
	{
		std::vector<uint64> Instances;
		if (Parts.Num() >= 3)
		{
			TArray<FString> Items;
			Parts[2].ParseIntoArray(Items, TEXT(","));
			for (const FString& Item : Items)
			{
				Instances.push_back(Number(Item));
			}
		}
		Backend.UpdateTrackedObjectives(Instances);
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: the pinned list (1458): %d instance(s)"), static_cast<int32>(Instances.size()));
	}
	else if (Parts.Num() == 3 && Parts[1] == TEXT("screen"))
	{
		Backend.NotifyOpenUiScreen(static_cast<uint32>(Number(Parts[2])));
	}
	else if (Parts.Num() == 3 && Parts[1] == TEXT("abandon"))
	{
		Backend.AbandonObjective(Number(Parts[2]));
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: abandon %s (3836)"), *Parts[2]);
	}
	else if (Parts.Num() == 3 && Parts[1] == TEXT("reset"))
	{
		Backend.ResetObjective(Number(Parts[2]));
		UE_LOG(LogSwcUiQuests, Log, TEXT("ui: quests: reset %s (5600)"), *Parts[2]);
	}
	else
	{
		UE_LOG(LogSwcUiQuests, Warning, TEXT("ui: a quest request with no form: %s"), *Request);
	}
	return true;
}
