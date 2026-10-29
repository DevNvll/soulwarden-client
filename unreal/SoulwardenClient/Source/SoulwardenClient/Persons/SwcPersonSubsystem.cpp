#include "Persons/SwcPersonSubsystem.h"

#include "Persons/SwcPersonAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Interact/SwcConversation.h"
#include "SwcBackendSubsystem.h"
#include "Engine/GameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/SwcLoadingGate.h"

DEFINE_LOG_CATEGORY(LogSwcPersons);

namespace
{
/** The persons spawn in this distance of the view and go away beyond the second one (cm; the spawn radius of the person slices
 *  is 90 m to 190 m: research p48-persons, aoi). A few spawn in one frame. */
constexpr double SpawnDistance = 15000.0;
/** The height of the retail Talk camera above the point of its preset (m; four retail Talk cameras: 0.052 to 0.065). */
constexpr double TalkCameraLift = 0.065;
constexpr double RemoveDistance = 20000.0;
constexpr int32 SpawnsPerUpdate = 4;
/** The imported player mesh faces 90 degrees to the left of the actor (the rule of the player character, stage 1.6). */
constexpr double MeshYaw = 90.0;

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

TSharedPtr<FJsonObject> ReadJson(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Out;
	if (FFileHelper::LoadFileToString(Text, *Path))
	{
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
	}
	return Out;
}

/** @return The first object path of a class of an asset of imported.json, or the one whose name ends with a suffix. */
FString Imported(const TSharedPtr<FJsonObject>& Assets, const FString& Key, const TCHAR* Class, const FString& Suffix = FString())
{
	const TSharedPtr<FJsonObject>* Entry = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Objects = nullptr;
	if (!Assets.IsValid() || !Assets->TryGetObjectField(Key, Entry) || !(*Entry)->TryGetArrayField(Class, Objects) || Objects->IsEmpty())
	{
		return FString();
	}
	if (Suffix.IsEmpty())
	{
		return (*Objects)[0]->AsString();
	}
	for (const TSharedPtr<FJsonValue>& Value : *Objects)
	{
		if (FPaths::GetBaseFilename(Value->AsString()).EndsWith(Suffix, ESearchCase::IgnoreCase))
		{
			return Value->AsString();
		}
	}
	return FString();
}

swc::interact::ScenePoint ReadPoint(const TArray<TSharedPtr<FJsonValue>>& Values)
{
	return {Values.Num() > 0 ? Values[0]->AsNumber() : 0.0, Values.Num() > 1 ? Values[1]->AsNumber() : 0.0, Values.Num() > 2 ? Values[2]->AsNumber() : 0.0};
}

/**
 * The camera of a line in the frame of a person (persons.json "conversation.camera", line-cameras.json): the points of the preset in that
 * frame (swc::interact::scene_point). A preset with no look-at point (NSR_INN_MED_SNGL) looks at the person at the height of the camera
 * (a client rule). The camera stands 0.065 m above the preset point; the look-at point does not move.
 */
void FillLineCamera(const FJsonObject& Camera, const swc::interact::ScenePoint& Feet, double EngineYaw, FSwcPerson::FLine& Line)
{
	const TArray<TSharedPtr<FJsonValue>>& OriginValues = Camera.GetArrayField(TEXT("origin"));
	const swc::interact::ScenePoint Origin = ReadPoint(OriginValues);
	const TArray<TSharedPtr<FJsonValue>>& LookAtValues = Camera.GetArrayField(TEXT("lookAt"));
	const swc::interact::ScenePoint LookAt = LookAtValues.Num() >= 3 ? ReadPoint(LookAtValues) : swc::interact::ScenePoint{0.0, 0.0, Origin.z};
	const auto ToUnreal = [](const swc::interact::ScenePoint& Point) { return FVector(Point.x * 100.0, -Point.y * 100.0, Point.z * 100.0); };
	Line.bCamera = OriginValues.Num() >= 3;
	Line.Camera = ToUnreal(swc::interact::scene_point(Feet, EngineYaw, Origin)) + FVector(0.0, 0.0, TalkCameraLift * 100.0);
	Line.LookAt = ToUnreal(swc::interact::scene_point(Feet, EngineYaw, LookAt));
	double Fov = 0.0;
	Line.Fov = Camera.TryGetNumberField(TEXT("fov"), Fov) ? static_cast<float>(Fov) : 0.0f;
	Line.bHidePlayer = Camera.GetBoolField(TEXT("hidePlayer"));
	// The dummy: its place in the frame of the person, its facing the yaw of the person turned by the z rotation.
	const TSharedPtr<FJsonObject>* Dummy = nullptr;
	Line.bDummy = false;
	if (Camera.TryGetObjectField(TEXT("dummy"), Dummy))
	{
		const TArray<TSharedPtr<FJsonValue>>& Turn = (*Dummy)->GetArrayField(TEXT("rotation"));
		const double DummyYaw = FMath::DegreesToRadians(EngineYaw + (Turn.Num() > 2 ? Turn[2]->AsNumber() : 0.0));
		Line.bDummy = true;
		Line.DummyFeet = ToUnreal(swc::interact::scene_point(Feet, EngineYaw, ReadPoint((*Dummy)->GetArrayField(TEXT("position")))));
		Line.DummyYaw = FVector(-FMath::Sin(DummyYaw), -FMath::Cos(DummyYaw), 0.0).Rotation().Yaw;
		Line.bDummyArmed = (*Dummy)->GetBoolField(TEXT("armed"));
	}
}

/** @return The first .caf clip of the PlayAnim layer of a timeline of persons.json (the clip pack has the file stem). */
FSwcPerson::FClip TimelineClip(const TSharedPtr<FJsonObject>& Timelines, const TSharedPtr<FJsonObject>& Assets, const FString& ClipsKey, const FString& Path)
{
	FSwcPerson::FClip Out;
	const TSharedPtr<FJsonObject>* Timeline = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Layers = nullptr;
	if (Path.IsEmpty() || !Timelines->TryGetObjectField(Path, Timeline) || !(*Timeline)->TryGetArrayField(TEXT("layers"), Layers))
	{
		return Out;
	}
	for (const TSharedPtr<FJsonValue>& Layer : *Layers)
	{
		for (const TSharedPtr<FJsonValue>& Clip : Layer->AsObject()->GetArrayField(TEXT("clips")))
		{
			const TSharedPtr<FJsonObject> Info = Clip->AsObject();
			const FString File = Info->GetStringField(TEXT("file"));
			if (Out.Path.IsEmpty() && File.EndsWith(TEXT(".caf")))
			{
				Out.Path = Imported(Assets, ClipsKey, TEXT("AnimSequence"), FPaths::GetBaseFilename(File));
				Out.Speed = static_cast<float>(Info->GetNumberField(TEXT("speed")));
				Out.BlendIn = static_cast<float>(Info->GetNumberField(TEXT("blendIn")));
				Out.bLoop = Info->GetBoolField(TEXT("loop"));
			}
		}
	}
	return Out;
}

/** A rotation of the game (w, x, y, z) and a position (m) in Unreal: the space map of stage 1.1 (the rule of the player gear). */
FTransform UnrealOffset(const TArray<TSharedPtr<FJsonValue>>& Position, const TArray<TSharedPtr<FJsonValue>>& Rotation)
{
	const auto At = [](const TArray<TSharedPtr<FJsonValue>>& Values, int32 Index) { return Values.IsValidIndex(Index) ? Values[Index]->AsNumber() : 0.0; };
	const FQuat Game(At(Rotation, 1), At(Rotation, 2), At(Rotation, 3), Rotation.IsEmpty() ? 1.0 : At(Rotation, 0));
	return FTransform(FQuat(-Game.X, Game.Y, -Game.Z, Game.W).GetNormalized(), FVector(At(Position, 0) * 100.0, -At(Position, 1) * 100.0, At(Position, 2) * 100.0));
}
} // namespace

