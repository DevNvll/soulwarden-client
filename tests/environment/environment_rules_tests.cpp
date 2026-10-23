// Tests of the rules of the runtime environment (Environment/SwcEnvironmentRules, stage E1; docs/ENVIRONMENT.md): the evaluator of the
// client against the evaluator of the converter (a table of tools/convert-environment.sh table: the keys of a preset and its values
// each 15 game minutes), the sun path against the samples of the research (proof-metadata.json: the public engine rule with the
// main-world mission), the moon, the dawn and dusk edges of the record, the two light multipliers, the light color, and the volumes.
// Usage: swc-environment-rules-tests [TABLE] [PROOF_METADATA] (a missing file skips its part).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "Environment/SwcEnvironmentRules.h"

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

void test_table(const char* path) {
    std::ifstream file(path);
    if (!file) {
        std::printf("  (no table %s: the evaluator compare is skipped)\n", path);
        return;
    }
    std::map<int, env::Curve> curves;
    struct Expected {
        int variable;
        double hour;
        double v[3];
    };
    std::vector<Expected> expected;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream in(line);
        std::string kind;
        in >> kind;
        if (kind == "key") {
            int variable = 0, channels = 1;
            env::Key key;
            in >> variable >> channels >> key.time >> key.value[0] >> key.value[1] >> key.value[2] >> key.flags;
            curves[variable].channels = channels;
            curves[variable].keys.push_back(key);
        } else if (kind == "value") {
            Expected e{};
            in >> e.variable >> e.hour >> e.v[0] >> e.v[1] >> e.v[2];
            expected.push_back(e);
        }
    }
    int mismatches = 0;
    double largest = 0.0;
    for (const Expected& e : expected) {
        const std::array<double, 3> value = env::evaluate(curves[e.variable], env::curve_time(e.hour));
        for (int c = 0; c < curves[e.variable].channels; ++c) {
            const double difference = std::fabs(value[c] - e.v[c]) / std::max(1.0, std::fabs(e.v[c]));
            largest = std::max(largest, difference);
            if (difference > 1e-9) {
                ++mismatches;
            }
        }
    }
    char text[200];
    std::snprintf(text, sizeof(text), "the evaluator of the client equals the evaluator of the converter: %zu values of %zu variables, %d mismatches, "
                  "the largest relative difference %.3g", expected.size(), curves.size(), mismatches, largest);
    check(!expected.empty() && mismatches == 0, text);
    std::printf("  %s\n", text);
}

