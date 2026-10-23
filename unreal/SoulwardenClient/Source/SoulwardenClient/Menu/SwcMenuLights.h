// The intensity of the lights of the menu scene (stage F4 of docs/FRONTEND.md). The source attenuation of a light with the bulb
// size r is 1 / (1 + d / r)^2 (docs/ENVIRONMENT.md, E2). The world lights read d from the bulb surface, so that the falloff is
// r^2 / D^2 from the center (an Unreal sphere light with the source radius r): that is exact far from the bulb. The light rig of
// the menu has bulbs of 0.5 m to 30 m within a few metres of the Player Entity: the body is near or in four of them, and r^2 / D^2
// gives up to nine times the light of the source there (white bodies in the run f4-scene). Here d is the distance from the center
// of the light, as the shader reads length(L): the attenuation stays below 1 in the bulb. An Unreal light has an inverse-square
// falloff, so the candela is matched at one reference distance: the body (the Player Entity + 1 m) when the light reaches it, else
// the middle of its range (a choice of this client: the rig lights the body; the other lights light the camp). Far from the bulb
// (D >> r) the candela is the one of the world rule, pi x 10,000 x DiffuseMultiplier x r^2.
// Plain C++: tools/test-menu.sh tests it.

#pragma once

#include <algorithm>
#include <cmath>

namespace swc::menu {

/** The luminance of one unit of a source light color (cd/m2): kSourceUnitLuminance of Environment/SwcLocalLightRules.h. */
constexpr double LightUnitLuminance = 10000.0;
/** The smallest bulb (m): kMinBulb of the world rules. */
constexpr double LightMinBulb = 0.01;

/** @return The reference distance (m) of a light: the body when the light reaches it, else the middle of its range. */
inline double LightReference(double BodyDistance, double Range)
{
	return BodyDistance < Range ? BodyDistance : 0.5 * Range;
}

/** @return The candela that gives the source illuminance pi x 10,000 x Diffuse / (1 + D / r)^2 at the reference distance D. */
inline double LightCandela(double Diffuse, double Bulb, double Reference)
{
	constexpr double Pi = 3.14159265358979323846;
	const double R = std::max(Bulb, LightMinBulb);
	const double D = std::max(Reference, 0.0);
	// The candela I gives I / D^2 at D: I = pi x 10,000 x Diffuse x D^2 / (1 + D / R)^2 = ... x (D R / (R + D))^2, which goes to
	// R^2 far from the bulb.
	const double Equivalent = D * R / (R + D);
	return Pi * LightUnitLuminance * Diffuse * Equivalent * Equivalent;
}

/** @return The source radius of the Unreal light (m): the bulb, at most half the reference distance (the body stays outside the
 *  sphere of the light). */
inline double LightSourceRadius(double Bulb, double Reference)
{
	return std::min(std::max(Bulb, LightMinBulb), 0.5 * std::max(Reference, 0.0));
}

/** @return The falloff exponent n of an Unreal light with no inverse square, (1 - (d / range)^2)^n, that equals the source
 *  attenuation 1 / (1 + d / r)^2 at d = r (1/4), or at d = range / 2 for a bulb of half the range or more (the creation scene,
 *  stage Q3: its key lights have bulbs of 1 m to 12 m, where the inverse square is many times too bright near the light). */
inline double SourceFalloffExponent(double Bulb, double Range)
{
	const double R = std::max(Bulb, LightMinBulb);
	const double Reach = std::max(Range, LightMinBulb);
	const double D = R < 0.5 * Reach ? R : 0.5 * Reach;
	const double Target = 1.0 / ((1.0 + D / R) * (1.0 + D / R));
	const double Window = 1.0 - (D / Reach) * (D / Reach);
	return std::max(std::log(Target) / std::log(Window), 0.1);
}

/** The largest falloff exponent of the source falloff: a light with a larger fit (a small bulb in a long range) keeps the inverse
 *  square of LightCandela, which is the source attenuation far from the bulb. */
constexpr double MaxSourceFalloffExponent = 16.0;

/** @return The factor of one ambient light of the source (the field Ambient of LightComponent: Light_Shadow, Light_ShadowAmbient,
 *  Light_Bounce_Ambient of the creation level) on the ambient at the distance D: lerp(1, Value, the source attenuation
 *  1 / (1 + D / r)^2), 1 beyond the range. Value is the color x DiffuseMultiplier: below 1 the light darkens the ambient (the
 *  near-black shadow lights of the ship hold), above 1 it brightens it (the bounce lights). That an ambient light multiplies the
 *  ambient is an inference [I] (the names and the values of the level; stage Q3 of docs/FRONTEND.md). */
inline double AmbientLightFactor(double Value, double Bulb, double Range, double Distance)
{
	if (Distance >= Range)
	{
		return 1.0;
	}
	const double R = std::max(Bulb, LightMinBulb);
	const double Attenuation = 1.0 / ((1.0 + std::max(Distance, 0.0) / R) * (1.0 + std::max(Distance, 0.0) / R));
	return 1.0 + (Value - 1.0) * Attenuation;
}

} // namespace swc::menu
