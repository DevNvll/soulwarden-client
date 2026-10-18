#include "Environment/SwcEnvironmentRules.h"

#include <algorithm>
#include <cmath>

namespace swc::environment {
namespace {

constexpr double kPi = 3.14159265358979323846;

double radians(double degrees) {
    return degrees * kPi / 180.0;
}

/** A 3 x 3 matrix in rows (the rotation matrices of the public engine: SetRotationX, Y, Z). */
using Matrix = std::array<std::array<double, 3>, 3>;

Matrix rotation_x(double a) {
    return {{{1, 0, 0}, {0, std::cos(a), -std::sin(a)}, {0, std::sin(a), std::cos(a)}}};
}

Matrix rotation_y(double a) {
    return {{{std::cos(a), 0, std::sin(a)}, {0, 1, 0}, {-std::sin(a), 0, std::cos(a)}}};
}

Matrix rotation_z(double a) {
    return {{{std::cos(a), -std::sin(a), 0}, {std::sin(a), std::cos(a), 0}, {0, 0, 1}}};
}

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
        }
    }
    return out;
}

Vec3 normalized(Vec3 v) {
    const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return length > 0.0 ? Vec3{v.x / length, v.y / length, v.z / length} : Vec3{0.0, 0.0, 1.0};
}

Vec3 transform(const std::array<double, 16>& m, const Vec3& p) {
    return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
}

bool in_polygon(double x, double y, const std::vector<std::array<double, 2>>& points) {
    bool result = false;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const auto& a = points[i];
        const auto& b = points[j];
        if ((a[1] > y) != (b[1] > y) && x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]) {
            result = !result;
        }
    }
    return result;
}

} // namespace

std::array<double, 3> evaluate(const Curve& curve, double t) {
    const std::vector<Key>& keys = curve.keys;
    if (keys.empty()) {
        return curve.saved;
    }
    for (const Key& key : keys) {
        if (std::fabs(key.time - t) < 1e-7) {
            return key.value;
        }
    }
    if (keys.size() == 1 || t <= keys.front().time) {
        return keys.front().value;
    }
    if (t >= keys.back().time) {
        return keys.back().value;
    }
    const std::size_t count = keys.size();
    std::vector<std::array<double, 3>> incoming(count), outgoing(count);
    for (int c = 0; c < 3; ++c) {
        outgoing[0][c] = (keys[1].value[c] - keys[0].value[c]) / 3.0;
        incoming[count - 1][c] = (keys[count - 1].value[c] - keys[count - 2].value[c]) / 3.0;
    }
    for (std::size_t i = 1; i + 1 < count; ++i) {
        const double weight = (keys[i].time - keys[i - 1].time) / (keys[i + 1].time - keys[i - 1].time);
        const std::uint32_t in = keys[i].flags & 7U;
        const std::uint32_t out = (keys[i].flags >> 3) & 7U;
        for (int c = 0; c < 3; ++c) {
            const double delta = keys[i + 1].value[c] - keys[i - 1].value[c];
            incoming[i][c] = delta * weight / 3.0;
            outgoing[i][c] = delta * (1.0 - weight) / 3.0;
            if (in == 2 || in == 3) {
                incoming[i][c] = 0.0;
            } else if (in == 4) {
                incoming[i][c] = (keys[i].value[c] - keys[i - 1].value[c]) / 3.0;
            }
            if (out == 2 || out == 3) {
                outgoing[i][c] = 0.0;
            } else if (out == 4) {
                outgoing[i][c] = (keys[i + 1].value[c] - keys[i].value[c]) / 3.0;
            }
        }
    }
    std::size_t j = 0;
    while (j + 2 < count && keys[j + 1].time <= t) {
        ++j;
    }
    if (((keys[j].flags >> 3) & 7U) == 3U) {
        return keys[j + 1].value;
    }
    if ((keys[j + 1].flags & 7U) == 3U) {
        return keys[j].value;
    }
    const double u = (t - keys[j].time) / (keys[j + 1].time - keys[j].time);
    std::array<double, 3> out{};
    for (int c = 0; c < 3; ++c) {
        const double p0 = keys[j].value[c];
        const double p3 = keys[j + 1].value[c];
        const double p1 = p0 + outgoing[j][c];
        const double p2 = p3 - incoming[j + 1][c];
        out[c] = (1 - u) * (1 - u) * (1 - u) * p0 + 3 * u * (1 - u) * (1 - u) * p1 + 3 * u * u * (1 - u) * p2 + u * u * u * p3;
    }
    return out;
}

double curve_time(double hour) {
    return static_cast<double>(static_cast<float>(hour) * static_cast<float>(1.0 / 24.0));
}

