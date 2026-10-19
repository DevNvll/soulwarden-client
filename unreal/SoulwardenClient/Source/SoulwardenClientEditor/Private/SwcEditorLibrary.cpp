#include "SwcEditorLibrary.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Editor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
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
	return CreateLandscapeWithLayers(RawFile, ComponentsPerSide, QuadsPerSection, Location, Scale, Material, Label, GridSize,
		bAlwaysLoaded, TArray<FString>(), TArray<FString>(), FString());
}

namespace
{
/** @return The layer info asset Folder/LI_NAME (made and saved when it does not exist): no weight blend. */
ULandscapeLayerInfoObject* FindOrCreateLayerInfo(const FString& Folder, const FString& LayerName)
{
	const FString AssetName = TEXT("LI_") + LayerName;
	const FString PackageName = Folder / AssetName;
	if (ULandscapeLayerInfoObject* Existing = LoadObject<ULandscapeLayerInfoObject>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr,
		LOAD_Quiet | LOAD_NoWarn))
	{
		return Existing;
	}
	UPackage* Package = CreatePackage(*PackageName);
	ULandscapeLayerInfoObject* Info = NewObject<ULandscapeLayerInfoObject>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	Info->SetLayerName(FName(*LayerName), false);
	Info->SetBlendMethod(ELandscapeTargetLayerBlendMethod::None, false);
	FAssetRegistryModule::AssetCreated(Info);
	Package->MarkPackageDirty();
	FSavePackageArgs Arguments;
	Arguments.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Info, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), Arguments);
	return Info;
}
} // namespace

UTexture2DArray* USwcEditorLibrary::CreateTextureArray(const FString& PackagePath, const TArray<UTexture2D*>& Slices,
	TEnumAsByte<TextureCompressionSettings> Compression, bool bSRGB)
{
	if (Slices.IsEmpty() || Slices.Contains(nullptr))
	{
		return nullptr;
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	UTexture2DArray* Array = FindObject<UTexture2DArray>(Package, *AssetName);
	if (!Array)
	{
		Array = NewObject<UTexture2DArray>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Array);
	}
	Array->PreEditChange(nullptr);
	Array->SourceTextures.Reset();
	for (UTexture2D* Slice : Slices)
	{
		Array->SourceTextures.Add(Slice);
	}
	Array->CompressionSettings = Compression;
	Array->SRGB = bSRGB;
	if (!Array->CheckArrayTexturesCompatibility() || !Array->UpdateSourceFromSourceTextures(true))
	{
		UE_LOG(LogTemp, Warning, TEXT("SWC texture array %s: the slices are not compatible (size or source format)"), *PackagePath);
		return nullptr;
	}
	Array->PostEditChange();
	Package->MarkPackageDirty();
	FSavePackageArgs Arguments;
	Arguments.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Array, *FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension()), Arguments);
	return Array;
}

