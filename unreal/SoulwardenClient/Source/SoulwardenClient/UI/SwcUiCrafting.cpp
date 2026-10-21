#include "UI/SwcUiCrafting.h"

#include "SwcBackendSubsystem.h"
#include "SwcUi/Runtime.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcUiCrafting, Log, All);

namespace
{
std::string Utf8Of(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}
}

void FSwcUiCrafting::Update(swc::ui::Runtime& Runtime, USwcBackendSubsystem& Backend)
{
	FString Key;
	FString Name;
	bool bFee = false;
	const bool bOpen = Backend.GetCraftingStation(Key, bFee, Name);
	const FString Shown = bOpen ? Key + TEXT("|") + Name : FString();
	if (Shown == ShownKey)
	{
		return;
	}
	using FArg = swc::ui::Runtime::Arg;
	if (!ShownKey.IsEmpty())
	{
		Runtime.call("HostStation", {FArg::of(std::string("close"))});
		UE_LOG(LogSwcUiCrafting, Log, TEXT("ui: the crafting screen closes (the station use of %s ended)"), *ShownKey);
	}
	ShownKey = Shown;
	if (!bOpen)
	{
		return;
	}
	Runtime.call("HostStation", {FArg::of(std::string("open")), FArg::of(Utf8Of(Key)), FArg::of(bFee), FArg::of(Utf8Of(Name))});
	UE_LOG(LogSwcUiCrafting, Log, TEXT("ui: the crafting screen opens: %s (%s, fee %d)"), *Key, *Name, bFee ? 1 : 0);
}

bool FSwcUiCrafting::HandleRequest(const FString& Request, USwcBackendSubsystem& Backend)
{
	if (Request != TEXT("station end"))
	{
		return false;
	}
	UE_LOG(LogSwcUiCrafting, Log, TEXT("ui: the crafting screen ends the station use (2473)"));
	Backend.EndCraftingStation();
	return true;
}
