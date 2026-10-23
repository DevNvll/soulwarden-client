#include "Scenes/SwcScenePlayer.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcScenes, Log, All);

namespace
{
const TCHAR* const AnimClip = TEXT("b8d39822-6cab-42c0-af6e-5b53c3e243fc");
const TCHAR* const TriggerClip = TEXT("0d3b86f8-458d-4045-8ce6-24a8d4a83859");
const TCHAR* const CommandClip = TEXT("baad4e91-45aa-4ea4-8450-f1b54fa15ac0");
const TCHAR* const SubtitlePrefix = TEXT("PlayWhisperSubtitle_");
const TCHAR* const MeshComponent = TEXT("2f4bad46-c857-4dcb-a454-c412de67852a");   // the MeshComponent of a static prop

/** @return The first .cgf path of a JSON tree (the static mesh asset of a MeshComponent). */
FString CgfOf(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return FString();
	}
	if (Value->Type == EJson::String)
	{
		return Value->AsString().EndsWith(TEXT(".cgf")) ? Value->AsString() : FString();
	}
	if (Value->Type == EJson::Object)
	{
		for (const auto& Field : Value->AsObject()->Values)
		{
			const FString Found = CgfOf(Field.Value);
			if (!Found.IsEmpty())
			{
				return Found;
			}
		}
	}
	if (Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
		{
			const FString Found = CgfOf(Item);
			if (!Found.IsEmpty())
			{
				return Found;
			}
		}
	}
	return FString();
}
constexpr float MaxStep = 0.05f;
constexpr float SceneBoundsScale = 20.0f;   // research/scene-curves.md 4: the tick splits an advance into steps of at most 0.05 s

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

const TSharedPtr<FJsonObject>& Child(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	static const TSharedPtr<FJsonObject> None;
	const TSharedPtr<FJsonObject>* Out = nullptr;
	return Object.IsValid() && Object->TryGetObjectField(Field, Out) ? *Out : None;
}

TArray<TSharedPtr<FJsonValue>> Items(const TSharedPtr<FJsonObject>& Object)
{
	const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
	return Object.IsValid() && Object->TryGetArrayField(TEXT("items"), Out) ? *Out : TArray<TSharedPtr<FJsonValue>>();
}

float Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float Default = 0.0f)
{
	double Value = Default;
	return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? static_cast<float>(Value) : Default;
}

FString Text(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	FString Value;
	return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : FString();
}

bool Flag(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool Default)
{
	bool Value = Default;
	return Object.IsValid() && Object->TryGetBoolField(Field, Value) ? Value : Default;
}

/** The axes of a Curve of the converted timeline: "X", "Y", "Z" (research/scene-curves.md 1). */
void ReadCurve(const TSharedPtr<FJsonObject>& Curve, swc::scene::Curve Out[3])
{
	for (const TSharedPtr<FJsonValue>& AxisValue : Items(Child(Curve, TEXT("axes"))))
	{
		const TSharedPtr<FJsonObject> Axis = AxisValue->AsObject();
		const FString Name = Text(Axis, TEXT("name"));
		const int32 Index = Name == TEXT("X") ? 0 : Name == TEXT("Y") ? 1 : Name == TEXT("Z") ? 2 : INDEX_NONE;
		if (Index == INDEX_NONE)
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& KeyValue : Items(Child(Axis, TEXT("Keyframes"))))
		{
			const TSharedPtr<FJsonObject> Key = KeyValue->AsObject();
			Out[Index].keys.push_back({Number(Key, TEXT("time")), Number(Key, TEXT("value")), Number(Key, TEXT("IncomingHandleX")),
				Number(Key, TEXT("IncomingHandleY")), Number(Key, TEXT("OutgoingHandleX")), Number(Key, TEXT("OutgoingHandleY")),
				static_cast<int>(Number(Key, TEXT("IncomingType")))});
		}
	}
}

/** @return The first AnimSequence of a folder of the plugin (Interchange shortens a long clip name: graceintrohelp_grac_omalley_cns_igc_Anim).
 *  The registry of a game run has no scan of the plugin: the folder is scanned first. */
UObject* FirstClipIn(const FString& Folder)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({Folder}, false);
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*Folder), Assets, true);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetClassPath.GetAssetName() == FName(TEXT("AnimSequence")))
		{
			return Asset.GetAsset();
		}
	}
	return nullptr;
}

/** @return The object of a class of an asset of player/imported.json (the one whose name ends with a suffix when one is given). */
UObject* FindImported(const TSharedPtr<FJsonObject>& Assets, const FString& Key, const TCHAR* Class, const FString& Suffix = FString())
{
	const TSharedPtr<FJsonObject>& Entry = Child(Assets, *Key);
	const TArray<TSharedPtr<FJsonValue>>* Objects = nullptr;
	if (!Entry.IsValid() || !Entry->TryGetArrayField(Class, Objects))
	{
		return nullptr;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Objects)
	{
		if (Suffix.IsEmpty() || FPaths::GetBaseFilename(Value->AsString()).EndsWith(Suffix, ESearchCase::IgnoreCase))
		{
			return FSoftObjectPath(Value->AsString()).TryLoad();
		}
	}
	return nullptr;
}
} // namespace