bool USwcPersonSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcPersonSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcPersonSubsystem, STATGROUP_Tickables);
}

void USwcPersonSubsystem::Load()
{
	const FString Folder = ContentFolder();
	const TSharedPtr<FJsonObject> Document = ReadJson(FPaths::Combine(Folder, TEXT("persons/persons.json")));
	const TSharedPtr<FJsonObject> ImportedDoc = ReadJson(FPaths::Combine(Folder, TEXT("player/imported.json")));
	if (!Document.IsValid() || !ImportedDoc.IsValid())
	{
		UE_LOG(LogSwcPersons, Warning, TEXT("persons: no persons.json or imported.json in %s (tools/convert-persons.sh, tools/import-player.sh): no persons"), *Folder);
		return;
	}
	const TSharedPtr<FJsonObject> Assets = ImportedDoc->GetObjectField(TEXT("assets"));
	const TSharedPtr<FJsonObject> Timelines = Document->GetObjectField(TEXT("timelines"));
	// The cameras of the lines of the conversation states and topics (tools/convert-persons.sh line-cameras; ut567). With no file, only
	// the first line of a person has a camera.
	LineCameras.Reset();
	const TSharedPtr<FJsonObject> LineCameraDoc = ReadJson(FPaths::Combine(Folder, TEXT("persons/line-cameras.json")));
	const TSharedPtr<FJsonObject>* LineCameraMap = nullptr;
	if (LineCameraDoc.IsValid() && LineCameraDoc->TryGetObjectField(TEXT("cameras"), LineCameraMap))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*LineCameraMap)->Values)
		{
			if (Entry.Value.IsValid() && Entry.Value->Type == EJson::Object)
			{
				LineCameras.Add(Entry.Key.ToLower(), Entry.Value->AsObject());
			}
		}
	}
	UE_LOG(LogSwcPersons, Log, TEXT("persons: %d line cameras of the conversation states (persons/line-cameras.json)"), LineCameras.Num());
	const FString ClipsKey = TEXT("clips|male|persons");
	int32 NoBody = 0;
	int32 NoIdle = 0;
	for (const TSharedPtr<FJsonValue>& Value : Document->GetArrayField(TEXT("persons")))
	{
		const TSharedPtr<FJsonObject> Info = Value->AsObject();
		FSwcPerson Person;
		Person.Key = Info->GetStringField(TEXT("key"));
		Person.Name = Info->GetStringField(TEXT("name"));
		Person.Title = Info->GetStringField(TEXT("title"));
		Person.NpcId = Info->GetStringField(TEXT("npcId"));
		const TArray<TSharedPtr<FJsonValue>>& Position = Info->GetArrayField(TEXT("position"));
		Person.Place = FVector(Position[0]->AsNumber() * 100.0, -Position[1]->AsNumber() * 100.0, Position[2]->AsNumber() * 100.0);
		// The yaw of the data: counterclockwise from north (+y) in engine space; the facing in Unreal space (y mirrored).
		const double Yaw = FMath::DegreesToRadians(Info->GetNumberField(TEXT("yaw")));
		Person.Yaw = FVector(-FMath::Sin(Yaw), -FMath::Cos(Yaw), 0.0).Rotation().Yaw;
		Person.Scale = static_cast<float>(Info->GetNumberField(TEXT("scale")));
		const FString Cdf = Info->GetStringField(TEXT("cdf"));
		Person.BodyPath = Imported(Assets, TEXT("person|") + Cdf, TEXT("SkeletalMesh"));
		NoBody += Person.BodyPath.IsEmpty() ? 1 : 0;
		// The idle: the first clip of the layer of the start timeline Play_idleLoop (it loops: RunOnActivate). The talk timelines of
		// the variation row.
		for (const TSharedPtr<FJsonValue>& Start : Info->GetArrayField(TEXT("start")))
		{
			const TSharedPtr<FJsonObject> StartInfo = Start->AsObject();
			if (StartInfo->GetStringField(TEXT("entity")) == TEXT("Play_idleLoop") && Person.Idle.Path.IsEmpty())
			{
				Person.Idle = TimelineClip(Timelines, Assets, ClipsKey, StartInfo->GetStringField(TEXT("timeline")));
				Person.Idle.bLoop = true;
			}
		}
		const TSharedPtr<FJsonObject>* Talk = nullptr;
		if (Info->TryGetObjectField(TEXT("talkTimelines"), Talk))
		{
			FString Path;
			Person.Greet = TimelineClip(Timelines, Assets, ClipsKey, (*Talk)->TryGetStringField(TEXT("GreetTimeline"), Path) ? Path : FString());
			Person.Interact = TimelineClip(Timelines, Assets, ClipsKey, (*Talk)->TryGetStringField(TEXT("InteractTimeline"), Path) ? Path : FString());
			Person.Bye = TimelineClip(Timelines, Assets, ClipsKey, (*Talk)->TryGetStringField(TEXT("ByeTimeline"), Path) ? Path : FString());
		}
		NoIdle += Person.Idle.Path.IsEmpty() ? 1 : 0;
		// The items: the CA_BONE attachments of the .cdf, and the row AttachedMesh in the right hand.
		for (const TSharedPtr<FJsonValue>& Item : Info->GetObjectField(TEXT("look"))->GetArrayField(TEXT("items")))
		{
			const TSharedPtr<FJsonObject> ItemInfo = Item->AsObject();
			FSwcPerson::FItem Entry;
			Entry.MeshPath = Imported(Assets, TEXT("personitem|") + ItemInfo->GetStringField(TEXT("mesh")), TEXT("StaticMesh"));
			Entry.Bone = FName(*ItemInfo->GetStringField(TEXT("bone")));
			Entry.Offset = UnrealOffset(ItemInfo->GetArrayField(TEXT("position")), ItemInfo->GetArrayField(TEXT("rotationWXYZ")));
			if (!Entry.MeshPath.IsEmpty())
			{
				Person.Items.Add(Entry);
			}
		}
		const FString Attached = Info->GetStringField(TEXT("attachedMesh"));
		if (!Attached.IsEmpty())
		{
			FSwcPerson::FItem Entry;
			Entry.MeshPath = Imported(Assets, TEXT("personitem|") + Attached, TEXT("StaticMesh"));
			Entry.Bone = TEXT("right_hand_attach");
			if (!Entry.MeshPath.IsEmpty())
			{
				Person.Items.Add(Entry);
			}
		}
		Person.bHasSwap = !Info->GetStringField(TEXT("swap")).IsEmpty();
		Person.bShowOnLocked = Info->GetBoolField(TEXT("showOnLocked"));
		const TArray<TSharedPtr<FJsonValue>>* Rule = nullptr;
		if (Info->TryGetArrayField(TEXT("presenceRule"), Rule))
		{
			for (const TSharedPtr<FJsonValue>& Code : *Rule)
			{
				const FString Op = Code->Type == EJson::String ? Code->AsString() : FString();
				Person.PresenceRule.push_back(Op == TEXT("!") ? swc::interact::kPresenceNot : Op == TEXT("&") ? swc::interact::kPresenceAnd
					: Op == TEXT("|") ? swc::interact::kPresenceOr : Op.IsEmpty() ? static_cast<int>(Code->AsNumber()) : swc::interact::kPresenceFalse);
			}
		}
		Person.bVisible = swc::interact::presence_visible(Person.bHasSwap, Person.PresenceRule, {}, Person.bShowOnLocked);
		FString Interaction;
		Person.InteractionKey = Info->TryGetStringField(TEXT("interactionData"), Interaction) ? Interaction : FString();
		const TSharedPtr<FJsonObject>* Interact = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
		if (Info->TryGetObjectField(TEXT("interact"), Interact) && (*Interact)->TryGetArrayField(TEXT("options"), Options))
		{
			Person.bTalk = Options->ContainsByPredicate([](const TSharedPtr<FJsonValue>& Option) { return Option->AsString() == TEXT("OpenConversation"); });
		}
		// The frame of the camera points of the Talk lines: the Interact entity of the person (persons.json "talkFrame"; the body can stand
		// 0.6 m from it or turned 180 degrees from it), else the body. Retail Talk cameras of MVP-H (Purcell, Ozian Atreus, Sai Kumal; the
		// commander of Oxboro from P4.8): the place within 0.004 m, the heading within 0.25 degrees, the pitch within 0.3 degrees; Ser Payne
		// Bennett (a line of a state, line-cameras.json; ut567): 0.00 m and 0.0 degrees of heading (docs/PERSONS.md "The Talk camera").
		swc::interact::ScenePoint Feet = ReadPoint(Position);
		double EngineYaw = Info->GetNumberField(TEXT("yaw"));
		const TSharedPtr<FJsonObject>* Frame = nullptr;
		if (Info->TryGetObjectField(TEXT("talkFrame"), Frame))
		{
			Feet = ReadPoint((*Frame)->GetArrayField(TEXT("position")));
			EngineYaw = (*Frame)->GetNumberField(TEXT("yaw"));
		}
		Person.TalkFrame = FVector(Feet.x, Feet.y, Feet.z);
		Person.TalkFrameYaw = EngineYaw;
		// The first line and its camera.
		const TSharedPtr<FJsonObject>* Conversation = nullptr;
		const TSharedPtr<FJsonObject>* Camera = nullptr;
		if (Info->TryGetObjectField(TEXT("conversation"), Conversation) && (*Conversation)->TryGetObjectField(TEXT("camera"), Camera))
		{
			Person.Line.bValid = true;
			Person.Line.LineKey = (*Conversation)->GetStringField(TEXT("line"));
			const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
			if ((*Conversation)->TryGetArrayField(TEXT("lines"), Lines))
			{
				for (const TSharedPtr<FJsonValue>& Each : *Lines)
				{
					const TSharedPtr<FJsonObject>& Entry = Each->AsObject();
					Person.Line.Lines.Add(Entry->GetStringField(TEXT("line")) + TEXT("\t") + Entry->GetStringField(TEXT("speaker")));
				}
			}
			Person.Line.Speaker = (*Conversation)->GetStringField(TEXT("speaker"));
			Person.Line.Text = (*Conversation)->GetStringField(TEXT("text"));
			FillLineCamera(**Camera, Feet, EngineYaw, Person.Line);
		}
		Persons.Add(MoveTemp(Person));
	}
	UE_LOG(LogSwcPersons, Log, TEXT("persons: %d persons (%d visible for this character, %d with Talk, %d with interaction data); %d with no body, %d with no idle clip"),
		Persons.Num(), Persons.FilterByPredicate([](const FSwcPerson& P) { return P.bVisible; }).Num(),
		Persons.FilterByPredicate([](const FSwcPerson& P) { return P.bTalk; }).Num(),
		Persons.FilterByPredicate([](const FSwcPerson& P) { return !P.InteractionKey.IsEmpty(); }).Num(), NoBody, NoIdle);
}

