#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Scenes/SwcSceneCurve.h"

#include "SwcScenePlayer.generated.h"

class FJsonObject;
class FJsonValue;
class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * The play of one in-game scene (stage Q5, AST-240; docs/QUESTS.md "The scenes, the cinematics, and the whispers";
 * research/quest-scenes.md 2 and 3, research/scene-curves.md): the slice of SWC_CONTENT/quests/scenes/NAME.json at the trigger of its
 * spawner, its timelines, and their clips. FSwcScenes owns the handshake; this actor only plays.
 *
 * - The place: the slice root at the world matrix of the trigger (the spawner placement and its hierarchy); each entity at its slice
 *   value m_worldTM (research/quest-scenes.md 2.3). The skinned actors: the bodies of the player pipeline (PersonBodies), the
 *   characters (other skeletons), and the dummy of the player: a copy of the body parts of the local character (playercharacterupdate.luac
 *   copies the look and the equipment; dummyIsArmed false: no weapon) [I: the copy of the parts of this client], else its own body.
 * - The timelines: a TimelineEntry starts with its action (0 None, 1 Play, 2 Pause, 3 Reset and Play, 4 Reset and Pause, 5 Reset to End
 *   and Pause, 6 Simulate to End and Pause); the start and stop events of CutsceneInfo trigger and untrigger them. A layer goes to the
 *   slice entity of its LayerEntityAssignment; a layer with Ignore does not run. A step is at most 0.05 s; a clip begins when the time
 *   reaches its start and ends at its end (one step can cross a whole clip: begin and end).
 * - The clips: animation (the clip of the alias at its time and speed), ShowMesh and HideMesh, ActivateEntity and DeactivateEntity (the
 *   deactivation of the slice root ends the scene), TriggerEntity and UnTriggerEntity (the entry of the target; an entity with the
 *   cinematic component starts its video), CommandClip (PlayWhisperSubtitle_@KEY with its duration: an event for the subtitle), and the
 *   transform clip (position and Euler curves; order 0 is Ry Rz Rx in the slice frame [D: it gives the matrix of the camera start
 *   entity]; the camera looks along -Y of that frame [I: the C1 framing]). The FOV is the CameraComponent value (the beach FOV layer has
 *   Ignore) as a vertical FOV [U: the projection axis].
 * - Not played (declared limits): the lights, the particle effects, the material clips, the audio and the lip sync (no sound).
 */
UCLASS()
class ASwcScenePlayer : public AActor
{
	GENERATED_BODY()

public:
	ASwcScenePlayer();

	/** Builds the scene. Record: the record of the scene in quests/scenes.json; RootRows: the world matrix of the slice root (rows of
	 *  4 numbers, m). @return False with the reason in Error. */
	bool Build(const FString& Name, const TSharedPtr<FJsonObject>& Record, const TArray<TSharedPtr<FJsonValue>>& RootRows, APawn* Player,
		FString& Error);
	/** The start (bStart) or stop events of CutsceneInfo (type 0 trigger, 1 untrigger, 2 activate, 3 deactivate). */
	void Dispatch(bool bStart);
	void Advance(float DeltaSeconds);
	/** The stop of a skip or a cancel: the stop events (the main entry simulates its end), then the end if nothing ended it. */
	void Stop();
	bool HasEnded() const { return bEnded; }
	bool GetView(FVector& Location, FRotator& Rotation, float& VerticalFov) const;
	/** The events since the last call: "subtitle\tKEY\tSECONDS", "cinematic\tID", "end". */
	TArray<FString> TakeEvents();
	const TSharedPtr<FJsonObject>& GetInfo() const { return Info; }
	double GetMainTime() const;

