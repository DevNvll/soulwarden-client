// The tests of the water data of the client (stage WF5, Water/SwcWaterData; docs/WATER.md): the readers of the files of the
// converter (SWC_CONTENT/water: REGION.water, REGION.waterq), the query of the quadtree against the answers of the reader of the
// converter (water/samples.json: random points of each region, half of them at the surfaces), and the bodies against water.json
// (the counts, the order of the triangles: counterclockwise from above, but the folds of the tight river bends). Build and run: tools/test-water.sh.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Water/SwcWaterData.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& text)
{
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("  FAIL %s\n", text.c_str());
	}
}

std::vector<std::uint8_t> read_file(const std::string& path)
{
	std::ifstream input(path, std::ios::binary);
	return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: swc-water-tests WATER_FOLDER\n");
		return 2;
	}
	const std::string folder = argv[1];
	std::ifstream indexFile(folder + "/water.json");
	std::ifstream samplesFile(folder + "/samples.json");
	if (!indexFile || !samplesFile) {
		std::fprintf(stderr, "no water.json or samples.json in %s (python3 -B -m swc_convert.water write, samples)\n", folder.c_str());
		return 2;
	}
	const nlohmann::json index = nlohmann::json::parse(indexFile);
	const nlohmann::json samples = nlohmann::json::parse(samplesFile);
	std::printf("the water quadtree: the query of the client against the reader of the converter\n");
	int points = 0;
	int wet = 0;
	for (const auto& [name, list] : samples["regions"].items()) {
		swc::water::Tree tree;
		std::string error;
		const std::vector<std::uint8_t> bytes = read_file(folder + "/" + name + ".waterq");
		if (!tree.read(bytes, error)) {
			check(false, name + ": the quadtree reads (" + error + ")");
			continue;
		}
		int wrong = 0;
		for (const auto& point : list) {
			++points;
			const auto found = tree.at(point[0].get<double>(), point[1].get<double>());
			if (point.size() == 2) {
				wrong += found ? 1 : 0;
				continue;
			}
			++wet;
			if (!found || found->surface != point[2].get<float>() || found->floor != point[3].get<float>() || found->flags != point[4].get<std::uint32_t>()) {
				++wrong;
			}
		}
		check(wrong == 0, name + ": " + std::to_string(wrong) + " point(s) differ");
	}
	std::printf("  %d points, %d with water\n", points, wet);
	check(points > 0 && wet > 0, "the samples have points with water");
	// Outside the region: no water.
	{
		swc::water::Tree tree;
		std::string error;
		tree.read(read_file(folder + "/r_+04_+02.waterq"), error);
		check(!tree.at(-0.5, 10.0) && !tree.at(10.0, 2048.0), "a point outside the region has no water");
	}

	std::printf("the surfaces: the bodies of each region against water.json\n");
	int volumes = 0;
	int rivers = 0;
	int clockwise = 0;
	int triangles = 0;
	for (const auto& [name, region] : index["regions"].items()) {
		if (!region.contains("file")) {
			continue;
		}
		std::vector<swc::water::Body> bodies;
		std::string error;
		if (!swc::water::read_bodies(read_file(folder + "/" + region["file"].get<std::string>()), bodies, error)) {
			check(false, name + ": the surfaces read (" + error + ")");
			continue;
		}
		int regionVolumes = 0;
		int regionRivers = 0;
		std::size_t vertices = 0;
		std::size_t regionTriangles = 0;
		for (const swc::water::Body& body : bodies) {
			(body.kind == swc::water::Kind::volume ? regionVolumes : regionRivers) += 1;
			vertices += body.vertex_count();
			regionTriangles += body.indices.size() / 3;
			for (std::size_t k = 0; k + 2 < body.indices.size(); k += 3) {
				const float* a = &body.vertices[body.indices[k] * 5];
				const float* b = &body.vertices[body.indices[k + 1] * 5];
				const float* c = &body.vertices[body.indices[k + 2] * 5];
				const double cross = (b[0] - a[0]) * static_cast<double>(c[1] - a[1]) - (b[1] - a[1]) * static_cast<double>(c[0] - a[0]);
				clockwise += cross < -1e-6 ? 1 : 0;
			}
		}
		volumes += regionVolumes;
		rivers += regionRivers;
		triangles += static_cast<int>(regionTriangles);
		check(regionVolumes == region["volumes"].get<int>() && regionRivers == region["rivers"].get<int>()
				&& vertices == region["vertices"].get<std::size_t>() && regionTriangles == region["triangles"].get<std::size_t>(),
			name + ": the counts of water.json");
	}
	std::printf("  %d volumes, %d rivers, %d triangles, %d clockwise\n", volumes, rivers, triangles, clockwise);
	check(volumes > 600 && rivers == 277, "the world has the volumes and the 277 rivers");
	// A tight bend of a river (a radius smaller than half of its width) folds its inner bank: those triangles are clockwise (the
	// source rule of the cross-sections makes the same fold).
	check(clockwise * 200 <= triangles, "the triangles are counterclockwise from above (engine space), except the folds of tight river bends (at most 0.5 %)");
	std::printf("the depth classes (the rule of the retail client)\n");
	{
		using swc::water::DepthClass;
		const swc::water::Sample river{123.25F, 113.25F, 7};
		float depth = -1.0F;
		check(swc::water::depth_class(std::nullopt, 100.0F, &depth) == DepthClass::none && depth == 0.0F, "no water: None");
		// 1 mm on each side of each limit (the subtraction in float has a step of 8e-6 m at 123 m).
		check(swc::water::depth_class(river, 123.25F - 0.199F) == DepthClass::none, "0.199 m: None");
		check(swc::water::depth_class(river, 123.25F - 0.201F) == DepthClass::veryShallow, "0.201 m: VeryShallow");
		check(swc::water::depth_class(river, 123.25F - 0.899F) == DepthClass::veryShallow, "0.899 m: VeryShallow");
		check(swc::water::depth_class(river, 123.25F - 0.901F) == DepthClass::shallow, "0.901 m: Shallow (wade)");
		check(swc::water::depth_class(river, 123.25F - 1.799F) == DepthClass::shallow, "1.799 m: Shallow");
		check(swc::water::depth_class(river, 123.25F - 1.801F, &depth) == DepthClass::deep && std::fabs(depth - 1.801F) < 1e-4F, "1.801 m: Deep (swim)");
		const swc::water::Sample exact{0.9F, 0.0F, 4};
		check(swc::water::depth_class(exact, 0.0F) == DepthClass::shallow && swc::water::depth_class(swc::water::Sample{1.8F, 0.0F, 4}, 0.0F) == DepthClass::deep,
			"a depth equal to a limit has the deeper class");
		check(swc::water::depth_class(river, 125.0F, &depth) == DepthClass::none && depth == 0.0F, "above the surface: None, depth 0");
		check(swc::water::depth_class(river, 113.0F) == DepthClass::none, "below the floor of the leaf: None");
	}
	{
		const auto beach = swc::water::with_ocean(std::nullopt, 40.0F);
		check(beach && beach->surface == 40.0F && beach->floor == 0.0F && beach->flags == swc::water::kFlagSalt
				&& swc::water::depth_class(beach, 40.0F - 0.25F) == swc::water::DepthClass::veryShallow
				&& swc::water::depth_class(beach, 38.6F) == swc::water::DepthClass::shallow,
			"no water in the quadtree: the ocean at 40 m (a swimmer at 38.6 m is Shallow, as the retail byte 2)");
		const swc::water::Sample river{123.25F, 113.25F, 7};
		check(swc::water::with_ocean(river, 40.0F)->surface == 123.25F, "a leaf with water keeps its surface");
	}
	check(!swc::water::region_of("x") && swc::water::region_of("r_+04_+02") == std::pair<int, int>{4, 2}, "the region of a name");
	std::printf("the shore foam sheets (the particle curves of env_island.xml)\n");
	{
		// The Alpha of Water.ShoreFoamWaveDecal: (d=4.32,s=4.32,flags=9; t=0.23,v=1; t=0.67,v=1; t=1,s=-2.97).
		swc::water::Curve alpha;
		alpha.keys = {{0.0F, 0.0F, 4.3220334F, 4.3220334F}, {0.23F, 1.0F, 0.0F, 0.0F}, {0.67F, 1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, -2.9651165F, 0.0F}};
		check(alpha.at(0.0F) == 0.0F && alpha.at(0.23F) == 1.0F && alpha.at(0.5F) == 1.0F && std::fabs(alpha.at(1.0F)) < 1e-6F,
			"the alpha curve: 0 at the start, 1 from 0.23 to 0.67, 0 at the end");
		check(alpha.at(0.1F) > 0.3F && alpha.at(0.1F) < 0.9F && alpha.at(0.85F) > 0.2F && alpha.at(0.85F) < 0.9F, "the alpha curve rises and falls between its keys");
		// The SizeY of the sheet: 6, Random=0.2, (v=0.698, d=0.604; t=0.604, v=1; t=1, v=0.827, s=-0.345).
		// A key: d is the slope to its right (out), s the slope to its left (in).
		swc::water::Curve size{6.0F, 0.2F, {{0.0F, 0.698F, 0.0F, 0.60392153F}, {0.604F, 1.0F, 0.0F, 0.0F}, {1.0F, 0.827F, -0.3450979F, 0.0F}}};
		check(size.at(0.3F) > 0.698F && size.at(0.3F) < 1.0F && size.at(0.8F) < 1.0F && size.at(0.8F) > 0.827F, "the size grows to 1 and goes back");
		check(std::fabs(size.value(0.604F, 0.5F) - 6.0F) < 1e-5F && std::fabs(size.value(0.0F, 1.0F) - 6.0F * 1.2F * 0.698F) < 1e-5F,
			"a size: the base x (1 + random x (2 r - 1)) x the curve");
		swc::water::Curve none;
		check(none.at(0.4F) == 1.0F, "no key: 1");
		std::int64_t cycle = -1;
		check(swc::water::sheet_age(1.0, 1.5F, 5.0F) < 0.0F && std::fabs(swc::water::sheet_age(4.0, 1.5F, 5.0F, &cycle) - 0.5F) < 1e-6F && cycle == 0
				&& std::fabs(swc::water::sheet_age(9.0, 1.5F, 5.0F, &cycle) - 0.5F) < 1e-6F && cycle == 1,
			"the age of a continuous sheet: none before its delay, then 5 s cycles");
		const float r0 = swc::water::sheet_random(3, 1, 7, 0);
		check(r0 >= 0.0F && r0 < 1.0F && r0 == swc::water::sheet_random(3, 1, 7, 0) && r0 != swc::water::sheet_random(3, 1, 8, 0),
			"the random value of a particle: 0 to 1, the same for the same particle, new for the next cycle");
	}
	std::printf("the source tone of the Unreal water (docs/WATER.md \"The source tone\")\n");
	{
		// The most common volume of the world: FogColor (0.165, 0.667, 0.208), fogColorMultiplier 0.1, fogDensity 1.5 per m.
		const float fog[3] = {0.165F, 0.667F, 0.208F};
		const swc::water::Tone volume = swc::water::volume_tone(fog, 0.1F, 1.5F, 4.0F);
		bool gray = true;
		for (int c = 0; c < 3; ++c) {
			gray = gray && std::fabs(volume.absorption[c] + volume.scattering[c] - 0.015F) < 1e-7F;
		}
		check(gray, "a volume: the extinction is density / 100 per cm in each channel (gray, as the source)");
		check(std::fabs(volume.scattering[1] / 0.015F - 4.0F * 0.667F * 0.1F) < 1e-5F, "a volume: the albedo is 4 x FogColor x FogColorScale");
		const float bright[3] = {1.0F, 1.0F, 1.0F};
		check(std::fabs(swc::water::volume_tone(bright, 1.0F, 1.0F, 4.0F).scattering[0] / 0.01F - swc::water::kToneMaxAlbedo) < 1e-5F,
			"the albedo stops at 0.95");
		// The starting beach (POI_TOD_StartingBeach_A_(40mOcean)): FogColor (0.0118, 0.137, 0.235), multiplier 0.07, density 0.3.
		const float beach[3] = {0.011764699F, 0.13725489F, 0.2352941F};
		const swc::water::Tone ocean = swc::water::ocean_tone(beach, 0.07F, 0.3F, 4.0F);
		const float expected = 0.13725489F * 0.07F * 4.0F / (1.442695F * 0.3F * 1.5F);
		check(std::fabs(ocean.scattering[1] / 0.003F - expected) < 1e-4F && std::fabs(ocean.absorption[1] + ocean.scattering[1] - 0.003F) < 1e-7F,
			"the ocean: the albedo is 4 x FogColor x multiplier / (1.4427 density x 1.5), the extinction density / 100 per cm");
		check(ocean.scattering[2] > ocean.scattering[1] && ocean.scattering[1] > ocean.scattering[0], "the starting beach is blue (b > g > r)");
		// The blue gain of the ocean (research/water-hue.md section 5): the extinction of each channel stays, only the blue albedo changes.
		const float blue[3] = {1.0F, 1.0F, 1.25F};
		const swc::water::Tone bluer = swc::water::albedo_gain(ocean, blue);
		bool held = true;
		for (int c = 0; c < 3; ++c) {
			held = held && std::fabs(bluer.absorption[c] + bluer.scattering[c] - (ocean.absorption[c] + ocean.scattering[c])) < 1e-9F;
		}
		check(held, "the albedo gain: the extinction of each channel is held");
		check(bluer.scattering[0] == ocean.scattering[0] && bluer.scattering[1] == ocean.scattering[1]
				&& std::fabs(bluer.scattering[2] / ocean.scattering[2] - 1.25F) < 1e-5F,
			"the albedo gain: blue x 1.25, red and green unchanged");
		const float one[3] = {1.0F, 1.0F, 1.0F};
		const swc::water::Tone same = swc::water::albedo_gain(ocean, one);
		check(same.absorption[2] == ocean.absorption[2] && same.scattering[2] == ocean.scattering[2], "the albedo gain 1 is the tone unchanged");
		const float large[3] = {100.0F, 100.0F, 100.0F};
		const swc::water::Tone capped = swc::water::albedo_gain(ocean, large);
		check(std::fabs(capped.scattering[2] / 0.003F - swc::water::kToneMaxAlbedo) < 1e-5F, "the albedo gain stops at the albedo 0.95");
		const float realm[3] = {0.0196F, 0.141F, 0.125F};
		const swc::water::Tone clear = swc::water::ocean_tone(realm, 0.0F, 0.0F, 4.0F);
		check(clear.scattering[0] == 0.0F && clear.absorption[0] == 0.0F, "a density of 0 is clear water (the magic realm box)");
		float absorption[4];
		float scattering[4];
		swc::water::tone_parameters(volume, absorption, scattering);
		check(std::fabs(1.0F / (absorption[0] * absorption[3]) - volume.absorption[0]) < 1e-8F
				&& std::fabs(scattering[1] * scattering[1] / 1000.0F - volume.scattering[1]) < 1e-8F,
			"the parameters give the coefficients back through the graph of Water_Material");
		// The default of the plugin: Absorption (10, 150, 350, 8): 1 / 80 per cm in red.
		check(std::fabs(1.0F / (10.0F * 8.0F) - 0.0125F) < 1e-9F, "the plugin default: the red absorption is 1 / 80 per cm");
	}
	std::printf("the ocean waves of the source (research/water-storm-waves.md)\n");
	{
		const std::vector<swc::water::OceanWave> storm = swc::water::ocean_waves(0.3F, 1.0F);
		check(static_cast<int>(storm.size()) == swc::water::kOceanWaveCount, "24 waves: 12 bins, two copies each");
		double squares = 0.0;
		bool unit = true;
		for (const swc::water::OceanWave& wave : storm) {
			squares += 0.5 * wave.amplitude * wave.amplitude;
			unit = unit && std::fabs(std::hypot(wave.direction[0], wave.direction[1]) - 1.0F) < 1e-5F;
		}
		check(unit, "each direction is a unit vector");
		// The ideal RMS height of the source field: 24 x S x sqrt(sum P) = 0.15045 m for the Cutlass Keys storm (S 0.3).
		check(std::fabs(std::sqrt(squares) - 15.045) < 0.01, "the storm: the RMS height of the list is the source 0.150 m");
		check(std::fabs(storm[0].amplitude - 6.49268F) < 1e-4F && storm[0].wavelength == 8000.0F && storm[1].wavelength == 4000.0F
				&& storm[0].direction[0] == -1.0F,
			"the storm: the axial bin [1, 0]: 6.49 cm, 80 m and 40 m, the direction -x");
		check(std::fabs(storm[0].steepness - 2.0F * 3.14159265F / 8000.0F * 6.49268F * 2.0F) < 1e-6F
				&& std::fabs(storm[1].steepness - 2.0F * 3.14159265F / 4000.0F * 6.49268F * 3.0F) < 1e-6F,
			"the steepness: k x the amplitude x 2 (the base copy) or 3 (the second copy)");
		// The bin [1, -1]: beta = 1 - 1/2.
		check(std::fabs(storm[6].steepness / (2.0F * 3.14159265F / storm[6].wavelength * storm[6].amplitude * 2.0F) - 0.5F) < 1e-5F,
			"the shear: beta = 1 + nx ny / r^2 (0.5 for the bin [1, -1])");
		const std::vector<swc::water::OceanWave> mission = swc::water::ocean_waves(0.08F, 2.0F);
		check(mission[0].wavelength == 4000.0F && std::fabs(mission[0].amplitude - 1.73138F) < 1e-4F,
			"the mission ocean (S 0.08, M 2): 40 m and 1.73 cm for the axial bin");
		const std::vector<swc::water::OceanWave> raid = swc::water::ocean_waves(0.7F, 1.0F);
		check(std::fabs(raid[0].amplitude / storm[0].amplitude - 0.7F / 0.3F) < 1e-5F && raid[0].wavelength == storm[0].wavelength,
			"a size change scales the amplitude only (the same field)");
		check(swc::water::ocean_waves(0.0F, 5.0F).empty() && swc::water::ocean_waves(0.2F, 0.0F).empty(),
			"a size of 0 (Tod_Ocean, the magic realm) or an amount of 0: no wave");
	}
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
