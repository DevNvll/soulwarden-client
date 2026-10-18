// The range rule of the interactions (docs/INTERACTIONS.md "The range"): the rule of the client (Interact/SwcInteraction: in_range,
// transform_point, Use::moved) against the world of the backend (its core library, world/interactions.cpp Interactions::check). The
// made-up client of the backend tests (the interaction fixture) sends input reports with the position of the character and the
// request; the server takes the position of a report before its request (as it reads the field: the height in steps of 1100 / 65535
// m, swc::net::position_field), and it refuses a request when the squared 3D distance to the bound is more than the squared range
// (equal passes, no margin). The cases: the edge of the range on one axis and in 3D, one float step over it, the edge in the steps
// of the height, a report with the request on the other side of the edge than the report before it, the case of the P5.1
// run (a node at 10 m: the press at 9.28 m in a fall, the landing at 10.08 m before the end of the hold), and the bounds of a
// turned server object through the PositionInTheWorld state of the backend encoder. The backend is the core library of the
// Soulwarden sources (read only) in SWC_WORK/build/backend-core. Build and run: tools/test-net.sh.

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "Interact/SwcInteraction.h"
#include "server/rep/states/state_codecs.h"
#include "support/interaction_fixture.h"

namespace {

namespace backend = soulwarden::backend;
namespace fixture = backend::tests::interaction;
namespace gamedata = backend::gamedata;

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& text)
{
	++g_checks;
	if (!condition) {
		++g_failures;
	}
	std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", text.c_str());
}

swc::net::WorldPosition client_of(const backend::state::WorldPosition& place)
{
	return swc::net::WorldPosition{place.x, place.y, place.z};
}

/** The P5.1 node: the saltpeter bound (range 10 m) 9.28 m from the press, at the offsets of the run, near the characters. */
constexpr std::uint8_t kNode = 10;
constexpr backend::state::WorldPosition kPress{100.0F, 200.0F, 30.0F};

/** The shop of the interaction cases with one more object: the node of the P5.1 run (its offsets from the press of the run). */
struct RangeShop : fixture::Shop {
	RangeShop() : fixture::Shop(false, ":memory:", false)
	{
		fixture::DataParts parts = fixture::data_parts();
		// The run: the press at 8485.760 5298.650 186.804 (the report of input 2588), the bound at 8478.388 5297.833 192.373.
		const backend::state::WorldPosition node{kPress.x + (8478.388F - 8485.760F), kPress.y + (5297.833F - 5298.650F), kPress.z + (192.373F - 186.804F)};
		parts.objects.push_back(fixture::object_of(kNode, gamedata::InteractionKind::lorePage, node, {fixture::kReadLore, fixture::kAddLore}, 1));
		parts.objects.back().bounds[0].rangeSq = 100.0F;
		if (!host.interactions.set(std::move(parts.options), std::move(parts.achievements), std::move(parts.objects))) {
			throw std::runtime_error("range test: the data was not set");
		}
	}

	/** The character A goes to a place in steps of 0.2 m (5 m/s: in the limits of the position check of the backend). */
	void walk(const backend::state::WorldPosition& to)
	{
		for (;;) {
			const float dx = to.x - placeA.x;
			const float dy = to.y - placeA.y;
			const float dz = to.z - placeA.z;
			const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (length <= 0.2F) {
				break;
			}
			placeA = backend::state::WorldPosition{placeA.x + dx / length * 0.2F, placeA.y + dy / length * 0.2F, placeA.z + dz / length * 0.2F};
			report(clientA, std::nullopt);
			host.steps(4, {&clientA});
		}
		placeA = to;
		report(clientA, std::nullopt);
		host.steps(4, {&clientA});
	}

	/** One report at a place with the request of the first option of an object: the result of the reply 3743 (0: no reply). */
	unsigned request_at(std::uint8_t tag, const backend::state::WorldPosition& place)
	{
		const std::size_t from = clientA.got.size();
		placeA = place;
		request(clientA, fixture::block_for(host.interactions, tag, fixture::kReadLore));
		const auto got = fixture::replies(clientA, from);
		end_block(clientA);
		return got.size() == 1 ? got[0].second : 0U;
	}
};

/** The bound of an object of the data (the same values as the data of the object layer of the client). */
const gamedata::InteractionBound& bound_of(const RangeShop& shop, std::uint8_t tag)
{
	return fixture::object_tagged(shop.host.interactions, tag).bounds[0];
}

/** The client rule and the server reply at a place (the report with the request): they must agree, and give `expected`. */
void edge_case(RangeShop& shop, std::uint8_t tag, const backend::state::WorldPosition& place, bool expected, const std::string& what)
{
	const gamedata::InteractionBound& bound = bound_of(shop, tag);
	const swc::net::WorldPosition character = swc::net::position_field(client_of(place));
	const bool client = swc::interact::in_range(character, client_of(bound.position), bound.rangeSq);
	const unsigned result = shop.request_at(tag, place);
	char text[160];
	std::snprintf(text, sizeof(text), " (distance %.7f m, range %.1f m; client %s, server result %u)",
		std::sqrt(static_cast<double>(swc::interact::distance_sq(character, client_of(bound.position)))), std::sqrt(static_cast<double>(bound.rangeSq)),
		client ? "in" : "out", result);
	check(client == expected && result == (expected ? 1U : 2U), what + text);
}

