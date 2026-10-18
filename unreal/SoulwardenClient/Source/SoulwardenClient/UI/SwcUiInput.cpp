#include "UI/SwcUiInput.h"

#include "InputCoreTypes.h"
#include "UI/SwcUiSubsystem.h"

FString FSwcUiInputProcessor::CryName(const FKey& Key)
{
	// The names of the keys in the action maps of the client profile (CryInput; gamedata/actionmaps.json).
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
	};
	if (const FString* Found = Names.Find(Key))
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

bool FSwcUiInputProcessor::HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& InKeyEvent)
{
	const FString Name = CryName(InKeyEvent.GetKey());
	if (Name.IsEmpty() || InKeyEvent.IsRepeat())
	{
		return false;
	}
	const bool bUsed = Owner->UiKey(Name, true);
	return bUsed || !Owner->UiKeyToGame(Name);
}

bool FSwcUiInputProcessor::HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& InKeyEvent)
{
	const FString Name = CryName(InKeyEvent.GetKey());
	if (!Name.IsEmpty())
	{
		Owner->UiKey(Name, false);
	}
	return false;
}

bool FSwcUiInputProcessor::HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent& MouseEvent)
{
	if (Owner->UiCursor())
	{
		Owner->UiPointer(MouseEvent.GetScreenSpacePosition());
		return true;
	}
	return !Owner->UiKeyToGame(TEXT("maxis_x"));
}

bool FSwcUiInputProcessor::HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& MouseEvent)
{
	if (!Owner->UiCursor())
	{
		return false;
	}
	Owner->UiPointer(MouseEvent.GetScreenSpacePosition());
	const FKey Button = MouseEvent.GetEffectingButton();
	if (Button == EKeys::LeftMouseButton || Button == EKeys::RightMouseButton)
	{
		Owner->UiButton(Button == EKeys::LeftMouseButton ? TEXT("left") : TEXT("right"), true);
	}
	return true;
}

bool FSwcUiInputProcessor::HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& MouseEvent)
{
	const FKey Button = MouseEvent.GetEffectingButton();
	if (Button == EKeys::LeftMouseButton || Button == EKeys::RightMouseButton)
	{
		// A release always goes to the UI (a press can have started a drag) and to the game (a held attack ends).
		Owner->UiButton(Button == EKeys::LeftMouseButton ? TEXT("left") : TEXT("right"), false);
	}
	return false;
}

bool FSwcUiInputProcessor::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	return HandleMouseButtonDownEvent(SlateApp, MouseEvent);
}

bool FSwcUiInputProcessor::HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent& InWheelEvent, const FPointerEvent*)
{
	if (!Owner->UiCursor())
	{
		return false;
	}
	Owner->UiWheel(InWheelEvent.GetWheelDelta());
	return true;
}
