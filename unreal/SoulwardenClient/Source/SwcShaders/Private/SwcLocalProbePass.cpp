#include "SwcLocalProbePass.h"

#include "GlobalShader.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "SceneView.h"
#include "ShaderCore.h"
#include "ShaderParameterStruct.h"

class FSwcShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The shader folder of the project (a cooked build has the compiled shaders and needs no folder).
		AddShaderSourceDirectoryMapping(TEXT("/SwcShaders"), FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"))));
	}
};

IMPLEMENT_MODULE(FSwcShadersModule, SwcShaders)

namespace
{
/** The six values of one probe in the parameter array: the center and the falloff, the three rows, the two halves of the cube. */
constexpr int32 ProbeStride = 6;
}

class FSwcLocalProbePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSwcLocalProbePS);
	SHADER_USE_PARAMETER_STRUCT(FSwcLocalProbePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferB)
		SHADER_PARAMETER(uint32, ProbeCount)
		SHADER_PARAMETER(FVector4f, WorldPositive)
		SHADER_PARAMETER(FVector4f, WorldNegative)
		SHADER_PARAMETER_ARRAY(FVector4f, Probes, [FSwcProbeShadingList::MaxProbes * ProbeStride])
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MAX_PROBES"), FSwcProbeShadingList::MaxProbes);
		OutEnvironment.SetDefine(TEXT("PROBE_STRIDE"), ProbeStride);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSwcLocalProbePS, "/SwcShaders/Private/SwcLocalProbes.usf", "MainPS", SF_Pixel);

void AddSwcLocalProbePass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SceneDepth, FRDGTextureRef GBufferA,
	FRDGTextureRef GBufferB, FRDGTextureRef GBufferC, const FSwcProbeShadingList& List)
{
	const int32 Count = FMath::Min(List.Probes.Num(), FSwcProbeShadingList::MaxProbes);
	if (SceneDepth == nullptr || GBufferA == nullptr || GBufferB == nullptr || GBufferC == nullptr || Count == 0)
	{
		return;
	}
	FSwcLocalProbePS::FParameters* Parameters = GraphBuilder.AllocParameters<FSwcLocalProbePS::FParameters>();
	Parameters->View = View.ViewUniformBuffer;
	Parameters->SceneDepth = SceneDepth;
	Parameters->GBufferA = GBufferA;
	Parameters->GBufferB = GBufferB;
	Parameters->ProbeCount = static_cast<uint32>(Count);
	Parameters->WorldPositive = FVector4f(List.WorldPositive, 0.0f);
	Parameters->WorldNegative = FVector4f(List.WorldNegative, 0.0f);
	// The centers in the translated world of the view (the large world coordinates stay on the CPU).
	const FVector PreViewTranslation = View.ViewMatrices.GetPreViewTranslation();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FSwcProbeShadingProbe& Probe = List.Probes[Index];
		FVector4f* Out = &Parameters->Probes[Index * ProbeStride];
		Out[0] = FVector4f(FVector3f(Probe.Center + PreViewTranslation), Probe.Falloff);
		Out[1] = FVector4f(Probe.Rows[0], 0.0f);
		Out[2] = FVector4f(Probe.Rows[1], 0.0f);
		Out[3] = FVector4f(Probe.Rows[2], 0.0f);
		Out[4] = FVector4f(Probe.Positive, 0.0f);
		Out[5] = FVector4f(Probe.Negative, 0.0f);
	}
	Parameters->RenderTargets[0] = FRenderTargetBinding(GBufferC, ERenderTargetLoadAction::ELoad);
	// Only the alpha (the material AO): the new AO is the old AO times the ratio of the pixel.
	FRHIBlendState* Blend = TStaticBlendState<CW_ALPHA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FSwcLocalProbePS> Shader(ShaderMap);
	const FIntPoint Extent = GBufferC->Desc.Extent;
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("SwcLocalProbes %d", Count), Shader, Parameters,
		FIntRect(0, 0, Extent.X, Extent.Y), Blend);
}
