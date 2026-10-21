// Tests of the rules of the local lights (Environment/SwcLocalLightRules, stage E2; docs/ENVIRONMENT.md): the Unreal intensity
// against the attenuation of the source (the bulb size), the rect light, the draw distance, the grid of the lights near a point,
// the owner rule of the NPC lights, and the matrix product of a bound light.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Environment/SwcLocalLightRules.h"

namespace {

namespace env = swc::environment;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance * std::max(1.0, std::fabs(b));
}

constexpr double kPi = 3.14159265358979323846;

void test_intensity() {
    // The source (research/lights-emission.md "Point lights"): the light C x M x K with K = (1 + 1 / r)^2 and the distance weight
    // A = f / (1 + max(d - r, 0) / r)^2, f = sat(5 (1 - d / R))^2, d from the center. Unreal: E = I / (d^2 + 0.0001) x UEfade times
    // the light function point_falloff; the radiance of a white surface that faces the light is E / pi.
    for (const double multiplier : {0.1, 2.0, 4.0}) {
        for (const double bulb : {0.05, 0.6, 5.0}) {
            env::LocalLight light;
            light.diffuse = multiplier;
            light.bulb = bulb;
            light.range = 6.0;
            const double candela = env::light_candela(light, 1.0);
            for (const double d : {0.0, 0.02, 0.5, 2.0, 4.8, 5.5, 5.99}) {
                const double f = std::pow(std::clamp(5.0 * (1.0 - d / light.range), 0.0, 1.0), 2.0);
                const double K = (1.0 + 1.0 / bulb) * (1.0 + 1.0 / bulb);
                const double source = env::kSourceUnitLuminance * multiplier * K * f / std::pow(1.0 + std::max(d - bulb, 0.0) / bulb, 2.0);
                const double fade = std::pow(std::clamp(1.0 - std::pow(d / light.range, 4.0), 0.0, 1.0), 2.0);
                const double unreal = candela / (d * d + 0.0001) * fade * env::point_falloff(d, bulb, light.range) / kPi;
                check(near(unreal, source, 1e-6 * std::max(source, 1.0)), "a point light: the Unreal light with the falloff function has the source falloff (M " +
                      std::to_string(multiplier) + ", bulb " + std::to_string(bulb) + ", d " + std::to_string(d) + ")");
            }
            check(env::source_radius(light) == 0.0, "a point light has no sphere radius (the bulb is only the attenuation)");
        }
    }
    double largest = 0.0;
    for (int i = 0; i < 10000; ++i) {
        largest = std::max(largest, env::point_falloff(0.01 + 6.0 * i / 10000.0, 0.01, 6.0));
    }
    check(largest > 2.8 && largest < env::kPointFalloffGain, "the falloff correction stays under the gain of the light function (" + std::to_string(largest) + ")");
    check(near(env::point_falloff(6.0 * (1.0 - 1e-5), 0.01, 6.0), 25.0 / 16.0, 1e-3) && env::point_falloff(6.0, 0.01, 6.0) == 0.0,
          "the falloff correction at the range end: 25 / 16, then 0");
    env::LocalLight scaled;
    scaled.diffuse = 2.0;
    scaled.bulb = 0.6;
    check(near(env::light_candela(scaled, 0.5), 0.5 * env::light_candela(scaled, 1.0), 1e-12), "the look scale multiplies the intensity");
    check(near(env::light_candela(scaled, 1.0), kPi * 10000.0 * 2.0 * 2.56, 1e-9), "a torch light (M 2, bulb 0.6 m): about 160,800 cd");
    env::LocalLight zero;
    zero.bulb = 0.0;
    check(near(env::point_bulb(zero), env::kMinPointBulb, 1e-12) && env::light_candela(zero, 1.0) > 0.0, "a bulb of 0 has the smallest bulb");
    env::LocalLight rect;
    rect.type = env::LocalLight::Type::rect;
    rect.diffuse = 2.0;
    rect.width = 1.5;
    rect.height = 3.0;
    check(near(env::light_candela(rect, 1.0), 10000.0 * 2.0 * 4.5, 1e-9), "a rect: the surface luminance 10,000 x M over its area");
    check(env::source_radius(rect) == 0.0, "a rect has no sphere radius");
    env::LocalLight spot;
    spot.type = env::LocalLight::Type::spot;
    spot.diffuse = 2.5;
    spot.bulb = 0.05;
    check(near(env::light_candela(spot, 1.0), kPi * 10000.0 * 2.5 * 0.0025, 1e-9), "a spot: the rule of a point (the projector bulb)");
}