ASwcScenePlayer::FMatrix4 ASwcScenePlayer::FMatrix4::operator*(const FMatrix4& Other) const
{
	FMatrix4 Out;
	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 Column = 0; Column < 4; ++Column)
		{
			double Sum = 0.0;
			for (int32 K = 0; K < 4; ++K)
			{
				Sum += M[Row][K] * Other.M[K][Column];
			}
			Out.M[Row][Column] = Sum;
		}
	}
	return Out;
}

ASwcScenePlayer::FMatrix4 ASwcScenePlayer::FMatrix4::FromRows(const TArray<TSharedPtr<FJsonValue>>& Rows)
{
	FMatrix4 Out;
	for (int32 Row = 0; Row < FMath::Min(4, Rows.Num()); ++Row)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = Rows[Row]->AsArray();
		for (int32 Column = 0; Column < FMath::Min(4, Values.Num()); ++Column)
		{
			Out.M[Row][Column] = Values[Column]->AsNumber();
		}
	}
	return Out;
}

ASwcScenePlayer::FMatrix4 ASwcScenePlayer::FMatrix4::FromEuler(const FVector& Position, const FVector& Degrees)
{
	// Order 0 "X, Up, Forward" (axis indices 0, 2, 1; research/scene-curves.md 3): R = Ry Rz Rx in the slice frame (it gives the matrix
	// of the camera start entity of the beach scene). Degrees with the full turns (no wrap).
	const double X = FMath::DegreesToRadians(Degrees.X), Y = FMath::DegreesToRadians(Degrees.Y), Z = FMath::DegreesToRadians(Degrees.Z);
	const double Cx = FMath::Cos(X), Sx = FMath::Sin(X), Cy = FMath::Cos(Y), Sy = FMath::Sin(Y), Cz = FMath::Cos(Z), Sz = FMath::Sin(Z);
	const double Rx[3][3] = {{1, 0, 0}, {0, Cx, -Sx}, {0, Sx, Cx}};
	const double Ry[3][3] = {{Cy, 0, Sy}, {0, 1, 0}, {-Sy, 0, Cy}};
	const double Rz[3][3] = {{Cz, -Sz, 0}, {Sz, Cz, 0}, {0, 0, 1}};
	double Yz[3][3] = {};
	for (int32 I = 0; I < 3; ++I)
	{
		for (int32 J = 0; J < 3; ++J)
		{
			for (int32 K = 0; K < 3; ++K)
			{
				Yz[I][J] += Ry[I][K] * Rz[K][J];
			}
		}
	}
	FMatrix4 Out;
	for (int32 I = 0; I < 3; ++I)
	{
		for (int32 J = 0; J < 3; ++J)
		{
			double Sum = 0.0;
			for (int32 K = 0; K < 3; ++K)
			{
				Sum += Yz[I][K] * Rx[K][J];
			}
			Out.M[I][J] = Sum;
		}
	}
	Out.M[0][3] = Position.X;
	Out.M[1][3] = Position.Y;
	Out.M[2][3] = Position.Z;
	return Out;
}

FTransform ASwcScenePlayer::FMatrix4::ToUnreal() const
{
	// The space map of the client (game x, y, z in m -> Unreal x, -y, z in cm): a rotation R is S R S (S mirrors Y), so the Unreal axes
	// are S c0, -S c1, S c2 (the columns c of R); the mesh yaw rule of the persons and creatures gives the same world rotation.
	const FVector C0 = Axis(0), C1 = Axis(1), C2 = Axis(2);
	const FVector X(C0.X, -C0.Y, C0.Z), Y(-C1.X, C1.Y, -C1.Z), Z(C2.X, -C2.Y, C2.Z);
	FMatrix Matrix(FPlane(X, 0.0), FPlane(Y, 0.0), FPlane(Z, 0.0), FPlane(M[0][3] * 100.0, -M[1][3] * 100.0, M[2][3] * 100.0, 1.0));
	return FTransform(Matrix);
}

ASwcScenePlayer::ASwcScenePlayer()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

int32 ASwcScenePlayer::Find(const FString& Id) const
{
	const int32* Index = ById.Find(Id.ToLower());
	return Index != nullptr ? *Index : INDEX_NONE;
}

USkeletalMeshComponent* ASwcScenePlayer::AddMesh(USkeletalMesh* Mesh, const FTransform& World)
{
	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(this);
	Component->SetSkeletalMesh(Mesh);
	Component->SetupAttachment(RootComponent);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->RegisterComponent();
	Component->SetWorldTransform(World);
	Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	// The root motion of a scene clip moves the body some meters from its entity (Aelstrom: 3 m); the bounds of the reference pose
	// stay at the entity, and the body was culled in the close-ups (live ast240-beach2): larger bounds.
	Component->SetBoundsScale(SceneBoundsScale);
	Keep.Add(Component);
	return Component;
}