bool USwcPersonSubsystem::GetLineCamera(const FSwcPerson& Person, const FString& LineKey, FSwcPerson::FLine& Out) const
{
	const TSharedPtr<FJsonObject>* Camera = LineCameras.Find(LineKey.ToLower());
	if (Camera == nullptr || !Camera->IsValid())
	{
		return false;
	}
	Out = FSwcPerson::FLine();
	Out.bValid = true;
	Out.LineKey = LineKey;
	Out.Speaker = Person.Line.Speaker;
	Out.Text = Person.Line.Text;
	FillLineCamera(**Camera, swc::interact::ScenePoint{Person.TalkFrame.X, Person.TalkFrame.Y, Person.TalkFrame.Z}, Person.TalkFrameYaw, Out);
	return Out.bCamera;
}

bool USwcPersonSubsystem::GetViewLocation(FVector& Out) const
{
	// The loading gate (docs/WORLD.md "The loading"): during a loading screen the place of the character, else the camera.
	return SwcLoadingGate::GetView(GetWorld(), Out);
}

void USwcPersonSubsystem::Deinitialize()
{
	SwcLoadingGate::Unregister(GetWorld(), TEXT("persons"));
	Super::Deinitialize();
}

void USwcPersonSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bLoadTried)
	{
		bLoadTried = true;
		Load();
		// The loading gate: the visible persons within the spawn distance of the place of a loading screen have their actor
		// (body, items, idle clip) before the screen ends.
		SwcLoadingGate::Register(GetWorld(), TEXT("persons"), [this](const FVector& Place, FString& Why)
		{
			if (!bPresenceKnown)
			{
				Why = TEXT("the presence waits for the Achievement state of the character");
				return false;
			}
			int32 Missing = 0;
			for (const FSwcPerson& Person : Persons)
			{
				Missing += !Person.Actor.IsValid() && Person.bVisible && !Person.BodyPath.IsEmpty() && FVector::Dist(Place, Person.Place) <= SpawnDistance;
			}
			Why = FString::Printf(TEXT("%d person(s) to spawn"), Missing);
			return Missing == 0;
		});
	}
	for (FSwcPerson& Person : Persons)
	{
		UpdateTalk(Person);
	}
	if (!UpdatePresence())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	FVector View;
	FVector Place;
	// A loading screen: the near persons in each frame, within ScreenSpawnSeconds of a frame (at least one; a spawn loads its body,
	// items, and idle clip at once), and not in the first frames of the screen (AST-273 P8: the screen draws first).
	const bool bScreen = SwcLoadingGate::GetPlace(GetWorld(), Place);
	if (Persons.IsEmpty() || (!bScreen && Now < NextUpdate) || (bScreen && !SwcLoadingGate::IsSettled(GetWorld())) || !GetViewLocation(View))
	{
		return;
	}
	NextUpdate = Now + 0.25;
	int32 Budget = bScreen ? TNumericLimits<int32>::Max() : SpawnsPerUpdate;
	constexpr double ScreenSpawnSeconds = 0.05;
	for (FSwcPerson& Person : Persons)
	{
		const double Distance = FVector::Dist(View, Person.Place);
		if (Person.Actor.IsValid() && Distance > RemoveDistance)
		{
			Person.Actor->Destroy();
			Person.Actor.Reset();
		}
		else if (!Person.Actor.IsValid() && Person.bVisible && !Person.BodyPath.IsEmpty() && Distance <= SpawnDistance && Budget > 0)
		{
			--Budget;
			Spawn(Person);
			if (bScreen && FPlatformTime::Seconds() - Now > ScreenSpawnSeconds)
			{
				Budget = 0;   // the next frame of the screen spawns the next ones
			}
		}
	}
}