void test_draw_distance() {
    env::LocalLight light;
    light.range = 1.5;
    check(near(env::draw_distance(light), 50.0, 1e-12), "a small light: the smallest draw distance");
    light.range = 4.0;
    check(near(env::draw_distance(light), 100.0, 1e-12), "25 times the range");
    light.range = 20.0;
    check(near(env::draw_distance(light), 250.0, 1e-12), "a large light: the largest draw distance");
    light.range = 6.0;
    light.cap = 80.0;
    check(near(env::draw_distance(light), 80.0, 1e-12), "the cap of the source limits the draw distance");
    light.cap = 300.0;
    check(near(env::draw_distance(light), 150.0, 1e-12), "a cap above the rule changes nothing");
}

void test_grid() {
    env::LightGrid grid(64.0);
    grid.add(0, {8880.0, 4250.0, 140.0});
    grid.add(1, {8900.0, 4260.0, 150.0});
    grid.add(2, {8700.0, 4250.0, 140.0});
    grid.add(3, {-10.0, -10.0, 0.0});
    check(grid.size() == 4, "the grid has 4 lights");
    std::vector<int> out;
    grid.query({8885.0, 4255.0, 140.0}, 30.0, out);
    const auto has = [&](int index) { return std::find(out.begin(), out.end(), index) != out.end(); };
    check(has(0) && has(1) && !has(2) && !has(3), "the query finds the cells near the point");
    out.clear();
    grid.query({0.0, 0.0, 0.0}, 20.0, out);
    check(out.size() == 1 && out[0] == 3, "a negative place is in its cell");
    out.clear();
    grid.query({8800.0, 4250.0, 140.0}, 110.0, out);
    check(has(0) && has(1) && has(2), "a large radius finds the lights of several cells");
}

void test_owner() {
    const std::string slice = "slices/characters/mission/npc_0422_innkeep.dynamicslice";
    const env::Vec3 root{8870.0, 4240.0, 141.0};
    check(env::owns_lights(slice, root, "Slices/Characters/Mission/NPC_0422_Innkeep.dynamicslice", {8875.0, 4245.0, 141.0}),
          "the creature of the slice near its place owns the lights (the path in any case)");
    check(!env::owns_lights(slice, root, slice, {8870.0, 4271.0, 141.0}), "a creature of the slice 31 m away does not");
    check(!env::owns_lights(slice, root, "slices/characters/wolf_grey.dynamicslice", root), "a creature of another slice does not");
    check(!env::owns_lights("", root, "", root), "a static light has no owner");
}

void test_matrix() {
    // The light of a bound NPC: the creature matrix times the local matrix of the light (rows, translation in column 3).
    const double a = kPi / 2.0;
    const std::array<double, 16> creature{std::cos(a), -std::sin(a), 0, 100.0, std::sin(a), std::cos(a), 0, 200.0, 0, 0, 1, 10.0, 0, 0, 0, 1};
    const std::array<double, 16> local{1, 0, 0, 1.0, 0, 1, 0, 0.0, 0, 0, 1, 2.0, 0, 0, 0, 1};
    const std::array<double, 16> world = env::multiply(creature, local);
    check(near(world[3], 100.0, 1e-9) && near(world[7], 201.0, 1e-9) && near(world[11], 12.0, 1e-9),
          "a light 1 m in front of a creature that faces +Y is 1 m north of it, 2 m up");
    check(near(world[0], 0.0, 1e-9) && near(world[4], 1.0, 1e-9), "its +X axis turns with the creature");
}

} // namespace

int main() {
    test_intensity();
    test_draw_distance();
    test_grid();
    test_owner();
    test_matrix();
    std::printf("local light rules: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