void test_sun_path(const char* path) {
    // The research model at noon (proof-metadata.json): toward the light -0.368688, 0.526541, 0.766044 with SunRotation 215, Longitude 130.
    const env::Vec3 noon = env::sun_toward(12.0, 215.0, 130.0);
    check(near(noon.x, -0.368688, 1e-5) && near(noon.y, 0.526541, 1e-5) && near(noon.z, 0.766044, 1e-5), "the sun at noon (the research model)");
    // The night delay of the sun path (research e5-menu-probe.md): the menu mission (DawnTime 355, DawnDuration 10, DuskTime 365,
    // DuskDuration 10 minutes) at the hour override 16.7000008 gives the hour 16.166487 and the sun 25.72 degrees high.
    const env::Edges menuEdges = env::mission_edges(355.0, 10.0, 365.0, 10.0);
    check(near(menuEdges.dawnStart, 5.833333, 1e-6) && near(menuEdges.dawnEnd, 6.0, 1e-9) && near(menuEdges.duskStart, 18.0, 1e-9)
              && near(menuEdges.duskEnd, 18.166667, 1e-6),
        "the edges of the menu mission (each window centered on its time, the dusk + 12 h)");
    const double menuSunHour = env::sun_path_hour(16.7000008, menuEdges.dawnStart, menuEdges.duskEnd, 0.7);
    check(near(menuSunHour, 16.166487, 1e-5), "the night delay of the sun path at the menu hour");
    const env::Vec3 menuSun = env::sun_toward(menuSunHour, 190.0, 110.0);
    check(near(menuSun.x, -0.900940, 1e-5) && near(menuSun.y, 0.001518, 1e-5) && near(menuSun.z, 0.433941, 1e-5),
        "the menu sun of the source (25.72 degrees high)");
    check(env::sun_path_hour(4.0, 6.0, 19.0, 0.7) == 4.0 && env::sun_path_hour(12.0, 6.0, 18.0, 0.7) == 12.0
              && env::sun_path_hour(10.0, 6.0, 18.0, 0.0) == 10.0,
        "the night delay: none at night, none at the middle of a symmetric day, none for a zero delay");
    // The sun step (AST-273 P1): 0.5 cm of shadow move for each meter of height; 0.0192 degrees at a sun 15 degrees high, the cap of
    // 0.1 degrees at noon (50 degrees high), a turn in each frame at the horizon, below it, and with the step off.
    const double sine15 = std::sin(15.0 * 3.14159265358979323846 / 180.0);
    check(near(env::sun_step_degrees(sine15, 0.005, 0.1), 0.0191905, 1e-6), "the sun step at 15 degrees (0.5 cm per meter of height)");
    check(env::sun_step_degrees(noon.z, 0.005, 0.1) == 0.1, "the sun step at noon is the cap");
    check(env::sun_step_degrees(0.0, 0.005, 0.1) == 0.0 && env::sun_step_degrees(-0.2, 0.005, 0.1) == 0.0
              && env::sun_step_degrees(0.5, 0.005, 0.0) == 0.0 && env::sun_step_degrees(0.5, 0.0, 0.1) == 0.0,
        "no sun step at or below the horizon, or with the step off");
    const env::Vec3 midnight = env::sun_toward(0.0, 215.0, 130.0);
    check(midnight.z < 0.0, "the sun at midnight is below the horizon");
    const env::Vec3 moon = env::moon_toward(240.0, 35.0);
    check(near(moon.x, 0.409576, 1e-5) && near(moon.y, 0.709406, 1e-5) && near(moon.z, 0.573576, 1e-5), "the moon (the research model: about 35 degrees high)");
    std::ifstream file(path);
    if (!file) {
        std::printf("  (no research metadata %s: the sun path compare is skipped)\n", path);
        return;
    }
    // The samples: "[hour, x, y, z]" in the member sun_path_samples (a plain scan of the numbers of each sample).
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    const std::size_t start = text.find("\"sun_path_samples\"");
    int compared = 0;
    double largest = 0.0;
    for (std::size_t at = text.find('[', text.find('[', start) + 1); at != std::string::npos && at < text.find(']', text.rfind("]]")) && compared < 721;
         at = text.find('[', at + 1)) {
        double hour = 0, x = 0, y = 0, z = 0;
        if (std::sscanf(text.c_str() + at, "[ %lf , %lf , %lf , %lf ]", &hour, &x, &y, &z) != 4) {
            break;
        }
        const env::Vec3 sun = env::sun_toward(hour, 215.0, 130.0);
        largest = std::max({largest, std::fabs(sun.x - x), std::fabs(sun.y - y), std::fabs(sun.z - z)});
        ++compared;
    }
    char line[160];
    std::snprintf(line, sizeof(line), "the sun path equals the samples of the research: %d samples, the largest difference %.3g", compared, largest);
    check(compared == 721 && largest < 1e-9, line);
    std::printf("  %s\n", line);
}

void test_edges_and_lights() {
    const env::Edges edges = env::edges_of(21600, 60, 68400, 60, 1800);
    check(near(edges.dawnStart, 6.0, 1e-12) && near(edges.dawnEnd, 6.0 + 22.0 / 60.0, 1e-12) && near(edges.duskStart, 19.0, 1e-12)
              && near(edges.duskEnd, 19.0 + 22.0 / 60.0, 1e-12),
          "the visual dawn and dusk of the record: 6.0 to 6.3667 and 19.0 to 19.3667 (client 0x644ECC0)");
    const double dawnMiddle = 6.0 + 11.0 / 60.0;
    auto lights = env::light_multipliers(3.0, edges);
    check(lights.moon == 1.0 && lights.sun == 0.0, "the night: the moon");
    lights = env::light_multipliers(6.1, edges);
    check(lights.sun == 0.0 && near(lights.moon, (dawnMiddle - 6.1) / (11.0 / 60.0), 1e-12), "the first half of the dawn: the moon fades out");
    lights = env::light_multipliers(6.3, edges);
    check(lights.moon == 0.0 && near(lights.sun, (6.3 - dawnMiddle) / (11.0 / 60.0), 1e-12), "the second half of the dawn: the sun fades in");
    lights = env::light_multipliers(12.0, edges);
    check(lights.sun == 1.0 && lights.moon == 0.0, "the day: the sun");
    lights = env::light_multipliers(19.1, edges);
    check(lights.moon == 0.0 && lights.sun > 0.0 && lights.sun < 1.0, "the first half of the dusk: the sun fades out");
    lights = env::light_multipliers(19.3, edges);
    check(lights.sun == 0.0 && lights.moon > 0.0 && lights.moon < 1.0, "the second half of the dusk: the moon fades in");
    lights = env::light_multipliers(19.0 + 22.0 / 60.0, edges);
    check(lights.moon == 1.0, "the end of the dusk: the moon");
    const env::LightColor white = env::light_color({1.0, 1.0, 1.0});
    check(near(white.intensityFactor, 1.0, 1e-12), "a white light: its intensity is its illuminance");
    const env::LightColor warm = env::light_color({1.0, 0.791298, 0.610496});
    const double luminance = 0.2126 + 0.7152 * 0.791298 + 0.0722 * 0.610496;
    check(near(warm.intensityFactor * luminance, 1.0, 1e-12) && warm.color[0] == 1.0, "a colored light keeps its luminance (the source rule)");
}