AActor* USwcEditorLibrary::CreateLandscapeWithLayers(const FString& RawFile, int32 ComponentsPerSide, int32 QuadsPerSection,
	const FVector& Location, const FVector& Scale, UMaterialInterface* Material, const FString& Label, int32 GridSize,
	bool bAlwaysLoaded, const TArray<FString>& LayerNames, const TArray<FString>& LayerFiles, const FString& LayerInfoFolder)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	const int32 Size = ComponentsPerSide * QuadsPerSection + 1;
	TArray<uint8> Bytes;
	if (!World || !FFileHelper::LoadFileToArray(Bytes, *RawFile) || Bytes.Num() != Size * Size * 2 || LayerNames.Num() != LayerFiles.Num())
	{
		return nullptr;
	}
	TArray<uint16> Heights;
	Heights.SetNumUninitialized(Size * Size);
	FMemory::Memcpy(Heights.GetData(), Bytes.GetData(), Bytes.Num());   // little-endian, as the platform
	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));
	// The target layers: the raw weights on the vertex grid (the same rows and columns as the heights).
	TArray<FLandscapeImportLayerInfo> ImportLayers;
	for (int32 Index = 0; Index < LayerNames.Num(); ++Index)
	{
		FLandscapeImportLayerInfo Layer{FName(*LayerNames[Index])};
		if (!FFileHelper::LoadFileToArray(Layer.LayerData, *LayerFiles[Index]) || Layer.LayerData.Num() != Size * Size)
		{
			UE_LOG(LogTemp, Warning, TEXT("SWC landscape: the weight file %s has not %d x %d bytes"), *LayerFiles[Index], Size, Size);
			return nullptr;
		}
		Layer.LayerInfo = FindOrCreateLayerInfo(LayerInfoFolder, LayerNames[Index]);
		Layer.SourceFilePath = LayerFiles[Index];
		ImportLayers.Add(MoveTemp(Layer));
	}
	TArray<ULandscapeLayerInfoObject*> LayerInfos;
	for (const FLandscapeImportLayerInfo& Layer : ImportLayers)
	{
		LayerInfos.Add(Layer.LayerInfo);
	}
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	MaterialLayerDataPerLayers.Add(FGuid(), MoveTemp(ImportLayers));

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
		// The target layers of the Landscape (the steps of the New Landscape tool after its import).
		for (ULandscapeLayerInfoObject* LayerInfo : LayerInfos)
		{
			Landscape->AddTargetLayer(LayerInfo->GetLayerName(), FLandscapeTargetLayerSettings(LayerInfo));
			const int32 LayerIndex = Info->GetLayerInfoIndex(LayerInfo->GetLayerName());
			if (LayerIndex != INDEX_NONE)
			{
				Info->Layers[LayerIndex].LayerInfoObj = LayerInfo;
			}
		}
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
	return SpawnInstanceActorRange(File, 0, MAX_int32);
}

int32 USwcEditorLibrary::CountInstanceActors(const FString& File)
{
	return SpawnInstanceActorRange(File, 0, 0);
}

