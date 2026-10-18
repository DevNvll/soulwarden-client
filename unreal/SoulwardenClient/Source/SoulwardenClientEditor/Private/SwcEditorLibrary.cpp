#include "SwcEditorLibrary.h"

#include "AssetCompilingManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "TextureResource.h"

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
