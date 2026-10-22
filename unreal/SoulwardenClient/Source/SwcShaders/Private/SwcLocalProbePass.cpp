#include "SwcLocalProbePass.h"

#include "GlobalShader.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "TextureResource.h"
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

namespace
{
/** The five values of one probe of the specular pass: the center and the falloff, the three rows, the color and the box
 *  projection, the box size. */
constexpr int32 SpecularStride = 6;
}

class FSwcProbeSpecularPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSwcProbeSpecularPS);
	SHADER_USE_PARAMETER_STRUCT(FSwcProbeSpecularPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferC)
		SHADER_PARAMETER_TEXTURE(TextureCube, WorldCube)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube0)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube1)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube2)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube3)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube4)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube5)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube6)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube7)
		SHADER_PARAMETER_SAMPLER(SamplerState, CubeSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
		SHADER_PARAMETER(uint32, ProbeCount)
		SHADER_PARAMETER(float, SourceLuminance)
		SHADER_PARAMETER(float, SsdoReflection)
		SHADER_PARAMETER(FVector4f, WorldColor)
		SHADER_PARAMETER_ARRAY(FVector4f, Probes, [FSwcProbeShadingList::MaxSpecular * SpecularStride])
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MAX_SPECULAR"), FSwcProbeShadingList::MaxSpecular);
		OutEnvironment.SetDefine(TEXT("SPECULAR_STRIDE"), SpecularStride);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSwcProbeSpecularPS, "/SwcShaders/Private/SwcProbeSpecular.usf", "MainPS", SF_Pixel);

void AddSwcProbeSpecularPass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth,
	FRDGTextureRef GBufferA, FRDGTextureRef GBufferB, FRDGTextureRef GBufferC, FRHITexture* PreIntegratedGF, const FSwcProbeShadingList& List)
{
	if (SceneColor == nullptr || SceneDepth == nullptr || GBufferA == nullptr || GBufferB == nullptr || GBufferC == nullptr
		|| PreIntegratedGF == nullptr || List.WorldSpecCube == nullptr || List.WorldSpecCube->TextureRHI == nullptr)
	{
		return;
	}
	FSwcProbeSpecularPS::FParameters* Parameters = GraphBuilder.AllocParameters<FSwcProbeSpecularPS::FParameters>();
	Parameters->View = View.ViewUniformBuffer;
	Parameters->SceneDepth = SceneDepth;
	Parameters->GBufferA = GBufferA;
	Parameters->GBufferB = GBufferB;
	Parameters->GBufferC = GBufferC;
	Parameters->WorldCube = List.WorldSpecCube->TextureRHI;
	Parameters->CubeSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->PreIntegratedGF = PreIntegratedGF;
	Parameters->PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->SourceLuminance = List.SourceLuminance;
	Parameters->SsdoReflection = List.SsdoReflection;
	Parameters->WorldColor = FVector4f(List.WorldSpecColor, 0.0f);
	FRHITexture* Black = GBlackTextureCube->TextureRHI;
	FRHITexture** Slots[FSwcProbeShadingList::MaxSpecular] = {&Parameters->Cube0, &Parameters->Cube1, &Parameters->Cube2, &Parameters->Cube3,
		&Parameters->Cube4, &Parameters->Cube5, &Parameters->Cube6, &Parameters->Cube7};
	// The probes of the list in their order, the first MaxSpecular with a cube (a probe with no cube has no specular light of its own:
	// its weight stays with the world probe).
	const FVector PreViewTranslation = View.ViewMatrices.GetPreViewTranslation();
	int32 Count = 0;
	for (const FSwcProbeShadingProbe& Probe : List.Probes)
	{
		if (Count == FSwcProbeShadingList::MaxSpecular)
		{
			break;
		}
		if (Probe.SpecCube == nullptr || Probe.SpecCube->TextureRHI == nullptr)
		{
			continue;
		}
		FVector4f* Out = &Parameters->Probes[Count * SpecularStride];
		Out[0] = FVector4f(FVector3f(Probe.Center + PreViewTranslation), Probe.Falloff);
		Out[1] = FVector4f(Probe.Rows[0], 0.0f);
		Out[2] = FVector4f(Probe.Rows[1], 0.0f);
		Out[3] = FVector4f(Probe.Rows[2], 0.0f);
		Out[4] = FVector4f(Probe.SpecColor, Probe.bBoxProject ? 1.0f : 0.0f);
		Out[5] = FVector4f(Probe.BoxHalf, 0.0f);
		*Slots[Count] = Probe.SpecCube->TextureRHI;
		++Count;
	}
	for (int32 Index = Count; Index < FSwcProbeShadingList::MaxSpecular; ++Index)
	{
		*Slots[Index] = Black;
	}
	Parameters->ProbeCount = static_cast<uint32>(Count);
	Parameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FSwcProbeSpecularPS> Shader(ShaderMap);
	// The whole target (the shader keeps to the view rectangle, as the AO pass).
	const FIntPoint Extent = SceneColor->Desc.Extent;
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("SwcProbeSpecular %d probes", Count), Shader, Parameters,
		FIntRect(0, 0, Extent.X, Extent.Y), TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI());
}