void ASwcScenePlayer::AddDummy(int32 Index, APawn* Player, USkeletalMesh* Fallback)
{
	FEntity& Entity = Entities[Index];
	const FTransform World = (Root * Entity.Slice).ToUnreal();
	ACharacter* Character = Cast<ACharacter>(Player);
	if (Character == nullptr || Character->GetMesh() == nullptr || Character->GetMesh()->GetSkeletalMeshAsset() == nullptr)
	{
		if (Fallback != nullptr)
		{
			Entity.Mesh = AddMesh(Fallback, World);
		}
		return;
	}
	// The look and the equipment of the local character (playercharacterupdate.luac PreviewAllEquipmentForTarget; dummyIsArmed false:
	// the parts on a socket, the weapons, stay out) [I: the copy of the parts of this client].
	USkeletalMeshComponent* Leader = AddMesh(Character->GetMesh()->GetSkeletalMeshAsset(), World);
	Leader->SetVisibility(Character->GetMesh()->IsVisible());
	Entity.Mesh = Leader;
	TArray<USkeletalMeshComponent*> Parts;
	Character->GetComponents<USkeletalMeshComponent>(Parts);
	for (USkeletalMeshComponent* Part : Parts)
	{
		if (Part == Character->GetMesh() || Part->GetSkeletalMeshAsset() == nullptr || !Part->IsVisible() || Part->GetAttachSocketName() != NAME_None)
		{
			continue;
		}
		USkeletalMeshComponent* Copy = NewObject<USkeletalMeshComponent>(this);
		Copy->SetSkeletalMesh(Part->GetSkeletalMeshAsset());
		Copy->SetupAttachment(Leader);
		Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Copy->RegisterComponent();
		for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
		{
			Copy->SetMaterial(Slot, Part->GetMaterial(Slot));
		}
		Copy->SetLeaderPoseComponent(Leader);
		Copy->SetBoundsScale(SceneBoundsScale);
		Keep.Add(Copy);
		Entity.Followers.Add(Copy);
	}
}


bool ASwcScenePlayer::Build(const FString& Name, const TSharedPtr<FJsonObject>& Record, const TArray<TSharedPtr<FJsonValue>>& RootRows, APawn* Player,
	FString& Error)
{
	SceneName = Name.ToLower();
	const FString Folder = ContentFolder();
	Doc = ReadJson(FPaths::Combine(Folder, TEXT("quests/scenes"), SceneName + TEXT(".json")));
	if (!Doc.IsValid())
	{
		Error = FString::Printf(TEXT("no quests/scenes/%s.json in %s (tools/convert-scenes.sh write)"), *SceneName, *Folder);
		return false;
	}
	Info = Child(Doc, TEXT("info"));
	Root = FMatrix4::FromRows(RootRows);
	// The entities of the slice: the id, the parent, the slice value (m_worldTM), the camera, the cinematic, the attachment.
	TArray<FString> Parents;
	for (const TSharedPtr<FJsonValue>& Value : Doc->GetArrayField(TEXT("entities")))
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		FEntity& Entity = Entities.AddDefaulted_GetRef();
		Entity.Id = Text(Object, TEXT("id")).ToLower();
		Entity.Name = Text(Object, TEXT("name"));
		const TArray<TSharedPtr<FJsonValue>>* World = nullptr;
		if (Object->TryGetArrayField(TEXT("world"), World))
		{
			Entity.Slice = FMatrix4::FromRows(*World);
		}
		Entity.bActive = Flag(Object, TEXT("active"), true);
		Entity.Cinematic = Text(Object, TEXT("cinematic"));
		const TSharedPtr<FJsonObject>& Attach = Child(Object, TEXT("attach"));
		if (Attach.IsValid())
		{
			Entity.AttachTarget = Text(Attach, TEXT("target")).ToLower();
			Entity.AttachBone = Text(Attach, TEXT("bone"));
			const TArray<TSharedPtr<FJsonValue>>* Offset = nullptr;
			if (Attach->TryGetArrayField(TEXT("offset"), Offset) && Offset->Num() >= 9)
			{
				// [I] Only the scale of the offset (the bone frames of the converted skeletons are not the game frames).
				Entity.AttachScale = static_cast<float>(FVector((*Offset)[0]->AsNumber(), (*Offset)[1]->AsNumber(), (*Offset)[2]->AsNumber()).Size());
			}
		}
		for (const TSharedPtr<FJsonValue>& ComponentValue : Object->GetArrayField(TEXT("components")))
		{
			const TSharedPtr<FJsonObject> Component = ComponentValue->AsObject();
			const FString Class = Text(Component, TEXT("class"));
			if (Class == TEXT("CameraComponent"))
			{
				Entity.bCamera = true;
				Entity.Fov = Number(Component, TEXT("Field of View"), 40.0f);
			}
			else if (Class == TEXT("TriggerActivatePlayerCutsceneComponent"))
			{
				RootEntity = Entities.Num() - 1;
			}
		}
		ById.Add(Entity.Id, Entities.Num() - 1);
		Parents.Add(Text(Object, TEXT("parent")).ToLower());
	}
	for (int32 Index = 0; Index < Entities.Num(); ++Index)
	{
		const int32 Parent = Find(Parents[Index]);
		Entities[Index].Parent = Parent;
		if (Parent != INDEX_NONE)
		{
			Entities[Parent].Children.Add(Index);
		}
		if (Entities[Index].bCamera && CameraEntity == INDEX_NONE)
		{
			CameraEntity = Index;
		}
	}
	if (!BuildActors(Record, Player, Error))
	{
		return false;
	}
	BuildTimelines();
	const FVector Place = Root.Point();
	UE_LOG(LogSwcScenes, Log, TEXT("scene %s: %d entities, %d timelines, root %s, camera %s, at %.2f %.2f %.2f m"), *SceneName, Entities.Num(),
		Timelines.Num(), RootEntity != INDEX_NONE ? *Entities[RootEntity].Name : TEXT("none"), CameraEntity != INDEX_NONE ? *Entities[CameraEntity].Name : TEXT("none"),
		Place.X, Place.Y, Place.Z);
	return true;
}

