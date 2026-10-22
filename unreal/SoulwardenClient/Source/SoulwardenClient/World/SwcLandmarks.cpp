#include "World/SwcLandmarks.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Environment/SwcEnvironmentSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WorldPartition/WorldPartition.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcLandmarks, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarLandmarks(TEXT("swc.Landmarks"), 1,
	TEXT("The landmarks of the world (the sky maelstrom of the Shattered Mountain): 1 shows them, 0 hides them, 2 hides only the fog cards of the column (the cost check)."));

FString LandmarkFile(const TCHAR* Name)
{
	FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	if (Content.IsEmpty())
	{
		FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
		if (Work.IsEmpty())
		{
			Work = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden-client"));
		}
		Content = FPaths::Combine(Work, TEXT("content"));
	}
	return FPaths::Combine(Content, TEXT("landmarks"), Name);
}

FVector ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Object == nullptr || !Object->TryGetArrayField(Field, Values) || Values->Num() < 3)
	{
		return FVector::ZeroVector;
	}
	return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
}

/** @return The Unreal transform of a record of space.unreal_transform (location cm, rotation x y z w, scale). */
FTransform ReadTransform(const TSharedPtr<FJsonObject>& Object)
{
	const TArray<TSharedPtr<FJsonValue>>* Rotation = nullptr;
	FQuat Quat = FQuat::Identity;
	if (Object != nullptr && Object->TryGetArrayField(TEXT("rotation"), Rotation) && Rotation->Num() == 4)
	{
		Quat = FQuat((*Rotation)[0]->AsNumber(), (*Rotation)[1]->AsNumber(), (*Rotation)[2]->AsNumber(), (*Rotation)[3]->AsNumber());
	}
	return FTransform(Quat, ReadVector(Object, TEXT("location")), ReadVector(Object, TEXT("scale")));
}

/** @return The asset of a path, or the first asset of the class under a folder (Interchange puts a mesh into SCENE/StaticMeshes/). */
template <typename T>
T* LoadFirst(const FString& Path)
{
	if (T* Object = LoadObject<T>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn))
	{
		return Object;
	}
	// The path of an Interchange import of one glTF: FOLDER/NAME/StaticMeshes/NAME (NAME: the last part of the folder).
	const FString Name = FPaths::GetBaseFilename(Path);
	if (T* Object = LoadObject<T>(nullptr, *FString::Printf(TEXT("%s/%s/StaticMeshes/%s.%s"), *Path, *Name, *Name, *Name), nullptr,
		LOAD_Quiet | LOAD_NoWarn))
	{
		return Object;
	}
	TArray<FAssetData> Found;
	IAssetRegistry::GetChecked().GetAssetsByPath(FName(*Path), Found, true);
	for (const FAssetData& Data : Found)
	{
		if (Data.IsInstanceOf(T::StaticClass()))
		{
			return Cast<T>(Data.GetAsset());
		}
	}
	return nullptr;
}
} // namespace

bool USwcLandmarks::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcLandmarks::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcLandmarks, STATGROUP_Tickables);
}

void USwcLandmarks::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	// The open world only (a World Partition level; not the menu scene).
	if (World.GetWorldPartition() == nullptr || FParse::Param(FCommandLine::Get(), TEXT("SwcNoLandmarks")))
	{
		return;
	}
	Build();
}

