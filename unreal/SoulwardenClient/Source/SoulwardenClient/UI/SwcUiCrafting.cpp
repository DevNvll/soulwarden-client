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
		Backend.SetCraftingScreenShown(false);
		UE_LOG(LogSwcUiCrafting, Log, TEXT("ui: the crafting screen closes (the station use of %s ended)"), *ShownKey);
	}
	ShownKey = Shown;
	if (!bOpen)
	{
		return;
	}
	std::string Result;
	const bool bCalled = Runtime.call("HostStation", {FArg::of(std::string("open")), FArg::of(Utf8Of(Key)), FArg::of(bFee), FArg::of(Utf8Of(Name))}, &Result);
	if (!bCalled || Result != "ok")
	{
		// The screen did not open: the client ends the station use (2473), so that the player is not stuck (AST-287).
		UE_LOG(LogSwcUiCrafting, Warning, TEXT("ui: the crafting screen of %s did not open (%s): the client ends the station use"), *Key,
			bCalled ? UTF8_TO_TCHAR(Result.c_str()) : TEXT("the call of Swc.HostStation failed"));
		Backend.EndCraftingStation();
		return;
	}
	Backend.SetCraftingScreenShown(true);
	UE_LOG(LogSwcUiCrafting, Log, TEXT("ui: the crafting screen opens: %s (%s, fee %d)"), *Key, *Name, bFee ? 1 : 0);
}

void FSwcUiCrafting::Feed(swc::ui::Runtime& Runtime, const swc::net::RepClient::OwnEntity& Own)
{
	using FArg = swc::ui::Runtime::Arg;
	if (Own.craftRevision != SentCraftRevision)
	{
		SentCraftRevision = Own.craftRevision;
		std::string Windows;
		for (const swc::net::RepClient::OwnEntity::CraftCooldown& Window : Own.craftCooldowns)
		{
			Windows += std::to_string(Window.recipe) + " " + std::to_string(Window.used) + " " + std::to_string(Window.wallEndNs / 1000000000ULL) + "\n";
		}
		std::string Bonus;
		for (const auto& [Code, Value] : Own.craftGearBonus)
		{
			Bonus += std::to_string(Code) + " " + std::to_string(Value) + "\n";
		}
		Runtime.call("HostCraftCooldowns", {FArg::of(Windows)});
		Runtime.call("HostCraftBonus", {FArg::of(Bonus)});
		UE_LOG(LogSwcUiCrafting, Log, TEXT("ui: the craft state: %d cooldown window(s), %d gear score bonus(es)"), static_cast<int32>(Own.craftCooldowns.size()),
			static_cast<int32>(Own.craftGearBonus.size()));
	}
	if (Own.craftProgresses != SentProgresses)
	{
		SentProgresses = Own.craftProgresses;
		Runtime.call("HostCraftProgress", {FArg::of(Own.craftRecipe), FArg::of(static_cast<double>(Own.craftCompleted)),
			FArg::of(static_cast<double>(Own.craftTotal))});
	}
}

bool FSwcUiCrafting::HandleRequest(const FString& Request, USwcBackendSubsystem& Backend)
{
	if (Request == TEXT("station end"))
	{
		UE_LOG(LogSwcUiCrafting, Log, TEXT("ui: the crafting screen ends the station use (2473)"));
		Backend.EndCraftingStation();
		return true;
	}
	if (!Request.StartsWith(TEXT("craft ")))
	{
		return false;
	}
	// "craft RECIPE COUNT FEE GEARSCORE UPGRADE PERKS ITEMS" (swc/crafting.lua CraftRecipe): PERKS and ITEMS are comma lists of CRC-32 values
	// ("-": none).
	TArray<FString> Parts;
	Request.ParseIntoArray(Parts, TEXT(" "));
	if (Parts.Num() != 8)
	{
		UE_LOG(LogSwcUiCrafting, Warning, TEXT("ui: a craft request with no form: %s"), *Request);
		return true;
	}
	const auto List = [](const FString& Text)
	{
		TArray<uint32> Out;
		TArray<FString> Items;
		Text.ParseIntoArray(Items, TEXT(","));
		for (const FString& Item : Items)
		{
			if (Item != TEXT("-"))
			{
				Out.Add(static_cast<uint32>(FCString::Strtoui64(*Item, nullptr, 10)));
			}
		}
		return Out;
	};
	const bool bSent = Backend.RequestCraft(static_cast<uint32>(FCString::Strtoui64(*Parts[1], nullptr, 10)), FCString::Atoi(*Parts[2]),
		FCString::Atoi(*Parts[3]), static_cast<uint16>(FCString::Atoi(*Parts[4])), List(Parts[6]), static_cast<uint8>(FCString::Atoi(*Parts[5])),
		List(Parts[7]));
	UE_LOG(LogSwcUiCrafting, Log, TEXT("ui: the crafting screen asks for a craft (%s): %s"), *Request, bSent ? TEXT("sent") : TEXT("not sent"));
	return true;
}