bool ASwcScenePlayer::BuildActors(const TSharedPtr<FJsonObject>& Record, APawn* Player, FString& Error)
{
	const FString Folder = ContentFolder();
	const TSharedPtr<FJsonObject> Imported = Child(ReadJson(FPaths::Combine(Folder, TEXT("player/imported.json"))), TEXT("assets"));
	const TSharedPtr<FJsonObject> Manifest = Child(ReadJson(FPaths::Combine(Folder, TEXT("manifest.json"))), TEXT("assets"));
	const TArray<TSharedPtr<FJsonValue>>* Wanted = nullptr;
	if (!Record.IsValid() || !Record->TryGetArrayField(TEXT("wanted"), Wanted))
	{
		Error = TEXT("the scene has no wanted actors in quests/scenes.json");
		return false;
	}
	TMap<FString, FString> CafOf;   // the alias of an entity (lower case) -> the stem of its clip file
	int32 Placed = 0;
	for (const TSharedPtr<FJsonValue>& Value : *Wanted)
	{
		const TSharedPtr<FJsonObject> Actor = Value->AsObject();
		const int32 Index = Find(Text(Actor, TEXT("id")));
		if (Index == INDEX_NONE)
		{
			continue;
		}
		FEntity& Entity = Entities[Index];
		const FString Cdf = Text(Actor, TEXT("cdf"));
		const bool bPerson = Text(Actor, TEXT("kind")) == TEXT("person");
		FString CharacterFolder;
		if (!bPerson)
		{
			CharacterFolder = Text(Child(Manifest, *Cdf), TEXT("unreal"));
		}
		const FString Stem = FPaths::GetBaseFilename(Cdf);
		// A character of the content import: FOLDER/STEM/SkeletalMeshes/STEM (and its clip STEM_Anim) [the layout of import_content.py].
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(bPerson ? FindImported(Imported, TEXT("person|") + Cdf, TEXT("SkeletalMesh"))
			: CharacterFolder.IsEmpty() ? nullptr : FSoftObjectPath(FString::Printf(TEXT("%s/%s/SkeletalMeshes/%s.%s"), *CharacterFolder, *Stem, *Stem, *Stem)).TryLoad());
		for (const TSharedPtr<FJsonValue>& ClipValue : Actor->GetArrayField(TEXT("clips")))
		{
			const TSharedPtr<FJsonObject> Clip = ClipValue->AsObject();
			const FString ClipStem = FPaths::GetBaseFilename(Text(Clip, TEXT("caf")));
			if (!ClipStem.IsEmpty())
			{
				CafOf.Add(Entity.Id + TEXT("|") + Text(Clip, TEXT("alias")).ToLower(),
					(bPerson ? TEXT("person|") : FString::Printf(TEXT("%s/%s/SkeletalMeshes|"), *CharacterFolder, *FPaths::GetBaseFilename(Cdf))) + ClipStem);
			}
		}
		if (Entity.Name == TEXT("IGC_Dummy_Player"))
		{
			AddDummy(Index, Player, Mesh);
		}
		else if (Mesh != nullptr)
		{
			Entity.Mesh = AddMesh(Mesh, (Root * Entity.Slice).ToUnreal());
		}
		if (Entity.Mesh.IsValid())
		{
			++Placed;
		}
		else
		{
			UE_LOG(LogSwcScenes, Warning, TEXT("scene %s: no imported mesh for %s (%s; tools/convert-scenes.sh convert, the imports)"), *SceneName, *Entity.Name, *Cdf);
		}
	}
	// The static props of the slice (the corruption plants and the pistol of the Grace scene, the props of the Corinth flyover): the static
	// mesh of the content import at FOLDER/STEM/StaticMeshes/STEM.
	int32 Props = 0;
	int32 PropsMissing = 0;
	const TArray<TSharedPtr<FJsonValue>>& EntityValues = Doc->GetArrayField(TEXT("entities"));
	for (int32 Index = 0; Index < EntityValues.Num() && Index < Entities.Num(); ++Index)
	{
		for (const TSharedPtr<FJsonValue>& ComponentValue : EntityValues[Index]->AsObject()->GetArrayField(TEXT("components")))
		{
			if (Text(ComponentValue->AsObject(), TEXT("type")) != MeshComponent)
			{
				continue;
			}
			const FString Cgf = CgfOf(ComponentValue);
			const FString Folder2 = Text(Child(Manifest, *Cgf), TEXT("unreal"));
			const FString Stem = FPaths::GetBaseFilename(Cgf);
			UStaticMesh* StaticMesh = Folder2.IsEmpty() ? nullptr
				: Cast<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("%s/%s/StaticMeshes/%s.%s"), *Folder2, *Stem, *Stem, *Stem)).TryLoad());
			if (StaticMesh == nullptr)
			{
				++PropsMissing;
				continue;
			}
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
			Component->SetStaticMesh(StaticMesh);
			Component->SetupAttachment(RootComponent);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->RegisterComponent();
			Component->SetWorldTransform((Root * Entities[Index].Slice).ToUnreal());
			Component->SetVisibility(Entities[Index].bActive, true);
			Keep.Add(Component);
			Entities[Index].Static = Component;
			++Props;
		}
	}
	// The props on a bone of an actor (AttachmentComponent: the target entity and its bone).
	for (FEntity& Entity : Entities)
	{
		const int32 Owner = Find(Entity.AttachTarget);
		if (Entity.Static.IsValid() && Owner != INDEX_NONE && Entities[Owner].Mesh.IsValid() && !Entity.AttachBone.IsEmpty())
		{
			Entity.Static->AttachToComponent(Entities[Owner].Mesh.Get(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName(*Entity.AttachBone));
			Entity.Static->SetRelativeScale3D(FVector(Entity.AttachScale));
		}
		const int32 Target = Find(Entity.AttachTarget);
		if (Entity.Mesh.IsValid() && Target != INDEX_NONE && Entities[Target].Mesh.IsValid() && !Entity.AttachBone.IsEmpty())
		{
			Entity.Mesh->AttachToComponent(Entities[Target].Mesh.Get(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName(*Entity.AttachBone));
			Entity.Mesh->SetRelativeScale3D(FVector(Entity.AttachScale));
		}
	}
	// The clips of the aliases: the scene pack of the player pipeline (clips|male|scenes) or the folder of the character.
	for (TPair<FString, FString>& Pair : CafOf)
	{
		FString Where, Stem;
		Pair.Value.Split(TEXT("|"), &Where, &Stem, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		// The clip of a character: STEM_Anim (one clip in its glTF), else STEM_Anim_CLIP.
		const FString MeshStem = FPaths::GetBaseFilename(FPaths::GetPath(Where));   // Where: FOLDER/STEM/SkeletalMeshes
		UObject* Sequence = Where == TEXT("person") ? FindImported(Imported, TEXT("clips|male|scenes"), TEXT("AnimSequence"), Stem)
			: FSoftObjectPath(FString::Printf(TEXT("%s/%s_Anim.%s_Anim"), *Where, *MeshStem, *MeshStem)).TryLoad();
		if (Sequence == nullptr && Where != TEXT("person"))
		{
			Sequence = FirstClipIn(Where);   // one clip for each scene character
		}
		Sequences.Add(Pair.Key, Cast<UAnimSequence>(Sequence));
		if (Sequence == nullptr)
		{
			UE_LOG(LogSwcScenes, Warning, TEXT("scene %s: no imported clip %s"), *SceneName, *Stem);
		}
		else
		{
			Keep.Add(Sequence);
		}
	}
	UE_LOG(LogSwcScenes, Log, TEXT("scene %s: %d of %d actor(s) placed, %d clip(s), %d static prop(s) (%d with no imported mesh)"), *SceneName, Placed,
		Wanted->Num(), Sequences.Num(), Props, PropsMissing);
	return true;
}

void ASwcScenePlayer::BuildTimelines()
{
	for (const TSharedPtr<FJsonValue>& Value : Doc->GetArrayField(TEXT("timelines")))
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		FTimeline& Timeline = Timelines.AddDefaulted_GetRef();
		Timeline.Owner = Find(Text(Object, TEXT("entityId")));
		Timeline.Asset = Text(Object, TEXT("asset"));
		const TSharedPtr<FJsonObject>& Entry = Child(Object, TEXT("entry"));
		Timeline.OnTrigger = static_cast<int32>(Number(Entry, TEXT("OnTriggerAction"), 3.0f));
		Timeline.OnUntrigger = static_cast<int32>(Number(Entry, TEXT("OnUntriggerAction"), 4.0f));
		const TSharedPtr<FJsonObject>& Assigned = Child(Object, TEXT("assigned"));
		for (const TSharedPtr<FJsonValue>& LayerValue : Items(Child(Child(Object, TEXT("timeline")), TEXT("Layers"))))
		{
			const TSharedPtr<FJsonObject> LayerObject = LayerValue->AsObject();
			FLayer& Layer = Timeline.Layers.AddDefaulted_GetRef();
			Layer.Name = Text(LayerObject, TEXT("name"));
			Layer.bIgnore = Flag(LayerObject, TEXT("Ignore"), false);
			Layer.Entity = Find(Text(Assigned, *Text(LayerObject, TEXT("id"))));
			for (const TSharedPtr<FJsonValue>& Holder : Items(Child(LayerObject, TEXT("Clips"))))
			{
				for (const TSharedPtr<FJsonValue>& ClipValue : Items(Holder->AsObject()))
				{
					const TSharedPtr<FJsonObject> Object2 = ClipValue->AsObject();
					if (!Object2.IsValid())
					{
						continue;
					}
					FClip& Clip = Layer.Clips.AddDefaulted_GetRef();
					const TSharedPtr<FJsonObject>& Base = Child(Object2, TEXT("BaseClass1"));
					Clip.Start = Number(Base, TEXT("StartTime"));
					Clip.End = Number(Base, TEXT("EndTime"));
					const FString Class = Text(Object2, TEXT("class"));
					const FString Type = Text(Object2, TEXT("type"));
					Clip.bChildren = Flag(Object2, TEXT("ApplyToChildren"), Flag(Object2, TEXT("ApplyToAllChildren"), true));
					if (Type == AnimClip || Object2->HasField(TEXT("AnimationAlias")))
					{
						Clip.Kind = EKind::Anim;
						Clip.Alias = Text(Object2, TEXT("AnimationAlias")).ToLower();
						Clip.Speed = Number(Object2, TEXT("playbackSpeed"), 1.0f);
						Clip.From = Number(Object2, TEXT("AnimationStartTime"));
						Clip.bLoop = Flag(Object2, TEXT("loop"), false);
						Clip.AnimLayer = static_cast<int32>(Number(Object2, TEXT("Layer")));
					}
					else if (Class == TEXT("ShowMesh") || Class == TEXT("HideMesh"))
					{
						Clip.Kind = Class == TEXT("ShowMesh") ? EKind::Show : EKind::Hide;
					}
					else if (Class == TEXT("ActivateEntity") || Class == TEXT("DeactivateEntity"))
					{
						Clip.Kind = Class == TEXT("ActivateEntity") ? EKind::Activate : EKind::Deactivate;
					}
					else if (Type == TriggerClip || Class == TEXT("UnTriggerEntity"))
					{
						Clip.Kind = Type == TriggerClip ? EKind::Trigger : EKind::Untrigger;
					}
					else if (Type == CommandClip)
					{
						Clip.Kind = EKind::Command;
						Clip.Text = Text(Object2, TEXT("#8EFC3AF2"));
						Clip.Command = static_cast<int32>(Number(Object2, TEXT("#1AB2E2E7")));
					}
					else if (Object2->HasField(TEXT("TranslationCurve")) || Object2->HasField(TEXT("RotationCurve")))
					{
						Clip.Kind = EKind::Transform;
						Clip.EulerOrder = static_cast<int32>(Number(Object2, TEXT("EulerRotationOrder")));
						ReadCurve(Child(Object2, TEXT("TranslationCurve")), Clip.Position);
						ReadCurve(Child(Object2, TEXT("RotationCurve")), Clip.Rotation);
					}
					Timeline.Duration = FMath::Max(Timeline.Duration, Clip.End);
				}
			}
		}
	}
}

void ASwcScenePlayer::Dispatch(bool bStart)
{
	// CutsceneInfo m_onCutsceneStartEvents and m_onCutsceneStoppedEvents (research/quest-scenes.md 2.1): type 0 trigger, 1 untrigger, 2 and
	// 3 the activation of the entity [I: the audio entity of the beach has types 2 and 3].
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Doc->TryGetArrayField(bStart ? TEXT("startEvents") : TEXT("stopEvents"), List))
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		const TSharedPtr<FJsonObject> Event = Value->AsObject();
		const int32 Entity = Find(Text(Event, TEXT("entity")));
		const int32 Type = static_cast<int32>(Number(Event, TEXT("type")));
		if (Entity == INDEX_NONE)
		{
			continue;
		}
		if (Type == 0 || Type == 1)
		{
			TriggerEntity(Entity, Type == 0);
		}
		else
		{
			SetActive(Entity, Type == 2);
		}
	}
}

