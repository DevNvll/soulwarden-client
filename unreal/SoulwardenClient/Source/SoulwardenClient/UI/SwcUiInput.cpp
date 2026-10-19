#include "UI/SwcUiInput.h"

#include "InputCoreTypes.h"
#include "UI/SwcKeyBindings.h"
#include "UI/SwcUiSubsystem.h"

FString FSwcUiInputProcessor::CryName(const FKey& Key)
{
	return USwcKeyBindings::CryName(Key);
}

FKey FSwcUiInputProcessor::KeyOf(const FString& Name)
{
	return USwcKeyBindings::KeyOf(Name);
}

bool FSwcUiInputProcessor::HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& InKeyEvent)
{
	const FString Name = CryName(InKeyEvent.GetKey());
	if (Name.IsEmpty() || InKeyEvent.IsRepeat())
	{
		return false;
	}
	const bool bUsed = Owner->UiKey(Name, true);
	const bool bKept = bUsed || !Owner->UiKeyToGame(Name);
	if (bLogKeys)
	{
		UE_LOG(LogSwcUi, Log, TEXT("ui: key %s: %s"), *Name,
			bUsed ? TEXT("the UI used it") : bKept ? TEXT("the UI keeps it from the game (its game maps are off)") : TEXT("it goes to the game"));
	}
	return bKept;
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
	// The key page of the options screen binds a key (LyShineManagerBus SetIsKeybinding): a mouse button is a key
	// ("mouse1" to "mouse5"), as the native input gives it to KeyInputNotificationBus.
	if (Owner->UiKeybinding())
	{
		const FString Name = CryName(MouseEvent.GetEffectingButton());
		if (!Name.IsEmpty())
		{
			Owner->UiKey(Name, true);
			return true;
		}
	}
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
	if (Owner->UiKeybinding())
	{
		const FString Name = CryName(Button);
		if (!Name.IsEmpty())
		{
			Owner->UiKey(Name, false);
		}
	}
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
	// The key page binds a key: a step of the wheel is a key ("mwheel_up", "mwheel_down").
	if (Owner->UiKeybinding() && InWheelEvent.GetWheelDelta() != 0.0f)
	{
		const FString Name = InWheelEvent.GetWheelDelta() > 0.0f ? TEXT("mwheel_up") : TEXT("mwheel_down");
		Owner->UiKey(Name, true);
		Owner->UiKey(Name, false);
		return true;
	}
	if (!Owner->UiCursor())
	{
		return false;
	}
	Owner->UiWheel(InWheelEvent.GetWheelDelta());
	return true;
}
