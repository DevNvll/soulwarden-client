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
	if (Person == nullptr || !Person->Line.bValid || Person->Line.LineKey.IsEmpty())
	{
		// No line (a person with no conversation state): no screen; the client ends the Talk as before (E, a refusal).
		UE_LOG(LogSwcUiConversation, Log, TEXT("ui: no conversation screen for %s: the person has no line"), *Key);
		return;
	}
	Runtime.call("HostConversation", {FArg::of(std::string("open")), FArg::of(Utf8Of(Key)), FArg::of(Utf8Of(Person->Line.Speaker)),
		FArg::of(Utf8Of(Person->Line.LineKey)), FArg::of(Utf8Of(Person->Name)), FArg::of(Utf8Of(Person->Title)),
		FArg::of(Utf8Of(FString::Join(Person->Line.Lines, TEXT("\n"))))});
	Backend.SetConversationScreenShown(true);
	UE_LOG(LogSwcUiConversation, Log, TEXT("ui: the conversation screen opens: %s, line %s, %d line(s)"), *Person->Name, *Person->Line.LineKey,
		FMath::Max(1, Person->Line.Lines.Num()));
}

bool FSwcUiConversation::HandleRequest(const FString& Request, USwcBackendSubsystem& Backend)
{
	if (Request != TEXT("conversation end"))
	{
		return false;
	}
	UE_LOG(LogSwcUiConversation, Log, TEXT("ui: the conversation screen ends the conversation (6705, then 2473)"));
	Backend.EndConversation();
	return true;
}