bool USwcPersonSubsystem::UpdatePresence()
{
	const UGameInstance* GameInstance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
	const USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	swc::net::RepClient::OwnEntity Own;
	if (Backend == nullptr || !Backend->GetLocalPlayer(Own))
	{
		return bPresenceKnown;
	}
	if (bPresenceKnown && Own.achievementRevision == SeenAchievementRevision)
	{
		return true;
	}
	bPresenceKnown = true;
	SeenAchievementRevision = Own.achievementRevision;
	int32 Shown = 0;
	int32 Changed = 0;
	for (FSwcPerson& Person : Persons)
	{
		const bool bNow = swc::interact::presence_visible(Person.bHasSwap, Person.PresenceRule, Own.achievements, Person.bShowOnLocked);
		Changed += bNow != Person.bVisible;
		Person.bVisible = bNow;
		Shown += bNow;
		if (!bNow && Person.Actor.IsValid())
		{
			Person.Actor->Destroy();
			Person.Actor.Reset();
		}
	}
	if (Changed > 0 || PresenceRevision == 0)
	{
		++PresenceRevision;
	}
	UE_LOG(LogSwcPersons, Log, TEXT("persons: the presence for the Achievement state (%d byte(s), revision %u): %d of %d persons show, %d changed"),
		static_cast<int32>(Own.achievements.size()), Own.achievementRevision, Shown, Persons.Num(), Changed);
	return true;
}

