#include "SwcEditorLibrary.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "TextureResource.h"
#include "GameFramework/WorldSettings.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"

void USwcEditorLibrary::FinishCompilation()
{
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	FAssetCompilingManager::Get().ProcessAsyncTasks();
	IStreamingManager::Get().StreamAllResources(10.0f);
	FlushRenderingCommands();
}

bool USwcEditorLibrary::CaptureToPng(USceneCaptureComponent2D* Capture, const FString& Path, int32 Frames)
{
	if (!Capture || !Capture->TextureTarget)
	{
		return false;
	}
	// The first frames ask for the shaders and the textures of the view: compile them, then render the picture.
	for (int32 Warmup = 0; Warmup < 3; ++Warmup)
	{
		Capture->CaptureScene();
		FlushRenderingCommands();
		FinishCompilation();
	}
	for (int32 Frame = 0; Frame < FMath::Max(Frames, 1); ++Frame)
	{
		Capture->CaptureScene();
		FlushRenderingCommands();
	}
	FTextureRenderTargetResource* Resource = Capture->TextureTarget->GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.IsEmpty())
	{
		return false;
	}
	const int32 Width = Capture->TextureTarget->SizeX;
	const int32 Height = Capture->TextureTarget->SizeY;
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}
	IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TSharedPtr<IImageWrapper> Png = Images.CreateImageWrapper(EImageFormat::PNG);
	if (!Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
	{
		return false;
	}
	return FFileHelper::SaveArrayToFile(Png->GetCompressed(), *Path);
}

AActor* USwcEditorLibrary::SpawnActorOfClass(TSubclassOf<AActor> Class, const FTransform& Transform, const FString& Label)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || !Class)
	{
		return nullptr;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(Class, Transform, Parameters);
	if (Actor && !Label.IsEmpty())
	{
		Actor->SetActorLabel(Label);
	}
	return Actor;
}

AActor* USwcEditorLibrary::SpawnMeshActor(UStaticMesh* Mesh, const FTransform& Transform, const FString& Label)
{
	AStaticMeshActor* Actor = Cast<AStaticMeshActor>(SpawnActorOfClass(AStaticMeshActor::StaticClass(), Transform, Label));
	if (Actor && Mesh)
	{
		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Actor->SetActorTransform(Transform);
	}
	return Actor;
}

AActor* USwcEditorLibrary::CreateLandscape(const FString& RawFile, int32 ComponentsPerSide, int32 QuadsPerSection, const FVector& Location,
	const FVector& Scale, UMaterialInterface* Material, const FString& Label, int32 GridSize, bool bAlwaysLoaded)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	const int32 Size = ComponentsPerSide * QuadsPerSection + 1;
	TArray<uint8> Bytes;
	if (!World || !FFileHelper::LoadFileToArray(Bytes, *RawFile) || Bytes.Num() != Size * Size * 2)
	{
		return nullptr;
	}
	TArray<uint16> Heights;
	Heights.SetNumUninitialized(Size * Size);
	FMemory::Memcpy(Heights.GetData(), Bytes.GetData(), Bytes.Num());   // little-endian, as the platform
	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator);
	Landscape->LandscapeMaterial = Material;
	Landscape->SetActorRelativeScale3D(Scale);
	Landscape->Import(FGuid::NewGuid(), 0, 0, Size - 1, Size - 1, 1, QuadsPerSection, HeightDataPerLayers, nullptr,
		MaterialLayerDataPerLayers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
	if (!Label.IsEmpty())
	{
		Landscape->SetActorLabel(Label);
	}
	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (Info)
	{
		Info->UpdateLayerInfoMap(Landscape);
		ULandscapeSubsystem* Landscapes = World->GetSubsystem<ULandscapeSubsystem>();
		if (Landscapes && Landscapes->IsGridBased())
		{
			Landscapes->ChangeGridSize(Info, static_cast<uint32>(FMath::Max(GridSize, 1)));
		}
		Info->ForEachLandscapeProxy([&Label, bAlwaysLoaded](ALandscapeProxy* Proxy)
		{
			if (bAlwaysLoaded)
			{
				Proxy->SetIsSpatiallyLoaded(false);
			}
			if (!Label.IsEmpty() && Proxy->IsA<ALandscapeStreamingProxy>())
			{
				Proxy->SetActorLabel(Label + TEXT(" proxy"));
			}
			return true;
		});
	}
	return Landscape;
}

bool USwcEditorLibrary::ConfigureStreaming(int32 CellSize, int32 LoadingRange)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UWorldPartition* Partition = World ? World->GetWorldPartition() : nullptr;
	UObject* Hash = Partition ? Partition->RuntimeHash.Get() : nullptr;
	FArrayProperty* Partitions = Hash ? FindFProperty<FArrayProperty>(Hash->GetClass(), TEXT("RuntimePartitions")) : nullptr;
	FStructProperty* Desc = Partitions ? CastField<FStructProperty>(Partitions->Inner) : nullptr;
	if (!Desc)
	{
		return false;
	}
	FScriptArrayHelper Array(Partitions, Partitions->ContainerPtrToValuePtr<void>(Hash));
	FObjectProperty* MainLayer = FindFProperty<FObjectProperty>(Desc->Struct, TEXT("MainLayer"));
	bool bChanged = false;
	Hash->Modify();
	for (int32 Index = 0; Index < Array.Num() && MainLayer; ++Index)
	{
		UObject* Layer = MainLayer->GetObjectPropertyValue_InContainer(Array.GetRawPtr(Index));
		if (!Layer)
		{
			continue;
		}
		Layer->Modify();
		if (FUInt32Property* Size = FindFProperty<FUInt32Property>(Layer->GetClass(), TEXT("CellSize")))
		{
			Size->SetPropertyValue_InContainer(Layer, static_cast<uint32>(CellSize));
		}
		if (FIntProperty* Range = FindFProperty<FIntProperty>(Layer->GetClass(), TEXT("LoadingRange")))
		{
			Range->SetPropertyValue_InContainer(Layer, LoadingRange);
			bChanged = true;
		}
	}
	if (bChanged)
	{
		Hash->MarkPackageDirty();
		World->GetWorldSettings()->MarkPackageDirty();
	}
	return bChanged;
}