	struct FMatrix4
	{
		double M[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
		FMatrix4 operator*(const FMatrix4& Other) const;
		static FMatrix4 FromRows(const TArray<TSharedPtr<FJsonValue>>& Rows);
		/** The matrix of the Euler angles (degrees) of order 0 (X, Up, Forward: Ry Rz Rx) at a position. */
		static FMatrix4 FromEuler(const FVector& Position, const FVector& Degrees);
		FVector Point() const { return FVector(M[0][3], M[1][3], M[2][3]); }
		FVector Axis(int32 Column) const { return FVector(M[0][Column], M[1][Column], M[2][Column]); }
		FTransform ToUnreal() const;
	};

private:
	enum class EKind : uint8
	{
		Other,
		Anim,
		Show,
		Hide,
		Activate,
		Deactivate,
		Trigger,
		Untrigger,
		Command,
		Transform
	};
	struct FClip
	{
		EKind Kind = EKind::Other;
		float Start = 0.0f;
		float End = 0.0f;
		FString Text;
		int32 Command = 0;
		FString Alias;
		TWeakObjectPtr<UAnimSequence> Sequence;
		float Speed = 1.0f;
		float From = 0.0f;
		bool bLoop = false;
		int32 AnimLayer = 0;
		bool bChildren = true;
		int32 EulerOrder = 0;
		swc::scene::Curve Position[3];
		swc::scene::Curve Rotation[3];
		bool bBegun = false;
		bool bEnded = false;
	};
	struct FLayer
	{
		FString Name;
		int32 Entity = INDEX_NONE;
		bool bIgnore = false;
		TArray<FClip> Clips;
	};
	struct FTimeline
	{
		int32 Owner = INDEX_NONE;
		FString Asset;
		int32 OnTrigger = 3;
		int32 OnUntrigger = 4;
		TArray<FLayer> Layers;
		float Duration = 0.0f;
		float Time = 0.0f;
		bool bPlaying = false;
	};
	struct FEntity
	{
		FString Id;
		FString Name;
		int32 Parent = INDEX_NONE;
		TArray<int32> Children;
		FMatrix4 Slice;
		bool bHasOverride = false;
		FMatrix4 Override;
		bool bActive = true;
		bool bVisible = true;
		bool bCamera = false;
		float Fov = 40.0f;
		FString Cinematic;
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		/** A static prop of the slice (a MeshComponent with a .cgf: the static mesh of the content import). */
		TWeakObjectPtr<class UStaticMeshComponent> Static;
		TArray<TWeakObjectPtr<USkeletalMeshComponent>> Followers;
		FString AttachTarget;
		FString AttachBone;
		float AttachScale = 1.0f;
	};

	int32 Find(const FString& Id) const;
	void Action(int32 Timeline, int32 Code);
	void Run(int32 Timeline, float From, float To, bool bSimulate);
	void Begin(FLayer& Layer, FClip& Clip, bool bSimulate);
	void Sample(const FLayer& Layer, const FClip& Clip, float Time);
	void SetVisible(int32 Entity, bool bVisible, bool bChildren);
	void SetActive(int32 Entity, bool bActive);
	void TriggerEntity(int32 Entity, bool bTrigger);
	void Place(int32 Entity);
	USkeletalMeshComponent* AddMesh(USkeletalMesh* Mesh, const FTransform& World);
	void AddDummy(int32 Entity, APawn* Player, USkeletalMesh* Fallback);
	bool BuildActors(const TSharedPtr<FJsonObject>& Record, APawn* Player, FString& Error);
	void BuildTimelines();

	FString SceneName;
	FMatrix4 Root;
	int32 RootEntity = INDEX_NONE;
	int32 CameraEntity = INDEX_NONE;
	TArray<FEntity> Entities;
	TMap<FString, int32> ById;
	TArray<FTimeline> Timelines;
	TSharedPtr<FJsonObject> Info;
	TSharedPtr<FJsonObject> Doc;
	TArray<FString> Events;
	TMap<FString, TWeakObjectPtr<UAnimSequence>> Sequences;   // "ENTITY|alias" -> the clip
	bool bEnded = false;
	bool bStopped = false;
	float StoppedFor = 0.0f;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> Keep;
};
