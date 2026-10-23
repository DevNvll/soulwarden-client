#include "Objects/SwcServerVisuals.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Creatures/SwcCreatureData.h"
#include "Creatures/SwcCreatureSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/SwcSpace.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WorldPartition/WorldPartition.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcServerVisuals, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarServerVisuals(TEXT("swc.ServerVisuals"), 1,
	TEXT("The visuals of the backend objects (AST-275: the crafting stations of the settlements): 1 shows them, 0 hides them."));
}

bool USwcServerVisualSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcServerVisualSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcServerVisualSubsystem, STATGROUP_Tickables);
}

void USwcServerVisualSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	// The open world only (a World Partition level; not the menu scene).
	if (World.GetWorldPartition() == nullptr || FParse::Param(FCommandLine::Get(), TEXT("SwcNoServerVisuals")))
	{
		return;
	}
	bEnabled = Read();
}

void USwcServerVisualSubsystem::Deinitialize()
{
	for (TPair<uint32, FVisual>& Pair : Visuals)
	{
		if (Pair.Value.Load.IsValid())
		{
			Pair.Value.Load->CancelHandle();
		}
		if (AActor* Actor = Pair.Value.Actor.Get())
		{
			Actor->Destroy();
		}
	}
	Visuals.Empty();
	Super::Deinitialize();
}

bool USwcServerVisualSubsystem::Read()
{
	const FString Path = FPaths::Combine(FSwcCreatureLibrary::ContentFolder(), TEXT("world-objects/server-visuals.json"));
	FString Text;
	TSharedPtr<FJsonObject> Document;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document) || !Document.IsValid())
	{
		UE_LOG(LogSwcServerVisuals, Warning, TEXT("server visuals: no file %s (converter/swc_convert/server_visuals.py)"), *Path);
		return false;
	}
	int32 PartCount = 0;
	const TSharedPtr<FJsonObject>* SliceMap = nullptr;
	if (Document->TryGetObjectField(TEXT("slices"), SliceMap))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Slice : (*SliceMap)->Values)
		{
			TArray<FPart>& Parts = Slices.Add(Slice.Key);
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
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Slot : Object->GetObjectField(TEXT("slots"))->Values)
				{
					// import_content.py: the material instance of a material record is /SwcGameContent/Materials/KEY.
					const FString Key = Slot.Value->AsString();
					Part.Slots.Emplace(FName(*Slot.Key), FSoftObjectPath(FString::Printf(TEXT("/SwcGameContent/Materials/%s.%s"), *Key, *Key)));
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
				++PartCount;
			}
		}
	}
	for (const TSharedPtr<FJsonValue>& Value : Document->GetArrayField(TEXT("places")))
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		const TArray<TSharedPtr<FJsonValue>>& P = Object->GetArrayField(TEXT("place"));
		if (P.Num() == 3)
		{
			const swc::net::WorldPosition Engine{static_cast<float>(P[0]->AsNumber()), static_cast<float>(P[1]->AsNumber()), static_cast<float>(P[2]->AsNumber())};
			Places.Add(Object->GetStringField(TEXT("key")), SwcEngineToUnreal(Engine));
		}
	}
	UE_LOG(LogSwcServerVisuals, Log, TEXT("server visuals: %d slice(s) with %d part(s), %d backend place(s)"), Slices.Num(), PartCount, Places.Num());
	return !Slices.IsEmpty();
}

void USwcServerVisualSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!bEnabled || World == nullptr)
	{
		return;
	}
	const int32 Mode = CVarServerVisuals.GetValueOnGameThread();
	if (Mode != ShownMode)
	{
		ShownMode = Mode;
		for (TPair<uint32, FVisual>& Pair : Visuals)
		{
			if (AActor* Actor = Pair.Value.Actor.Get())
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
	const USwcCreatureSubsystem* Creatures = World->GetSubsystem<USwcCreatureSubsystem>();
	if (Creatures == nullptr)
	{
		return;
	}
	const swc::creatures::EntityTable& Table = Creatures->GetTable();
	// The stop of a record (the backend removes a station at 200 m) or a new entity at its interest number: the visual goes.
	for (auto It = Visuals.CreateIterator(); It; ++It)
	{
		const swc::creatures::Entity* Entity = Table.find(It.Key());
		if (Entity == nullptr || Entity->serial != It.Value().Serial)
		{
			if (It.Value().Load.IsValid())
			{
				It.Value().Load->CancelHandle();
			}
			if (AActor* Actor = It.Value().Actor.Get())
			{
				Actor->Destroy();
			}
			UE_LOG(LogSwcServerVisuals, Log, TEXT("server visual %u: removed (%s)"), It.Key(), *It.Value().Key);
			It.RemoveCurrent();
		}
	}
	for (const auto& [Interest, Entity] : Table.entities())
	{
		if (!Entity.hasMetadata || !Entity.hasWorldPlace || Visuals.Contains(Interest))
		{
			continue;
		}
		const FString Key = UTF8_TO_TCHAR(Entity.slice_key().c_str());
		if (!Slices.Contains(Key))
		{
			continue;
		}
		const FVector Place = SwcEngineToUnreal(Entity.worldPlace);
		TArray<FVector> Known;
		Places.MultiFind(Key, Known);
		const bool bAtPlace = Known.ContainsByPredicate([&Place](const FVector& P) { return FVector::Dist2D(P, Place) <= PlaceM * 100.0; });
		if (!bAtPlace)
		{
			// Not a place of the backend data: an object that the cells may have (a station "on start"), so no visual (no double draw).
			if (!Unmatched.Contains(Key))
			{
				Unmatched.Add(Key);
				UE_LOG(LogSwcServerVisuals, Log, TEXT("server visual %u: %s at %.2f %.2f %.2f is no backend place of the data: not drawn"), Interest, *Key,
					Entity.worldPlace.x, Entity.worldPlace.y, Entity.worldPlace.z);
			}
			continue;
		}
		FVisual& Visual = Visuals.Add(Interest);
		Visual.Serial = Entity.serial;
		Visual.Key = Key;
		Spawn(Interest, Visual, Place, SwcEngineToUnreal(Entity.worldPlaceRotation));
	}
}

void USwcServerVisualSubsystem::Spawn(uint32 Interest, FVisual& Visual, const FVector& Place, const FQuat& Rotation)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Rotation.GetNormalized(), Place), Parameters);
	if (Actor == nullptr)
	{
		return;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorTransform(FTransform(Rotation.GetNormalized(), Place));
	Actor->SetActorHiddenInGame(ShownMode == 0);
	Visual.Actor = Actor;
	TArray<FSoftObjectPath> Paths;
	for (const FPart& Part : Slices[Visual.Key])
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
	UE_LOG(LogSwcServerVisuals, Log, TEXT("server visual %u: %s at %.0f %.0f %.0f cm, %d asset(s) load"), Interest, *Visual.Key, Place.X, Place.Y, Place.Z, Paths.Num());
	TWeakObjectPtr<USwcServerVisualSubsystem> Weak(this);
	const uint64 Serial = Visual.Serial;
	Visual.Load = Streamer.RequestAsyncLoad(Paths, [Weak, Interest, Serial]()
	{
		USwcServerVisualSubsystem* Self = Weak.Get();
		FVisual* Now = Self != nullptr ? Self->Visuals.Find(Interest) : nullptr;
		if (Now != nullptr && Now->Serial == Serial)
		{
			Self->Build(*Now);
		}
	});
}

void USwcServerVisualSubsystem::Build(FVisual& Visual)
{
	AActor* Actor = Visual.Actor.Get();
	if (Actor == nullptr)
	{
		return;
	}
	USceneComponent* Root = Actor->GetRootComponent();
	int32 Instances = 0;
	for (const FPart& Part : Slices[Visual.Key])
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(Part.Mesh.ResolveObject());
		if (Mesh == nullptr || Part.Transforms.IsEmpty())
		{
			continue;
		}
		// The same components as the cells (SwcEditorLibrary.cpp, SpawnInstanceActorRange): the render mesh with no collision, the
		// override materials by slot name, and the collision of the game as hidden instances of the collision mesh.
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Actor);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		if (Part.bDecal)
		{
			Component->SetCastShadow(false);   // a decal companion (the deferred decal subsets of the main mesh)
		}
		for (const TPair<FName, FSoftObjectPath>& Slot : Part.Slots)
		{
			const int32 Index = Mesh->GetMaterialIndex(Slot.Key);
			if (UMaterialInterface* Material = Cast<UMaterialInterface>(Slot.Value.ResolveObject()); Material && Index != INDEX_NONE)
			{
				Component->SetMaterial(Index, Material);
			}
		}
		Component->SetupAttachment(Root);
		Component->AddInstances(Part.Transforms, false, false, false);
		Component->RegisterComponent();
		Instances += Part.Transforms.Num();
		if (UStaticMesh* CollisionMesh = Cast<UStaticMesh>(Part.Collision.ResolveObject()); CollisionMesh && !Part.bDecal)
		{
			UInstancedStaticMeshComponent* Collision = NewObject<UInstancedStaticMeshComponent>(Actor);
			Collision->SetStaticMesh(CollisionMesh);
			Collision->SetVisibility(false);
			Collision->SetHiddenInGame(true);
			Collision->SetCastShadow(false);
			Collision->bAffectDistanceFieldLighting = false;
			Collision->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Collision->ComponentTags.Add(TEXT("SwcCollision"));
			Collision->SetupAttachment(Root);
			Collision->AddInstances(Part.Transforms, false, false, false);
			Collision->RegisterComponent();
		}
	}
	UE_LOG(LogSwcServerVisuals, Log, TEXT("server visual: %s ready, %d instance(s)"), *Visual.Key, Instances);
}
