#include "SwcLocalProbePass.h"

#include "GlobalShader.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
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
/** The six values of one probe of the specular pass: the center and the falloff, the three rows, the color and the box
 *  projection, the half sizes of the proxy box and the slot of its cube. */
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
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube8)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube9)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube10)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube11)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube12)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube13)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube14)
		SHADER_PARAMETER_TEXTURE(TextureCube, Cube15)
		SHADER_PARAMETER_SAMPLER(SamplerState, CubeSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, BrdfTable)
		SHADER_PARAMETER_SAMPLER(SamplerState, BrdfSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Ssr0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Ssr1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Ssr2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Ssr3)
		SHADER_PARAMETER_SAMPLER(SamplerState, SsrSampler)
		SHADER_PARAMETER(float, SsrOn)
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

/** The ray pass of the source SSR (Shaders/Private/SwcProbeSsr.usf RayPS): the half-size target. */
class FSwcProbeSsrRayPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSwcProbeSsrRayPS);
	SHADER_USE_PARAMETER_STRUCT(FSwcProbeSsrRayPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, History)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSwcProbeSsrRayPS, "/SwcShaders/Private/SwcProbeSsr.usf", "RayPS", SF_Pixel);

/** The reduced and blurred levels of the source SSR (SwcProbeSsr.usf DownPS). */
class FSwcProbeSsrDownPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSwcProbeSsrDownPS);
	SHADER_USE_PARAMETER_STRUCT(FSwcProbeSsrDownPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Source)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER(FVector4f, SourceSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSwcProbeSsrDownPS, "/SwcShaders/Private/SwcProbeSsr.usf", "DownPS", SF_Pixel);

void AddSwcProbeSsrPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SceneDepth, FRDGTextureRef GBufferA,
	FRDGTextureRef GBufferB, FRDGTextureRef History, FRDGTextureRef OutLevels[4])
{
	for (int32 Level = 0; Level < 4; ++Level)
	{
		OutLevels[Level] = nullptr;
	}
	if (SceneDepth == nullptr || GBufferA == nullptr || GBufferB == nullptr || History == nullptr)
	{
		return;
	}
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	FIntPoint Size = FIntPoint::DivideAndRoundUp(View.UnconstrainedViewRect.Size(), 2);
	const auto Target = [&GraphBuilder](FIntPoint Extent, const TCHAR* Name)
	{
		return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(FMath::Max(Extent.X, 1), FMath::Max(Extent.Y, 1)), PF_FloatRGBA,
			FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable), Name);
	};
	// The ray pass at half size.
	OutLevels[0] = Target(Size, TEXT("SwcSsrRay"));
	{
		FSwcProbeSsrRayPS::FParameters* Parameters = GraphBuilder.AllocParameters<FSwcProbeSsrRayPS::FParameters>();
		Parameters->View = View.ViewUniformBuffer;
		Parameters->SceneDepth = SceneDepth;
		Parameters->GBufferA = GBufferA;
		Parameters->GBufferB = GBufferB;
		Parameters->History = History;
		Parameters->PointSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->RenderTargets[0] = FRenderTargetBinding(OutLevels[0], ERenderTargetLoadAction::ENoAction);
		TShaderMapRef<FSwcProbeSsrRayPS> Shader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("SwcProbeSsr ray %dx%d", Size.X, Size.Y), Shader, Parameters,
			FIntRect(FIntPoint::ZeroValue, Size));
	}
	// The quarter, eighth, and sixteenth levels.
	static const TCHAR* const Names[3] = {TEXT("SwcSsr1"), TEXT("SwcSsr2"), TEXT("SwcSsr3")};
	for (int32 Level = 1; Level < 4; ++Level)
	{
		const FIntPoint Before = OutLevels[Level - 1]->Desc.Extent;
		const FIntPoint Next = FIntPoint(FMath::Max(FMath::DivideAndRoundUp(Before.X, 2), 1), FMath::Max(FMath::DivideAndRoundUp(Before.Y, 2), 1));
		OutLevels[Level] = Target(Next, Names[Level - 1]);
		FSwcProbeSsrDownPS::FParameters* Parameters = GraphBuilder.AllocParameters<FSwcProbeSsrDownPS::FParameters>();
		Parameters->Source = OutLevels[Level - 1];
		Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->SourceSize = FVector4f(Before.X, Before.Y, 1.0f / Before.X, 1.0f / Before.Y);
		Parameters->RenderTargets[0] = FRenderTargetBinding(OutLevels[Level], ERenderTargetLoadAction::ENoAction);
		TShaderMapRef<FSwcProbeSsrDownPS> Shader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("SwcProbeSsr level %d", Level), Shader, Parameters,
			FIntRect(FIntPoint::ZeroValue, Next));
	}
}