void ASwcScenePlayer::TriggerEntity(int32 Entity, bool bTrigger)
{
	for (int32 Index = 0; Index < Timelines.Num(); ++Index)
	{
		if (Timelines[Index].Owner == Entity)
		{
			Action(Index, bTrigger ? Timelines[Index].OnTrigger : Timelines[Index].OnUntrigger);
		}
	}
	if (bTrigger && !Entities[Entity].Cinematic.IsEmpty())
	{
		Events.Add(TEXT("cinematic\t") + Entities[Entity].Cinematic);
		UE_LOG(LogSwcScenes, Log, TEXT("scene %s: the trigger of %s starts the cinematic %s"), *SceneName, *Entities[Entity].Name, *Entities[Entity].Cinematic);
	}
}

void ASwcScenePlayer::Action(int32 Index, int32 Code)
{
	// The timeline actions (0x5CBBB2A): 0 None, 1 Play, 2 Pause, 3 Reset and Play, 4 Reset and Pause, 5 Reset to End and Pause,
	// 6 Simulate to End and Pause.
	FTimeline& Timeline = Timelines[Index];
	const auto Reset = [&Timeline]()
	{
		Timeline.Time = 0.0f;
		for (FLayer& Layer : Timeline.Layers)
		{
			for (FClip& Clip : Layer.Clips)
			{
				Clip.bBegun = Clip.bEnded = false;
			}
		}
	};
	switch (Code)
	{
	case 1: Timeline.bPlaying = true; break;
	case 2: Timeline.bPlaying = false; break;
	case 3: Reset(); Timeline.bPlaying = true; break;
	case 4: Reset(); Timeline.bPlaying = false; break;
	case 5: Timeline.Time = Timeline.Duration; Timeline.bPlaying = false; break;
	case 6:
		Timeline.bPlaying = false;
		Run(Index, Timeline.Time, Timeline.Duration, true);
		Timelines[Index].Time = Timelines[Index].Duration;
		break;
	default: break;
	}
	UE_LOG(LogSwcScenes, Log, TEXT("scene %s: timeline %s (owner %s): action %d at %.2f s"), *SceneName, *FPaths::GetBaseFilename(Timeline.Asset),
		Timeline.Owner != INDEX_NONE ? *Entities[Timeline.Owner].Name : TEXT("?"), Code, Timeline.Time);
}