void edges()
{
	std::printf("the edge of the range: the client rule and the replies of the world of the backend\n");
	RangeShop shop;
	shop.enter(false);
	// The page of the fixture at 100 202 30, range 5 m (rangeSq 25).
	shop.walk({100.0F, 197.5F, 30.0F});
	edge_case(shop, fixture::kPage, {100.0F, 197.0F, 30.0F}, true, "5 m to the south: an equal distance passes");
	edge_case(shop, fixture::kPage, {100.0F, std::nextafter(197.0F, 0.0F), 30.0F}, false, "one float step more than 5 m: refused");
	shop.walk({103.0F, 198.0F, 30.0F});
	edge_case(shop, fixture::kPage, {103.0F, 198.0F, 30.0F}, true, "3 m to the east and 4 m to the south (3D 5 m): passes");
	edge_case(shop, fixture::kPage, {std::nextafter(103.0F, 200.0F), 198.0F, 30.0F}, false, "the same, one float step to the east: refused");
	// The page in the cellar at 100 202 24: the edge 5 m above it is in the steps of the height of the position field. The last
	// step in the range passes, the next step is refused; a place between two steps has the lower step (the client cuts it off).
	const auto step_height = [](int step) { return -100.0F + static_cast<float>(step) / 65535.0F * 1100.0F; };
	int last = static_cast<int>((29.0F + 100.0F) / 1100.0F * 65535.0F) + 2;
	while ((step_height(last) - 24.0F) * (step_height(last) - 24.0F) > 25.0F) {
		--last;
	}
	const float inside = 0.5F * (step_height(last) + step_height(last + 1));
	const float outside = 0.5F * (step_height(last + 1) + step_height(last + 2));
	shop.walk({100.0F, 202.0F, inside});
	edge_case(shop, fixture::kLowPage, {100.0F, 202.0F, inside}, true, "the page below the character, the last step of the height in the range: passes");
	edge_case(shop, fixture::kLowPage, {100.0F, 202.0F, outside}, false, "the next step of the height: refused");
	const swc::net::WorldPosition page = client_of(bound_of(shop, fixture::kLowPage).position);
	check(!swc::interact::in_range(swc::net::WorldPosition{100.0F, 202.0F, inside}, page, 25.0F),
		"the float height of the same place is out of the range: the rule must use the height of the field");

	// The position of the report that has the request, not the position of the report before it.
	std::printf("the position of the request: the report that has it\n");
	shop.walk({100.0F, 197.5F, 30.0F});
	edge_case(shop, fixture::kPage, {100.0F, 196.9F, 30.0F}, false, "the report before at 4.5 m, the report with the request at 5.1 m: refused");
	shop.walk({100.0F, 196.9F, 30.0F});
	edge_case(shop, fixture::kPage, {100.0F, 197.5F, 30.0F}, true, "the report before at 5.1 m, the report with the request at 4.5 m: passes");
}

/**
 * The case of the P5.1 run (evidence P5.1, chests-client.log and chests-backend.log): E at a saltpeter node at 9.28 m in a fall after a
 * teleport; the landing 1.14 m lower 0.36 s later, 10.08 m from the bound; the hold of 0.5 s ended after the landing, and the server
 * refused the request. With the rule of the client the hold ends at the landing, with no request; a request at the press place passes.
 */
void run_case()
{
	std::printf("the case of the P5.1 run: a press in a fall, the landing out of the range before the end of the hold\n");
	RangeShop shop;
	shop.enter(false);
	const backend::state::WorldPosition landing{kPress.x + (8485.827F - 8485.760F), kPress.y + (5299.046F - 5298.650F), kPress.z + (185.663F - 186.804F)};
	const gamedata::InteractionBound& bound = bound_of(shop, kNode);
	// The client: the focus at the press, the hold of 0.5 s (GatherOption), the landing in the hold.
	swc::interact::Option gather;
	gather.name = "GatherOption";
	gather.action = 14;
	gather.hold = 0.5F;
	swc::interact::Candidate candidate;
	candidate.option = &gather;
	candidate.place = client_of(bound.position);
	candidate.rangeSq = bound.rangeSq;
	candidate.distanceSq = swc::interact::distance_sq(client_of(kPress), candidate.place);
	check(swc::interact::in_range(client_of(kPress), candidate.place, candidate.rangeSq) && std::fabs(std::sqrt(candidate.distanceSq) - 9.28F) < 0.01F,
		"the press: the node in the range (9.28 m of 10 m)");
	swc::interact::Use use;
	use.press(35.024, candidate);
	use.tick(35.2);
	check(!use.moved(swc::net::position_field(client_of(kPress))) && use.phase() == swc::interact::Use::Phase::holding, "the fall: the hold goes on");
	const bool ended = use.moved(swc::net::position_field(client_of(landing)));
	use.tick(35.539);
	const swc::interact::Use::Effects effects = use.take();
	check(ended && use.requests() == 0 && !effects.requestChanged && !effects.cancel,
		"the landing at 10.08 m: the hold ends with no request (the run sent it and the server refused it)");
	// The server: the request after the landing is refused, the request at the press place passes.
	shop.walk(kPress);
	check(shop.request_at(kNode, kPress) == 1U, "the server: a request at the press place (9.28 m) passes");
	shop.walk(landing);
	check(shop.request_at(kNode, landing) == 2U, "the server: a request at the landing place (10.08 m) is refused, as in the run");
}

