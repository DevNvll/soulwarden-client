#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FSceneView;

/** One local environment probe for the pixel pass (stage E5 step 3, research/e5-local-probes.md), in Unreal axes and units. */
struct FSwcProbeShadingProbe
{
	/** The center of the influence box, Unreal world (cm). */
	FVector Center = FVector::ZeroVector;
	/** The three rows that give the box space -1 to 1: the unit axes of the box over its half sizes (1/cm). */
	FVector3f Rows[3] = {FVector3f::ZeroVector, FVector3f::ZeroVector, FVector3f::ZeroVector};
	/** The edge fade of the source (AttenuationFalloffMax, quantized to 1/255, at least 0.001). */
	float Falloff = 1.0f;
	/** The luminance of the diffuse light of the probe toward +X, +Y, +Z and -X, -Y, -Z (the ambient cube; the same unit as World). */
	FVector3f Positive = FVector3f::ZeroVector;
	FVector3f Negative = FVector3f::ZeroVector;
};

/** The probes of one frame, in the order of the source light list (the first takes its fade first). */
struct FSwcProbeShadingList
{
	static constexpr int32 MaxProbes = 64;
	TArray<FSwcProbeShadingProbe> Probes;
	/** The ambient cube of the world probe (the sky light of the client), the same unit as the probes. */
	FVector3f WorldPositive = FVector3f::OneVector;
	FVector3f WorldNegative = FVector3f::OneVector;
};

/**
 * Adds the pass of the local probes after the base pass: for each lit pixel, the ordered blend of the local probes with the world
 * probe in the rest (tiledshading.cfib ApplyProbe of the source), over the world probe alone, multiplies the material AO in
 * G-buffer C (alpha). Lumen and the sky light read that AO; so a pixel in the box of a dark interior probe gets the light of its
 * probe and a pixel outside gets the world probe. The ratio is a luminance (no tint) and at most 1 (the AO cannot add light).
 */
SWCSHADERS_API void AddSwcLocalProbePass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SceneDepth,
	FRDGTextureRef GBufferA, FRDGTextureRef GBufferB, FRDGTextureRef GBufferC, const FSwcProbeShadingList& List);
