#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

class USwcUiSubsystem;

/**
 * The input of the UI (docs/UI.md): a Slate input pre-processor that sees each key and mouse event before the game
 * viewport. Each key goes to the UI runtime (Swc.HostInput: an editing text input, else the game actions of the action
 * maps). The UI keeps an event from the game when it used it, when the UI has the cursor (the pointer events), or when
 * each game map that binds the key is off (a shown screen turned the map off; Swc.HostKeyToGame). A key release always
 * goes to the game: a held game key ends.
 */
class FSwcUiInputProcessor : public IInputProcessor
{
public:
	explicit FSwcUiInputProcessor(USwcUiSubsystem* InOwner) : Owner(InOwner) {}

	/** @return The CryInput name of a key of the action maps ("escape", "w", "lshift", "tilde"), or empty. */
	static FString CryName(const FKey& Key);
	/** @return The key of a CryInput name, or EKeys::Invalid. */
	static FKey KeyOf(const FString& Name);

	/** A log line for each key press (the harness input of the UI): the UI used it, keeps it, or gives it to the game. */
	bool bLogKeys = false;

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("SwcUiInput"); }

private:
	USwcUiSubsystem* Owner;
};