void USwcPersonSubsystem::Spawn(FSwcPerson& Person)
{
	USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr, *Person.BodyPath);
	if (Body == nullptr)
	{
		UE_LOG(LogSwcPersons, Warning, TEXT("persons: %s: no body %s"), *Person.Key, *Person.BodyPath);
		Person.BodyPath.Reset();
		return;
	}
	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FRotator(0.0, Person.Yaw, 0.0), Person.Place), Parameters);
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorLocationAndRotation(Person.Place, FRotator(0.0, Person.Yaw, 0.0));
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Actor, TEXT("Body"));
	Mesh->SetSkeletalMesh(Body);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetupAttachment(Root);
	Mesh->SetRelativeRotation(FRotator(0.0, MeshYaw, 0.0));
	Mesh->SetRelativeScale3D(FVector(Person.Scale));
	Mesh->RegisterComponent();
	// The clips of the timelines (USwcPersonAnimInstance). The start timeline plays from time 0 (no random start: research
	// p48-persons, animations).
	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(USwcPersonAnimInstance::StaticClass());
	for (const FSwcPerson::FItem& Item : Person.Items)
	{
		UStaticMesh* ItemMesh = LoadObject<UStaticMesh>(nullptr, *Item.MeshPath);
		if (ItemMesh == nullptr)
		{
			continue;
		}
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor);
		Component->SetStaticMesh(ItemMesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetupAttachment(Mesh, Item.Bone);
		Component->SetRelativeTransform(Item.Offset);
		Component->RegisterComponent();
	}
	Person.Actor = Actor;
	++Spawned;
	Person.Talk = FSwcPerson::ETalk::None;
	FSwcPerson::FClip Start = Person.Idle;
	Start.BlendIn = 0.0f;
	Play(Person, Start, TEXT("idle"));
	Mesh->UpdateBounds();
	UE_LOG(LogSwcPersons, Log, TEXT("persons: the body of %s: %d LOD(s), %d section(s), bounds center %s extent %s, visible %d"), *Person.NpcId,
		Body->GetLODNum(), Body->GetResourceForRendering() != nullptr && Body->GetResourceForRendering()->LODRenderData.Num() > 0
			? Body->GetResourceForRendering()->LODRenderData[0].RenderSections.Num() : -1,
		*Mesh->Bounds.Origin.ToString(), *Mesh->Bounds.BoxExtent.ToString(), Mesh->IsVisible() ? 1 : 0);
	UE_LOG(LogSwcPersons, Log, TEXT("persons: %s (%s) at %.2f %.2f %.2f m, yaw %.1f, %d item(s), idle %s"), *Person.Name, *Person.NpcId, Person.Place.X / 100.0,
		-Person.Place.Y / 100.0, Person.Place.Z / 100.0, Person.Yaw, Person.Items.Num(), *FPaths::GetBaseFilename(Person.Idle.Path));
}

