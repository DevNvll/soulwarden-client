#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "SwcKeyBindings.generated.h"

/**
 * The key table of the actions (stage P2.3; docs/UI.md "The keys"): the key of each action of each action map of the
 * client profile, as CryInput names ("w", "lshift", "mouse1"). The defaults come from the action maps of the data
 * (tools/convert-ui.sh actionmaps: gamedata/actionmaps.json; the first keyboard or mouse input of an action). The rebinds
 * of the original options screen go on top (the UI emulation asks for them: swc/keys.lua) and go to a file of this
 * client: SWC_WORK/settings/<the name of the session data folder>/rebindings.json (no user id; never in the data folder of
 * the backend). The player controller maps its play actions from this table and maps them again at OnChanged.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcKeyBindings : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** @return The CryInput name of the key of an action of a map, or an empty text (no such action, or no key). */
	FString GetCryKey(const FString& Map, const FString& Action) const;
	/** @return The key of an action of a map, or EKeys::Invalid. */
	FKey GetKey(const FString& Map, const FString& Action) const;
	/** A rebind of the options screen (a CryInput name; " " for no key). OnChanged follows. */
	void Rebind(const FString& Map, const FString& Action, const FString& CryKey);
	/** The defaults again (the reset of the options screen); the file goes too. OnChanged follows. */
	void Reset();
	/** Writes the rebinds to the file of this client (SaveRebindData of the options screen). */
	void Save() const;
	/** @return The rebinds as lines "MAP ACTION KEY" ("-" for no key): the UI emulation takes them at its start. */
	FString OverrideLines() const;
	int32 OverrideCount() const { return Overrides.Num(); }

	/** The key table changed (a rebind or a reset). */
	FSimpleMulticastDelegate OnChanged;

	/** @return The CryInput name of an Unreal key (the names of the action maps), or an empty text. */
	static FString CryName(const FKey& Key);
	/** @return The Unreal key of a CryInput name, or EKeys::Invalid. */
	static FKey KeyOf(const FString& CryName);
	/** @return The settings folder of this client: SWC_WORK/settings/<the name of the session data folder> (-SwcSettings=DIR
	 *  gives another folder). The rebinds and the options (UI/SwcGameSettings) go there. */
	static FString SettingsDir();

private:
	FString SavePath() const;
	void Load();

	/** "map/action" -> the CryInput name of the profile. */
	TMap<FString, FString> Defaults;
	/** "map/action" -> the CryInput name of a rebind. */
	TMap<FString, FString> Overrides;
};
