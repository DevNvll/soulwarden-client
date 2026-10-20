#include "World/SwcLoadingGate.h"

#include "Camera/PlayerCameraManager.h"
#include "ContentStreaming.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "ShaderCompiler.h"
#include "UObject/ObjectKey.h"
#include "AssetCompilingManager.h"

namespace
{
struct FGateState
{
	TOptional<FVector> Place;
	TMap<FName, SwcLoadingGate::FCheck> Checks;
};

TMap<TObjectKey<UWorld>, FGateState>& States()
{
	static TMap<TObjectKey<UWorld>, FGateState> Map;
	return Map;
}

FGateState* Find(const UWorld* World)
{
	return World != nullptr ? States().Find(TObjectKey<UWorld>(World)) : nullptr;
}

FGateState& FindOrAdd(const UWorld* World)
{
	return States().FindOrAdd(TObjectKey<UWorld>(World));
}

/** The exposure of the game view in each frame (the eye adaptation of the renderer, read back a few frames late). */
class FExposureWatch : public FSceneViewExtensionBase
{
public:
	explicit FExposureWatch(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{
		if (InViewFamily.Scene != nullptr && InView.bIsGameView && !InView.bIsSceneCapture)
		{
			Sample.Exposure = InView.GetLastEyeAdaptationExposure();
			Sample.Luminance = InView.GetLastAverageSceneLuminance();
			Sample.Frame = GFrameCounter;
		}
	}

	SwcLoadingGate::FLight Sample;
};

TSharedPtr<FExposureWatch, ESPMode::ThreadSafe>& Watch()
{
	static TSharedPtr<FExposureWatch, ESPMode::ThreadSafe> Extension;
	return Extension;
}
}

bool SwcLoadingGate::GetLight(FLight& Out)
{
	if (!Watch().IsValid())
	{
		Watch() = FSceneViewExtensions::NewExtension<FExposureWatch>();
	}
	Out = Watch()->Sample;
	return Out.Frame > 0;
}

void SwcLoadingGate::SetPlace(const UWorld* World, const FVector& Place)
{
	if (World != nullptr)
	{
		FindOrAdd(World).Place = Place;
	}
}

void SwcLoadingGate::ClearPlace(const UWorld* World)
{
	if (FGateState* State = Find(World))
	{
		State->Place.Reset();
	}
}

bool SwcLoadingGate::GetPlace(const UWorld* World, FVector& Out)
{
	const FGateState* State = Find(World);
	if (State == nullptr || !State->Place.IsSet())
	{
		return false;
	}
	Out = State->Place.GetValue();
	return true;
}

bool SwcLoadingGate::GetView(const UWorld* World, FVector& Out)
{
	if (GetPlace(World, Out))
	{
		return true;
	}
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr || Controller->PlayerCameraManager == nullptr || Controller->GetPawn() == nullptr)
	{
		return false;   // no place and no character yet: the camera is at the origin of the world
	}
	Out = Controller->PlayerCameraManager->GetCameraLocation();
	return true;
}

void SwcLoadingGate::Register(const UWorld* World, FName Name, FCheck Check)
{
	if (World != nullptr)
	{
		FindOrAdd(World).Checks.Add(Name, MoveTemp(Check));
	}
}

void SwcLoadingGate::Unregister(const UWorld* World, FName Name)
{
	if (FGateState* State = Find(World))
	{
		State->Checks.Remove(Name);
		if (State->Checks.IsEmpty() && !State->Place.IsSet())
		{
			States().Remove(TObjectKey<UWorld>(World));
		}
	}
}

bool SwcLoadingGate::ChecksReady(const UWorld* World, const FVector& Place, FString& NotReady)
{
	NotReady.Reset();
	const FGateState* State = Find(World);
	if (State == nullptr)
	{
		return true;
	}
	bool bReady = true;
	for (const TPair<FName, FCheck>& Pair : State->Checks)
	{
		FString Why;
		if (!Pair.Value(Place, Why))
		{
			bReady = false;
			NotReady += FString::Printf(TEXT("%s%s: %s"), NotReady.IsEmpty() ? TEXT("") : TEXT("; "), *Pair.Key.ToString(), *Why);
		}
	}
	return bReady;
}

bool SwcLoadingGate::EngineReady(FString& NotReady)
{
	const int32 Shaders = GShaderCompilingManager != nullptr ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
	// The assets that an uncooked client still builds (the meshes, the textures, the skinned meshes, the animation clips: the
	// "Preparing ..." lines of the engine on the screen).
	const int32 Assets = FAssetCompilingManager::Get().GetNumRemainingAssets();
	const int32 Streaming = IStreamingManager::Get().GetNumWantingResources();
	NotReady = FString::Printf(TEXT("shader jobs %d, assets building %d, textures streaming %d"), Shaders, Assets, Streaming);
	return Shaders == 0 && Assets == 0 && Streaming == 0;
}