/**
 * The bounds of a turned server object (a camp: its bound 0.835 m from its origin): the PositionInTheWorld state of the backend
 * encoder (server/rep/states/object_states.cpp) into the readers of the client (the place and the rotation of the wire), then the bound
 * place of the client (transform_point) against the bound place of the server (gamedata::transform_point with the values before the
 * wire). The wire has the height in steps of 2100 / 65535 m and the rotation in steps of 1 / 32767.5: the client place is in those
 * steps. With no rotation (the rule before this change) the bound of a camp at a yaw of 180 degrees is 1.67 m wrong.
 */
void turned_bounds()
{
	std::printf("the bounds of a turned server object: the place and the rotation of the wire\n");
	const backend::state::WorldPosition place{8471.25F, 5302.5F, 188.77F};
	const backend::state::WorldPosition local{0.076F, -0.835F, 0.139F};
	const float clientLocal[3]{local.x, local.y, local.z};
	const auto yaw = [](float degrees) { return backend::server::rep::yaw_rotation(degrees); };
	const float s = std::sqrt(0.5F);
	const std::vector<std::pair<std::string, gamedata::Rotation>> rotations{{"no rotation", {0.0F, 0.0F, 0.0F, 1.0F}}, {"a yaw of 90 degrees", yaw(90.0F)},
		{"a yaw of 180 degrees", yaw(180.0F)}, {"a yaw of 237.5 degrees", yaw(237.5F)}, {"a yaw of -45 degrees", yaw(-45.0F)},
		{"a tilt of 90 degrees about x", {s, 0.0F, 0.0F, s}}, {"a turn about a slanted axis", {0.2F, -0.3F, 0.5F, std::sqrt(1.0F - 0.38F)}}};
	float worstFlat = 0.0F;
	float worstHeight = 0.0F;
	for (const auto& [name, rotation] : rotations) {
		const backend::server::hub::Bytes state = backend::server::rep::position_in_the_world_state(place, rotation);
		backend::tests::Fragment fragment;
		fragment.type = swc::net::kStatePositionInTheWorld;
		std::size_t position = 0;
		if (!backend::tests::detail::read_state(state, position, fragment)) {
			check(false, name + ": the state of the backend encoder reads");
			continue;
		}
		const swc::net::WorldPosition wirePlace = swc::net::read_object_place(backend::tests::field_of(fragment, 0, 0));
		const swc::net::Quaternion wireRotation = swc::net::read_place_rotation(backend::tests::field_of(fragment, 0, 1));
		const swc::net::WorldPosition client = swc::interact::transform_point(wirePlace, wireRotation, clientLocal);
		const backend::state::WorldPosition server = gamedata::transform_point(place, rotation, local);
		const float flat = std::max(std::fabs(client.x - server.x), std::fabs(client.y - server.y));
		const float height = std::fabs(client.z - server.z);
		worstFlat = std::max(worstFlat, flat);
		worstHeight = std::max(worstHeight, height);
		char text[200];
		std::snprintf(text, sizeof(text), "%s: the bound place of the client is %.6f m (x, y) and %.4f m (height) from the place of the server",
			name.c_str(), static_cast<double>(flat), static_cast<double>(height));
		check(flat <= 2.0e-4F && height <= 1050.0F / 65535.0F + 2.0e-4F, text);
	}
	char text[160];
	std::snprintf(text, sizeof(text), "the largest differences: %.6f m (x, y), %.4f m (height; the wire step is %.4f m)", static_cast<double>(worstFlat),
		static_cast<double>(worstHeight), 2100.0 / 65535.0);
	check(true, text);
	const gamedata::Rotation half = yaw(180.0F);
	const backend::state::WorldPosition server = gamedata::transform_point(place, half, local);
	const float plain = std::hypot(place.x + local.x - server.x, place.y + local.y - server.y);
	check(plain > 1.6F, "the bound of a camp at a yaw of 180 degrees with no rotation: " + std::to_string(plain) + " m wrong");
}

} // namespace

int main()
{
	try {
		edges();
		run_case();
		turned_bounds();
	} catch (const std::exception& error) {
		check(false, error.what());
	}
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
