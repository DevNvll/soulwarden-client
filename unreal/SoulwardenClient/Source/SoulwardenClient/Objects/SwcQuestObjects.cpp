#include "Objects/SwcQuestObjects.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Creatures/SwcCreatureData.h"
#include "Creatures/SwcCreatureSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/SwcSpace.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SwcBackendSubsystem.h"
#include "WorldPartition/WorldPartition.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcQuestObjects, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarQuestObjects(TEXT("swc.QuestObjects"), 1,
	TEXT("The destructible quest objects (Q9: the nodules, the monolith): 1 shows them, 0 hides them."));

const TCHAR* StateName(uint8 State)
{
	return State == 1 ? TEXT("active") : State == 2 ? TEXT("destroyed") : TEXT("hidden");
}

bool ParseActor(const FString& Hex, std::array<uint8, 16>& Out)
{
	if (Hex.Len() != 32)
	{
		return false;
	}
	for (int32 Index = 0; Index < 16; ++Index)
	{
		Out[Index] = static_cast<uint8>(FParse::HexDigit(Hex[Index * 2]) * 16 + FParse::HexDigit(Hex[Index * 2 + 1]));
	}
	return true;
}
} // namespace

bool USwcQuestObjectSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcQuestObjectSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcQuestObjectSubsystem, STATGROUP_Tickables);
}

void USwcQuestObjectSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	// The open world only (a World Partition level; not the menu scene).
	if (World.GetWorldPartition() == nullptr || FParse::Param(FCommandLine::Get(), TEXT("SwcNoQuestObjects")))
	{
		return;
	}
	bEnabled = Read();
}

void USwcQuestObjectSubsystem::Deinitialize()
{
	for (FObject& Object : Objects)
	{
		Clear(Object);
	}
	Objects.Empty();
	Super::Deinitialize();
}

bool USwcQuestObjectSubsystem::Read()
{
	const FString Path = FPaths::Combine(FSwcCreatureLibrary::ContentFolder(), TEXT("world-objects/quest-objects.json"));
	FString Text;
	TSharedPtr<FJsonObject> Document;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document) || !Document.IsValid())
	{
		UE_LOG(LogSwcQuestObjects, Warning, TEXT("quest objects: no file %s (converter/swc_convert/quest_objects.py)"), *Path);
		return false;
	}
	const TSharedPtr<FJsonObject>* SliceMap = nullptr;
	if (Document->TryGetObjectField(TEXT("slices"), SliceMap))
	{
		for (const auto& Slice : (*SliceMap)->Values)
		{
			TArray<FPart>& Parts = Slices.Add(FString(Slice.Key));
			for (const TSharedPtr<FJsonValue>& Value : Slice.Value->AsArray())
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				FPart& Part = Parts.AddDefaulted_GetRef();
				Part.Mesh = FSoftObjectPath(Object->GetStringField(TEXT("mesh")));
				const FString Collision = Object->GetStringField(TEXT("collision"));
				if (!Collision.IsEmpty())
				{
					Part.Collision = FSoftObjectPath(Collision);
				}
				Part.bDecal = Object->GetBoolField(TEXT("decal"));
				for (const auto& Slot : Object->GetObjectField(TEXT("slots"))->Values)
				{
					const FString Key = Slot.Value->AsString();
					Part.Slots.Emplace(FName(FString(Slot.Key)), FSoftObjectPath(FString::Printf(TEXT("/SwcGameContent/Materials/%s.%s"), *Key, *Key)));
				}
				for (const TSharedPtr<FJsonValue>& Row : Object->GetArrayField(TEXT("transforms")))
				{
					const TArray<TSharedPtr<FJsonValue>>& V = Row->AsArray();
					if (V.Num() == 10)
					{
						Part.Transforms.Emplace(FQuat(V[3]->AsNumber(), V[4]->AsNumber(), V[5]->AsNumber(), V[6]->AsNumber()),
							FVector(V[0]->AsNumber(), V[1]->AsNumber(), V[2]->AsNumber()), FVector(V[7]->AsNumber(), V[8]->AsNumber(), V[9]->AsNumber()));
					}
				}
			}
		}
	}
	for (const TSharedPtr<FJsonValue>& Value : Document->GetArrayField(TEXT("objects")))
	{
		const TSharedPtr<FJsonObject> Source = Value->AsObject();
		FObject Object;
		Object.Key = Source->GetStringField(TEXT("key"));
		if (!ParseActor(Source->GetStringField(TEXT("actor")), Object.Actor))
		{
			continue;
		}
		Object.Tag = Source->GetStringField(TEXT("tag")).ToLower();
		Object.bOnlyOnQuest = Source->GetBoolField(TEXT("onlyOnQuest"));
		Object.Name = Source->GetStringField(TEXT("name"));
		const TArray<TSharedPtr<FJsonValue>>& P = Source->GetArrayField(TEXT("place"));
		const TArray<TSharedPtr<FJsonValue>>& R = Source->GetArrayField(TEXT("rotation"));
		if (P.Num() != 3 || R.Num() != 4)
		{
			continue;
		}
		Object.Place = SwcEngineToUnreal(swc::net::WorldPosition{static_cast<float>(P[0]->AsNumber()), static_cast<float>(P[1]->AsNumber()),
			static_cast<float>(P[2]->AsNumber())});
		Object.Rotation = SwcEngineToUnreal(swc::net::Quaternion{static_cast<float>(R[0]->AsNumber()), static_cast<float>(R[1]->AsNumber()),
			static_cast<float>(R[2]->AsNumber()), static_cast<float>(R[3]->AsNumber())}).GetNormalized();
		const TSharedPtr<FJsonObject> Sources = Source->GetObjectField(TEXT("slices"));
		for (const TCHAR* Field : {TEXT("placed"), TEXT("healthy"), TEXT("base")})
		{
			const FString SlicePath = Sources->GetStringField(Field);
			if (!SlicePath.IsEmpty() && Slices.Contains(SlicePath))
			{
				Object.ActiveSlices.AddUnique(SlicePath);
			}
		}
		const FString Depleted = Sources->GetStringField(TEXT("depleted"));
		if (!Depleted.IsEmpty() && Slices.Contains(Depleted))
		{
			Object.DestroyedSlices.Add(Depleted);
		}
		const TSharedPtr<FJsonObject> Marker = Source->GetObjectField(TEXT("marker"));
		Object.MarkerType = Marker->GetStringField(TEXT("type"));
		Object.MarkerOverride = Marker->GetStringField(TEXT("override"));
		Object.bForceHealthBar = Marker->GetBoolField(TEXT("forceHealthBar"));
		Objects.Add(MoveTemp(Object));
	}
	UE_LOG(LogSwcQuestObjects, Log, TEXT("quest objects: %d object(s), %d slice(s) with parts"), Objects.Num(), Slices.Num());
	return !Objects.IsEmpty();
}

void USwcQuestObjectSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!bEnabled || World == nullptr)
	{
		return;
	}
	const int32 Mode = CVarQuestObjects.GetValueOnGameThread();
	if (Mode != ShownMode)
	{
		ShownMode = Mode;
		for (FObject& Object : Objects)
		{
			if (AActor* Actor = Object.Shown.Get())
			{
				Actor->SetActorHiddenInGame(Mode == 0);
			}
		}
	}
	const double Now = World->GetTimeSeconds();
	if (Now < NextUpdate)
	{
		return;
	}
	NextUpdate = Now + UpdateSeconds;
	const APlayerController* Player = World->GetFirstPlayerController();
	const APawn* Pawn = Player != nullptr ? Player->GetPawn() : nullptr;
	USwcBackendSubsystem* Backend = World->GetGameInstance() != nullptr ? World->GetGameInstance()->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	swc::net::RepClient::OwnEntity Own;
	if (Pawn == nullptr || Backend == nullptr || !Backend->GetLocalPlayer(Own))
	{
		return;
	}
	PlayerPlace = Pawn->GetActorLocation();
	const TSet<FString>& Tags = Backend->GetQuestTags();
	for (int32 Index = 0; Index < Objects.Num(); ++Index)
	{
		FObject& Object = Objects[Index];
		EState State = EState::Hidden;
		if (FVector::Dist(PlayerPlace, Object.Place) <= DrawM * 100.0)
		{
			bool bLooted = false;
			for (const swc::net::LootClaim& Claim : Own.lootClaims)
			{
				if (Claim.actor == Object.Actor)
				{
					bLooted = Claim.state == swc::net::kLootLooted;
					break;
				}
			}
			const bool bOnTask = !Object.bOnlyOnQuest || Tags.Contains(Object.Tag);
			State = !bOnTask ? EState::Hidden : bLooted ? EState::Destroyed : EState::Active;
		}
		if (State != Object.State)
		{
			UE_LOG(LogSwcQuestObjects, Log, TEXT("quest object %s (%s): %s -> %s"), *Object.Key, *Object.Tag, StateName(static_cast<uint8>(Object.State)),
				StateName(static_cast<uint8>(State)));
			Show(Index, State);
		}
	}
}

void USwcQuestObjectSubsystem::Clear(FObject& Object)
{
	if (Object.Load.IsValid())
	{
		Object.Load->CancelHandle();
		Object.Load.Reset();
	}
	if (AActor* Actor = Object.Shown.Get())
	{
		Actor->Destroy();
	}
	Object.Shown.Reset();
}

