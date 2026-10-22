#include "Environment/SwcProbeShading.h"

#include "Environment/SwcWorldTimeSubsystem.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "SceneViewExtension.h"

namespace
{
class FProbeShading : public FSceneViewExtensionBase
{
public:
	FProbeShading(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override
	{
		FSwcProbeShadingList List;
		{
			FScopeLock Lock(&Section);
			if (!Current.bAo && !Current.bSpecular)
			{
				return;
			}
			List = Current;
		}
		// The targets of the base pass, by the names of the scene textures of the renderer.
		FRDGTextureRef GBuffer[3] = {nullptr, nullptr, nullptr};
		FRDGTextureRef SceneColor = nullptr;
		for (const FRenderTargetBinding& Binding : RenderTargets.Output)
		{
			if (FRDGTextureRef Texture = Binding.GetTexture())
			{
				for (int32 Index = 0; Index < 3; ++Index)
				{
					static const TCHAR* Names[3] = {TEXT("GBufferA"), TEXT("GBufferB"), TEXT("GBufferC")};
					if (FCString::Strcmp(Texture->Name, Names[Index]) == 0)
					{
						GBuffer[Index] = Texture;
					}
				}
				if (FCString::Strcmp(Texture->Name, TEXT("SceneColor")) == 0)
				{
					SceneColor = Texture;
				}
			}
		}
		FRDGTextureRef Depth = RenderTargets.DepthStencil.GetTexture();
		if (!bReported)
		{
			bReported = true;
			UE_LOG(LogSwcEnvironment, Log, TEXT("probe shading: the targets of the base pass: G-buffer A %s, B %s, C %s, the scene color %s, the depth %s"),
				GBuffer[0] != nullptr ? TEXT("found") : TEXT("missing"), GBuffer[1] != nullptr ? TEXT("found") : TEXT("missing"),
				GBuffer[2] != nullptr ? TEXT("found") : TEXT("missing"), SceneColor != nullptr ? TEXT("found") : TEXT("missing"),
				Depth != nullptr ? Depth->Name : TEXT("missing"));
		}
		// The specular term of the probes (swc.ProbeSpecular 1) first: it reads the material AO before the AO pass scales it. The
		// lighting adds to the scene color after the base pass, the fog and the translucency come later (as the source).
		if (List.bSpecular)
		{
			// The source SSR (reflection part 3): the history of the lit scene of the previous frame (CaptureHistory), in a game view
			// with the same buffer size.
			FRDGTextureRef Levels[4] = {nullptr, nullptr, nullptr, nullptr};
			const bool bHistory = History.IsValid() && SceneColor != nullptr && History->GetDesc().Extent == SceneColor->Desc.Extent;
			if (List.bSsr && InView.bIsGameView && !InView.bIsSceneCapture && bHistory)
			{
				AddSwcProbeSsrPasses(GraphBuilder, InView, Depth, GBuffer[0], GBuffer[1], GraphBuilder.RegisterExternalTexture(History), Levels);
			}
			if (List.bSsr && InView.bIsGameView && !InView.bIsSceneCapture && SsrReports < 12)
			{
				// One line at each change of the state (the first frames have no history yet).
				const FString State = FString::Printf(TEXT("SSR %s (history %s, the extents %s and %s)"), Levels[0] != nullptr ? TEXT("on") : TEXT("off"),
					History.IsValid() ? TEXT("kept") : TEXT("none"), History.IsValid() ? *History->GetDesc().Extent.ToString() : TEXT("-"),
					SceneColor != nullptr ? *SceneColor->Desc.Extent.ToString() : TEXT("-"));
				if (State != SsrState)
				{
					++SsrReports;
					SsrState = State;
					UE_LOG(LogSwcEnvironment, Log, TEXT("probe shading: %s"), *State);
				}
			}
			AddSwcProbeSpecularPass(GraphBuilder, InView, SceneColor, Depth, GBuffer[0], GBuffer[1], GBuffer[2], List, Levels);
		}
		if (List.bAo && !List.Probes.IsEmpty())
		{
			AddSwcLocalProbePass(GraphBuilder, InView, Depth, GBuffer[0], GBuffer[1], GBuffer[2], List);
		}
	}

	// The history of the source SSR: the scene color before the depth of field of each game view frame (the lit scene, the fog, the
	// translucency), kept for the next frame (the source reads $HDRTargetPrev at the reprojected hit).
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override
	{
		if (Pass != EPostProcessingPass::BeforeDOF || !InView.bIsGameView || InView.bIsSceneCapture)
		{
			return;
		}
		FScopeLock Lock(&Section);
		if (Current.bSpecular && Current.bSsr)
		{
			InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FProbeShading::CaptureHistory));
		}
	}

	FScreenPassTexture CaptureHistory(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
	{
		const FScreenPassTextureSlice Scene = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
		if (Scene.TextureSRV != nullptr)
		{
			FRDGTextureRef Source = Scene.TextureSRV->Desc.Texture;
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Source->Desc.Extent, Source->Desc.Format, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_RenderTargetable);
			FRDGTextureRef Copy = GraphBuilder.CreateTexture(Desc, TEXT("SwcSsrHistory"));
			AddCopyTexturePass(GraphBuilder, Source, Copy);
			GraphBuilder.QueueTextureExtraction(Copy, &History);
		}
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	TRefCountPtr<IPooledRenderTarget> History;
	int32 SsrReports = 0;
	FString SsrState;
	FCriticalSection Section;
	FSwcProbeShadingList Current;
	bool bReported = false;
};

TSharedPtr<FProbeShading, ESPMode::ThreadSafe>& Extension()
{
	static TSharedPtr<FProbeShading, ESPMode::ThreadSafe> Instance;
	if (!Instance.IsValid())
	{
		Instance = FSceneViewExtensions::NewExtension<FProbeShading>();
	}
	return Instance;
}
}

void SwcProbeShading::SetList(FSwcProbeShadingList&& List)
{
	FProbeShading& Shading = *Extension();
	FScopeLock Lock(&Shading.Section);
	Shading.Current = MoveTemp(List);
}