void test_volumes() {
    auto cylinder = [](double x, double y, double z) {
        env::Volume v;
        v.shape = env::Volume::Shape::cylinder;
        v.matrix = {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1};
        v.inverse = env::inverse_of(v.matrix);
        v.radius = 350.0;
        v.height = 400.0;
        v.priority = 3;
        v.preset = "libs/timeofday/zones/everfall_a.xml";
        return v;
    };
    std::vector<env::Volume> volumes{cylinder(9049.0, 3966.0, 104.0), cylinder(8730.0, 4045.0, 104.0), cylinder(9108.0, 4266.0, 140.0)};
    const env::Vec3 oxboro{8880.0, 4250.0, 140.5};
    check(env::select_volume(volumes, oxboro) == 0, "the Oxboro point: the first Everfall cylinder of equal priority");
    env::Volume inn;
    inn.shape = env::Volume::Shape::box;
    const double a = 0.5;   // a turned box
    inn.matrix = {std::cos(a), -std::sin(a), 0, 8880.0, std::sin(a), std::cos(a), 0, 4250.0, 0, 0, 1, 140.0, 0, 0, 0, 1};
    inn.inverse = env::inverse_of(inn.matrix);
    inn.dimensions = {20.0, 10.0, 8.0};
    inn.priority = 10;
    inn.hasHourOverride = true;
    volumes.push_back(inn);
    check(env::select_volume(volumes, oxboro) == 3, "a box of a higher priority (an interior) holds the point");
    check(env::select_volume(volumes, {8880.0, 4250.0, 150.0}) == 0, "above the box: the cylinder again");
    env::Volume prism;
    prism.shape = env::Volume::Shape::polygonPrism;
    prism.matrix = {1, 0, 0, 100, 0, 1, 0, 100, 0, 0, 1, 10, 0, 0, 0, 1};
    prism.inverse = env::inverse_of(prism.matrix);
    prism.height = 5.0;
    prism.vertices = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    check(env::contains(prism, {105.0, 105.0, 12.0}) && !env::contains(prism, {105.0, 105.0, 9.0}) && !env::contains(prism, {111.0, 105.0, 12.0}),
          "a polygon prism goes up from its entity");
    check(env::select_volume(volumes, {0.0, 0.0, 0.0}) == -1, "no volume: -1");
}

