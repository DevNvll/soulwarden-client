#include "Environment/SwcLocalLightRules.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace swc::environment {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

double light_candela(const LocalLight& light, double scale) {
    if (light.type == LocalLight::Type::rect) {
        return kSourceUnitLuminance * light.diffuse * light.width * light.height * scale;
    }
    if (light.type == LocalLight::Type::point) {
        const double r = point_bulb(light);
        return kPi * kSourceUnitLuminance * light.diffuse * (1.0 + r) * (1.0 + r) * scale;
    }
    const double r = std::max(light.bulb, kMinBulb);
    return kPi * kSourceUnitLuminance * light.diffuse * r * r * scale;
}

double source_radius(const LocalLight& light) {
    return light.type == LocalLight::Type::spot ? std::max(light.bulb, kMinBulb) : 0.0;
}

double point_bulb(const LocalLight& light) {
    return std::max(light.bulb, kMinPointBulb);
}

double point_falloff(double d, double bulb, double range) {
    if (range <= 0.0 || d >= range) {
        return 0.0;
    }
    const double r = std::max(bulb, kMinPointBulb);
    const double x = d / range;
    const double f = std::pow(std::clamp(5.0 * (1.0 - x), 0.0, 1.0), 2.0);
    const double q = std::clamp(1.0 - x * x * x * x, 0.0, 1.0);
    const double ue = q * q;
    const double ratio = ue > 1e-6 ? f / ue : 25.0 / 16.0;
    const double near = std::max(d, r);
    return (d * d + 0.0001) / (near * near) * ratio;
}

double draw_distance(const LocalLight& light) {
    const double distance = std::clamp(light.range * kDrawRangeRatio, kDrawMin, kDrawMax);
    return light.cap > 0.0 ? std::min(distance, light.cap) : distance;
}

void LightGrid::add(int index, const Vec3& p) {
    const auto cx = static_cast<std::int64_t>(std::floor(p.x / cell_));
    const auto cy = static_cast<std::int64_t>(std::floor(p.y / cell_));
    cells_[key(cx, cy)].push_back(index);
    ++count_;
}

void LightGrid::query(const Vec3& center, double radius, std::vector<int>& out) const {
    const auto x0 = static_cast<std::int64_t>(std::floor((center.x - radius) / cell_));
    const auto x1 = static_cast<std::int64_t>(std::floor((center.x + radius) / cell_));
    const auto y0 = static_cast<std::int64_t>(std::floor((center.y - radius) / cell_));
    const auto y1 = static_cast<std::int64_t>(std::floor((center.y + radius) / cell_));
    for (std::int64_t cx = x0; cx <= x1; ++cx) {
        for (std::int64_t cy = y0; cy <= y1; ++cy) {
            const auto found = cells_.find(key(cx, cy));
            if (found != cells_.end()) {
                out.insert(out.end(), found->second.begin(), found->second.end());
            }
        }
    }
}

bool owns_lights(const std::string& npcSlice, const Vec3& root, const std::string& creatureSlice, const Vec3& creature) {
    if (npcSlice.empty() || lower(npcSlice) != lower(creatureSlice)) {
        return false;
    }
    const double dx = root.x - creature.x;
    const double dy = root.y - creature.y;
    const double dz = root.z - creature.z;
    return dx * dx + dy * dy + dz * dz <= kOwnerDistance * kOwnerDistance;
}

std::array<double, 16> multiply(const std::array<double, 16>& a, const std::array<double, 16>& b) {
    std::array<double, 16> out{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] + a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
    return out;
}

} // namespace swc::environment
