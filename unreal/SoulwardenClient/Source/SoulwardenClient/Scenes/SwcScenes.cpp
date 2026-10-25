#include "Scenes/SwcScenes.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformMisc.h"
#include "Interact/SwcConversation.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Scenes/SwcScenePlayer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "zlib.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcSceneFlow, Log, All);

namespace
{
constexpr double PendingLimit = 30.0;   // research/quest-scenes.md 1.4: the constructor 0x2EE31A3 sets 30 s
const TCHAR* const ExitCameraDefault = TEXT("IGC_CameraState_Generic");

FString ContentFolder()
{
	const FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	if (!Content.IsEmpty())
	{
		return Content;
	}
	const FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
	return FPaths::Combine(Work.IsEmpty() ? FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden-client")) : Work,
		TEXT("content"));
}

uint32 NameCrc(const FString& Name)
{
	const FTCHARToUTF8 Lower(*Name.ToLower());
	return static_cast<uint32>(crc32(0L, reinterpret_cast<const Bytef*>(Lower.Get()), static_cast<uInt>(Lower.Length())));
}

FString InfoField(const TSharedPtr<FJsonObject>& Info, const TCHAR* Field, const TCHAR* Default)
{
	if (!Info.IsValid() || !Info->HasField(Field))
	{
		return Default;
	}
	const TSharedPtr<FJsonValue> Value = Info->TryGetField(Field);
	if (Value->Type == EJson::Boolean)
	{
		return Value->AsBool() ? TEXT("1") : TEXT("0");
	}
	if (Value->Type == EJson::Number)
	{
		return FString::SanitizeFloat(Value->AsNumber());
	}
	return Value->AsString();
}
} // namespace

FSwcScenes::FSwcScenes() = default;

FSwcScenes::~FSwcScenes()
{
	DestroyPlayer();
}

void FSwcScenes::Load()
{
	bLoaded = true;
	FString Text;
	const FString Path = FPaths::Combine(ContentFolder(), TEXT("quests/scenes.json"));
	TSharedPtr<FJsonObject> Doc;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		UE_LOG(LogSwcSceneFlow, Warning, TEXT("scenes: no %s (tools/convert-scenes.sh write): no quest scenes"), *Path);
		return;
	}
	const TSharedPtr<FJsonObject>* ScenesObject = nullptr;
	if (Doc->TryGetObjectField(TEXT("scenes"), ScenesObject))
	{
		Scenes = *ScenesObject;
	}
	for (const TSharedPtr<FJsonValue>& Value : Doc->GetArrayField(TEXT("spawners")))
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		const TSharedPtr<FJsonObject> Trigger = Object->GetObjectField(TEXT("trigger"));
		const TSharedPtr<FJsonObject> RootObject = Object->GetObjectField(TEXT("root"));
		FSpawner& Spawner = Spawners.AddDefaulted_GetRef();
		Spawner.Cutscene = Trigger->GetStringField(TEXT("cutscene"));
		Spawner.Crc = NameCrc(Spawner.Cutscene);
		Spawner.TriggerEntity = Trigger->GetStringField(TEXT("entity"));
		Trigger->TryGetBoolField(TEXT("playOnLoad"), Spawner.bPlayOnLoad);
		Spawner.TriggerWorld = Trigger->GetArrayField(TEXT("world"));
		const TArray<TSharedPtr<FJsonValue>>& Position = Object->GetArrayField(TEXT("position"));
		Spawner.Position = FVector(Position[0]->AsNumber(), Position[1]->AsNumber(), Position[2]->AsNumber());
		double Aoi = 100.0;
		if (RootObject->TryGetNumberField(TEXT("aoiRadius"), Aoi) && Aoi > 0.0)
		{
			Spawner.AoiRadius = static_cast<float>(Aoi);
		}
		for (const TSharedPtr<FJsonValue>& EventValue : RootObject->GetArrayField(TEXT("events")))
		{
			const TSharedPtr<FJsonObject> EventObject = EventValue->AsObject();
			FEvent& Event = Spawner.Events.AddDefaulted_GetRef();
			Event.Name = EventObject->GetStringField(TEXT("name"));
			Event.Delay = static_cast<float>(EventObject->GetNumberField(TEXT("delay")));
			EventObject->TryGetBoolField(TEXT("onEnterAoi"), Event.bOnEnterAoi);
			EventObject->TryGetBoolField(TEXT("onSatisfied"), Event.bOnSatisfied);
			for (const TSharedPtr<FJsonValue>& Code : EventObject->GetArrayField(TEXT("rule")))
			{
				const FString Op = Code->Type == EJson::String ? Code->AsString() : FString();
				Event.Rule.push_back(Op == TEXT("!") ? swc::interact::kPresenceNot : Op == TEXT("&") ? swc::interact::kPresenceAnd
					: Op == TEXT("|") ? swc::interact::kPresenceOr : Op.IsEmpty() ? static_cast<int>(Code->AsNumber()) : swc::interact::kPresenceFalse);
			}
			for (const TSharedPtr<FJsonValue>& EntityEvent : EventObject->GetArrayField(TEXT("entityEvents")))
			{
				const TSharedPtr<FJsonObject> Entities = EntityEvent->AsObject();
				for (const TSharedPtr<FJsonValue>& Name : Entities->GetArrayField(TEXT("entities")))
				{
					if (Name->AsString() == Spawner.TriggerEntity)
					{
						Event.TriggerTypes.Add(static_cast<int32>(Entities->GetNumberField(TEXT("eventType"))));
					}
				}
			}
		}
	}
	UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: %d cutscene spawner(s) from %s"), Spawners.Num(), *Path);
}