void ASwcScenePlayer::Run(int32 Index, float From, float To, bool bSimulate)
{
	// The forward clip dispatch (0x5CEA750; research/scene-curves.md 4): a clip begins when the time reaches its start, ends at its end;
	// one step can cross a whole clip. Then the continuous clips sample the new time.
	for (int32 LayerIndex = 0; LayerIndex < Timelines[Index].Layers.Num(); ++LayerIndex)
	{
		if (Timelines[Index].Layers[LayerIndex].bIgnore)
		{
			continue;
		}
		for (int32 ClipIndex = 0; ClipIndex < Timelines[Index].Layers[LayerIndex].Clips.Num(); ++ClipIndex)
		{
			FLayer& Layer = Timelines[Index].Layers[LayerIndex];
			FClip& Clip = Layer.Clips[ClipIndex];
			if (!Clip.bBegun && Clip.Start <= To)
			{
				Clip.bBegun = true;
				Begin(Layer, Clip, bSimulate);
			}
			if (Clip.bBegun && !Clip.bEnded && Clip.End <= To)
			{
				Clip.bEnded = true;
			}
			if (bEnded)
			{
				return;
			}
		}
	}
	for (const FLayer& Layer : Timelines[Index].Layers)
	{
		if (Layer.bIgnore)
		{
			continue;
		}
		for (const FClip& Clip : Layer.Clips)
		{
			if ((Clip.Kind == EKind::Anim || Clip.Kind == EKind::Transform) && Clip.Start <= To && (To < Clip.End || (Clip.End <= To && From < Clip.End)))
			{
				Sample(Layer, Clip, FMath::Min(To, Clip.End));
			}
		}
	}
}