bool USwcPersonSubsystem::Play(FSwcPerson& Person, const FSwcPerson::FClip& Clip, const TCHAR* What)
{
	USkeletalMeshComponent* Mesh = Person.Actor.IsValid() ? Person.Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	USwcPersonAnimInstance* Animation = Mesh != nullptr ? Cast<USwcPersonAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	UAnimSequence* Sequence = Clip.Path.IsEmpty() ? nullptr : LoadObject<UAnimSequence>(nullptr, *Clip.Path);
	if (Animation == nullptr || Sequence == nullptr)
	{
		return false;
	}
	Animation->PlayClip(Sequence, Clip.bLoop, Clip.Speed, Clip.BlendIn);
	UE_LOG(LogSwcPersons, Log, TEXT("persons: %s plays the %s clip %s (%.2f s, loop %d, blend %.2f s)"), *Person.NpcId, What, *FPaths::GetBaseFilename(Clip.Path),
		Sequence->GetPlayLength(), Clip.bLoop ? 1 : 0, Clip.BlendIn);
	return true;
}

void USwcPersonSubsystem::SetTalking(const FString& InteractionKey, bool bTalking)
{
	FSwcPerson* Person = const_cast<FSwcPerson*>(FindByInteraction(InteractionKey));
	if (Person == nullptr)
	{
		return;
	}
	const bool bNow = Person->Talk == FSwcPerson::ETalk::Greet || Person->Talk == FSwcPerson::ETalk::Interact;
	if (bTalking == bNow)
	{
		return;
	}
	if (bTalking)
	{
		Person->Talk = Play(*Person, Person->Greet, TEXT("greet")) ? FSwcPerson::ETalk::Greet
			: Play(*Person, Person->Interact, TEXT("interact")) ? FSwcPerson::ETalk::Interact : FSwcPerson::ETalk::None;
	}
	else
	{
		Person->Talk = Play(*Person, Person->Bye, TEXT("bye")) ? FSwcPerson::ETalk::Bye : FSwcPerson::ETalk::None;
		if (Person->Talk == FSwcPerson::ETalk::None)
		{
			Play(*Person, Person->Idle, TEXT("idle"));
		}
	}
}