void USwcLandmarks::Build()
{
	const FString Path = LandmarkFile(TEXT("maelstrom.json"));
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		UE_LOG(LogSwcLandmarks, Log, TEXT("landmarks: no %s (python3 -B -m swc_convert.landmarks write; tools/import-landmarks.sh)"), *Path);
		return;
	}
	const FString Folder = Doc->GetStringField(TEXT("folder"));
	const double RangeCm = Doc->GetNumberField(TEXT("draw range m")) * 100.0;
	const TSharedPtr<FJsonObject>* Rotation = nullptr;
	if (Doc->TryGetObjectField(TEXT("rotation"), Rotation))
	{
		StepSeconds = FMath::Max((*Rotation)->GetNumberField(TEXT("step seconds")), 0.001);
		StepDegrees = (*Rotation)->GetNumberField(TEXT("step degrees"));
	}

	FActorSpawnParameters Parameters;
	Parameters.Name = MakeUniqueObjectName(GetWorld(), AActor::StaticClass(), TEXT("SwcLandmarkMaelstrom"));
	Parameters.ObjectFlags |= RF_Transient;
	AActor* Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	if (Actor == nullptr)
	{
		return;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Owner = Actor;

	// The two pivots at their world transforms (the source m_worldTM of the slice x the capital matrix).
	TMap<FString, USceneComponent*> Pivots;
	TMap<FString, FTransform> PivotWorld;
	const TSharedPtr<FJsonObject>* PivotObjects = nullptr;
	if (Doc->TryGetObjectField(TEXT("pivots"), PivotObjects))
	{
		for (const auto& Pair : (*PivotObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Pivot = Pair.Value->AsObject();
			USceneComponent* Component = NewObject<USceneComponent>(Actor, *FString::Printf(TEXT("Pivot_%s"), *FString(Pair.Key)));
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetupAttachment(Root);
			const FTransform World = ReadTransform(Pivot->GetObjectField(TEXT("world")));
			Component->SetRelativeTransform(World);
			Component->RegisterComponent();
			Pivots.Add(FString(Pair.Key), Component);
			PivotWorld.Add(FString(Pair.Key), World);
		}
	}
	// The fog cards of the column (AST-265): their saved world transforms already hold their parents (Fog_Center): under the root.
	Pivots.Add(TEXT("root"), Root);
	PivotWorld.Add(TEXT("root"), Root->GetComponentTransform());
	if (Rotation != nullptr)
	{
		if (USceneComponent** Pivot = Pivots.Find((*Rotation)->GetStringField(TEXT("pivot"))))
		{
			Turning = *Pivot;
		}
	}

	// The parts: each one under its pivot, with the transform relative to the pivot (the original child offsets).
	const TSharedPtr<FJsonObject> Meshes = Doc->GetObjectField(TEXT("meshes"));
	int32 Placed = 0;
	for (const TSharedPtr<FJsonValue>& Value : Doc->GetArrayField(TEXT("parts")))
	{
		const TSharedPtr<FJsonObject> Part = Value->AsObject();
		const FString PivotKey = Part->GetStringField(TEXT("pivot"));
		USceneComponent** Pivot = Pivots.Find(PivotKey);
		const TSharedPtr<FJsonObject>* MeshRecord = nullptr;
		if (Pivot == nullptr || !Meshes->TryGetObjectField(Part->GetStringField(TEXT("mesh")), MeshRecord))
		{
			continue;
		}
		UStaticMesh* Mesh = LoadFirst<UStaticMesh>((*MeshRecord)->GetStringField(TEXT("unreal")));
		const FString Material = Part->GetStringField(TEXT("material"));
		const FString Stem = FPaths::GetBaseFilename(Material);
		UMaterialInterface* Instance = LoadFirst<UMaterialInterface>(FString::Printf(TEXT("%s/Materials/MI_%s.MI_%s"), *Folder, *Stem, *Stem));
		if (Mesh == nullptr || Instance == nullptr)
		{
			UE_LOG(LogSwcLandmarks, Warning, TEXT("landmarks: part %s: mesh %s, material %s (tools/import-landmarks.sh)"),
				*Part->GetStringField(TEXT("name")), Mesh ? TEXT("ok") : TEXT("missing"), Instance ? TEXT("ok") : TEXT("missing"));
			continue;
		}
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor, *FString::Printf(TEXT("%s_%d"), *Part->GetStringField(TEXT("name")), Placed));
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		for (int32 Slot = 0; Slot < FMath::Max(Mesh->GetStaticMaterials().Num(), 1); ++Slot)
		{
			Component->SetMaterial(Slot, Instance);
		}
		Component->SetCastShadow(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		Component->bAffectDistanceFieldLighting = false;
		Component->bVisibleInRayTracing = false;
		Component->SetCullDistance(RangeCm);   // MaxViewDistance 10000 m, UseManualViewDistance (the source render node)
		Component->bNeverDistanceCull = false;
		Component->SetupAttachment(*Pivot);
		Component->SetRelativeTransform(ReadTransform(Part->GetObjectField(TEXT("world"))).GetRelativeTransform(PivotWorld[PivotKey]));
		Component->RegisterComponent();
		if (PivotKey == TEXT("root"))
		{
			Cards.Add(Component);
		}
		++Placed;
	}
	Collection = LoadObject<UMaterialParameterCollection>(nullptr, *FString::Printf(TEXT("%s/Materials/MPC_SwcLandmarks.MPC_SwcLandmarks"), *Folder),
		nullptr, LOAD_Quiet | LOAD_NoWarn);
	UE_LOG(LogSwcLandmarks, Log, TEXT("landmarks: the maelstrom with %d parts (turning pivot %s, %.6f degrees each %.3f s, range %.0f m)"), Placed,
		Turning.IsValid() ? *Turning->GetName() : TEXT("none"), StepDegrees, StepSeconds, RangeCm / 100.0);
}

void USwcLandmarks::Tick(float DeltaTime)
{
	AActor* Actor = Owner.Get();
	if (Actor == nullptr)
	{
		return;
	}
	const int32 Mode = CVarLandmarks.GetValueOnGameThread();
	if (Mode != ShownMode)
	{
		ShownMode = Mode;
		Actor->SetActorHiddenInGame(Mode == 0);
		for (const TWeakObjectPtr<UPrimitiveComponent>& Card : Cards)
		{
			if (Card.IsValid())
			{
				Card->SetVisibility(Mode != 2);
			}
		}
	}
	UpdateLight();
	// The script rule of the source (transformovertime.luac): one step when the accumulator passes the interval, then it resets to 0
	// (no delta time in the step; a long frame gives one step).
	Accumulator += DeltaTime;
	if (Accumulator >= StepSeconds && Turning.IsValid())
	{
		Accumulator = 0.0;
		Turning->AddLocalRotation(FRotator(0.0, StepDegrees, 0.0));
	}
}

void USwcLandmarks::UpdateLight()
{
	UMaterialParameterCollectionInstance* Instance = Collection ? GetWorld()->GetParameterCollectionInstance(Collection) : nullptr;
	const USwcEnvironmentSubsystem* Environment = GetWorld()->GetSubsystem<USwcEnvironmentSubsystem>();
	const UDirectionalLightComponent* Light = Environment ? Environment->GetSkyLightSource() : nullptr;
	if (Instance == nullptr || Light == nullptr)
	{
		return;
	}
	// The source light on the ground (swc.SunSource 1: color x lux); else the Unreal light of the environment (its intensity in lux).
	FLinearColor Color = Environment->GetSourceSunLight();
	if (Color.A < 0.5f)
	{
		Color = Light->GetLightColor() * Light->Intensity;
	}
	Color.A = 1.0f;
	const FVector Toward = -Light->GetForwardVector();
	Instance->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(Toward.X, Toward.Y, Toward.Z, 0.0f));
	Instance->SetVectorParameterValue(TEXT("SunColor"), Color);
}