bool FSwcScenes::HidesPlayer() const
{
	return State == EState::Playing && bHidePlayer;
}

bool FSwcScenes::GetView(FVector& Location, FRotator& Rotation, float& VerticalFov) const
{
	return State == EState::Playing && Player.IsValid() && Player->GetView(Location, Rotation, VerticalFov);
}

void FSwcScenes::ReleaseExitCamera()
{
	if (bExitCamera)
	{
		bExitCamera = false;
		UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: the input of the player releases the exit camera %s"), *ExitCamera);
	}
}

void FSwcScenes::DestroyPlayer()
{
	if (Player.IsValid())
	{
		Player->Destroy();
	}
	Player.Reset();
}

void FSwcScenes::Request(int32 Index, FSwcRepConnection* Connection)
{
	if (State != EState::Idle || Connection == nullptr)
	{
		UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: %s: no request (a scene is %s)"), *Spawners[Index].Cutscene,
			State == EState::Pending ? TEXT("pending") : TEXT("active"));
		return;
	}
	State = EState::Pending;
	Current = Index;
	CurrentName = Spawners[Index].Cutscene;
	CurrentCrc = Spawners[Index].Crc;
	PendingFor = 0.0;
	const uint32 Crc = CurrentCrc;
	Connection->Queue([Crc](swc::net::RepClient& Client) { Client.request_cutscene(Crc); });
	UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: %s (%08x): the request 6703"), *CurrentName, Crc);
}

