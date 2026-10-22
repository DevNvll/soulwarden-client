#pragma once

#include "CoreMinimal.h"

class FTextureRenderTargetResource;

/**
 * The sun rays of the source (AST-232; research/godrays.md; docs/ENVIRONMENT.md "The sun rays"): a scene view extension runs the mask
 * and the two radial passes of the source after the depth of field (the HDR scene color) into a half-size RGBA8 target; the grade
 * M_SwcGrade adds that target with the screen blend of the source after its sRGB step (the environment sets its tint RayTint).
 */
namespace SwcGodRays
{
struct FSettings
{
	/** The rays of this frame: the TOD gates of the source passed and swc.GodRays is on. */
	bool bActive = false;
	/** The direction toward the active light (Unreal world; the sun, the moon at night). */
	FVector Toward = FVector::UpVector;
	/** "Sun rays attenuation" of the TOD (the screen radius of the falloff). */
	float Attenuation = 1.0f;
	/** The depth of the sky of the source (cm). */
	float FarDepth = 1433600.0f;
	/** swc.GodRays.Debug: 0 the rays, 1 the mask, 2 the first pass. */
	int32 Stage = 0;
	/** The half-size target that the grade reads (its resource; set on the game thread). */
	FTextureRenderTargetResource* Target = nullptr;
};

void SetSettings(const FSettings& Settings);
}