int32 USwcEditorLibrary::SpawnInstanceActorRange(const FString& File, int32 FirstActor, int32 MaxActors)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TArray<uint8> Bytes;
	if (!World || !FFileHelper::LoadFileToArray(Bytes, *File) || Bytes.Num() < 12 || FMemory::Memcmp(Bytes.GetData(), "SWCI", 4) != 0)
	{
		return -1;
	}
	FSwcReader Reader{Bytes, 4};
	const uint32 Version = Reader.Read<uint32>();
	if (Version < 1 || Version > 3)
	{
		return -1;
	}
	// One entry for each group (mesh, material override, wind) of the file, or a decal companion (version 3; version 2:
	// no flags; version 1: the mesh alone). An entry loads its assets only when an actor of the range uses it: a region
	// has thousands of meshes, a call spawns a few actors, and the count loads nothing.
	TArray<FString> Folders;
	TArray<TArray<TPair<FString, FString>>> SlotKeys;   // (slot name, material key) of each entry
	TArray<uint8> Flags;   // cells.py: bit 0 the wind (receiveWind of the render node), bit 1 a decal companion
	const uint32 MeshCount = Reader.Read<uint32>();
	for (uint32 Index = 0; Index < MeshCount && !Reader.bFailed; ++Index)
	{
		Folders.Add(Reader.Text());
		TArray<TPair<FString, FString>>& Slots = SlotKeys.AddDefaulted_GetRef();
		const uint16 SlotCount = Version >= 2 ? Reader.Read<uint16>() : 0;
		for (uint16 Slot = 0; Slot < SlotCount && !Reader.bFailed; ++Slot)
		{
			FString SlotName = Reader.Text();
			Slots.Emplace(MoveTemp(SlotName), Reader.Text());
		}
		Flags.Add(Version >= 3 ? Reader.Read<uint8>() : 0);
	}
	TArray<bool> Resolved;
	Resolved.Init(false, Folders.Num());
	TArray<UStaticMesh*> Meshes;
	Meshes.Init(nullptr, Folders.Num());
	TArray<UStaticMesh*> CollisionMeshes;   // the collision of the game of each mesh (P4.1, design 2), or none
	CollisionMeshes.Init(nullptr, Folders.Num());
	TArray<TArray<TPair<int32, UMaterialInterface*>>> SlotMaterials;   // the slots that the override changes
	SlotMaterials.SetNum(Folders.Num());
	TMap<FString, UMaterialInterface*> MaterialCache;
	int32 MissingSlots = 0;
	int32 Checked = 0;
	int32 Misplaced = 0;   // the components with a first instance away from its place in the file (more than 1 cm)
	int32 BadBounds = 0;   // the components with bounds that do not hold their first instance (before UpdateBounds)
	int32 StillBad = 0;   // the same after UpdateBounds
	int32 WideActors = 0;   // the actors with a bounds box wider than 2 km (a cell is 256 m)
	auto Resolve = [&](int32 Index)
	{
		if (Resolved[Index])
		{
			return;
		}
		Resolved[Index] = true;
		UStaticMesh* Mesh = FindStaticMesh(Folders[Index]);
		Meshes[Index] = Mesh;
		// import_content.py section 6: /SwcGameContent/Collision/FOLDER_NAME/C_MESH_NAME.
		if (!(Flags[Index] & 2))   // a decal companion has no collision
		{
			CollisionMeshes[Index] = FindStaticMesh(TEXT("/SwcGameContent/Collision/") + FPaths::GetCleanFilename(Folders[Index]));
		}
		for (const TPair<FString, FString>& Slot : SlotKeys[Index])
		{
			UMaterialInterface*& Material = MaterialCache.FindOrAdd(Slot.Value);
			if (!Material)
			{
				// import_content.py: the material instance of a material record is /SwcGameContent/Materials/KEY.
				Material = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/SwcGameContent/Materials/%s.%s"), *Slot.Value, *Slot.Value), nullptr, LOAD_Quiet | LOAD_NoWarn);
			}
			const int32 MaterialIndex = Mesh ? Mesh->GetMaterialIndex(FName(*Slot.Key)) : INDEX_NONE;
			if (Material && MaterialIndex != INDEX_NONE)
			{
				SlotMaterials[Index].Emplace(MaterialIndex, Material);
			}
			else
			{
				++MissingSlots;
			}
		}
	};
	// The bounds of a component at the spawn time: World Partition streams a cell actor by the bounds of its components.
	// While the mesh of a component compiles (async), UpdateBounds waits and the bounds stay at the world origin; the
	// saved actor has the right bounds (build_world.py check_bounds reads them in a later call). The count shows how many
	// components the save must wait for.
	auto CheckBounds = [&](UInstancedStaticMeshComponent* Checked, const FTransform& FirstInstance)
	{
		const FVector Place = FirstInstance.GetLocation();
		if (Checked->Bounds.GetBox().ExpandBy(100.0).IsInside(Place))
		{
			return;
		}
		++BadBounds;
		Checked->UpdateBounds();
		StillBad += !Checked->Bounds.GetBox().ExpandBy(100.0).IsInside(Place);
	};
	const uint32 ActorCount = Reader.Read<uint32>();
	if (MaxActors == 0)
	{
		return Reader.bFailed ? -1 : static_cast<int32>(ActorCount);   // CountInstanceActors
	}
	int32 Spawned = 0;
	for (uint32 ActorIndex = 0; ActorIndex < ActorCount && !Reader.bFailed; ++ActorIndex)
	{
		const FString Label = Reader.Text();
		FVector Location;
		Location.X = Reader.Read<double>();
		Location.Y = Reader.Read<double>();
		Location.Z = Reader.Read<double>();
		if (static_cast<int32>(ActorIndex) < FirstActor || Spawned >= MaxActors)
		{
			// Skip this actor: read past its groups (the range of a call).
			const uint32 Skip = Reader.Read<uint32>();
			for (uint32 Group = 0; Group < Skip && !Reader.bFailed; ++Group)
			{
				Reader.Read<uint32>();
				const uint32 Count = Reader.Read<uint32>();
				Reader.Offset += static_cast<int64>(Count) * (3 * sizeof(double) + 7 * sizeof(float));
				Reader.bFailed |= Reader.Offset > Bytes.Num();
			}
			continue;
		}
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
			if (MeshIndex >= static_cast<uint32>(Folders.Num()) || Transforms.IsEmpty())
			{
				continue;
			}
			Resolve(static_cast<int32>(MeshIndex));
			UStaticMesh* Mesh = Meshes[MeshIndex];
			if (!Mesh)
			{
				continue;
			}
			UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Actor, NAME_None, RF_Transactional);
			Component->SetMobility(EComponentMobility::Static);
			Component->SetStaticMesh(Mesh);
			Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);   // the collision is below
			if (Flags[MeshIndex] & 1)
			{
				// The wind of the vegetation material: custom primitive data 0 "SwcWind" (E1, the materials track).
				Component->SetDefaultCustomPrimitiveDataFloat(0, 1.0f);
			}
			if (Flags[MeshIndex] & 2)
			{
				Component->SetCastShadow(false);   // a decal companion (the deferred decal subsets of the main mesh)
			}
			for (const TPair<int32, UMaterialInterface*>& Slot : SlotMaterials[MeshIndex])
			{
				Component->SetMaterial(Slot.Key, Slot.Value);   // the material override of the placements (P4.1)
			}
			Component->SetupAttachment(Root);
			Actor->AddInstanceComponent(Component);
			// The instances before the registration: the render and physics state are made once, with all instances.
			// (World-space transforms need the component transform: the root is at the actor location.)
			Component->SetWorldTransform(Root->GetComponentTransform());
			Component->AddInstances(Transforms, false, true, false);
			Component->RegisterComponent();
			FTransform First;
			++Checked;
			if (!Component->GetInstanceTransform(0, First, true) || !First.GetLocation().Equals(Transforms[0].GetLocation(), 1.0))
			{
				++Misplaced;
			}
			CheckBounds(Component, Transforms[0]);
			UStaticMesh* CollisionMesh = CollisionMeshes[MeshIndex];
			if (!CollisionMesh)
			{
				continue;   // the game has no collision for this mesh
			}
			// The collision of the game: hidden instances of the collision-only mesh at the same transforms.
			UInstancedStaticMeshComponent* Collision = NewObject<UInstancedStaticMeshComponent>(Actor, NAME_None, RF_Transactional);
			Collision->SetMobility(EComponentMobility::Static);
			Collision->SetStaticMesh(CollisionMesh);
			Collision->SetVisibility(false);
			Collision->SetHiddenInGame(true);
			Collision->SetCastShadow(false);
			Collision->bAffectDistanceFieldLighting = false;
			Collision->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Collision->ComponentTags.Add(TEXT("SwcCollision"));
			Collision->SetupAttachment(Root);
			Actor->AddInstanceComponent(Collision);
			Collision->SetWorldTransform(Root->GetComponentTransform());
			Collision->AddInstances(Transforms, false, true, false);
			Collision->RegisterComponent();
			CheckBounds(Collision, Transforms[0]);
		}
		if (!Label.IsEmpty())
		{
			Actor->SetActorLabel(Label);
		}
		const FBox ActorBox = Actor->GetComponentsBoundingBox(true);
		WideActors += ActorBox.IsValid && FMath::Max(ActorBox.GetSize().X, ActorBox.GetSize().Y) > 200000.0;
		++Spawned;
	}
	if (Checked > 0)
	{
		UE_LOG(LogTemp, Display, TEXT("SWC cells: %d render components, %d with a misplaced first instance (%s)"), Checked, Misplaced, *File);
		UE_LOG(LogTemp, Display, TEXT("SWC cells: %d components with bounds away from their first instance, %d of them after UpdateBounds; %d actors wider than 2 km (%s)"), BadBounds, StillBad, WideActors, *File);
	}
	if (MissingSlots > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SWC cells: %d override slots with no slot or no material instance (%s)"), MissingSlots, *File);
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

int32 USwcEditorLibrary::RemoveInstanceComponentsOf(AActor* Actor, const TArray<FString>& MeshFolders)
{
	if (!Actor)
	{
		return 0;
	}
	TArray<UInstancedStaticMeshComponent*> Components;
	Actor->GetComponents(Components);
	int32 Removed = 0;
	for (UInstancedStaticMeshComponent* Component : Components)
	{
		const UStaticMesh* Mesh = Component->GetStaticMesh();
		const FString Path = Mesh ? Mesh->GetPathName() : FString();
		if (!MeshFolders.ContainsByPredicate([&Path](const FString& Folder) { return Path.StartsWith(Folder + TEXT("/")); }))
		{
			continue;
		}
		Actor->Modify();
		Actor->RemoveInstanceComponent(Component);
		Component->DestroyComponent();
		++Removed;
	}
	return Removed;
}

namespace
{
/** Reads a collision file of the converter: the vertices (engine m) and the triangles (engine winding). */
bool ReadCollisionFile(const FString& File, TArray<FVector3f>& OutVertices, TArray<uint32>& OutIndices)
{
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *File) || Data.Num() < 16 || FMemory::Memcmp(Data.GetData(), "SWCC", 4) != 0)
	{
		return false;
	}
	uint32 Header[3];
	FMemory::Memcpy(Header, Data.GetData() + 4, sizeof(Header));
	const int64 VertexCount = Header[1];
	const int64 TriangleCount = Header[2];
	if (Header[0] != 1 || Data.Num() != 16 + VertexCount * 12 + TriangleCount * 12)
	{
		return false;
	}
	OutVertices.SetNumUninitialized(VertexCount);
	OutIndices.SetNumUninitialized(TriangleCount * 3);
	FMemory::Memcpy(OutVertices.GetData(), Data.GetData() + 16, VertexCount * 12);
	FMemory::Memcpy(OutIndices.GetData(), Data.GetData() + 16 + VertexCount * 12, TriangleCount * 12);
	return true;
}
} // namespace