void test_blend_fog_and_stars() {
    // The depth and the weight of a volume (stage E3).
    env::Volume cylinder;
    cylinder.shape = env::Volume::Shape::cylinder;
    cylinder.matrix = {1, 0, 0, 100, 0, 1, 0, 200, 0, 0, 1, 50, 0, 0, 0, 1};
    cylinder.inverse = env::inverse_of(cylinder.matrix);
    cylinder.radius = 350.0;
    cylinder.height = 400.0;
    check(near(env::inside_depth(cylinder, {100, 200, 50}), 200.0, 1e-9), "the center of a cylinder: the depth to the caps");
    check(near(env::inside_depth(cylinder, {440, 200, 50}), 10.0, 1e-9), "10 m inside the side");
    check(near(env::inside_depth(cylinder, {460, 200, 50}), -10.0, 1e-9), "10 m outside the side");
    check(env::volume_weight(cylinder, {440, 200, 50}) == 1.0 && env::volume_weight(cylinder, {460, 200, 50}) == 0.0,
          "no blend distance: 1 inside, 0 outside");
    cylinder.blendDistance = 50.0;
    check(near(env::volume_weight(cylinder, {440, 200, 50}), 0.2, 1e-9), "a blend distance of 50 m: 10 m inside gives 0.2");
    check(env::volume_weight(cylinder, {100, 200, 50}) == 1.0, "deep inside: 1");
    env::Volume box;
    box.shape = env::Volume::Shape::box;
    box.matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    box.inverse = env::inverse_of(box.matrix);
    box.dimensions = {20.0, 10.0, 8.0};
    check(near(env::inside_depth(box, {0, 0, 0}), 4.0, 1e-9) && near(env::inside_depth(box, {9, 0, 0}), 1.0, 1e-9), "the depth in a box");
    env::Volume prism;
    prism.shape = env::Volume::Shape::polygonPrism;
    prism.matrix = box.matrix;
    prism.inverse = box.inverse;
    prism.height = 5.0;
    prism.vertices = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    check(near(env::inside_depth(prism, {5, 5, 2}), 2.0, 1e-9) && near(env::inside_depth(prism, {5, 1, 2.5}), 1.0, 1e-9)
          && env::inside_depth(prism, {12, 5, 2}) < 0.0, "the depth in a polygon prism");
    check(near(env::approach_weight(0.0, 1.0, 8.0, 2.0), 0.25, 1e-12) && near(env::approach_weight(1.0, 0.0, 8.0, 2.0), 0.75, 1e-12)
          && env::approach_weight(0.3, 1.0, 0.0, 0.1) == 1.0 && env::approach_weight(0.9, 1.0, 8.0, 2.0) == 1.0,
          "the weight goes to the target in the blend time");
    // The height falloff of the fog: the Everfall layers 0.05 at 0 m and 0.02 at 400 m.
    check(near(env::fog_height_falloff(0.05, 0.02, 0.0, 400.0), std::log2(2.5) / 400.0 * 10.0, 1e-12), "the fog falloff of two layers");
    check(env::fog_height_falloff(0.05, 0.05, 0.0, 400.0) == 0.0 && env::fog_height_falloff(0.05, 0.02, 400.0, 0.0) == 0.0, "no falloff");
    // The eye adaptation of the source (HDRFinalScenePS): the noon sun of the data, the night moon of Everfall, a dark scene at the bound.
    check(near(env::source_ev(100000.0, 5.0, 17.0, 1.5), std::log2(100000.0 * 100.0 / 330.0) - (std::log10(100001.0) - 3.0) / 2.0 * 1.5, 1e-12),
          "the EV of the source at noon");
    check(near(env::source_ev(100000.0, 5.0, 17.0, 1.5), 13.387, 1e-3), "the EV of the source at noon, 13.39");
    check(near(env::source_ev(5000.0, 10.0, 17.0, 1.5), 10.041, 1e-3), "the EV of the source with the moon of 5,000 lux");
    check(env::source_ev(100.0, 10.0, 17.0, 1.5) == 10.0 && env::source_ev(1e9, 5.0, 17.0, 1.5) == 17.0, "the EV bounds of the source");
    // The Unreal bias curve gives the exposure of the source: 0.18 x 2^bias / M = 1 / (1.2 x 2^EV) with E = pi x M / meterScale.
    for (double ev100 = -4.0; ev100 <= 16.0; ev100 += 0.5) {
        const double meterScale = 0.1882;
        const double metered = 0.18 * std::exp2(ev100);
        const double unreal = 0.18 * std::exp2(env::source_exposure_bias(ev100, 10.0, 17.0, 1.5, meterScale)) / metered;
        const double source = 1.0 / (1.2 * std::exp2(env::source_ev(3.14159265358979 * metered / meterScale, 10.0, 17.0, 1.5)));
        check(near(unreal / source, 1.0, 1e-9), "the exposure of the curve at the EV100 " + std::to_string(ev100));
    }
    // The sky model of the source (the Everfall noon: "Sky light: Sun intensity" (0.0730, 0.1990, 0.5417) x 100, Rayleigh 0.3, Mie
    // 4.8, g -0.99, 693 / 596 / 450 nm, the sun 50 degrees high in the south) against a numeric reference (256 even steps on the view
    // ray, 48 to the sun; the analysis script of E5 step 2b): the zenith (0.009039, 0.044013, 0.345127), 10 degrees above the north
    // horizon (0.036436, 0.174862, 1.238876), and 5 degrees from the sun (0.189981, 0.545149, 1.793409: the Mie glow).
    {
        env::SkyModel sky;
        const double el = 50.0 * 3.14159265358979 / 180.0;
        sky.sunToward = {0.0, -std::cos(el), std::sin(el)};
        sky.sunIntensity = {7.30, 19.90, 54.17};
        sky.rayleigh = 0.3;
        sky.mie = 4.8;
        const auto color = [&](const env::Vec3& v) {
            const env::SkyParts parts = env::source_sky(sky, v);
            const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            const double cosine = (v.x * sky.sunToward.x + v.y * sky.sunToward.y + v.z * sky.sunToward.z) / len;
            const std::array<double, 2> phase = env::source_sky_phases(sky.g, cosine);
            std::array<double, 3> out{};
            for (int c = 0; c < 3; ++c) {
                out[c] = parts.mie[c] * phase[0] + parts.rayleigh[c] * phase[1];
            }
            return out;
        };
        const std::array<double, 3> zenith = color({0.0, 0.0, 1.0});
        const std::array<double, 3> north = color({0.0, std::cos(10.0 * 3.14159265358979 / 180.0), std::sin(10.0 * 3.14159265358979 / 180.0)});
        const std::array<double, 3> zenithRef{0.009039, 0.044013, 0.345127}, northRef{0.036436, 0.174862, 1.238876};
        const std::array<double, 3> nearSun = color({0.0, -std::cos(45.0 * 3.14159265358979 / 180.0), std::sin(45.0 * 3.14159265358979 / 180.0)});
        const std::array<double, 3> nearSunRef{0.189981, 0.545149, 1.793409};
        for (int c = 0; c < 3; ++c) {
            check(near(zenith[c] / zenithRef[c], 1.0, 0.03), "the sky of the source at the zenith, channel " + std::to_string(c) + ": " + std::to_string(zenith[c]));
            check(near(north[c] / northRef[c], 1.0, 0.05), "the sky of the source 10 degrees above the horizon, channel " + std::to_string(c) + ": " + std::to_string(north[c]));
            check(near(nearSun[c] / nearSunRef[c], 1.0, 0.05), "the sky of the source near the sun, channel " + std::to_string(c) + ": " + std::to_string(nearSun[c]));
        }
        const std::array<double, 3> below = color({0.0, 1.0, -0.2});
        check(below[2] > 0.0, "below the horizon: the horizon color");
    }
    // The stars turn with the sun: the sun is at the celestial e_y at each hour, and the celestial pole is fixed in the sky.
    env::Vec3 pole0{};
    for (int step = 0; step < 24; ++step) {
        const double hour = step + 0.37;
        const env::Rotation r = env::celestial_from_engine(hour, 215.0, 130.0);
        const env::Vec3 sun = env::rotate(r, env::sun_toward(hour, 215.0, 130.0));
        check(near(sun.x, 0.0, 1e-9) && near(sun.y, 1.0, 1e-9) && near(sun.z, 0.0, 1e-9), "the sun is at ra 0, dec 0 at " + std::to_string(hour));
        // The pole in the engine space: the transpose times e_z.
        const env::Vec3 pole{r[2][0], r[2][1], r[2][2]};
        if (step == 0) {
            pole0 = pole;
        }
        check(near(pole.x, pole0.x, 1e-9) && near(pole.y, pole0.y, 1e-9) && near(pole.z, pole0.z, 1e-9), "the celestial pole is fixed");
    }
    check(pole0.z > 0.5, "the celestial pole is above the horizon");
    const env::Vec3 sirius = env::star_vector(6.753, -16.71);
    check(near(sirius.x * sirius.x + sirius.y * sirius.y + sirius.z * sirius.z, 1.0, 1e-12) && sirius.z < 0.0, "a star vector is a unit vector");
}