namespace
{
/** A reader of the little-endian file of the instance actors. */
struct FSwcReader
{
	const TArray<uint8>& Bytes;
	int64 Offset = 0;
	bool bFailed = false;

	template <typename T>
	T Read()
	{
		T Value{};
		if (Offset + static_cast<int64>(sizeof(T)) > Bytes.Num())
		{
			bFailed = true;
			return Value;
		}
		FMemory::Memcpy(&Value, Bytes.GetData() + Offset, sizeof(T));
		Offset += sizeof(T);
		return Value;
	}

	FString Text()
	{
		const uint16 Length = Read<uint16>();
		if (bFailed || Offset + Length > Bytes.Num())
		{
			bFailed = true;
			return FString();
		}
		FString Out = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Offset), Length));
		Offset += Length;
		return Out;
	}
};

UStaticMesh* FindStaticMesh(const FString& Folder)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Found;
	Registry.GetAssetsByPath(FName(*Folder), Found, true);
	for (const FAssetData& Data : Found)
	{
		if (Data.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName())
		{
			return Cast<UStaticMesh>(Data.GetAsset());
		}
	}
	return nullptr;
}
} // namespace

int32 USwcEditorLibrary::SpawnInstanceActors(const FString& File)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TArray<uint8> Bytes;
	if (!World || !FFileHelper::LoadFileToArray(Bytes, *File) || Bytes.Num() < 12 || FMemory::Memcmp(Bytes.GetData(), "SWCI", 4) != 0)
	{
		return -1;
	}
	FSwcReader Reader{Bytes, 4};
	if (Reader.Read<uint32>() != 1)
	{
		return -1;
	}
	TArray<UStaticMesh*> Meshes;
	const uint32 MeshCount = Reader.Read<uint32>();
	for (uint32 Index = 0; Index < MeshCount && !Reader.bFailed; ++Index)
	{
		Meshes.Add(FindStaticMesh(Reader.Text()));
	}
	const uint32 ActorCount = Reader.Read<uint32>();
	int32 Spawned = 0;
	for (uint32 ActorIndex = 0; ActorIndex < ActorCount && !Reader.bFailed; ++ActorIndex)
	{
		const FString Label = Reader.Text();
		FVector Location;
		Location.X = Reader.Read<double>();
		Location.Y = Reader.Read<double>();
		Location.Z = Reader.Read<double>();
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Parameters);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"), RF_Transactional);
		Root->SetMobility(EComponentMobility::Static);
		Actor->SetRootComponent(Root);
		Actor->AddInstanceComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		const uint32 GroupCount = Reader.Read<uint32>();
		for (uint32 Group = 0; Group < GroupCount && !Reader.bFailed; ++Group)
		{
			const uint32 MeshIndex = Reader.Read<uint32>();
			const uint32 Count = Reader.Read<uint32>();
			TArray<FTransform> Transforms;
			Transforms.Reserve(Count);
			for (uint32 Instance = 0; Instance < Count && !Reader.bFailed; ++Instance)
			{
				FVector Translation;
				Translation.X = Reader.Read<double>();
				Translation.Y = Reader.Read<double>();
				Translation.Z = Reader.Read<double>();
				FQuat Rotation;
				Rotation.X = Reader.Read<float>();
				Rotation.Y = Reader.Read<float>();
				Rotation.Z = Reader.Read<float>();
				Rotation.W = Reader.Read<float>();
				FVector Scale3D;
				Scale3D.X = Reader.Read<float>();
				Scale3D.Y = Reader.Read<float>();
				Scale3D.Z = Reader.Read<float>();
				Transforms.Emplace(Rotation, Translation, Scale3D);
			}
			UStaticMesh* Mesh = MeshIndex < static_cast<uint32>(Meshes.Num()) ? Meshes[MeshIndex] : nullptr;
			if (!Mesh || Transforms.IsEmpty())
			{
				continue;
			}
			UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Actor, NAME_None, RF_Transactional);
			Component->SetMobility(EComponentMobility::Static);
			Component->SetStaticMesh(Mesh);
			Component->SetupAttachment(Root);
			Actor->AddInstanceComponent(Component);
			Component->RegisterComponent();
			Component->AddInstances(Transforms, false, true);
		}
		if (!Label.IsEmpty())
		{
			Actor->SetActorLabel(Label);
		}
		++Spawned;
	}
	return Reader.bFailed ? -1 : Spawned;
}

void USwcEditorLibrary::SetSpatiallyLoaded(AActor* Actor, bool bSpatiallyLoaded)
{
	if (Actor)
	{
		Actor->SetIsSpatiallyLoaded(bSpatiallyLoaded);
	}
}