void AddSwcProbeSpecularPass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth,
	FRDGTextureRef GBufferA, FRDGTextureRef GBufferB, FRDGTextureRef GBufferC, const FSwcProbeShadingList& List, const FRDGTextureRef* SsrLevels)
{
	if (SceneColor == nullptr || SceneDepth == nullptr || GBufferA == nullptr || GBufferB == nullptr || GBufferC == nullptr
		|| List.BrdfTable == nullptr || List.BrdfTable->TextureRHI == nullptr || List.WorldSpecCube == nullptr
		|| List.WorldSpecCube->TextureRHI == nullptr)
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
	Parameters->BrdfTable = List.BrdfTable->TextureRHI;
	Parameters->BrdfSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	// The source SSR levels (AddSwcProbeSsrPasses), else a black 1 x 1 texture (no SSR: the probe term alone).
	const bool bSsr = SsrLevels != nullptr && SsrLevels[0] != nullptr && SsrLevels[3] != nullptr;
	FRDGTextureRef NoSsr = nullptr;
	if (!bSsr)
	{
		NoSsr = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("SwcSsrNone"));
		AddClearRenderTargetPass(GraphBuilder, NoSsr, FLinearColor::Transparent);
	}
	Parameters->Ssr0 = bSsr ? SsrLevels[0] : NoSsr;
	Parameters->Ssr1 = bSsr ? SsrLevels[1] : NoSsr;
	Parameters->Ssr2 = bSsr ? SsrLevels[2] : NoSsr;
	Parameters->Ssr3 = bSsr ? SsrLevels[3] : NoSsr;
	Parameters->SsrSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->SsrOn = bSsr ? 1.0f : 0.0f;
	Parameters->SourceLuminance = List.SourceLuminance;
	Parameters->SsdoReflection = List.SsdoReflection;
	Parameters->WorldColor = FVector4f(List.WorldSpecColor, 0.0f);
	FRHITexture* Black = GBlackTextureCube->TextureRHI;
	FRHITexture** Slots[FSwcProbeShadingList::MaxSpecularCubes] = {&Parameters->Cube0, &Parameters->Cube1, &Parameters->Cube2,
		&Parameters->Cube3, &Parameters->Cube4, &Parameters->Cube5, &Parameters->Cube6, &Parameters->Cube7, &Parameters->Cube8,
		&Parameters->Cube9, &Parameters->Cube10, &Parameters->Cube11, &Parameters->Cube12, &Parameters->Cube13, &Parameters->Cube14,
		&Parameters->Cube15};
	// All the probes of the list in their order (the nearest MaxProbes; research probe-flags.md: no limit of the first eight cubes);
	// the probes share the distinct cubes in MaxSpecularCubes slots. A probe with no cube or no free slot has no specular light of its
	// own: its weight stays with the probes after it and the world probe.
	const FVector PreViewTranslation = View.ViewMatrices.GetPreViewTranslation();
	TArray<FRHITexture*, TInlineAllocator<FSwcProbeShadingList::MaxSpecularCubes>> Cubes;
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
		int32 Slot = Cubes.Find(Probe.SpecCube->TextureRHI);
		if (Slot == INDEX_NONE)
		{
			if (Cubes.Num() == FSwcProbeShadingList::MaxSpecularCubes)
			{
				continue;
			}
			Slot = Cubes.Add(Probe.SpecCube->TextureRHI);
		}
		FVector4f* Out = &Parameters->Probes[Count * SpecularStride];
		Out[0] = FVector4f(FVector3f(Probe.Center + PreViewTranslation), Probe.Falloff);
		Out[1] = FVector4f(Probe.Rows[0], 0.0f);
		Out[2] = FVector4f(Probe.Rows[1], 0.0f);
		Out[3] = FVector4f(Probe.Rows[2], 0.0f);
		Out[4] = FVector4f(Probe.SpecColor, Probe.bBoxProject ? 1.0f : 0.0f);
		Out[5] = FVector4f(Probe.BoxHalf, static_cast<float>(Slot));
		++Count;
	}
	for (int32 Index = 0; Index < FSwcProbeShadingList::MaxSpecularCubes; ++Index)
	{
		*Slots[Index] = Index < Cubes.Num() ? Cubes[Index] : Black;
	}
	Parameters->ProbeCount = static_cast<uint32>(Count);
	Parameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FSwcProbeSpecularPS> Shader(ShaderMap);
	// The whole target (the shader keeps to the view rectangle, as the AO pass).
	const FIntPoint Extent = SceneColor->Desc.Extent;
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("SwcProbeSpecular %d probes, %d cubes", Count, Cubes.Num()), Shader, Parameters,
		FIntRect(0, 0, Extent.X, Extent.Y), TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI());
}