void ASwcScenePlayer::Begin(FLayer& Layer, FClip& Clip, bool bSimulate)
{
	switch (Clip.Kind)
	{
	case EKind::Show:
	case EKind::Hide:
		SetVisible(Layer.Entity, Clip.Kind == EKind::Show, Clip.bChildren);
		break;
	case EKind::Activate:
	case EKind::Deactivate:
		SetActive(Layer.Entity, Clip.Kind == EKind::Activate);
		break;
	case EKind::Trigger:
	case EKind::Untrigger:
		if (Layer.Entity != INDEX_NONE)
		{
			TriggerEntity(Layer.Entity, Clip.Kind == EKind::Trigger);
		}
		break;
	case EKind::Command:
		// The subtitle commands (whispers.luac OnCommandClipBegin: CommandId 0, PlayWhisperSubtitle_ then @KEY; the clip duration).
		// A simulated end shows no subtitle.
		if (!bSimulate && Clip.Command == 0 && Clip.Text.StartsWith(SubtitlePrefix) && Clip.Text.Mid(FCString::Strlen(SubtitlePrefix)).StartsWith(TEXT("@")))
		{
			Events.Add(FString::Printf(TEXT("subtitle\t%s\t%.3f"), *Clip.Text.Mid(FCString::Strlen(SubtitlePrefix)), Clip.End - Clip.Start));
		}
		break;
	default:
		break;
	}
}

void ASwcScenePlayer::Sample(const FLayer& Layer, const FClip& Clip, float Time)
{
	if (Layer.Entity == INDEX_NONE)
	{
		return;
	}
	FEntity& Entity = Entities[Layer.Entity];
	const float Local = Time - Clip.Start;
	if (Clip.Kind == EKind::Anim)
	{
		if (Clip.AnimLayer > 0)
		{
			return;   // [I] an overlay layer (the face clip of the statue scene, Layer 1): no layered blend in this client
		}
		const TWeakObjectPtr<UAnimSequence>* Found = Sequences.Find(Entity.Id + TEXT("|") + Clip.Alias);
		UAnimSequence* Sequence = Found != nullptr ? Found->Get() : nullptr;
		USkeletalMeshComponent* Mesh = Entity.Mesh.Get();
		if (Sequence == nullptr || Mesh == nullptr)
		{
			return;
		}
		if (Mesh->GetSingleNodeInstance() == nullptr || Mesh->GetSingleNodeInstance()->GetAnimationAsset() != Sequence)
		{
			Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Mesh->SetAnimation(Sequence);
			Mesh->Stop();
		}
		const float Length = Sequence->GetPlayLength();
		float Position = Clip.From + Local * Clip.Speed;
		Position = Clip.bLoop && Length > 0.0f ? FMath::Fmod(Position, Length) : FMath::Clamp(Position, 0.0f, Length);
		if (UAnimSingleNodeInstance* Instance = Mesh->GetSingleNodeInstance())
		{
			Instance->SetPosition(Position, false);
		}
		return;
	}
	if (Clip.Kind == EKind::Transform)
	{
		const FVector Current = FVector::ZeroVector;   // an axis with no key: 0 in the local frame
		const FVector Position(Clip.Position[0].evaluate(Local, Current.X), Clip.Position[1].evaluate(Local, Current.Y), Clip.Position[2].evaluate(Local, Current.Z));
		const FVector Degrees(Clip.Rotation[0].evaluate(Local), Clip.Rotation[1].evaluate(Local), Clip.Rotation[2].evaluate(Local));
		// The values are the local transform under the parent entity (the camera under its Camera Group: the C1 and I1 close-ups of
		// Aelstrom at 0.7 to 1.7 m; research/scene-curves.md 3 leaves the final space [U]).
		const int32 Parent = Entity.Parent;
		const FMatrix4 ParentMatrix = Parent != INDEX_NONE && Parent != RootEntity
			? (Entities[Parent].bHasOverride ? Entities[Parent].Override : Entities[Parent].Slice) : FMatrix4();
		Entity.Override = ParentMatrix * FMatrix4::FromEuler(Position, Degrees);
		Entity.bHasOverride = true;
		Place(Layer.Entity);
	}
}

