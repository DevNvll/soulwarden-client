#include "UI/SwcKeyBindings.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcKeys, Log, All);

namespace
{
/** The CryInput names of the keys and the mouse (the names of the action maps of the client profile; the letters are
 *  their lower-case names). */
const TMap<FKey, FString>& KeyNames()
{
	static const TMap<FKey, FString> Names = {
		{EKeys::Escape, TEXT("escape")}, {EKeys::Enter, TEXT("enter")}, {EKeys::SpaceBar, TEXT("space")},
		{EKeys::Tab, TEXT("tab")}, {EKeys::BackSpace, TEXT("backspace")}, {EKeys::Delete, TEXT("delete")},
		{EKeys::Insert, TEXT("insert")}, {EKeys::Home, TEXT("home")}, {EKeys::End, TEXT("end")},
		{EKeys::PageUp, TEXT("pgup")}, {EKeys::PageDown, TEXT("pgdn")}, {EKeys::Left, TEXT("left")},
		{EKeys::Right, TEXT("right")}, {EKeys::Up, TEXT("up")}, {EKeys::Down, TEXT("down")},
		{EKeys::LeftShift, TEXT("lshift")}, {EKeys::RightShift, TEXT("rshift")}, {EKeys::LeftControl, TEXT("lctrl")},
		{EKeys::RightControl, TEXT("rctrl")}, {EKeys::LeftAlt, TEXT("lalt")}, {EKeys::RightAlt, TEXT("ralt")},
		{EKeys::Tilde, TEXT("tilde")}, {EKeys::Semicolon, TEXT("semicolon")}, {EKeys::Comma, TEXT("comma")},
		{EKeys::Period, TEXT("period")}, {EKeys::Slash, TEXT("slash")}, {EKeys::Backslash, TEXT("backslash")},
		{EKeys::Hyphen, TEXT("minus")}, {EKeys::Equals, TEXT("equals")}, {EKeys::LeftBracket, TEXT("lbracket")},
		{EKeys::RightBracket, TEXT("rbracket")}, {EKeys::Apostrophe, TEXT("apostrophe")},
		{EKeys::Zero, TEXT("0")}, {EKeys::One, TEXT("1")}, {EKeys::Two, TEXT("2")}, {EKeys::Three, TEXT("3")},
		{EKeys::Four, TEXT("4")}, {EKeys::Five, TEXT("5")}, {EKeys::Six, TEXT("6")}, {EKeys::Seven, TEXT("7")},
		{EKeys::Eight, TEXT("8")}, {EKeys::Nine, TEXT("9")}, {EKeys::NumPadZero, TEXT("np_0")},
		{EKeys::NumPadOne, TEXT("np_1")}, {EKeys::NumPadTwo, TEXT("np_2")}, {EKeys::NumPadThree, TEXT("np_3")},
		{EKeys::NumPadFour, TEXT("np_4")}, {EKeys::NumPadFive, TEXT("np_5")}, {EKeys::NumPadSix, TEXT("np_6")},
		{EKeys::NumPadSeven, TEXT("np_7")}, {EKeys::NumPadEight, TEXT("np_8")}, {EKeys::NumPadNine, TEXT("np_9")},
		{EKeys::F1, TEXT("f1")}, {EKeys::F2, TEXT("f2")}, {EKeys::F3, TEXT("f3")}, {EKeys::F4, TEXT("f4")},
		{EKeys::F5, TEXT("f5")}, {EKeys::F6, TEXT("f6")}, {EKeys::F7, TEXT("f7")}, {EKeys::F8, TEXT("f8")},
		{EKeys::F9, TEXT("f9")}, {EKeys::F10, TEXT("f10")}, {EKeys::F11, TEXT("f11")}, {EKeys::F12, TEXT("f12")},
		// The mouse: the buttons and the steps of the wheel.
		{EKeys::LeftMouseButton, TEXT("mouse1")}, {EKeys::RightMouseButton, TEXT("mouse2")},
		{EKeys::MiddleMouseButton, TEXT("mouse3")}, {EKeys::ThumbMouseButton, TEXT("mouse4")},
		{EKeys::ThumbMouseButton2, TEXT("mouse5")}, {EKeys::MouseScrollUp, TEXT("mwheel_up")},
		{EKeys::MouseScrollDown, TEXT("mwheel_down")},
	};
	return Names;
}

FString WorkDir()
{
	FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
	if (Work.IsEmpty())
	{
		Work = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("Games/soulwarden-client"));
	}
	return Work;
}
} // namespace

FString USwcKeyBindings::CryName(const FKey& Key)
{
	if (const FString* Found = KeyNames().Find(Key))
	{
		return *Found;
	}
	const FString Name = Key.GetFName().ToString();
	if (Name.Len() == 1 && FChar::IsAlpha(Name[0]))
	{
		return Name.ToLower();
	}
	return FString();
}

FKey USwcKeyBindings::KeyOf(const FString& Name)
{
	if (const FKey* Found = KeyNames().FindKey(Name))
	{
		return *Found;
	}
	if (Name.Len() == 1 && FChar::IsAlpha(Name[0]))
	{
		return FKey(FName(*Name.ToUpper()));
	}
	return EKeys::Invalid;
}

