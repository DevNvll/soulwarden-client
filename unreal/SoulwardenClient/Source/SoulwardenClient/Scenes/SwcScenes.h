#pragma once

#include "CoreMinimal.h"
#include "Net/SwcRepConnection.h"

class APawn;
class ASwcScenePlayer;
class FJsonObject;
class FJsonValue;
class UWorld;

/**
 * The in-game scenes of the quests (stage Q5, AST-240; docs/QUESTS.md "The scenes, the cinematics, and the whispers";
 * research/quest-scenes.md): the activation by the bits of the character, the handshake, and the end paths. ASwcScenePlayer plays.
 *
 * - The activation (1.1, 1.4): a spawner of quests/scenes.json runs its AchievementEvents when its condition (the compiled rule) becomes
 *   true while the player is in its AOI radius, or when the player enters the radius with the condition true [I: the AOI radius as the
 *   load gate of the spawner]; after the event delay, entity event 0 on the trigger with playOnLoad, or entity event 2, requests the
 *   scene [I: 0 activates, 2 triggers, 1 deactivates the trigger]. No local timer before 6703 (the 2 s of L2 is not a rule).
 * - The handshake (1.3, 1.4, 2.1): one pending request (6703 with the CRC-32 of the lower-case name), 30 s at most (then the failure:
 *   6705, no second 6703); the grant 6707 of the same CRC builds and starts the scene (the start events, the screen, then 6706 once); a
 *   grant of another CRC sends 6705 and starts nothing.
 * - The end (2.4, 3, 5, 6): the deactivation of the slice root (the normal end), a skip (the cutscene screen: RequestStopCurrentCutscene),
 *   a cancel 6708, a travel (a loading screen) go through one common end: the scene goes, the exit camera IGC_CameraState_Generic until
 *   the next input, the screen gets OnCutsceneEnded, 6705 goes out once; the hard input lock stays until 6709 (ignoreAI and
 *   playerInvincibility are true for the chain). A world exit or a lost connection ends all locally with no message.
 * - The development preview (console swc.ScenePreview NAME; development runs only): the play with no handshake and no quest effect.
 */
class FSwcScenes
{
public:
	FSwcScenes();
	~FSwcScenes();

	void Tick(UWorld* World, const FSwcRepConnection::FSnapshot& Snapshot, FSwcRepConnection* Connection, APawn* Player, float DeltaSeconds);
	/** The hard input lock of a granted scene (until 6709). */
	bool IsLocked() const { return State == EState::Playing || State == EState::Ending; }
	/** CutsceneInfo m_hidePlayerAvatar of the scene that plays. */
	bool HidesPlayer() const;
	bool GetView(FVector& Location, FRotator& Rotation, float& VerticalFov) const;
	/** True from the end of a scene until the next input of the player (m_lockCameraStateOnExitUntilInput). */
	bool HoldsExitCamera() const { return bExitCamera; }
	const FString& GetExitCamera() const { return ExitCamera; }
	void ReleaseExitCamera();
	/** The lines for swc/scenes.lua since the last call. */
	TArray<FString> TakeUiLines() { return MoveTemp(UiLines); }
	/** The skip of the cutscene screen (PlayerCutsceneComponentRequestBus RequestStopCurrentCutscene). */
	void RequestStop(const FString& Reason);
	bool Preview(UWorld* World, APawn* Player, const FString& Name, FString& Error);
	/** The end of the world or of the session: all ends locally, with no message. */
	void Teardown(const TCHAR* Why);

private:
	enum class EState : uint8
	{
		Idle,
		Pending,
		Playing,
		Ending
	};
	struct FEvent
	{
		FString Name;
		std::vector<int> Rule;
		float Delay = 0.0f;
		bool bOnEnterAoi = true;
		bool bOnSatisfied = true;
		/** The entity events on the trigger entity: their types (0 activate, 1 deactivate, 2 trigger). */
		TArray<int32> TriggerTypes;
		bool bLast = false;
	};
	struct FSpawner
	{
		FString Cutscene;
		uint32 Crc = 0;
		FString TriggerEntity;
		bool bPlayOnLoad = false;
		FVector Position = FVector::ZeroVector;
		float AoiRadius = 100.0f;
		TArray<TSharedPtr<FJsonValue>> TriggerWorld;
		TArray<FEvent> Events;
		bool bInAoi = false;
	};
	struct FDelayed
	{
		int32 Spawner = INDEX_NONE;
		int32 Event = INDEX_NONE;
		float Left = 0.0f;
	};

	void Load();
	void Request(int32 Spawner, FSwcRepConnection* Connection);
	bool Start(UWorld* World, APawn* Player, int32 Spawner, FString& Error);
	void CommonEnd(const TCHAR* Reason, FSwcRepConnection* Connection);
	void DestroyPlayer();

	bool bLoaded = false;
	TArray<FSpawner> Spawners;
	TSharedPtr<FJsonObject> Scenes;
	TArray<FDelayed> Delayed;
	EState State = EState::Idle;
	int32 Current = INDEX_NONE;
	FString CurrentName;
	FString StopReason;
	uint32 CurrentCrc = 0;
	double PendingFor = 0.0;
	bool bPreview = false;
	bool bHidePlayer = false;
	uint32 SeenGrants = 0;
	uint32 SeenCancels = 0;
	uint32 SeenLockEnds = 0;
	uint32 SeenAchievements = 0;
	bool bWasLoading = false;
	bool bExitCamera = false;
	FString ExitCamera;
	TWeakObjectPtr<ASwcScenePlayer> Player;
	TArray<FString> UiLines;
};
