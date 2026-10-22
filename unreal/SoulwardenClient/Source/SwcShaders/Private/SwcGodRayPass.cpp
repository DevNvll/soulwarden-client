#include "SwcGodRayPass.h"

#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

class FSwcGodRayMaskPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSwcGodRayMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FSwcGodRayMaskPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClamp)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointClamp)
		SHADER_PARAMETER(FVector4f, SceneColorRect)
		SHADER_PARAMETER(FVector4f, DepthRect)
		SHADER_PARAMETER(FVector2f, OutputSize)
		SHADER_PARAMETER(float, SourceScale)
		SHADER_PARAMETER(float, FarDepth)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSwcGodRayBlurPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSwcGodRayBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FSwcGodRayBlurPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Input)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClamp)
		SHADER_PARAMETER(FVector2f, OutputSize)
		SHADER_PARAMETER(FVector2f, SunUV)
		SHADER_PARAMETER(float, Facing)
		SHADER_PARAMETER(float, StepSize)
		SHADER_PARAMETER(float, Attenuation)
		SHADER_PARAMETER(float, Aspect)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSwcGodRayMaskPS, "/SwcShaders/Private/SwcGodRays.usf", "MaskPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FSwcGodRayBlurPS, "/SwcShaders/Private/SwcGodRays.usf", "BlurPS", SF_Pixel);

namespace
{
FVector4f UVRect(const FIntRect& Rect, FIntPoint Extent)
{
	const FVector2f Size(FMath::Max(Extent.X, 1), FMath::Max(Extent.Y, 1));
	return FVector4f(Rect.Min.X / Size.X, Rect.Min.Y / Size.Y, Rect.Width() / Size.X, Rect.Height() / Size.Y);
}
}

void AddSwcGodRayPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FSwcGodRayInputs& Inputs, FRDGTextureRef Output)
{
	if (Inputs.SceneColor == nullptr || Inputs.SceneDepth == nullptr || Output == nullptr)
	{
		return;
	}
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	const FIntPoint Size = Output->Desc.Extent;
	const FIntRect Rect(0, 0, Size.X, Size.Y);
	const FVector2f OutputSize(Size.X, Size.Y);
	// The intermediate targets of the source: RGBA8 (the clamp and the quantization after each pass).
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Size, PF_R8G8B8A8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FRDGTextureRef Mask = Inputs.Stage == 1 ? Output : GraphBuilder.CreateTexture(Desc, TEXT("SwcGodRayMask"));
	{
		FSwcGodRayMaskPS::FParameters* Parameters = GraphBuilder.AllocParameters<FSwcGodRayMaskPS::FParameters>();
		Parameters->View = View.ViewUniformBuffer;
		Parameters->SceneColor = Inputs.SceneColor;
		Parameters->SceneDepth = Inputs.SceneDepth;
		Parameters->LinearClamp = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->PointClamp = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->SceneColorRect = UVRect(Inputs.SceneColorRect, Inputs.SceneColor->Desc.Extent);
		Parameters->DepthRect = UVRect(Inputs.DepthRect, Inputs.SceneDepth->Desc.Extent);
		Parameters->OutputSize = OutputSize;
		Parameters->SourceScale = Inputs.SourceScale;
		Parameters->FarDepth = FMath::Max(Inputs.FarDepth, 1.0f);
		Parameters->RenderTargets[0] = FRenderTargetBinding(Mask, ERenderTargetLoadAction::ENoAction);
		TShaderMapRef<FSwcGodRayMaskPS> Shader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("SwcGodRayMask %dx%d", Size.X, Size.Y), Shader, Parameters, Rect);
	}
	if (Inputs.Stage == 1)
	{
		return;
	}
	// The aspect term of the source: a = 1.333 x the height over the width of the screen (the target is the whole view).
	const float Aspect = 1.333f * Size.Y / FMath::Max(Size.X, 1);
	auto Blur = [&](FRDGTextureRef From, FRDGTextureRef To, float Step)
	{
		FSwcGodRayBlurPS::FParameters* Parameters = GraphBuilder.AllocParameters<FSwcGodRayBlurPS::FParameters>();
		Parameters->Input = From;
		Parameters->LinearClamp = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->OutputSize = OutputSize;
		Parameters->SunUV = Inputs.SunUV;
		Parameters->Facing = Inputs.Facing;
		Parameters->StepSize = Step;
		Parameters->Attenuation = FMath::Clamp(Inputs.Attenuation, 0.0f, 10.0f);
		Parameters->Aspect = Aspect;
		Parameters->RenderTargets[0] = FRenderTargetBinding(To, ERenderTargetLoadAction::ENoAction);
		TShaderMapRef<FSwcGodRayBlurPS> Shader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("SwcGodRayBlur %.3f", Step), Shader, Parameters, Rect);
	};
	if (Inputs.Stage == 2)
	{
		Blur(Mask, Output, 0.1f);
		return;
	}
	FRDGTextureRef First = GraphBuilder.CreateTexture(Desc, TEXT("SwcGodRayBlur"));
	Blur(Mask, First, 0.1f);
	Blur(First, Output, 0.025f);
}
