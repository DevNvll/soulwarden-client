#pragma once

#include <cstdint>
#include <set>
#include <tuple>

namespace swc::combat {

/**
 * One melee contact for each target for each strike of a fragment visit (stage P2.8, docs/ACTIONS.md). The key is the visit,
 * the strike, and the entity group of the target, not its hit volume: when the nearest hit volume of the same creature
 * changes between two steps of one strike, no second contact goes out (the server refuses it as the same attack, or as dead
 * after a kill). Plain C++: tools/test-actions.sh tests it.
 */
class ContactFilter {
public:
	/** @return True the first time for this visit, strike, and target group. */
	bool take(std::uint32_t visit, int strike, std::uint64_t group) { return m_seen.emplace(visit, strike, group).second; }
	void reset() { m_seen.clear(); }

private:
	std::set<std::tuple<std::uint32_t, int, std::uint64_t>> m_seen;
};

} // namespace swc::combat
