#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "SwcGameSettings.generated.h"

/**
 * The settings of the original Options screen that this client applies (the game menu; docs/UI.md "The game menu"). The UI
 * emulation (swc/options.lua) sends the values of the shown rows ("options" requests: NODE and a typed value: b1/b0,
 * n<number>, s<text>); this subsystem keeps them, applies the display, the render scale, the frame rate, the V-Sync, the
 * quality, the field of view, and the invert, and writes them at "options-save" to a file of this client:
 * SWC_WORK/settings/<the name of the session data folder>/options.json, next to rebindings.json (USwcKeyBindings::SettingsDir;
 * no user id). A development run has its own data folder and thus its own file.
 *
 * Nothing goes to Config/Default*.ini or to the GameUserSettings of Unreal (its Saved folder is shared by the cooked build of the
 * user and the development runs with --cooked): the values apply as console variables set by code, as scalability levels with
 * no save, and as a resolution request.
 *
 * The display at the start: the saved window mode and resolution apply only when the command line has no -ResX, -ResY,
 * -windowed, or -fullscreen (tools/dev-run.sh gives the size of the agent desktop). A display row that the user changes in the
 * screen always applies.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcGameSettings : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The requests of the UI: "options\nNODE\tVALUE..." (values to keep and apply) and "options-save". @return True when it
	 *  was a settings request. */
	bool HandleRequest(const FString& Request);
	/** @return The saved values as lines "NODE\tVALUE": the UI emulation takes them at its start (Swc.HostOptions). */
	FString NodeLines() const;
	/** @return The display modes of the monitor as lines "WIDTHxHEIGHT" (Swc.HostResolutions). */
	static FString ResolutionLines();
	/** @return The window mode of the game viewport: 0 fullscreen, 1 windowed, 2 windowed fullscreen. */
	static int32 CurrentWindowMode();

	/** The vertical field of view of the player camera (the row Field of View, 40 to 70; default 57). */
	float GetFov() const { return Fov; }
	/** The row Invert Vertical Axis of the mouse. */
	bool GetInvertLook() const { return bInvertLook; }

private:
	FString SavePath() const;
	void Load();
	void Save() const;
	/** Applies one value (a node of the rows that this client applies; the other nodes are only kept). */
	void Apply(const FString& Node, bool bStart);
	void ApplyRenderScale();
	void ApplyQuality();
	/** The window mode and the resolution of the values, when they differ from the viewport. */
	void ApplyDisplay();
	bool Tick(float DeltaTime);
	double Number(const FString& Node, double Default) const;

	/** NODE -> the typed value of the UI ("b1", "n57", "sText"). */
	TMap<FString, FString> Values;
	float Fov = 57.0f;
	bool bInvertLook = false;
	/** The command line gives the display (-ResX, -ResY, -windowed, -fullscreen): the saved display does not apply at the start. */
	bool bDisplayFromCommandLine = false;
	bool bStartApplied = false;
	FTSTicker::FDelegateHandle TickHandle;
};
