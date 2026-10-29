#include "SwcMeterPass.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

class FSwcMeterCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSwcMeterCS);
	SHADER_USE_PARAMETER_STRUCT(FSwcMeterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SceneColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GBufferB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GBufferC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, OpaqueGBufferB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, OpaqueGBufferC)
		SHADER_PARAMETER(int32, UseOpaque)
		SHADER_PARAMETER(FIntVector4, Rect)
		SHADER_PARAMETER(int32, Step)
		SHADER_PARAMETER(int32, GroupsX)
		SHADER_PARAMETER(float, InvPreExposure)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, Output)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSwcMeterCS, "/SwcShaders/Private/SwcMeter.usf", "MainCS", SF_Compute);

FRDGBufferRef AddSwcMeterPass(FRDGBuilder& GraphBuilder, const FSwcMeterInputs& Inputs, int32& OutGroups)
{
	constexpr int32 Group = 16;
	const int32 Step = FMath::Max(1, Inputs.Step);
	const FIntPoint Samples(FMath::DivideAndRoundUp(Inputs.Rect.Width(), Step), FMath::DivideAndRoundUp(Inputs.Rect.Height(), Step));
	const FIntPoint Groups(FMath::DivideAndRoundUp(Samples.X, Group), FMath::DivideAndRoundUp(Samples.Y, Group));
	OutGroups = Groups.X * Groups.Y;
	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(float), OutGroups * SwcMeterGroupFloats);
	Desc.Usage |= EBufferUsageFlags::SourceCopy;
	FRDGBufferRef Output = GraphBuilder.CreateBuffer(Desc, TEXT("SwcMeterSums"));
	FSwcMeterCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSwcMeterCS::FParameters>();
	Parameters->SceneColor = Inputs.SceneColor;
	Parameters->GBufferB = Inputs.GBufferB;
	Parameters->GBufferC = Inputs.GBufferC;
	const bool bOpaque = Inputs.OpaqueGBufferB != nullptr && Inputs.OpaqueGBufferC != nullptr;
	Parameters->OpaqueGBufferB = bOpaque ? Inputs.OpaqueGBufferB : Inputs.GBufferB;
	Parameters->OpaqueGBufferC = bOpaque ? Inputs.OpaqueGBufferC : Inputs.GBufferC;
	Parameters->UseOpaque = bOpaque ? 1 : 0;
	Parameters->Rect = FIntVector4(Inputs.Rect.Min.X, Inputs.Rect.Min.Y, Inputs.Rect.Max.X, Inputs.Rect.Max.Y);
	Parameters->Step = Step;
	Parameters->GroupsX = Groups.X;
	Parameters->InvPreExposure = Inputs.PreExposure > 0.0f ? 1.0f / Inputs.PreExposure : 0.0f;
	Parameters->Output = GraphBuilder.CreateUAV(Output, PF_R32_FLOAT);
	TShaderMapRef<FSwcMeterCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SwcMeter %dx%d groups", Groups.X, Groups.Y), Shader, Parameters, FIntVector(Groups.X, Groups.Y, 1));
	return Output;
}