void USwcKeyBindings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	if (Content.IsEmpty())
	{
		Content = FPaths::Combine(WorkDir(), TEXT("content"));
	}
	const FString MapsFile = FPaths::Combine(Content, TEXT("ui/gamedata/actionmaps.json"));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *MapsFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) ||
		!Root.IsValid())
	{
		UE_LOG(LogSwcKeys, Warning, TEXT("keys: no action maps in %s (tools/convert-ui.sh actionmaps): no key table"), *MapsFile);
		return;
	}
	for (const TSharedPtr<FJsonValue>& MapValue : Root->GetArrayField(TEXT("maps")))
	{
		const TSharedPtr<FJsonObject> Map = MapValue->AsObject();
		const FString MapName = Map->GetStringField(TEXT("name"));
		for (const TSharedPtr<FJsonValue>& ActionValue : Map->GetArrayField(TEXT("actions")))
		{
			const TSharedPtr<FJsonObject> Action = ActionValue->AsObject();
			const FString Slot = MapName + TEXT("/") + Action->GetStringField(TEXT("name"));
			if (Defaults.Contains(Slot))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& InputValue : Action->GetArrayField(TEXT("inputs")))
			{
				FString Key;
				if (InputValue->AsObject()->TryGetStringField(TEXT("key"), Key) && !Key.IsEmpty())
				{
					Defaults.Add(Slot, Key);
					break;
				}
			}
		}
	}
	Load();
	UE_LOG(LogSwcKeys, Log, TEXT("keys: %d action keys of the profile, %d rebind(s) of %s"), Defaults.Num(), Overrides.Num(), *SavePath());
}

FString USwcKeyBindings::SavePath() const
{
	FString Dir;
	if (!FParse::Value(FCommandLine::Get(), TEXT("SwcSettings="), Dir))
	{
		// One folder for each data folder of the backend: the development data of a track, or the data of the user (its
		// folder name only; no user id).
		FString Session = FPaths::GetCleanFilename(FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_SESSION_DATA")));
		if (Session.IsEmpty())
		{
			Session = TEXT("default");
		}
		Dir = FPaths::Combine(WorkDir(), TEXT("settings"), Session);
	}
	return FPaths::Combine(Dir, TEXT("rebindings.json"));
}

void USwcKeyBindings::Load()
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *SavePath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) ||
		!Root.IsValid())
	{
		return;
	}
	const TSharedPtr<FJsonObject>* Keys = nullptr;
	if (Root->TryGetObjectField(TEXT("keyboardMouse"), Keys))
	{
		for (const auto& Pair : (*Keys)->Values)
		{
			const FString Slot(Pair.Key);
			if (Defaults.Contains(Slot))
			{
				Overrides.Add(Slot, Pair.Value->AsString());
			}
		}
	}
}

FString USwcKeyBindings::GetCryKey(const FString& Map, const FString& Action) const
{
	const FString Slot = Map + TEXT("/") + Action;
	if (const FString* Found = Overrides.Find(Slot))
	{
		return Found->TrimStartAndEnd();
	}
	const FString* Default = Defaults.Find(Slot);
	return Default != nullptr ? *Default : FString();
}

FKey USwcKeyBindings::GetKey(const FString& Map, const FString& Action) const
{
	const FString Name = GetCryKey(Map, Action);
	return Name.IsEmpty() ? EKeys::Invalid : KeyOf(Name);
}

void USwcKeyBindings::Rebind(const FString& Map, const FString& Action, const FString& CryKey)
{
	const FString Slot = Map + TEXT("/") + Action;
	if (!Defaults.Contains(Slot))
	{
		UE_LOG(LogSwcKeys, Warning, TEXT("keys: no action %s in the profile: no rebind"), *Slot);
		return;
	}
	if (CryKey == Defaults[Slot])
	{
		Overrides.Remove(Slot);
	}
	else
	{
		Overrides.Add(Slot, CryKey);
	}
	UE_LOG(LogSwcKeys, Log, TEXT("keys: %s -> %s"), *Slot, CryKey.TrimStartAndEnd().IsEmpty() ? TEXT("no key") : *CryKey);
	OnChanged.Broadcast();
}

void USwcKeyBindings::Reset()
{
	Overrides.Reset();
	IFileManager::Get().Delete(*SavePath(), false, false, true);
	UE_LOG(LogSwcKeys, Log, TEXT("keys: the defaults of the profile again (the file of the rebinds goes)"));
	OnChanged.Broadcast();
}

void USwcKeyBindings::Save() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Keys = MakeShared<FJsonObject>();
	TArray<FString> Slots;
	Overrides.GetKeys(Slots);
	Slots.Sort();
	for (const FString& Slot : Slots)
	{
		Keys->SetStringField(Slot, Overrides[Slot]);
	}
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetObjectField(TEXT("keyboardMouse"), Keys);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	const FString Path = SavePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	const bool bSaved = FFileHelper::SaveStringToFile(Text, *Path);
	UE_LOG(LogSwcKeys, Log, TEXT("keys: %d rebind(s) %s %s"), Overrides.Num(), bSaved ? TEXT("saved to") : TEXT("NOT saved to"), *Path);
}

FString USwcKeyBindings::OverrideLines() const
{
	FString Lines;
	for (const TPair<FString, FString>& Pair : Overrides)
	{
		FString Map, Action;
		Pair.Key.Split(TEXT("/"), &Map, &Action);
		const FString Key = Pair.Value.TrimStartAndEnd().IsEmpty() ? FString(TEXT("-")) : Pair.Value;
		Lines += Map + TEXT(" ") + Action + TEXT(" ") + Key + TEXT("\n");
	}
	return Lines;
}
