#include "Environment/SwcProbeShading.h"

#include "Environment/SwcWorldTimeSubsystem.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "RenderGraphBuilder.h"
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
			if (Current.Probes.IsEmpty())
			{
				return;
			}
			List = Current;
		}
		// The targets of the base pass, by the names of the scene textures of the renderer.
		FRDGTextureRef GBuffer[3] = {nullptr, nullptr, nullptr};
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
			}
		}
		FRDGTextureRef Depth = RenderTargets.DepthStencil.GetTexture();
		if (!bReported)
		{
			bReported = true;
			UE_LOG(LogSwcEnvironment, Log, TEXT("probe shading: the targets of the base pass: G-buffer A %s, B %s, C %s, the depth %s"),
				GBuffer[0] != nullptr ? TEXT("found") : TEXT("missing"), GBuffer[1] != nullptr ? TEXT("found") : TEXT("missing"),
				GBuffer[2] != nullptr ? TEXT("found") : TEXT("missing"), Depth != nullptr ? Depth->Name : TEXT("missing"));
		}
		AddSwcLocalProbePass(GraphBuilder, InView, Depth, GBuffer[0], GBuffer[1], GBuffer[2], List);
	}

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