Vec3 sun_toward(double hour, double sunRotationDegrees, double longitudeDegrees) {
    const double angle = (hour + 12.0) / 24.0 * 2.0 * kPi;
    const Matrix m = multiply(multiply(rotation_z(angle), rotation_x(0.5 * kPi - radians(longitudeDegrees))), rotation_y(radians(-sunRotationDegrees)));
    // (0, 1, 0) times the matrix (a row vector): the row 1; then the axes of the public engine: (x, -z, y).
    const Vec3 p{m[1][0], m[1][1], m[1][2]};
    return normalized({p.x, -p.z, p.y});
}

Vec3 moon_toward(double latitudeDegrees, double longitudeDegrees) {
    const double longitude = 0.5 * kPi - radians(longitudeDegrees);
    const double latitude = radians(latitudeDegrees) - kPi;
    return normalized({std::sin(longitude) * std::cos(latitude), std::sin(longitude) * std::sin(latitude), std::cos(longitude)});
}

Edges edges_of(std::uint32_t dawnTime, std::uint32_t dawnDuration, std::uint32_t duskTime, std::uint32_t duskDuration, std::uint32_t dayLength) {
    Edges out;
    const std::uint32_t factor = dayLength > 0 ? (dawnTime + 86400U - duskTime) / dayLength : 22U;
    out.dawnStart = dawnTime / 3600.0;
    out.dawnEnd = out.dawnStart + static_cast<double>(dawnDuration) * factor / 3600.0;
    out.duskStart = duskTime / 3600.0;
    out.duskEnd = out.duskStart + static_cast<double>(duskDuration) * factor / 3600.0;
    return out;
}

LightMultipliers light_multipliers(double hour, const Edges& e) {
    LightMultipliers out;
    if (hour < e.dawnStart || hour >= e.duskEnd) {
        out.moon = 1.0;
    } else if (hour < e.dawnEnd) {
        const double middle = 0.5 * (e.dawnStart + e.dawnEnd);
        if (hour < middle) {
            out.moon = (middle - hour) / (middle - e.dawnStart);
        } else {
            out.sun = (hour - middle) / (e.dawnEnd - middle);
        }
    } else if (hour < e.duskStart) {
        out.sun = 1.0;
    } else {
        const double middle = 0.5 * (e.duskStart + e.duskEnd);
        if (hour < middle) {
            out.sun = (middle - hour) / (middle - e.duskStart);
        } else {
            out.moon = (hour - middle) / (e.duskEnd - middle);
        }
    }
    return out;
}

LightColor light_color(const std::array<double, 3>& linear) {
    LightColor out;
    const double largest = std::max({linear[0], linear[1], linear[2], 1e-9});
    out.color = {linear[0] / largest, linear[1] / largest, linear[2] / largest};
    const double luminance = 0.2126 * out.color[0] + 0.7152 * out.color[1] + 0.0722 * out.color[2];
    out.intensityFactor = luminance > 1e-9 ? 1.0 / luminance : 1.0;
    return out;
}

std::array<double, 16> inverse_of(const std::array<double, 16>& m) {
    // The rigid part with scale: invert the 3 x 3 part by its adjugate, then the translation.
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    std::array<double, 16> out{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (std::fabs(det) < 1e-12) {
        return out;
    }
    const double r[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                         (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                         (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out[row * 4 + col] = r[row * 3 + col];
        }
        out[row * 4 + 3] = -(r[row * 3 + 0] * m[3] + r[row * 3 + 1] * m[7] + r[row * 3 + 2] * m[11]);
    }
    return out;
}

bool contains(const Volume& volume, const Vec3& point) {
    const Vec3 p = transform(volume.inverse, point);
    switch (volume.shape) {
    case Volume::Shape::cylinder:
        return std::hypot(p.x, p.y) <= volume.radius && std::fabs(p.z) <= volume.height / 2.0;
    case Volume::Shape::sphere:
        return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z) <= volume.radius;
    case Volume::Shape::box:
        return std::fabs(p.x) <= volume.dimensions[0] / 2.0 && std::fabs(p.y) <= volume.dimensions[1] / 2.0 && std::fabs(p.z) <= volume.dimensions[2] / 2.0;
    case Volume::Shape::capsule: {
        const double z = std::clamp(p.z, -volume.height / 2.0 + volume.radius, volume.height / 2.0 - volume.radius);
        return std::sqrt(p.x * p.x + p.y * p.y + (p.z - z) * (p.z - z)) <= volume.radius;
    }
    case Volume::Shape::polygonPrism:
        return p.z >= 0.0 && p.z <= volume.height && volume.vertices.size() >= 3 && in_polygon(p.x, p.y, volume.vertices);
    case Volume::Shape::none:
        break;
    }
    return false;
}

int select_volume(const std::vector<Volume>& volumes, const Vec3& point) {
    int best = -1;
    for (std::size_t i = 0; i < volumes.size(); ++i) {
        if ((best < 0 || volumes[i].priority > volumes[static_cast<std::size_t>(best)].priority) && contains(volumes[i], point)) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

} // namespace swc::environment
