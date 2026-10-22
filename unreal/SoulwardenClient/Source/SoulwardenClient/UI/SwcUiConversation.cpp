#include "UI/SwcUiConversation.h"

#include "Engine/World.h"
#include "Persons/SwcPersonSubsystem.h"
#include "SwcBackendSubsystem.h"
#include "SwcUi/Runtime.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcUiConversation, Log, All);

namespace
{
std::string Utf8Of(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}
}

void FSwcUiConversation::Update(swc::ui::Runtime& Runtime, UWorld* World, USwcBackendSubsystem& Backend)
{
	const FString Key = Backend.GetConversationSceneKey();
	if (Key == ShownKey)
	{
		return;
	}
	using FArg = swc::ui::Runtime::Arg;
	if (!ShownKey.IsEmpty())
	{
		Runtime.call("HostConversation", {FArg::of(std::string("close"))});
		Backend.SetConversationScreenShown(false);
		UE_LOG(LogSwcUiConversation, Log, TEXT("ui: the conversation screen closes (the scene of %s ended)"), *ShownKey);
	}
	ShownKey = Key;
	if (Key.IsEmpty())
	{
		return;
	}
	const USwcPersonSubsystem* Persons = World != nullptr ? World->GetSubsystem<USwcPersonSubsystem>() : nullptr;
	const FSwcPerson* Person = Persons != nullptr ? Persons->FindByInteraction(Key) : nullptr;
	const bool bLine = Person != nullptr && Person->Line.bValid && !Person->Line.LineKey.IsEmpty();
	if (Person == nullptr || (!bLine && Person->NpcId.IsEmpty()))
	{
		// No line and no npc id (a person with no conversation state): no screen; the client ends the Talk as before (E, a refusal).
		UE_LOG(LogSwcUiConversation, Log, TEXT("ui: no conversation screen for %s: the person has no line"), *Key);
		return;
	}
	Runtime.call("HostConversation", {FArg::of(std::string("open")), FArg::of(Utf8Of(Key)), FArg::of(Utf8Of(Person->Line.Speaker)),
		FArg::of(Utf8Of(Person->Line.LineKey)), FArg::of(Utf8Of(Person->Name)), FArg::of(Utf8Of(Person->Title)),
		FArg::of(Utf8Of(FString::Join(Person->Line.Lines, TEXT("\n")))), FArg::of(Utf8Of(Person->NpcId))});
	Backend.SetConversationScreenShown(true);
	UE_LOG(LogSwcUiConversation, Log, TEXT("ui: the conversation screen opens: %s (%s), line %s, %d line(s)"), *Person->Name, *Person->NpcId,
		*Person->Line.LineKey, FMath::Max(1, Person->Line.Lines.Num()));
}

bool FSwcUiConversation::HandleRequest(const FString& Request, USwcBackendSubsystem& Backend)
{
	// "conversation topic CRC" (3158) and "conversation end [CRC,CRC,...]" (6705, 2473, then 5397 with the finished topics): AST-248.
	TArray<FString> Parts;
	Request.ParseIntoArray(Parts, TEXT(" "));
	if (Parts.Num() < 2 || Parts[0] != TEXT("conversation"))
	{
		return false;
	}
	if (Parts[1] == TEXT("topic") && Parts.Num() == 3)
	{
		const uint32 Topic = static_cast<uint32>(FCString::Strtoui64(*Parts[2], nullptr, 10));
		UE_LOG(LogSwcUiConversation, Log, TEXT("ui: the conversation screen selects the topic %08x (3158)"), Topic);
		Backend.ConversationTopic(Topic);
		return true;
	}
	if (Parts[1] != TEXT("end"))
	{
		return false;
	}
	std::vector<uint32> Topics;
	if (Parts.Num() >= 3)
	{
		TArray<FString> Items;
		Parts[2].ParseIntoArray(Items, TEXT(","));
		for (const FString& Item : Items)
		{
			Topics.push_back(static_cast<uint32>(FCString::Strtoui64(*Item, nullptr, 10)));
		}
	}
	UE_LOG(LogSwcUiConversation, Log, TEXT("ui: the conversation screen ends the conversation (6705, then 2473; %d finished topic(s))"),
		static_cast<int32>(Topics.size()));
	Backend.EndConversation(Topics);
	return true;
}
