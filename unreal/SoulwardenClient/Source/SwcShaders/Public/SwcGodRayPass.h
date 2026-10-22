#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FSceneView;

/** The values of the sun ray passes of one frame (AST-232; research/godrays.md "Native rule"). */
struct FSwcGodRayInputs
{
	/** The scene color (HDR, with the pre-exposure) and its view rectangle; the scene depth (device depth of the view). */
	FRDGTextureRef SceneColor = nullptr;
	FIntRect SceneColorRect;
	FRDGTextureRef SceneDepth = nullptr;
	FIntRect DepthRect;
	/** The scene color to the source units: 1 / (pre-exposure x 10,000 cd/m2). */
	float SourceScale = 1.0f;
	/** The depth of the sky (cm): the normalized depth of the source is the linear view depth over it. */
	float FarDepth = 1433600.0f;
	/** The screen point of the active light (UV, not clamped), the signed facing f, and the attenuation (0 to 10). */
	FVector2f SunUV = FVector2f::ZeroVector;
	float Facing = 0.0f;
	float Attenuation = 1.0f;
	/** 0 the rays; 1 the mask only; 2 the mask and the first pass (the debug views). */
	int32 Stage = 0;
};

/**
 * Adds the mask pass and the two radial passes (steps 0.1 and 0.025) into Output (RGBA8, half the size of the view): the blurred
 * rays of the source, for the screen blend of the grade.
 */
SWCSHADERS_API void AddSwcGodRayPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FSwcGodRayInputs& Inputs, FRDGTextureRef Output);