bool USwcEditorLibrary::SetGameCollision(UStaticMesh* Mesh, const FString& File, const FString& CollisionAssetPath)
{
	if (!Mesh)
	{
		return false;
	}
	UStaticMesh* Collision = nullptr;
	if (!File.IsEmpty())
	{
		TArray<FVector3f> Vertices;
		TArray<uint32> Indices;
		if (!ReadCollisionFile(File, Vertices, Indices))
		{
			return false;
		}
		// The space map of stage 1.1: Unreal = diag(100, -100, 100) x engine. The corners keep their order: with the
		// mirror, the front of a triangle that faces out in the engine space faces out in Unreal too (the complex
		// collision ignores the back of a triangle; with the order turned, a line down went through each top face).
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		Description.ReserveNewVertices(Vertices.Num());
		for (const FVector3f& Vertex : Vertices)
		{
			const FVertexID Id = Description.CreateVertex();
			Positions[Id] = FVector3f(Vertex.X * 100.0f, -Vertex.Y * 100.0f, Vertex.Z * 100.0f);
		}
		const FPolygonGroupID Group = Description.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Collision");
		int32 Triangles = 0;
		for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
		{
			const uint32 A = Indices[Index], B = Indices[Index + 1], C = Indices[Index + 2];
			if (A >= uint32(Vertices.Num()) || B >= uint32(Vertices.Num()) || C >= uint32(Vertices.Num()) || A == B || B == C || A == C)
			{
				continue;   // a triangle with a repeated vertex has no area
			}
			FVertexInstanceID Corners[3] = {Description.CreateVertexInstance(FVertexID(A)), Description.CreateVertexInstance(FVertexID(B)),
				Description.CreateVertexInstance(FVertexID(C))};
			Description.CreateTriangle(Group, Corners);
			++Triangles;
		}
		if (Triangles == 0)
		{
			return false;
		}
		const FString AssetName = FPackageName::GetShortName(CollisionAssetPath);
		UPackage* Package = CreatePackage(*CollisionAssetPath);
		Collision = FindObject<UStaticMesh>(Package, *AssetName);
		if (!Collision)
		{
			Collision = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Collision);
		}
		Collision->Modify();
		if (Collision->GetNumSourceModels() == 0)
		{
			Collision->AddSourceModel();
		}
		FStaticMeshSourceModel& Source = Collision->GetSourceModel(0);
		Source.BuildSettings.bRecomputeNormals = true;
		Source.BuildSettings.bRecomputeTangents = false;
		Source.BuildSettings.bGenerateLightmapUVs = false;
		Source.BuildSettings.bBuildReversedIndexBuffer = false;
		Source.BuildSettings.DistanceFieldResolutionScale = 0.0f;   // never drawn: no distance field, no Lumen cards
		Source.BuildSettings.MaxLumenMeshCards = 0;
		Collision->bSupportRayTracing = false;
		Collision->CreateMeshDescription(0, MoveTemp(Description));
		Collision->CommitMeshDescription(0);
		FMeshNaniteSettings Nanite = Collision->GetNaniteSettings();
		Nanite.bEnabled = false;
		Collision->SetNaniteSettings(Nanite);
		Collision->SetStaticMaterials({FStaticMaterial(nullptr, TEXT("Collision"), TEXT("Collision"))});
		Collision->bAllowCPUAccess = true;   // the physics reads the triangles of its render data
		Collision->Build(true);
		// The build is async. The cook below reads the triangles of the render data: it must wait for the build, else it
		// cooks an empty body, and the cache keeps it under the new BodySetupGuid (the key of the cooked physics).
		FStaticMeshCompilingManager::Get().FinishCompilation({Collision});
		Collision->MarkPackageDirty();
	}
	// The render mesh: no collision. (A render mesh with a ComplexCollisionMesh crashed: its async post-load cooked the
	// body while the other mesh was still compiling, and GetPhysicsTriMeshDataCheckComplex checks HasValidRenderData.)
	Mesh->Modify();
	if (!Mesh->GetBodySetup())
	{
		Mesh->CreateBodySetup();
	}
	UBodySetup* Body = Mesh->GetBodySetup();
	Body->Modify();
	Body->RemoveSimpleCollision();
	Mesh->ComplexCollisionMesh = nullptr;
	Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;   // no simple shape: no collision at all
	Body->InvalidatePhysicsData();
	Body->CreatePhysicsMeshes();
	Mesh->SetCustomizedCollision(true);   // a new import keeps this collision
	Mesh->MarkPackageDirty();
	if (!Collision)
	{
		return true;
	}
	// The collision mesh: its own triangles are its collision (the standard path of a static mesh: its post-load has its
	// render data before its cook). The cells of the level place it, hidden, at the instances of the render mesh.
	if (!Collision->GetBodySetup())
	{
		Collision->CreateBodySetup();
	}
	UBodySetup* CollisionBody = Collision->GetBodySetup();
	CollisionBody->Modify();
	CollisionBody->RemoveSimpleCollision();
	CollisionBody->CollisionTraceFlag = CTF_UseComplexAsSimple;
	CollisionBody->InvalidatePhysicsData();
	CollisionBody->CreatePhysicsMeshes();
	Collision->SetCustomizedCollision(true);
	Collision->MarkPackageDirty();
	if (CollisionBody->TriMeshGeometries.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("SWC collision: %s has no triangles after the cook"), *Collision->GetPathName());
		return false;
	}
	return true;
}