void test_probes() {
    // The cube corners go onto the unit ball; the center and the faces keep their length.
    const env::Vec3 corner = env::map_cube_to_sphere({1.0, 1.0, 1.0});
    check(near(corner.x * corner.x + corner.y * corner.y + corner.z * corner.z, 1.0, 2e-3), "a cube corner is on the unit ball");
    const env::Vec3 face = env::map_cube_to_sphere({1.0, 0.0, 0.0});
    check(near(face.x, 1.0, 1e-12), "a face center is on the unit ball");
    // The fade: 1 at the center, 0 at the box faces and outside, and a hard edge for a small falloff.
    check(near(env::probe_fade({0.0, 0.0, 0.0}, 0.5, 1.0), 1.0, 1e-12), "the probe fade at the center");
    check(env::probe_fade({1.0, 0.0, 0.0}, 0.5, 1.0) == 0.0 && env::probe_fade({0.2, -1.2, 0.0}, 0.5, 1.0) == 0.0, "no fade outside the box");
    check(near(env::probe_fade({0.5, 0.0, 0.0}, 0.01, 1.0), 1.0, 1e-12), "a falloff of 0.01: full weight at half the box");
    // At half the box on one axis: 1 - 0.25 = 0.75 of the edge value; with the falloff 1: smoothstep(0.75).
    check(near(env::probe_fade({0.5, 0.0, 0.0}, 1.0, 1.0), 0.75 * 0.75 * (3.0 - 1.5), 1e-12), "the smoothstep of the fade");
    check(near(env::probe_fade({0.0, 0.0, 0.0}, 0.5, 0.4), 0.4, 1e-12), "the uniform attenuation");
    // The order: the first probe takes its fade, the next ones take the rest; the last value is the rest for the world probe.
    const std::vector<double> w = env::probe_weights({0.6, 0.5, 1.0});
    check(near(w[0], 0.6, 1e-12) && near(w[1], 0.2, 1e-12) && near(w[2], 0.2, 1e-12) && near(w[3], 0.0, 1e-12), "the weights in the list order");
    const std::vector<double> none = env::probe_weights({});
    check(none.size() == 1 && near(none[0], 1.0, 1e-12), "no probe: the world probe only");
}

