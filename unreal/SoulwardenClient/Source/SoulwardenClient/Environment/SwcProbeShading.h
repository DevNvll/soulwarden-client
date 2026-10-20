#pragma once

#include "CoreMinimal.h"
#include "SwcLocalProbePass.h"

/**
 * The local probes of each pixel (stage E5 step 3, swc.LocalProbes 2): a scene view extension that runs the pass of the module
 * SwcShaders after the base pass with the probe list of the game thread (Environment/SwcEnvironmentSky.cpp UpdatePixelProbes).
 */
namespace SwcProbeShading
{
/** Sets the probes of the next frames (the game thread). An empty list turns the pass off. */
void SetList(FSwcProbeShadingList&& List);
}
