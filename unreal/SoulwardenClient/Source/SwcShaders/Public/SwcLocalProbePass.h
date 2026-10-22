#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FSceneView;
class FTextureResource;

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
	/** The specular term (AST-247 part 2): the cube of the probe with the pre-filtered mips of the source (null: no specular light
	 *  of its own), its color x SpecularMultiplier x the time-of-day gain, the box projection and the half sizes of its proxy box (BoxSize / 2, cm, on the axes of the box). */
	FTextureResource* SpecCube = nullptr;
	FVector3f SpecColor = FVector3f::ZeroVector;
	bool bBoxProject = false;
	FVector3f BoxHalf = FVector3f::OneVector;
};

/** The probes of one frame, in the order of the source light list (the first takes its fade first). */
struct FSwcProbeShadingList
{
	static constexpr int32 MaxProbes = 64;
	TArray<FSwcProbeShadingProbe> Probes;
	/** The ambient cube of the world probe (the sky light of the client), the same unit as the probes. */
	FVector3f WorldPositive = FVector3f::OneVector;
	FVector3f WorldNegative = FVector3f::OneVector;
	/** The pass of the material AO (swc.LocalProbes 2). */
	bool bAo = false;
	/** The specular term of the probes (swc.ProbeSpecular 1): the cube of the world probe and its color, and the scene luminance of
	 *  one source unit (cd/m2; the pass multiplies the pre-exposure). */
	bool bSpecular = false;
	FTextureResource* WorldSpecCube = nullptr;
	FVector3f WorldSpecColor = FVector3f::OneVector;
	float SourceLuminance = 10000.0f;
	/** The SSDO strength of the specular occlusion (r_ssdoAmountReflection of the source: 3; 0 no SSDO). */
	float SsdoReflection = 3.0f;
	/** The specular term uses the first probes of the list that have a cube. */
	static constexpr int32 MaxSpecular = 8;
};

/**
 * Adds the pass of the local probes after the base pass: for each lit pixel, the ordered blend of the local probes with the world
 * probe in the rest (tiledshading.cfib ApplyProbe of the source), over the world probe alone, multiplies the material AO in
 * G-buffer C (alpha). Lumen and the sky light read that AO; so a pixel in the box of a dark interior probe gets the light of its
 * probe and a pixel outside gets the world probe. The ratio is a luminance (no tint) and at most 1 (the AO cannot add light).
 */
SWCSHADERS_API void AddSwcLocalProbePass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SceneDepth,
	FRDGTextureRef GBufferA, FRDGTextureRef GBufferB, FRDGTextureRef GBufferC, const FSwcProbeShadingList& List);

/**
 * Adds the specular term of the probes for each pixel (AST-247 part 2; research weapon-materials.md section 2, tiledshading.cfib of the
 * source): for each lit pixel the reflection direction R, the ordered blend of the local probes (the same weights as the AO pass) with
 * the world probe in the rest, each cube sampled at R (with the box projection of its probe) at the mip 6 - 6 x smoothness, times its
 * color; times the environment BRDF of F0, the roughness, and N.V, and the specular occlusion saturate((N.V + ao)^s - 1 + ao) with
 * ao = max(material AO, 0.3). The result adds to the scene color (the Unreal reflections are off while it runs). PreIntegratedGF: the
 * environment BRDF table of the renderer (GSystemTextures.PreintegratedGF).
 */
SWCSHADERS_API void AddSwcProbeSpecularPass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SceneColor,
	FRDGTextureRef SceneDepth, FRDGTextureRef GBufferA, FRDGTextureRef GBufferB, FRDGTextureRef GBufferC, FRHITexture* PreIntegratedGF,
	const FSwcProbeShadingList& List);