void USwcQuestObjectSubsystem::Show(int32 Index, EState State)
{
	FObject& Object = Objects[Index];
	Clear(Object);
	Object.State = State;
	const TArray<FString>& SliceList = State == EState::Active ? Object.ActiveSlices : Object.DestroyedSlices;
	if (State == EState::Hidden || SliceList.IsEmpty())
	{
		return;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Object.Rotation, Object.Place), Parameters);
	if (Actor == nullptr)
	{
		return;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorTransform(FTransform(Object.Rotation, Object.Place));
	Actor->SetActorHiddenInGame(ShownMode == 0);
	Object.Shown = Actor;
	TArray<FSoftObjectPath> Paths;
	for (const FString& SlicePath : SliceList)
	{
		for (const FPart& Part : Slices[SlicePath])
		{
			Paths.AddUnique(Part.Mesh);
			if (Part.Collision.IsValid())
			{
				Paths.AddUnique(Part.Collision);
			}
			for (const TPair<FName, FSoftObjectPath>& Slot : Part.Slots)
			{
				Paths.AddUnique(Slot.Value);
			}
		}
	}
	const uint32 Serial = ++Object.LoadSerial;
	TWeakObjectPtr<USwcQuestObjectSubsystem> Weak(this);
	Object.Load = Streamer.RequestAsyncLoad(Paths, [Weak, Index, Serial]()
	{
		if (USwcQuestObjectSubsystem* Self = Weak.Get(); Self != nullptr && Self->Objects.IsValidIndex(Index))
		{
			Self->Build(Index, Serial);
		}
	});
}

void USwcQuestObjectSubsystem::Build(int32 Index, uint32 Serial)
{
	FObject& Object = Objects[Index];
	AActor* Actor = Object.Shown.Get();
	if (Actor == nullptr || Serial != Object.LoadSerial)
	{
		return;
	}
	const bool bActive = Object.State == EState::Active;
	USceneComponent* Root = Actor->GetRootComponent();
	int32 Instances = 0;
	for (const FString& SlicePath : bActive ? Object.ActiveSlices : Object.DestroyedSlices)
	{
		for (const FPart& Part : Slices[SlicePath])
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(Part.Mesh.ResolveObject());
			if (Mesh == nullptr || Part.Transforms.IsEmpty())
			{
				continue;
			}
			// The render mesh with no collision and the override materials by slot name (the components of SwcServerVisuals).
			UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Actor);
			Component->SetStaticMesh(Mesh);
			Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			if (Part.bDecal)
			{
				Component->SetCastShadow(false);
			}
			for (const TPair<FName, FSoftObjectPath>& Slot : Part.Slots)
			{
				const int32 SlotIndex = Mesh->GetMaterialIndex(Slot.Key);
				if (UMaterialInterface* Material = Cast<UMaterialInterface>(Slot.Value.ResolveObject()); Material && SlotIndex != INDEX_NONE)
				{
					Component->SetMaterial(SlotIndex, Material);
				}
			}
			Component->SetupAttachment(Root);
			Component->AddInstances(Part.Transforms, false, false, false);
			Component->RegisterComponent();
			Instances += Part.Transforms.Num();
			// An active object blocks the movement of the player only (the rule of the creature body block: the object type and the
			// channel Pawn; the camera, the ground traces, and the wall line do not see it). A hidden or destroyed object has none.
			UStaticMesh* CollisionMesh = bActive ? Cast<UStaticMesh>(Part.Collision.ResolveObject()) : nullptr;
			if (CollisionMesh != nullptr && !Part.bDecal)
			{
				UInstancedStaticMeshComponent* Collision = NewObject<UInstancedStaticMeshComponent>(Actor);
				Collision->SetStaticMesh(CollisionMesh);
				Collision->SetVisibility(false);
				Collision->SetHiddenInGame(true);
				Collision->SetCastShadow(false);
				Collision->bAffectDistanceFieldLighting = false;
				Collision->SetCollisionObjectType(ECC_Pawn);
				Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
				Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
				Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Collision->CanCharacterStepUpOn = ECB_No;
				Collision->SetupAttachment(Root);
				Collision->AddInstances(Part.Transforms, false, false, false);
				Collision->RegisterComponent();
			}
		}
	}
	UE_LOG(LogSwcQuestObjects, Log, TEXT("quest object %s: %s ready, %d instance(s)"), *Object.Key, bActive ? TEXT("active") : TEXT("destroyed"), Instances);
}

void USwcQuestObjectSubsystem::GetPlates(TArray<FSwcCreaturePlate>& Out) const
{
	for (int32 Index = 0; Index < Objects.Num(); ++Index)
	{
		const FObject& Object = Objects[Index];
		const double Distance = FVector::Dist(PlayerPlace, Object.Place) / 100.0;
		if (Object.State != EState::Active || Object.MarkerType.IsEmpty() || Distance > PlateM || ShownMode == 0)
		{
			continue;
		}
		FSwcCreaturePlate& Plate = Out.AddDefaulted_GetRef();
		Plate.Interest = PlateInterestBase + static_cast<uint32>(Index);
		Plate.Serial = 1;
		Plate.Feet = Object.Place;
		Plate.PlatePoint = Object.Place + FVector(0.0, 0.0, 200.0);
		Plate.Name = Object.Name;
		Plate.MarkerType = Object.MarkerType;
		Plate.CreatureTypeOverride = Object.MarkerOverride;
		Plate.bHidesLevel = true;
		// The object has no health in the data (the work of the row in strikes): the health bar of the Lua code at its fallback, 1 of 1.
		Plate.Health = 1.0f;
		Plate.HealthMax = 1.0f;
		Plate.bForceDisplayHealthBar = Object.bForceHealthBar;
		Plate.Distance = static_cast<float>(Distance);
		Plate.bShown = true;
	}
}
