#pragma once

#include "CoreMinimal.h"

/**
 * The meter of the source for stage E5 (the light level of the scene). The source meter (hdrpostprocess@hdrsampleluminitialps of the
 * game copy, docs/ENVIRONMENT.md): the illuminance E = pi x L / albedo of each pixel (L the Rec. 709 luminance of the scene in cd/m2,
 * the albedo the Rec. 709 luminance of the G-buffer color, at least 0.01; 0.2 where the G-buffer has no color: the sky), the geometric
 * mean of the screen. The luminance is the scene color after the water, the fog, and the translucency; the material is the opaque
 * G-buffer at that place (the ground under the water: the water of the client is translucent), with the denominator of
 * research/e5-meter-water.md (the diffuse and the specular reflectance).
 */
namespace SwcMeterProbe
{
/** One measure of a frame of the game view. */
struct FResult
{
	/** The geometric mean of E (lux) by the source rule. */
	double SourceE = 0.0;
	/** The mean luminance that the Unreal meter had on that frame (its metered value, the luminance over the albedo). */
	double UnrealMetered = 0.0;
	/** The exposure that the frame had (the eye adaptation of Unreal: the pre-exposure of the frame). */
	double Exposure = 0.0;
	/** The part of the frame with no material color (the sky). */
	double SkyPart = 0.0;
	/** The readbacks that were not ready after one second (dropped). */
	uint32 Dropped = 0;
	/** The time of the measure (FPlatformTime::Seconds), and a serial that grows with each measure. */
	double Time = 0.0;
	uint32 Serial = 0;
};

/** Measures the next frame of the game view and logs one line (Label; the EV bounds and the compensation of the time of day). */
void Request(const FString& Label, double EvMin, double EvMax, double Compensation);

/** On: measures a frame each Interval seconds with an asynchronous readback (no stall). */
void SetWatch(bool bOn, double Interval = 0.25);

/** @return The newest measure of the watch, if one is there. */
bool GetLatest(FResult& Out);
}
