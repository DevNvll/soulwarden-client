#include "Environment/SwcGodRays.h"

#include "Engine/TextureRenderTarget2D.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "SwcGodRayPass.h"
#include "TextureResource.h"

namespace
{
/** The source unit of the light of the client (Environment/SwcEnvironmentSky.cpp SourceUnitLuminance): 10,000 cd/m2. */
constexpr float SourceUnitLuminance = 10000.0f;
/**
 * The CPU gate of the source (0x5F4E5B4 to 0x5F4E60E): it rejects the effect when a camera-matrix dot with the light direction is
 * greater than 0.45. The axis of that matrix is not proved (research/godrays.md, open points); this client takes it as the back of
 * the view: no rays when the light is more than about 27 degrees behind the plane of the camera (f < -0.45) [inference].
 */
constexpr float BehindGate = -0.45f;

class FGodRays : public FSceneViewExtensionBase
{
public:
	explicit FGodRays(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override
	{
		if (Pass != EPostProcessingPass::AfterDOF || !InView.bIsGameView || InView.bIsSceneCapture)
		{
			return;
		}
		FScopeLock Lock(&Section);
		if (Current.bActive && Current.Target != nullptr)
		{
			InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FGodRays::Render));
		}
	}

	FScreenPassTexture Render(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
	{
		SwcGodRays::FSettings Settings;
		{
			FScopeLock Lock(&Section);
			Settings = Current;
		}
		const FScreenPassTextureSlice Scene = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
		const TRDGUniformBufferRef<FSceneTextureUniformParameters> Textures = Inputs.SceneTextures.SceneTextures.GetUniformBuffer();
		FTextureRHIRef TargetRHI = Settings.Target != nullptr ? Settings.Target->GetRenderTargetTexture() : nullptr;
		if (Scene.TextureSRV == nullptr || Textures == nullptr || Textures->GetParameters()->SceneDepthTexture == nullptr || !TargetRHI.IsValid())
		{
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}
		FRDGTextureRef Output = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(TargetRHI, TEXT("SwcGodRays")));
		// The screen point of the light (SunShaftsGen of the source, 0x5FE15DC): a far point toward the light through the view
		// projection, UV = (0.5 (x + w), 0.5 (w - y)) / (w + 1e-6), not clamped; f the signed facing of the light (the dot with the view
		// direction; a rule of this client for the renderer basis of the source).
		const FVector Toward = Settings.Toward.GetSafeNormal();
		const FVector Translated = View.ViewMatrices.GetViewOrigin() + View.ViewMatrices.GetPreViewTranslation() + Toward * 1.0e7;
		const FVector4 Clip = View.ViewMatrices.GetTranslatedViewProjectionMatrix().TransformFVector4(FVector4(Translated, 1.0));
		const float Facing = static_cast<float>(FVector::DotProduct(Toward, View.GetViewDirection()));
		if (Facing < BehindGate)
		{
			AddClearRenderTargetPass(GraphBuilder, Output, FLinearColor::Black);
		}
		else
		{
			FSwcGodRayInputs Rays;
			Rays.SceneColor = Scene.TextureSRV->Desc.Texture;
			Rays.SceneColorRect = Scene.ViewRect;
			Rays.SceneDepth = Textures->GetParameters()->SceneDepthTexture;
			Rays.DepthRect = Scene.ViewRect;
			// The pre-exposure of a frame is the eye adaptation exposure of the frame before (as Environment/SwcMeterProbe.cpp).
			const float PreExposure = FMath::Max(View.GetLastEyeAdaptationExposure(), 1e-12f);
			Rays.SourceScale = 1.0f / (PreExposure * SourceUnitLuminance);
			Rays.FarDepth = Settings.FarDepth;
			const double W = Clip.W + 1.0e-6;
			Rays.SunUV = FVector2f(static_cast<float>(0.5 * (Clip.X + Clip.W) / W), static_cast<float>(0.5 * (Clip.W - Clip.Y) / W));
			Rays.Facing = Facing;
			Rays.Attenuation = Settings.Attenuation;
			Rays.Stage = Settings.Stage;
			AddSwcGodRayPasses(GraphBuilder, View, Rays, Output);
		}
		// The grade reads the target later in this graph as a material texture (no RDG dependency): a read-only state from here.
		GraphBuilder.UseExternalAccessMode(Output, ERHIAccess::SRVGraphics);
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	FCriticalSection Section;
	SwcGodRays::FSettings Current;
};

TSharedPtr<FGodRays, ESPMode::ThreadSafe>& Extension()
{
	static TSharedPtr<FGodRays, ESPMode::ThreadSafe> Instance;
	if (!Instance.IsValid())
	{
		Instance = FSceneViewExtensions::NewExtension<FGodRays>();
	}
	return Instance;
}
}

void SwcGodRays::SetSettings(const FSettings& Settings)
{
	FGodRays& Rays = *Extension();
	FScopeLock Lock(&Rays.Section);
	Rays.Current = Settings;
}
