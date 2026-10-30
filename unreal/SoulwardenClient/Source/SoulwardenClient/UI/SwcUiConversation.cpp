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
	// The NPC row: the person of the person layer, else the npc object of the interaction (a board, a note, a person with no record of
	// the person layer; AST-287: the Tour Board of Everfall has the row EF_Tour_2 and no person).
	const FString NpcId = Person != nullptr && !Person->NpcId.IsEmpty() ? Person->NpcId : Backend.GetConversationNpcId();
	const FString Name = Person != nullptr ? Person->Name : Backend.GetConversationName();
	if (!bLine && NpcId.IsEmpty())
	{
		// No line and no NPC row: the client cannot show a screen. It ends the conversation at once (6705, then 2473; AST-287).
		UE_LOG(LogSwcUiConversation, Warning, TEXT("ui: no conversation screen for %s (%s): the client ends the conversation"), *Key,
			Person == nullptr ? TEXT("no person record and no NPC row") : TEXT("the person has no line and no NPC row"));
		Backend.EndConversation();
		return;
	}
	std::string Result;
	const bool bCalled = Runtime.call("HostConversation", {FArg::of(std::string("open")), FArg::of(Utf8Of(Key)),
		FArg::of(Utf8Of(bLine ? Person->Line.Speaker : FString())), FArg::of(Utf8Of(bLine ? Person->Line.LineKey : FString())), FArg::of(Utf8Of(Name)),
		FArg::of(Utf8Of(Person != nullptr ? Person->Title : FString())), FArg::of(Utf8Of(bLine ? FString::Join(Person->Line.Lines, TEXT("\n")) : FString())),
		FArg::of(Utf8Of(NpcId))}, &Result);
	if (!bCalled || Result != "ok")
	{
		// The screen did not open (a script error, a canvas that did not load, no state and no line): the client ends the conversation.
		UE_LOG(LogSwcUiConversation, Warning, TEXT("ui: the conversation screen of %s did not open (%s): the client ends the conversation"), *Key,
			bCalled ? UTF8_TO_TCHAR(Result.c_str()) : TEXT("the call of Swc.HostConversation failed"));
		Runtime.call("HostConversation", {FArg::of(std::string("close"))});
		Backend.EndConversation();
		return;
	}
	Backend.SetConversationScreenShown(true);
	UE_LOG(LogSwcUiConversation, Log, TEXT("ui: the conversation screen opens: %s (%s), line %s, %d line(s)"), *Name, *NpcId,
		bLine ? *Person->Line.LineKey : TEXT("of the NPC row"), bLine ? FMath::Max(1, Person->Line.Lines.Num()) : 0);
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
	if (Parts[1] == TEXT("line") && Parts.Num() == 3)
	{
		// "conversation line KEY" (swc/conversation.lua, ut567): the line that the screen shows; its camera (persons/line-cameras.json).
		Backend.SetConversationLineKey(Parts[2] == TEXT("-") ? FString() : Parts[2]);
		return true;
	}
	// "conversation board [CRC,...]" (C-12 phase 2): the option 8 Open Faction Board ends the conversation as "end", then the board use
	// of the same person (action 51).
	const bool bBoard = Parts[1] == TEXT("board");
	if (Parts[1] != TEXT("end") && !bBoard)
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
	if (bBoard)
	{
		const bool bOpen = Backend.OpenPersonBoard();
		UE_LOG(LogSwcUiConversation, Log, TEXT("ui: the conversation option Open Faction Board: %s"),
			bOpen ? TEXT("the board use of the person follows (action 51)") : TEXT("the person has no board option"));
	}
	return true;
}