bool FSwcScenes::Start(UWorld* World, APawn* Pawn, int32 Index, FString& Error)
{
	const TSharedPtr<FJsonObject>* Record = nullptr;
	if (!Scenes.IsValid() || !Scenes->TryGetObjectField(Spawners[Index].Cutscene.ToLower(), Record))
	{
		Error = TEXT("no record of the scene in quests/scenes.json");
		return false;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASwcScenePlayer* Scene = World->SpawnActor<ASwcScenePlayer>(ASwcScenePlayer::StaticClass(), FTransform::Identity, Parameters);
	if (Scene == nullptr || !Scene->Build(Spawners[Index].Cutscene, *Record, Spawners[Index].TriggerWorld, Pawn, Error))
	{
		if (Scene != nullptr)
		{
			Scene->Destroy();
		}
		return false;
	}
	Player = Scene;
	const TSharedPtr<FJsonObject>& Info = Scene->GetInfo();
	bool bHide = false;
	bHidePlayer = Info.IsValid() && Info->TryGetBoolField(TEXT("m_hidePlayerAvatar"), bHide) && bHide;
	ExitCamera = InfoField(Info, TEXT("m_cameraStateToSetOnExit"), ExitCameraDefault);
	// The order of the granted start (0x2EF13F0; research/quest-scenes.md 2.1): the lock, the start events, the screen, then 6706.
	Scene->Dispatch(true);
	UiLines.Add(FString::Printf(TEXT("start\t%s\tcanSkip=%s\thideUIOnTrigger=%s\tplayFadeEffect=%s\tenterBlendTime=%s\texitBlendTime=%s\treleaseCameraOverridesIn=%s"),
		*Spawners[Index].Cutscene, *InfoField(Info, TEXT("m_canSkip"), TEXT("1")), *InfoField(Info, TEXT("m_hideUIOnTrigger"), TEXT("1")),
		*InfoField(Info, TEXT("m_playFadeEffect"), TEXT("0")), *InfoField(Info, TEXT("m_enterBlendTime"), TEXT("0")),
		*InfoField(Info, TEXT("m_exitBlendTime"), TEXT("0")), *InfoField(Info, TEXT("releaseCameraOverridesIn"), TEXT("-1"))));
	return true;
}

void FSwcScenes::CommonEnd(const TCHAR* Reason, FSwcRepConnection* Connection)
{
	// The common end (0x2F08890): the stopped events, the camera overrides released (the exit state), OnCutsceneEnded, the cutscene mode
	// off, 6705 for a nonlocal scene; the hard lock stays until 6709 (the chain has ignoreAI and playerInvincibility).
	const bool bWasPlaying = State == EState::Playing;
	DestroyPlayer();
	if (bWasPlaying)
	{
		UiLines.Add(FString::Printf(TEXT("end\t%s\t%s"), *CurrentName, Reason));
		bExitCamera = !ExitCamera.IsEmpty();
	}
	if (bPreview)
	{
		State = EState::Idle;
		bPreview = false;
		UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: the preview of %s ends (%s)"), *CurrentName, Reason);
		return;
	}
	if (Connection != nullptr)
	{
		Connection->Queue([](swc::net::RepClient& Client) { Client.end_cutscene(); });
	}
	State = bWasPlaying && Connection != nullptr ? EState::Ending : EState::Idle;
	UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: %s ends (%s): 6705%s"), *CurrentName, Reason,
		State == EState::Ending ? TEXT(", the input stays locked until 6709") : TEXT(""));
}

void FSwcScenes::RequestStop(const FString& Reason)
{
	if (State == EState::Playing && Player.IsValid())
	{
		UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: %s: the stop (%s) at %.2f s"), *CurrentName, *Reason, Player->GetMainTime());
		StopReason = Reason;
		Player->Stop();
	}
}

bool FSwcScenes::Preview(UWorld* World, APawn* Pawn, const FString& Name, FString& Error)
{
	if (!bLoaded)
	{
		Load();
	}
	if (State != EState::Idle)
	{
		Error = TEXT("a scene is active");
		return false;
	}
	const int32 Index = Spawners.IndexOfByPredicate([&Name](const FSpawner& Spawner) { return Spawner.Cutscene.Equals(Name, ESearchCase::IgnoreCase); });
	if (Index == INDEX_NONE)
	{
		Error = TEXT("no placed spawner of this scene in quests/scenes.json");
		return false;
	}
	Current = Index;
	CurrentName = Spawners[Index].Cutscene;
	CurrentCrc = Spawners[Index].Crc;
	if (!Start(World, Pawn, Index, Error))
	{
		return false;
	}
	bPreview = true;
	State = EState::Playing;
	UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: the preview of %s starts (no handshake)"), *CurrentName);
	return true;
}

