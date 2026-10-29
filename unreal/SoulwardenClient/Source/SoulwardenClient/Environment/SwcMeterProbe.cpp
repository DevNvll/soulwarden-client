#include "Environment/SwcMeterProbe.h"

#include "Environment/SwcEnvironmentRules.h"
#include "Environment/SwcWorldTimeSubsystem.h"
#include "Math/PackedVector.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "SwcMeterPass.h"

namespace
{
struct FRequest
{
	FString Label;
	double EvMin = 0.0;
	double EvMax = 0.0;
	double Compensation = 0.0;
};

BEGIN_SHADER_PARAMETER_STRUCT(FSwcMeterReadParameters, )
	RDG_TEXTURE_ACCESS(SceneColor, ERHIAccess::CopySrc)
	RDG_TEXTURE_ACCESS(Normal, ERHIAccess::CopySrc)
	RDG_TEXTURE_ACCESS(BaseColor, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

/** The reduction of the watch (AST-273 P4): 1 on the GPU (a pass of SwcShaders sums the samples; a readback of a few kilobytes), 0 the
 *  copies of the scene color and the G-buffer B and C to the CPU, 2 both on the same frame (the GPU sums go to the exposure; a log line
 *  compares the two). */
TAutoConsoleVariable<int32> CVarMeterGpu(TEXT("swc.MeterGpu"), 1,
	TEXT("The reduction of the source meter (AST-273 P4): 1 on the GPU (a readback of a few KB), 0 three screen copies to the CPU, 2 both ")
	TEXT("(the log line \"meter compare\")."));

TAutoConsoleVariable<float> CVarMeterInterval(TEXT("swc.MeterInterval"), 0.25f,
	TEXT("The time between two measures of the source meter (seconds; 0: as often as the readbacks allow, a cost test of AST-273 P4)."));

/** The water pixels of the meter (AST-200 stage 2c, research/water-near-tone.md "Repair meter input"): 1 the meter takes a Single Layer Water
 *  pixel with the luminance of the final pixel and the denominator of the floor under it (G-buffer B and C copied after the base pass, before
 *  the water pass writes its own values; the GPU reduction only); 0 it skips the water pixels (before 2026-10-29: an all-water view had no
 *  measure and kept the last correction). The source water is translucent and writes no Single Layer Water pixel: no change there. */
TAutoConsoleVariable<int32> CVarMeterWater(TEXT("swc.MeterWater"), 1,
	TEXT("1: the source meter takes the Single Layer Water pixels with the G-buffer of the floor under them (saved after the base pass); ")
	TEXT("0: it skips them (an all-water view then keeps the last correction)."));

/** The diagnostic of the meter (AST-200 stage 2c, research/water-near-tone.md): 1 logs each measure, also a measure with no accepted pixel
 *  (all Single Layer Water: the meter keeps its last correction), at most one line each second. */
TAutoConsoleVariable<int32> CVarMeterDiag(TEXT("swc.MeterDiag"), 0,
	TEXT("1: a log line for each measure of the source meter (at most each second): the accepted, sky, and water pixels, the exposure of the ")
	TEXT("frame, and the age of the last good measure; also for a measure with no accepted pixel (an all-water view keeps the last correction)."));

/** The step of the watch between the pixels that it reads in each direction (the geometric mean of a regular sample of the frame). */
constexpr int32 WatchStep = 6;

double Luminance(const FLinearColor& Color)
{
	return 0.2126 * Color.R + 0.7152 * Color.G + 0.0722 * Color.B;
}

/** The shading model id of Single Layer Water in G-buffer B alpha (SHADINGMODELID_SINGLELAYERWATER of ShadingCommon.ush; the low 4 bits). */
constexpr int32 SingleLayerWaterId = 10;

/** The sums of one frame: log2 of E, of L, and of the albedo, with the counts of the sky and the water. */
struct FMeterSums
{
	double SumE = 0.0, SumL = 0.0, SumAlbedo = 0.0;
	int32 Count = 0, Sky = 0, Water = 0;
	/** The water pixels that the GPU reduction took with the floor under them (swc.MeterWater 1; in Count too). */
	int32 WaterTaken = 0;

	/** Adds a pixel: L in cd/m2 (the scene color over the pre-exposure), G-buffer B (metallic, specular) and C (the base color). The
	 *  denominator of the source (research/e5-meter-water.md): the Rec. 709 luminance of the diffuse color and of the specular
	 *  reflectance; 0.2 where the diffuse luminance is 0 (the sky, a black material), else the largest of the two and 0.01. */
	void Add(double L, const FLinearColor& GbufferB, const FLinearColor& GbufferC)
	{
		// A Single Layer Water pixel (the Unreal water, docs/WATER.md "The Unreal water") writes its own G-buffer: a dark base color, not the
		// ground under the water (the translucent source water left the ground in the G-buffer). The meter skips it.
		if ((FMath::RoundToInt32(GbufferB.A * 255.0f) & 0xF) == SingleLayerWaterId)
		{
			++Water;
			return;
		}
		L = FMath::Max(L, 1e-6);
		const float Metallic = GbufferB.R;
		const FLinearColor Diffuse = GbufferC * (1.0f - Metallic);
		const FLinearColor Reflectance = FMath::Lerp(FLinearColor(0.08f, 0.08f, 0.08f) * GbufferB.G, GbufferC, Metallic);
		const double DiffuseY = Luminance(Diffuse);
		const bool bZero = DiffuseY == 0.0;
		const double Albedo = bZero ? 0.2 : FMath::Max3(DiffuseY, Luminance(Reflectance), 0.01);
		SumE += FMath::Log2(PI * L / Albedo);
		SumL += FMath::Log2(L);
		Sky += bZero ? 1 : 0;
		if (!bZero)
		{
			SumAlbedo += FMath::Log2(Albedo);
		}
		++Count;
	}

	double E() const { return Count > 0 ? FMath::Exp2(SumE / Count) : 0.0; }
};

/** @return The color of a texel of a locked readback (the formats of the scene color and of the G-buffer; G-buffer C is an sRGB
 *  target, bLegacyAlbedoSrgb of GBufferInfo.cpp: its bytes are decoded). */
bool ReadTexel(const uint8* Data, int32 RowPitchPixels, EPixelFormat Format, bool bSrgb, int32 X, int32 Y, FLinearColor& Out)
{
	const int32 Index = Y * RowPitchPixels + X;
	switch (Format)
	{
	case PF_FloatRGBA:
		Out = reinterpret_cast<const FFloat16Color*>(Data)[Index].GetFloats();
		return true;
	case PF_FloatR11G11B10:
		Out = reinterpret_cast<const FFloat3Packed*>(Data)[Index].ToLinearColor();
		return true;
	case PF_A32B32G32R32F:
		Out = reinterpret_cast<const FLinearColor*>(Data)[Index];
		return true;
	case PF_B8G8R8A8:
	{
		const FColor Texel = reinterpret_cast<const FColor*>(Data)[Index];
		Out = bSrgb ? FLinearColor(Texel) : Texel.ReinterpretAsLinear();
		return true;
	}
	case PF_R8G8B8A8:
	{
		const uint8* Bytes = Data + Index * 4;
		const FColor Texel(Bytes[0], Bytes[1], Bytes[2], Bytes[3]);
		Out = bSrgb ? FLinearColor(Texel) : Texel.ReinterpretAsLinear();
		return true;
	}
	case PF_A2B10G10R10:
	{
		const uint32 Bits = reinterpret_cast<const uint32*>(Data)[Index];
		Out = FLinearColor((Bits & 0x3FF) / 1023.0f, ((Bits >> 10) & 0x3FF) / 1023.0f, ((Bits >> 20) & 0x3FF) / 1023.0f, (Bits >> 30) / 3.0f);
		return true;
	}
	default:
		return false;
	}
}

/** The readbacks of one frame of the watch. */
struct FInFlight
{
	TUniquePtr<FRHIGPUTextureReadback> Color, Normal, Base;
	/** The sums of the GPU reduction (swc.MeterGpu 1 or 2): Groups x SwcMeterGroupFloats floats. */
	TUniquePtr<FRHIGPUBufferReadback> Sums;
	int32 Groups = 0;
	EPixelFormat ColorFormat = PF_Unknown, NormalFormat = PF_Unknown, BaseFormat = PF_Unknown;
	bool bNormalSrgb = false, bBaseSrgb = false;
	FIntRect Rect;
	float PreExposure = 1.0f;
	float Metered = 0.0f;
	double Start = 0.0;
};

/** Reads the scene color (after the depth of field: HDR, after the water, the fog, and the translucency; with the pre-exposure; the
 *  callbacks of the motion blur run only while the motion blur is on) and the G-buffer of a
 *  frame of the game view. */
class FMeterProbe : public FSceneViewExtensionBase
{
public:
	explicit FMeterProbe(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	/** After the base pass of a game view, on a frame that the watch measures (swc.MeterWater 1): copies G-buffer B and C (the floor under
	 *  the water, before the Single Layer Water pass) for the reduction of the same frame. The render targets of the base pass, by name
	 *  (FSceneTextures::GetGBufferRenderTargets: "GBufferB", "GBufferC"; the scene texture uniform buffer has no G-buffer at this time). */
	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override
	{
		OpaqueGraph = nullptr;
		OpaqueB = OpaqueC = nullptr;
		if (!InView.bIsGameView || InView.bIsSceneCapture || CVarMeterWater.GetValueOnRenderThread() == 0 || CVarMeterGpu.GetValueOnRenderThread() == 0)
		{
			return;
		}
		{
			// Only when the last measure saw Single Layer Water pixels (the reduction counts them also with no copies): the source water
			// writes none, so the source path never copies (the A/A check of 2026-10-29); with the Unreal water the first measure with
			// water skips them and the next ones take them.
			FScopeLock Lock(&Guard);
			if (!bWatch || InFlight.IsValid() || FPlatformTime::Seconds() < NextWatch || LastWater == 0)
			{
				return;
			}
		}
		FRDGTextureRef SourceB = nullptr;
		FRDGTextureRef SourceC = nullptr;
		for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
		{
			FRDGTextureRef Target = RenderTargets.Output[Index].GetTexture();
			if (Target != nullptr && Target->Desc.Dimension == ETextureDimension::Texture2D)
			{
				SourceB = FCString::Strcmp(Target->Name, TEXT("GBufferB")) == 0 ? Target : SourceB;
				SourceC = FCString::Strcmp(Target->Name, TEXT("GBufferC")) == 0 ? Target : SourceC;
			}
		}
		if (SourceB == nullptr || SourceC == nullptr)
		{
			return;
		}
		auto Copy = [&GraphBuilder](FRDGTextureRef Source, const TCHAR* Name)
		{
			FRDGTextureDesc Desc = Source->Desc;
			Desc.Flags = TexCreate_ShaderResource;
			FRDGTextureRef Target = GraphBuilder.CreateTexture(Desc, Name);
			AddCopyTexturePass(GraphBuilder, Source, Target);
			return Target;
		};
		OpaqueB = Copy(SourceB, TEXT("SwcMeterOpaqueGBufferB"));
		OpaqueC = Copy(SourceC, TEXT("SwcMeterOpaqueGBufferC"));
		OpaqueGraph = &GraphBuilder;
	}

	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override
	{
		if (Pass != EPostProcessingPass::AfterDOF || !InView.bIsGameView || InView.bIsSceneCapture)
		{
			return;
		}
		FScopeLock Lock(&Guard);
		const bool bWatchDue = bWatch && !InFlight.IsValid() && FPlatformTime::Seconds() >= NextWatch;
		if (Pending.IsSet() || bWatchDue)
		{
			InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FMeterProbe::Measure));
		}
	}

	FScreenPassTexture Measure(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
	{
		const FScreenPassTextureSlice Scene = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
		const TRDGUniformBufferRef<FSceneTextureUniformParameters> Textures = Inputs.SceneTextures.SceneTextures.GetUniformBuffer();
		if (Scene.TextureSRV == nullptr || Textures == nullptr || Textures->GetParameters()->GBufferCTexture == nullptr
			|| Textures->GetParameters()->GBufferBTexture == nullptr)
		{
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}
		FRDGTextureRef Color = Scene.TextureSRV->Desc.Texture;
		FRDGTextureRef Normal = Textures->GetParameters()->GBufferBTexture;
		FRDGTextureRef Base = Textures->GetParameters()->GBufferCTexture;
		// The pre-exposure of a frame is the eye adaptation exposure of the frame before (no r.EyeAdaptation.PreExposureOverride).
		const float PreExposure = View.GetLastEyeAdaptationExposure();
		const float Metered = View.GetLastAverageSceneLuminance();
		TOptional<FRequest> Request;
		bool bWatchFrame = false;
		{
			FScopeLock Lock(&Guard);
			Request = Pending;
			Pending.Reset();
			bWatchFrame = bWatch && !InFlight.IsValid() && FPlatformTime::Seconds() >= NextWatch;
			if (bWatchFrame)
			{
				NextWatch = FPlatformTime::Seconds() + Interval;
			}
		}
		if (Request.IsSet())
		{
			// The line of a matched view: a direct read of the whole frame (one stall at the shot).
			FSwcMeterReadParameters* Parameters = GraphBuilder.AllocParameters<FSwcMeterReadParameters>();
			Parameters->SceneColor = Color;
			Parameters->Normal = Normal;
			Parameters->BaseColor = Base;
			const FIntRect Rect = Scene.ViewRect;
			const FRequest Line = Request.GetValue();
			GraphBuilder.AddPass(RDG_EVENT_NAME("SwcMeterProbe"), Parameters, ERDGPassFlags::Readback | ERDGPassFlags::NeverCull,
				[Parameters, Rect, PreExposure, Metered, Line](FRHICommandListImmediate& RHICmdList)
				{
					TArray<FLinearColor> Colors, Normals, Albedos;
					RHICmdList.ReadSurfaceData(Parameters->SceneColor->GetRHI(), Rect, Colors, FReadSurfaceDataFlags(RCM_MinMax));
					RHICmdList.ReadSurfaceData(Parameters->Normal->GetRHI(), Rect, Normals, FReadSurfaceDataFlags(RCM_MinMax));
					RHICmdList.ReadSurfaceData(Parameters->BaseColor->GetRHI(), Rect, Albedos, FReadSurfaceDataFlags(RCM_MinMax));
					FMeterSums Sums;
					const int32 Count = FMath::Min3(Colors.Num(), Normals.Num(), Albedos.Num());
					for (int32 Index = 0; Index < Count && PreExposure > 0.0f; ++Index)
					{
						Sums.Add(Luminance(Colors[Index]) / PreExposure, Normals[Index], Albedos[Index]);
					}
					Report(Line, Sums, PreExposure, Metered);
				});
		}
		if (bWatchFrame)
		{
			const int32 Mode = CVarMeterGpu.GetValueOnAnyThread();
			const bool bTwoD = Color->Desc.Dimension == ETextureDimension::Texture2D && Normal->Desc.Dimension == ETextureDimension::Texture2D
				&& Base->Desc.Dimension == ETextureDimension::Texture2D;
			const bool bGpu = Mode != 0 && bTwoD;
			const bool bCopies = Mode != 1 || !bGpu;
			TUniquePtr<FInFlight> Frame = MakeUnique<FInFlight>();
			if (bGpu)
			{
				FSwcMeterInputs Meter;
				Meter.SceneColor = Color;
				Meter.GBufferB = Normal;
				Meter.GBufferC = Base;
				// The floor under the water of the same frame (PostRenderBasePassDeferred_RenderThread), when it was copied.
				if (OpaqueGraph == &GraphBuilder && OpaqueB != nullptr && OpaqueC != nullptr && OpaqueB->Desc.Extent == Base->Desc.Extent)
				{
					Meter.OpaqueGBufferB = OpaqueB;
					Meter.OpaqueGBufferC = OpaqueC;
				}
				OpaqueGraph = nullptr;
				OpaqueB = OpaqueC = nullptr;
				Meter.Rect = Scene.ViewRect;
				Meter.Step = WatchStep;
				Meter.PreExposure = PreExposure;
				const FRDGBufferRef Sums = AddSwcMeterPass(GraphBuilder, Meter, Frame->Groups);
				Frame->Sums = MakeUnique<FRHIGPUBufferReadback>(TEXT("SwcMeterSums"));
				AddEnqueueCopyPass(GraphBuilder, Frame->Sums.Get(), Sums, Frame->Groups * SwcMeterGroupFloats * sizeof(float));
			}
			if (bCopies)
			{
				Frame->Color = MakeUnique<FRHIGPUTextureReadback>(TEXT("SwcMeterColor"));
				Frame->Normal = MakeUnique<FRHIGPUTextureReadback>(TEXT("SwcMeterNormal"));
				Frame->Base = MakeUnique<FRHIGPUTextureReadback>(TEXT("SwcMeterBase"));
			}
			Frame->ColorFormat = Color->Desc.Format;
			Frame->NormalFormat = Normal->Desc.Format;
			Frame->BaseFormat = Base->Desc.Format;
			Frame->bNormalSrgb = EnumHasAnyFlags(Normal->Desc.Flags, TexCreate_SRGB);
			Frame->bBaseSrgb = EnumHasAnyFlags(Base->Desc.Flags, TexCreate_SRGB);
			Frame->Rect = Scene.ViewRect;
			Frame->PreExposure = PreExposure;
			Frame->Metered = Metered;
			Frame->Start = FPlatformTime::Seconds();
			if (bCopies)
			{
				AddEnqueueCopyPass(GraphBuilder, Frame->Color.Get(), Color);
				AddEnqueueCopyPass(GraphBuilder, Frame->Normal.Get(), Normal);
				AddEnqueueCopyPass(GraphBuilder, Frame->Base.Get(), Base);
			}
			FScopeLock Lock(&Guard);
			InFlight = MoveTemp(Frame);
		}
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	/** Reads the readbacks of the watch when the GPU has written them (a few frames later). */
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override
	{
		TUniquePtr<FInFlight> Frame;
		{
			FScopeLock Lock(&Guard);
			if (!InFlight.IsValid())
			{
				return;
			}
			const bool bCopiesReady = !InFlight->Color.IsValid() || (InFlight->Color->IsReady() && InFlight->Normal->IsReady() && InFlight->Base->IsReady());
			const bool bSumsReady = !InFlight->Sums.IsValid() || InFlight->Sums->IsReady();
			if (!bCopiesReady || !bSumsReady)
			{
				if (FPlatformTime::Seconds() - InFlight->Start > 1.0)
				{
					InFlight.Reset();
					++Dropped;
				}
				return;
			}
			Frame = MoveTemp(InFlight);
		}
		FMeterSums Sums;
		bool bFormats = Frame->PreExposure > 0.0f;
		if (Frame->Color.IsValid())
		{
			bFormats = ReadCopies(*Frame, Sums) && bFormats;
		}
		FMeterSums GpuSums;
		if (Frame->Sums.IsValid())
		{
			const float* Data = static_cast<const float*>(Frame->Sums->Lock(Frame->Groups * SwcMeterGroupFloats * sizeof(float)));
			for (int32 Group = 0; Data != nullptr && Group < Frame->Groups; ++Group)
			{
				const float* Values = Data + Group * SwcMeterGroupFloats;
				GpuSums.SumE += Values[0];
				GpuSums.SumL += Values[1];
				GpuSums.SumAlbedo += Values[2];
				GpuSums.Count += FMath::RoundToInt32(Values[3]);
				GpuSums.Sky += FMath::RoundToInt32(Values[4]);
				GpuSums.Water += FMath::RoundToInt32(Values[5]);
				GpuSums.WaterTaken += FMath::RoundToInt32(Values[6]);
			}
			Frame->Sums->Unlock();
			if (Frame->Color.IsValid() && Sums.Count > 0 && GpuSums.Count > 0)
			{
				// swc.MeterGpu 2: the two reductions of the same frame.
				UE_LOG(LogSwcEnvironment, Log, TEXT("meter compare: E %.4f lux (copies) against %.4f lux (GPU), %.5f EV; samples %d and %d, sky %d and %d, water %d and %d"),
					Sums.E(), GpuSums.E(), FMath::Log2(GpuSums.E() / Sums.E()), Sums.Count, GpuSums.Count, Sums.Sky, GpuSums.Sky, Sums.Water, GpuSums.Water);
			}
			Sums = GpuSums;
			FScopeLock Lock(&Guard);
			LastWater = GpuSums.Water;
		}
		if (CVarMeterDiag.GetValueOnAnyThread() != 0 && FPlatformTime::Seconds() >= NextDiag)
		{
			NextDiag = FPlatformTime::Seconds() + 1.0;
			const double Exposure = Frame->PreExposure;
			UE_LOG(LogSwcEnvironment, Log, TEXT("meter diag: %d accepted, %d sky, %d water pixels (%d taken with the floor, the rest skipped); the exposure of the frame %.4g (EV %.2f); E %.0f lux; the last good measure %.1f s ago"),
				Sums.Count, Sums.Sky, Sums.Water, Sums.WaterTaken, Exposure, Exposure > 0.0 ? -FMath::Log2(1.2 * Exposure) : 0.0, Sums.Count > 0 ? Sums.E() : 0.0,
				Latest.Time > 0.0 ? FPlatformTime::Seconds() - Latest.Time : -1.0);
		}
		if (!bFormats || Sums.Count == 0)
		{
			UE_CLOG(!bWarned, LogSwcEnvironment, Warning, TEXT("meter: no measure (the formats %s, %s, %s)"), GetPixelFormatString(Frame->ColorFormat),
				GetPixelFormatString(Frame->NormalFormat), GetPixelFormatString(Frame->BaseFormat));
			bWarned = true;
			return;
		}
		FScopeLock Lock(&Guard);
		Latest.SourceE = Sums.E();
		Latest.UnrealMetered = Frame->Metered;
		Latest.Exposure = Frame->PreExposure;
		Latest.SkyPart = static_cast<double>(Sums.Sky) / Sums.Count;
		Latest.Dropped = Dropped;
		Latest.Time = FPlatformTime::Seconds();
		++Latest.Serial;
	}

	/** The CPU loop of the copies (swc.MeterGpu 0 or 2): each WatchStep-th pixel. @return False for an unknown format. */
	static bool ReadCopies(FInFlight& Frame, FMeterSums& Sums)
	{
		int32 ColorPitch = 0, NormalPitch = 0, BasePitch = 0;
		const uint8* ColorData = static_cast<const uint8*>(Frame.Color->Lock(ColorPitch));
		const uint8* NormalData = static_cast<const uint8*>(Frame.Normal->Lock(NormalPitch));
		const uint8* BaseData = static_cast<const uint8*>(Frame.Base->Lock(BasePitch));
		bool bFormats = ColorData != nullptr && NormalData != nullptr && BaseData != nullptr && Frame.PreExposure > 0.0f;
		for (int32 Y = Frame.Rect.Min.Y; bFormats && Y < Frame.Rect.Max.Y; Y += WatchStep)
		{
			for (int32 X = Frame.Rect.Min.X; X < Frame.Rect.Max.X; X += WatchStep)
			{
				FLinearColor C, B, A;
				if (!ReadTexel(ColorData, ColorPitch, Frame.ColorFormat, false, X, Y, C)
					|| !ReadTexel(NormalData, NormalPitch, Frame.NormalFormat, Frame.bNormalSrgb, X, Y, B)
					|| !ReadTexel(BaseData, BasePitch, Frame.BaseFormat, Frame.bBaseSrgb, X, Y, A))
				{
					bFormats = false;
					break;
				}
				Sums.Add(Luminance(C) / Frame.PreExposure, B, A);
			}
		}
		Frame.Color->Unlock();
		Frame.Normal->Unlock();
		Frame.Base->Unlock();
		return bFormats;
	}

	static void Report(const FRequest& Request, const FMeterSums& Sums, float PreExposure, float Metered)
	{
		if (Sums.Count == 0)
		{
			UE_LOG(LogSwcEnvironment, Warning, TEXT("meter probe %s: no pixels"), *Request.Label);
			return;
		}
		const double SourceEv = swc::environment::source_ev(Sums.E(), Request.EvMin, Request.EvMax, Request.Compensation);
		// The exposure of Unreal as an EV of the source rule: exposure = 1 / (1.2 x 2^EV).
		const double ClientEv = PreExposure > 0.0f ? -FMath::Log2(1.2 * PreExposure) : 0.0;
		const int32 Ground = Sums.Count - Sums.Sky;
		UE_LOG(LogSwcEnvironment, Log, TEXT("meter probe %s: the source meter E %.0f lux, its EV %.2f (EV %.1f to %.1f, compensation %.2f); the EV of the client %.2f "
			"(exposure %.3g; the Unreal meter %.4g): the client minus the source %.2f EV; the mean luminance %.0f cd/m2, no material color (the sky) %.0f%%, "
			"the mean albedo of the rest %.3f; %d pixels"),
			*Request.Label, Sums.E(), SourceEv, Request.EvMin, Request.EvMax, Request.Compensation, ClientEv, PreExposure, Metered, ClientEv - SourceEv,
			FMath::Exp2(Sums.SumL / Sums.Count), 100.0 * Sums.Sky / Sums.Count, Ground > 0 ? FMath::Exp2(Sums.SumAlbedo / Ground) : 0.0, Sums.Count);
	}

	FCriticalSection Guard;
	TOptional<FRequest> Pending;
	bool bWatch = false;
	double Interval = 0.25;
	double NextWatch = 0.0;
	TUniquePtr<FInFlight> InFlight;
	SwcMeterProbe::FResult Latest;
	bool bWarned = false;
	uint32 Dropped = 0;
	double NextDiag = 0.0;
	/** The copies of G-buffer B and C after the base pass (render thread; valid only in the graph builder of that frame). */
	FRDGBuilder* OpaqueGraph = nullptr;
	/** The Single Layer Water pixels of the last GPU measure (0: no copies for the next one). */
	int32 LastWater = 0;
	FRDGTextureRef OpaqueB = nullptr;
	FRDGTextureRef OpaqueC = nullptr;
};

TSharedPtr<FMeterProbe, ESPMode::ThreadSafe>& Probe()
{
	static TSharedPtr<FMeterProbe, ESPMode::ThreadSafe> Extension;
	if (!Extension.IsValid())
	{
		Extension = FSceneViewExtensions::NewExtension<FMeterProbe>();
	}
	return Extension;
}
} // namespace

void SwcMeterProbe::Request(const FString& Label, double EvMin, double EvMax, double Compensation)
{
	FScopeLock Lock(&Probe()->Guard);
	Probe()->Pending = FRequest{Label, EvMin, EvMax, Compensation};
}

void SwcMeterProbe::SetWatch(bool bOn, double Interval)
{
	FScopeLock Lock(&Probe()->Guard);
	Probe()->bWatch = bOn;
	Probe()->Interval = Interval >= 0.0 ? Interval : FMath::Max(0.0f, CVarMeterInterval.GetValueOnGameThread());
}

bool SwcMeterProbe::GetLatest(FResult& Out)
{
	FScopeLock Lock(&Probe()->Guard);
	Out = Probe()->Latest;
	return Out.Serial > 0;
}
