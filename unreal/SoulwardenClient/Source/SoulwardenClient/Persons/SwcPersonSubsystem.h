#pragma once

#include "CoreMinimal.h"

#include <vector>
#include "Subsystems/WorldSubsystem.h"

#include "SwcPersonSubsystem.generated.h"

class FJsonObject;

class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;

DECLARE_LOG_CATEGORY_EXTERN(LogSwcPersons, Log, All);

/** The prefix of the Talk key of a person with no npc object of the interaction data (the key of persons.json follows). */
inline const TCHAR* const PersonTalkPrefix = TEXT("person:");

/** One person of the level data (SWC_CONTENT/persons/persons.json; stage P4.8, docs/PERSONS.md). */
struct FSwcPerson
{
	FString Key;
	FString Name;
	FString Title;
	FString NpcId;
	/** The place of the feet (Unreal cm), the facing (Unreal yaw, degrees), and the scale. */
	FVector Place = FVector::ZeroVector;
	double Yaw = 0.0;
	float Scale = 1.0f;
	/** The objects of the import (imported.json): the body, the clips of the timelines, the items on bones. */
	FString BodyPath;
	/** The first clip of the PlayAnim layer of a timeline: its object path, its speed, its blend-in time, its loop flag. */
	struct FClip
	{
		FString Path;
		float Speed = 1.0f;
		float BlendIn = 0.0f;
		bool bLoop = true;
	};
	/** The idle (the start timeline Play_idleLoop), and the talk timelines of the variation row: GreetTimeline, InteractTimeline,
	 *  ByeTimeline. */
	FClip Idle;
	FClip Greet;
	FClip Interact;
	FClip Bye;
	struct FItem
	{
		FString MeshPath;
		FName Bone;
		FTransform Offset;
	};
	TArray<FItem> Items;
	/** The presence (swc::interact::presence_visible): the swap expression in postfix order (persons.json "presenceRule"), the
	 *  ShowOnAchievementLocked mark, and the value for the Achievement state of the character. */
	bool bHasSwap = false;
	bool bShowOnLocked = false;
	std::vector<int> PresenceRule;
	bool bVisible = true;
	/** The key of the npc object of the interaction data (a Talk with a reply 1), or empty. */
	FString InteractionKey;
	bool bTalk = false;
	/** The conversation (persons.json "conversation": the state, its line, and the camera preset of the line; docs/PERSONS.md): the
	 *  speaker and the text, the camera place and its look-at point (Unreal world space), the vertical FOV (degrees), and the
	 *  HidePlayerAvatar mark of the preset. */
	struct FLine
	{
		bool bValid = false;
		/** The localization key of the line (persons.json "line"; the dialogue of the conversation screen). */
		FString LineKey;
		/** The lines of the conversation (persons.json "lines": the first line and its chain of dialogue-next): "KEY\tSPEAKER" each. */
		TArray<FString> Lines;
		FString Speaker;
		FString Text;
		/** True when the line has a camera point (an origin of three values). A line with no preset and no point (27 persons, for example
		 *  12_SerPayneBennett) keeps the camera of the play: the retail camera UI_Conversation of the script is not done (docs/PERSONS.md). */
		bool bCamera = false;
		FVector Camera = FVector::ZeroVector;
		FVector LookAt = FVector::ZeroVector;
		float Fov = 0.0f;
		bool bHidePlayer = false;
		/** The dummy of the player of a two-shot preset: its feet (Unreal world space), its facing (Unreal yaw), its weapons. */
		bool bDummy = false;
		FVector DummyFeet = FVector::ZeroVector;
		double DummyYaw = 0.0;
		bool bDummyArmed = false;
	};
	FLine Line;
	/** The frame of the camera points of the Talk lines: the Interact entity (persons.json "talkFrame"), else the body; engine meters
	 *  (x east, y north, z up) and the engine yaw (degrees). */
	FVector TalkFrame = FVector::ZeroVector;
	double TalkFrameYaw = 0.0;
	/** The spawned body, or null. */
	TWeakObjectPtr<AActor> Actor;
	/** The Talk of the body (SetTalking): none, the greet, the interact loop, the bye (then the idle). */
	enum class ETalk : uint8
	{
		None,
		Greet,
		Interact,
		Bye
	};
	ETalk Talk = ETalk::None;
};