void FSwcScenes::Teardown(const TCHAR* Why)
{
	if (State != EState::Idle)
	{
		UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: %s ends locally (%s): no message"), *CurrentName, Why);
		if (State == EState::Playing)
		{
			UiLines.Add(FString::Printf(TEXT("end\t%s\t%s"), *CurrentName, Why));
		}
	}
	DestroyPlayer();
	State = EState::Idle;
	bPreview = false;
	Delayed.Reset();
}

void FSwcScenes::Tick(UWorld* World, const FSwcRepConnection::FSnapshot& Snapshot, FSwcRepConnection* Connection, APawn* Pawn, float DeltaSeconds)
{
	if (!bLoaded)
	{
		Load();
	}
	const swc::net::RepClient::OwnEntity& Own = Snapshot.Own;
	const bool bInGame = Snapshot.Phase == swc::net::RepClient::Phase::inGame && Own.hasMetadata && !Snapshot.bFinished;
	if (!bInGame)
	{
		if (State != EState::Idle && !bPreview)
		{
			Teardown(TEXT("the session ended"));
		}
		SeenGrants = Own.cutsceneGrants;
		SeenCancels = Own.cutsceneCancels;
		SeenLockEnds = Own.lockEnds;
		SeenTravel = Own.travelRevision;
		return;
	}
	// The answers of the server.
	if (Own.lockEnds != SeenLockEnds)
	{
		SeenLockEnds = Own.lockEnds;
		if (State == EState::Ending)
		{
			State = EState::Idle;
			UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: 6709: the input of %s is free"), *CurrentName);
		}
	}
	if (Own.cutsceneGrants != SeenGrants)
	{
		SeenGrants = Own.cutsceneGrants;
		if (State == EState::Pending && Own.lastCutsceneGrant == CurrentCrc)
		{
			FString Error;
			if (Start(World, Pawn, Current, Error))
			{
				State = EState::Playing;
				const uint32 Crc = CurrentCrc;
				Connection->Queue([Crc](swc::net::RepClient& Client) { Client.cutscene_started(Crc); });
				UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: %s: the grant 6707 after %.1f s; the scene starts, 6706"), *CurrentName, PendingFor);
			}
			else
			{
				// [I] A scene that the client cannot build: the start and the end at once, so the quest goes on.
				UE_LOG(LogSwcSceneFlow, Error, TEXT("scenes: %s: the grant 6707, but the scene cannot play (%s): 6706 and 6705 at once"), *CurrentName, *Error);
				const uint32 Crc = CurrentCrc;
				Connection->Queue([Crc](swc::net::RepClient& Client) { Client.cutscene_started(Crc); });
				Connection->Queue([](swc::net::RepClient& Client) { Client.end_cutscene(); });
				State = EState::Ending;
			}
		}
		else
		{
			// The grant handler (0x2EFE880): a grant of another name sends 6705 and starts nothing.
			UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: a grant 6707 of %08x with no pending request of it: 6705"), Own.lastCutsceneGrant);
			Connection->Queue([](swc::net::RepClient& Client) { Client.end_cutscene(); });
		}
	}
	if (Own.cutsceneCancels != SeenCancels)
	{
		SeenCancels = Own.cutsceneCancels;
		if (State == EState::Pending)
		{
			CommonEnd(TEXT("Failed: the cancel 6708"), Connection);
		}
		else if (State == EState::Playing)
		{
			RequestStop(TEXT("Failed: the cancel 6708"));
		}
	}
	// A travel during a scene (a new travel of the server, with or with no loading screen): the end with the live connection (6705, then
	// 6709) [I: no retail travel handler; the backend ends each input lock at a travel with 6709].
	if (Own.travelRevision != SeenTravel)
	{
		SeenTravel = Own.travelRevision;
		if (State == EState::Pending || State == EState::Playing)
		{
			CommonEnd(TEXT("Travel"), Connection);
		}
	}
	// The guard of the lock (AST-287): the retail client waits for the answer 6709 with no limit; the client frees the input 5 s after
	// the 6705 when no answer comes, so that no path leaves the player stuck.
	EndingFor = State == EState::Ending ? EndingFor + DeltaSeconds : 0.0;
	if (State == EState::Ending && EndingFor >= swc::interact::TalkGuard::kAnswerWait)
	{
		UE_LOG(LogSwcSceneFlow, Warning, TEXT("scenes: %s: no answer 6709 %.1f s after the 6705: the client frees the input"), *CurrentName, EndingFor);
		State = EState::Idle;
		EndingFor = 0.0;
	}
	// The wait for the grant: 30 s, then the failure (0x2EFEDD3): the normal stop interface, 6705, no second 6703.
	if (State == EState::Pending)
	{
		PendingFor += DeltaSeconds;
		if (PendingFor >= PendingLimit)
		{
			CommonEnd(TEXT("Failed: no grant in 30 s"), Connection);
		}
	}
	// The play.
	if (State == EState::Playing && Player.IsValid())
	{
		Player->Advance(DeltaSeconds);
		for (const FString& Line : Player->TakeEvents())
		{
			if (Line == TEXT("end"))
			{
				CommonEnd(StopReason.IsEmpty() ? TEXT("Completed") : *StopReason, Connection);
				StopReason.Reset();
				break;
			}
			UiLines.Add(Line);
		}
	}
	// The activation by the bits of the character (the AchievementEvents of the spawners).
	if (Own.achievementRevision == 0 || Pawn == nullptr || Own.loadingScreen)
	{
		return;
	}
	const FVector Feet(Own.position.x, Own.position.y, Own.position.z);
	const bool bBitsChanged = Own.achievementRevision != SeenAchievements;
	SeenAchievements = Own.achievementRevision;
	for (int32 Index = 0; Index < Spawners.Num(); ++Index)
	{
		FSpawner& Spawner = Spawners[Index];
		const bool bInAoi = FVector::Dist(Feet, Spawner.Position) <= Spawner.AoiRadius;
		const bool bEntered = bInAoi && !Spawner.bInAoi;
		Spawner.bInAoi = bInAoi;
		if (!bInAoi)
		{
			continue;
		}
		for (int32 EventIndex = 0; EventIndex < Spawner.Events.Num(); ++EventIndex)
		{
			FEvent& Event = Spawner.Events[EventIndex];
			if (!bEntered && !bBitsChanged)
			{
				continue;
			}
			const bool bValue = swc::interact::presence_visible(true, Event.Rule, Own.achievements, false);
			const bool bFire = bValue && ((bEntered && Event.bOnEnterAoi) || (!Event.bLast && Event.bOnSatisfied));
			Event.bLast = bValue;
			if (bFire && (Event.TriggerTypes.Contains(2) || (Event.TriggerTypes.Contains(0) && Spawner.bPlayOnLoad)))
			{
				UE_LOG(LogSwcSceneFlow, Log, TEXT("scenes: %s: the event \"%s\" (the condition is true%s), the request in %.2f s"), *Spawner.Cutscene,
					*Event.Name, bEntered ? TEXT(" at the entry of the AOI") : TEXT(""), Event.Delay);
				Delayed.Add({Index, EventIndex, Event.Delay});
			}
		}
	}
	for (int32 Index = Delayed.Num() - 1; Index >= 0; --Index)
	{
		Delayed[Index].Left -= DeltaSeconds;
		if (Delayed[Index].Left <= 0.0f)
		{
			const int32 Spawner = Delayed[Index].Spawner;
			Delayed.RemoveAt(Index);
			Request(Spawner, Connection);
		}
	}
}