void USwcPersonSubsystem::UpdateTalk(FSwcPerson& Person)
{
	if (Person.Talk != FSwcPerson::ETalk::Greet && Person.Talk != FSwcPerson::ETalk::Bye)
	{
		return;
	}
	const USkeletalMeshComponent* Mesh = Person.Actor.IsValid() ? Person.Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	const USwcPersonAnimInstance* Animation = Mesh != nullptr ? Cast<USwcPersonAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (Animation == nullptr || Animation->GetClipTime() < Animation->GetClipLength())
	{
		return;
	}
	// The end of the clip with no loop: the greet goes to the interact loop, the bye to the idle (the StartIdle layer of the bye).
	if (Person.Talk == FSwcPerson::ETalk::Greet)
	{
		Person.Talk = Play(Person, Person.Interact, TEXT("interact")) ? FSwcPerson::ETalk::Interact : FSwcPerson::ETalk::None;
	}
	else
	{
		Person.Talk = FSwcPerson::ETalk::None;
		Play(Person, Person.Idle, TEXT("idle"));
	}
}

const FSwcPerson* USwcPersonSubsystem::FindByInteraction(const FString& Key) const
{
	// A person with no npc object (USwcInteractor: the Talk key "person:" + the key of persons.json).
	if (Key.StartsWith(PersonTalkPrefix))
	{
		return FindByKey(Key.Mid(FCString::Strlen(PersonTalkPrefix)));
	}
	return Persons.FindByPredicate([&Key](const FSwcPerson& Person) { return !Key.IsEmpty() && Person.InteractionKey == Key; });
}

const FSwcPerson* USwcPersonSubsystem::FindTalkWithNoData(const FVector& Point, double Distance) const
{
	const FSwcPerson* Best = nullptr;
	double BestDistance = Distance;
	for (const FSwcPerson& Person : Persons)
	{
		const double D = FVector::Dist(Point, Person.Place);
		if (Person.bTalk && Person.InteractionKey.IsEmpty() && Person.bVisible && Person.Actor.IsValid() && D <= BestDistance)
		{
			Best = &Person;
			BestDistance = D;
		}
	}
	return Best;
}

const FSwcPerson* USwcPersonSubsystem::FindByKey(const FString& Key) const
{
	return Persons.FindByPredicate([&Key](const FSwcPerson& Person) { return Person.Key == Key; });
}

const FSwcPerson* USwcPersonSubsystem::FindNear(const FVector& Point, double Distance) const
{
	const FSwcPerson* Best = nullptr;
	double BestDistance = Distance;
	for (const FSwcPerson& Person : Persons)
	{
		const double D = FVector::Dist(Point, Person.Place);
		if (D <= BestDistance)
		{
			Best = &Person;
			BestDistance = D;
		}
	}
	return Best;
}
