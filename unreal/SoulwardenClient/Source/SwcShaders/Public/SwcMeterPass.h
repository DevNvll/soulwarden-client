#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

/** The inputs of the meter reduction of one frame (AST-273 P4; the source meter of Environment/SwcMeterProbe). */
struct FSwcMeterInputs
{
	/** The scene color (HDR, with the pre-exposure), the G-buffer B (metallic, specular) and C (the base color), and the view rectangle. */
	FRDGTextureRef SceneColor = nullptr;
	FRDGTextureRef GBufferB = nullptr;
	FRDGTextureRef GBufferC = nullptr;
	/** The G-buffer B and C after the base pass (the floor under the Single Layer Water); null: the meter skips the water pixels. */
	FRDGTextureRef OpaqueGBufferB = nullptr;
	FRDGTextureRef OpaqueGBufferC = nullptr;
	FIntRect Rect;
	/** The step between the samples in each direction (pixels). */
	int32 Step = 6;
	float PreExposure = 1.0f;
};

/** The floats of one group in the output: the sums of log2 E, log2 L, log2 of the albedo, the count of the samples, the count of the sky. */
constexpr int32 SwcMeterGroupFloats = 8;

/**
 * Adds the reduction pass of the meter. @return A buffer of OutGroups x SwcMeterGroupFloats floats (the sums of each group of
 * 16 x 16 samples), for a readback.
 */
SWCSHADERS_API FRDGBufferRef AddSwcMeterPass(FRDGBuilder& GraphBuilder, const FSwcMeterInputs& Inputs, int32& OutGroups);