/**
 * The persons of the settlements (stage P4.8, docs/PERSONS.md): the client makes them itself from the level data, as the retail
 * client does (the backend sends no record for them). The bodies (the visible skins of their .cdf on the player skeleton), the
 * items on their bones, and the idle clip of their start timeline come from the player import (tools/convert-persons.sh,
 * tools/import-player.sh). The persons near the view spawn; the far ones go away.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcPersonSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual void Deinitialize() override;
	virtual TStatId GetStatId() const override;

	const TArray<FSwcPerson>& GetPersons() const { return Persons; }
	/** The revision of the presence of the persons for the local character (0: not known yet): the NPC states of the UI (AST-239). */
	uint32 GetPresenceRevision() const { return PresenceRevision; }
	/** @return The person of an npc object key of the interaction data (or of PersonTalkPrefix + a key of persons.json), or null. */
	const FSwcPerson* FindByInteraction(const FString& Key) const;
	/**
	 * @return The nearest shown person with the option OpenConversation and no npc object of the interaction data (a person of a
	 * client spawner, a person of a capital child) within a distance of a point (Unreal cm), or null.
	 */
	const FSwcPerson* FindTalkWithNoData(const FVector& Point, double Distance) const;
	/** @return The person of a key of persons.json, or null. */
	const FSwcPerson* FindByKey(const FString& Key) const;
	/** @return The nearest person within a distance of a point (Unreal cm), or null. */
	const FSwcPerson* FindNear(const FVector& Point, double Distance) const;
	/**
	 * The camera of a line of a conversation state or topic (SWC_CONTENT/persons/line-cameras.json, ut567; docs/PERSONS.md "The Talk
	 * camera"): the camera place, the look-at point, the FOV, the hide, and the dummy in the frame of the person (the same rule as the
	 * first line of persons.json). @return False when the line has no camera (the camera of the screen stays).
	 */
	bool GetLineCamera(const FSwcPerson& Person, const FString& LineKey, FSwcPerson::FLine& Out) const;
	/**
	 * The Talk of a person (the npc object key of the interaction data): the start plays the clip of GreetTimeline, then the loop of
	 * InteractTimeline; the end plays the clip of ByeTimeline, then the idle (docs/PERSONS.md "The Talk").
	 */
	void SetTalking(const FString& InteractionKey, bool bTalking);

private:
	void Load();
	void Spawn(FSwcPerson& Person);
	/** Plays a clip of a person on its body. @return False with no body or no clip. */
	bool Play(FSwcPerson& Person, const FSwcPerson::FClip& Clip, const TCHAR* What);
	/** The Talk sequence: the greet goes to the interact loop, the bye to the idle, at the end of the clip. */
	void UpdateTalk(FSwcPerson& Person);
	bool GetViewLocation(FVector& Out) const;

	/** The presence of each person for the Achievement state of the local character (at the entry and at each change of the state).
	 *  @return False before the first state. */
	bool UpdatePresence();
	bool bLoadTried = false;
	bool bPresenceKnown = false;
	uint32 SeenAchievementRevision = 0;
	/** C-13 step 1b: the owner is in a game mode (OwnEntity::gameModes): no person of the level data shows. */
	bool bInGameMode = false;
	uint32 PresenceRevision = 0;
	TArray<FSwcPerson> Persons;
	/** The cameras of the lines of the conversation states and topics (line-cameras.json "cameras": the lower case key -> the camera). */
	TMap<FString, TSharedPtr<FJsonObject>> LineCameras;
	double NextUpdate = 0.0;
	int32 Spawned = 0;
};