FString USwcEditorLibrary::DescribeCollision(UStaticMesh* Mesh)
{
	if (!Mesh || !Mesh->GetBodySetup())
	{
		return TEXT("no mesh or no body");
	}
	UBodySetup* Body = Mesh->GetBodySetup();
	const UStaticMesh* Complex = Mesh->ComplexCollisionMesh;
	int32 ComplexTriangles = -1;
	if (Complex && Complex->GetRenderData() && Complex->GetRenderData()->LODResources.Num() > 0)
	{
		ComplexTriangles = Complex->GetRenderData()->LODResources[0].GetNumTriangles();
	}
	FString MeshId(TEXT("none"));
	Mesh->GetMeshId(MeshId);
	FString Cooked;
	for (const Chaos::FTriangleMeshImplicitObjectPtr& TriMesh : Body->TriMeshGeometries)
	{
		if (TriMesh.IsValid())
		{
			const Chaos::FAABB3 Box = TriMesh->BoundingBox();
			Cooked += FString::Printf(TEXT(" [%d triangles, %s to %s]"), TriMesh->Elements().GetNumTriangles(), *FVector(Box.Min()).ToString(), *FVector(Box.Max()).ToString());
		}
	}
	return FString::Printf(TEXT("trace %d, complex %s (%d triangles), render data %d, contains tri data %d, cooked tri meshes %d, simple %d, guid %s, mesh id %s"),
		int32(Body->CollisionTraceFlag), Complex ? *Complex->GetName() : TEXT("none"), ComplexTriangles, Mesh->GetRenderData() != nullptr,
		Mesh->ContainsPhysicsTriMeshData(true), Body->TriMeshGeometries.Num(), Body->AggGeom.GetElementCount(),
		*Body->BodySetupGuid.ToString(), *MeshId.Right(40)) + Cooked;
}

bool USwcEditorLibrary::DestroyActor(AActor* Actor)
{
	UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	return World && World->EditorDestroyActor(Actor, true);
}
