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

} // namespace

int main(int argc, char** argv) {
    test_table(argc > 1 ? argv[1] : "");
    test_sun_path(argc > 2 ? argv[2] : "");
    test_edges_and_lights();
    test_volumes();
    std::printf("environment rules: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
