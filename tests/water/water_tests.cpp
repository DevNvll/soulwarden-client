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
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