void ASwcScenePlayer::Place(int32 Index)
{
	FEntity& Entity = Entities[Index];
	if (Entity.Mesh.IsValid() && Entity.Mesh->GetAttachParent() == RootComponent)
	{
		Entity.Mesh->SetWorldTransform((Root * (Entity.bHasOverride ? Entity.Override : Entity.Slice)).ToUnreal());
	}
	if (Entity.Static.IsValid() && Entity.Static->GetAttachParent() == RootComponent)
	{
		Entity.Static->SetWorldTransform((Root * (Entity.bHasOverride ? Entity.Override : Entity.Slice)).ToUnreal());
	}
}

void ASwcScenePlayer::SetVisible(int32 Index, bool bVisible, bool bChildren)
{
	if (Index == INDEX_NONE)
	{
		return;
	}
	FEntity& Entity = Entities[Index];
	Entity.bVisible = bVisible;
	if (Entity.Mesh.IsValid())
	{
		Entity.Mesh->SetVisibility(bVisible && Entity.bActive, true);
	}
	if (Entity.Static.IsValid())
	{
		Entity.Static->SetVisibility(bVisible && Entity.bActive, true);
	}
	if (bChildren)
	{
		for (const int32 Child2 : Entity.Children)
		{
			SetVisible(Child2, bVisible, true);
		}
	}
}

void ASwcScenePlayer::SetActive(int32 Index, bool bActive)
{
	if (Index == INDEX_NONE)
	{
		return;
	}
	Entities[Index].bActive = bActive;
	if (Entities[Index].Mesh.IsValid())
	{
		Entities[Index].Mesh->SetVisibility(bActive && Entities[Index].bVisible, true);
	}
	if (Entities[Index].Static.IsValid())
	{
		Entities[Index].Static->SetVisibility(bActive && Entities[Index].bVisible, true);
	}
	// The deactivation of the slice root (igc_destroy_slice_2s: DeactivateEntity at 0 on the root) ends the scene (research/quest-scenes.md 2.4).
	if (!bActive && Index == RootEntity && !bEnded)
	{
		bEnded = true;
		Events.Add(TEXT("end"));
		UE_LOG(LogSwcScenes, Log, TEXT("scene %s: the slice root is deactivated at %.2f s: the end"), *SceneName, GetMainTime());
	}
}

void ASwcScenePlayer::Advance(float DeltaSeconds)
{
	float Left = FMath::Max(DeltaSeconds, 0.0f);
	while (Left > 0.0f && !bEnded)
	{
		const float Step = FMath::Min(Left, MaxStep);
		Left -= Step;
		for (int32 Index = 0; Index < Timelines.Num() && !bEnded; ++Index)
		{
			if (!Timelines[Index].bPlaying)
			{
				continue;
			}
			const float From = Timelines[Index].Time;
			const float To = FMath::Min(From + Step, Timelines[Index].Duration);
			Timelines[Index].Time = To;
			Run(Index, From, To, false);
			if (To >= Timelines[Index].Duration)
			{
				Timelines[Index].bPlaying = false;   // Loop false (research/scene-curves.md 5)
			}
		}
	}
	if (bStopped && !bEnded)
	{
		StoppedFor += DeltaSeconds;
	}
	const bool bAnyPlaying = Timelines.ContainsByPredicate([](const FTimeline& Timeline) { return Timeline.bPlaying; });
	if (!bEnded && (!bAnyPlaying || StoppedFor > 1.0f))
	{
		// [I] A scene whose slice root no timeline deactivates ends when no timeline plays (or 1 s after a stop).
		bEnded = true;
		Events.Add(TEXT("end"));
		UE_LOG(LogSwcScenes, Warning, TEXT("scene %s: no timeline plays and the root is still active: the end"), *SceneName);
	}
}

void ASwcScenePlayer::Stop()
{
	if (bEnded || bStopped)
	{
		return;
	}
	bStopped = true;
	Dispatch(false);
}

bool ASwcScenePlayer::GetView(FVector& Location, FRotator& Rotation, float& VerticalFov) const
{
	if (CameraEntity == INDEX_NONE || bEnded || !Entities[CameraEntity].bActive)
	{
		return false;
	}
	const FEntity& Camera = Entities[CameraEntity];
	const FMatrix4 World = Root * (Camera.bHasOverride ? Camera.Override : Camera.Slice);
	const FVector Forward = World.Axis(1);   // the view along +Y (the C1 and I1 close-ups fit +Y under the Camera Group)
	const FVector Up = World.Axis(2);
	Location = FVector(World.M[0][3] * 100.0, -World.M[1][3] * 100.0, World.M[2][3] * 100.0);
	Rotation = FRotationMatrix::MakeFromXZ(FVector(Forward.X, -Forward.Y, Forward.Z), FVector(Up.X, -Up.Y, Up.Z)).Rotator();
	VerticalFov = Camera.Fov;
	return true;
}

TArray<FString> ASwcScenePlayer::TakeEvents()
{
	return MoveTemp(Events);
}

double ASwcScenePlayer::GetMainTime() const
{
	double Time = 0.0;
	for (const FTimeline& Timeline : Timelines)
	{
		Time = FMath::Max(Time, static_cast<double>(Timeline.Time));
	}
	return Time;
}