void test_snow() {
    // The count fade of a Snow volume: 1, 0.75, 0.4375, 0.19, and 0 at 0, 0.5 R, 0.75 R, 0.9 R, and R (research/snow.md section 1).
    const env::Vec3 c{9715.8213, 7609.3115, 368.4649};
    const double r = 1000.0;
    check(near(env::snow_count_weight(c, c, r), 1.0, 1e-12), "the snow weight at the center");
    check(near(env::snow_count_weight({c.x + 500.0, c.y, c.z}, c, r), 0.75, 1e-12), "the snow weight at 0.5 R");
    check(near(env::snow_count_weight({c.x, c.y - 750.0, c.z}, c, r), 0.4375, 1e-12), "the snow weight at 0.75 R");
    check(near(env::snow_count_weight({c.x, c.y, c.z + 900.0}, c, r), 0.19, 1e-12), "the snow weight at 0.9 R (a vertical distance)");
    check(env::snow_count_weight({c.x + 1000.0, c.y, c.z}, c, r) == 0.0 && env::snow_count_weight({c.x + 2000.0, c.y, c.z}, c, r) == 0.0,
          "no snow at and past the radius");
    check(env::snow_count_weight(c, c, 0.0) == 1.0 && env::snow_count_weight({c.x + 1.0, c.y, c.z}, c, 0.0) == 0.0, "a zero radius");
    // The saved Cleave inside camera: 943.454 m from 4_3_Snow, weight 0.109895, 54 flakes of 500 in each cluster.
    const env::Vec3 inside{8963.917, 8151.916, 542.6078};
    check(near(env::snow_count_weight(inside, c, r), 0.109895, 1e-5), "the weight of cleave-inside-noon");
    check(env::snow_flake_count(500, env::snow_count_weight(inside, c, r)) == 54, "the count of cleave-inside-noon (54)");
    check(env::snow_flake_count(500, 0.75) == 375 && env::snow_flake_count(500, 0.0) == 0, "the integer count");
    // The cluster life: 4 / max(G, 1) + (-2.5 to 2.5) s: 1.5 to 6.5 s for G 0.4; the fall speed G x 9.8 x (0.2 to 0.4).
    check(near(env::snow_cluster_life(0.4, 0.0), 1.5, 1e-12) && near(env::snow_cluster_life(0.4, 1.0), 6.5, 1e-12), "the cluster life");
    check(near(env::snow_cluster_life(4.0, 0.0), 0.1, 1e-12), "the life is at least 0.1 s");
    check(near(env::snow_fall_speed(0.4, 0.2), 0.784, 1e-12) && near(env::snow_fall_speed(0.4, 0.4), 1.568, 1e-12), "the Cleave fall speed");
}

} // namespace

int main(int argc, char** argv) {
    test_table(argc > 1 ? argv[1] : "");
    test_sun_path(argc > 2 ? argv[2] : "");
    test_edges_and_lights();
    test_volumes();
    test_blend_fog_and_stars();
    test_probes();
    test_snow();
    std::printf("environment rules: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
