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

double sun_step_degrees(double towardZ, double shiftPerMeter, double maxDegrees) {
    if (maxDegrees <= 0.0 || shiftPerMeter <= 0.0 || towardZ <= 0.0) {
        return 0.0;
    }
    const double sine = std::min(towardZ, 1.0);
    return std::min(maxDegrees, shiftPerMeter * sine * sine * 180.0 / 3.14159265358979323846);
}

double sun_path_hour(double hour, double dawnStart, double duskEnd, double nightDelay) {
    const double span = duskEnd - dawnStart;
    if (nightDelay <= 0.0 || span <= 0.0 || hour <= dawnStart || hour >= duskEnd) {
        return hour;
    }
    const double q = (hour - dawnStart) / span;
    const double d = std::min(nightDelay, span / 4.0);
    return dawnStart + d + q * (span - 2.0 * d);
}

Edges mission_edges(double dawnMinutes, double dawnDurationMinutes, double duskMinutes, double duskDurationMinutes) {
    Edges out;
    out.dawnStart = (dawnMinutes - dawnDurationMinutes / 2.0) / 60.0;
    out.dawnEnd = (dawnMinutes + dawnDurationMinutes / 2.0) / 60.0;
    out.duskStart = 12.0 + (duskMinutes - duskDurationMinutes / 2.0) / 60.0;
    out.duskEnd = 12.0 + (duskMinutes + duskDurationMinutes / 2.0) / 60.0;
    return out;
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

double inside_depth(const Volume& volume, const Vec3& point) {
    const Vec3 p = transform(volume.inverse, point);
    switch (volume.shape) {
    case Volume::Shape::cylinder:
        return std::min(volume.radius - std::hypot(p.x, p.y), volume.height / 2.0 - std::fabs(p.z));
    case Volume::Shape::sphere:
        return volume.radius - std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    case Volume::Shape::box:
        return std::min({volume.dimensions[0] / 2.0 - std::fabs(p.x), volume.dimensions[1] / 2.0 - std::fabs(p.y),
                         volume.dimensions[2] / 2.0 - std::fabs(p.z)});
    case Volume::Shape::capsule: {
        const double z = std::clamp(p.z, -volume.height / 2.0 + volume.radius, volume.height / 2.0 - volume.radius);
        return volume.radius - std::sqrt(p.x * p.x + p.y * p.y + (p.z - z) * (p.z - z));
    }
    case Volume::Shape::polygonPrism: {
        const std::size_t n = volume.vertices.size();
        if (n < 3) {
            return -1e9;
        }
        double edge = 1e18;
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const double ax = volume.vertices[j][0], ay = volume.vertices[j][1];
            const double bx = volume.vertices[i][0], by = volume.vertices[i][1];
            const double dx = bx - ax, dy = by - ay;
            const double length2 = dx * dx + dy * dy;
            const double t = length2 > 0.0 ? std::clamp(((p.x - ax) * dx + (p.y - ay) * dy) / length2, 0.0, 1.0) : 0.0;
            edge = std::min(edge, std::hypot(p.x - (ax + t * dx), p.y - (ay + t * dy)));
        }
        const double horizontal = in_polygon(p.x, p.y, volume.vertices) ? edge : -edge;
        return std::min({horizontal, p.z, volume.height - p.z});
    }
    case Volume::Shape::none:
        break;
    }
    return -1e9;
}

double volume_weight(const Volume& volume, const Vec3& point) {
    const double depth = inside_depth(volume, point);
    if (volume.blendDistance > 0.0) {
        return std::clamp(depth / volume.blendDistance, 0.0, 1.0);
    }
    return depth >= 0.0 ? 1.0 : 0.0;
}

double approach_weight(double weight, double target, double blendTime, double deltaSeconds) {
    if (blendTime <= 0.0) {
        return target;
    }
    const double step = deltaSeconds / blendTime;
    return weight < target ? std::min(target, weight + step) : std::max(target, weight - step);
}

double fog_height_falloff(double bottomDensity, double topDensity, double bottomHeight, double topHeight) {
    if (bottomDensity <= 0.0 || topDensity <= 0.0 || topHeight <= bottomHeight || topDensity >= bottomDensity) {
        return 0.0;
    }
    const double perMeter = std::log2(bottomDensity / topDensity) / (topHeight - bottomHeight);
    return perMeter / 100.0 * 1000.0;   // log2 units per cm, times 1000 (the property of the Unreal height fog)
}

Rotation celestial_from_engine(double hour, double sunRotationDegrees, double longitudeDegrees) {
    // sun_toward: the engine direction is S x M^T x e_y with M = Rz(angle) Rx(pi/2 - longitude) Ry(-rotation) (row vectors) and
    // S the axes (x, -z, y). The celestial frame has the sun at e_y: celestial = M x S^T x engine.
    const double angle = (hour + 12.0) / 24.0 * 2.0 * kPi;
    const Matrix m = multiply(multiply(rotation_z(angle), rotation_x(0.5 * kPi - radians(longitudeDegrees))), rotation_y(radians(-sunRotationDegrees)));
    const Matrix sT{{{1, 0, 0}, {0, 0, 1}, {0, -1, 0}}};
    // A half turn about the sun axis (e_y): the north celestial pole (+z) is the pole above the horizon (this sun path has the
    // other pole below it), and the star field keeps its hand.
    const Matrix half{{{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}}};
    return multiply(half, multiply(m, sT));
}

Vec3 star_vector(double rightAscensionHours, double declinationDegrees) {
    const double ra = rightAscensionHours / 12.0 * kPi;
    const double dec = radians(declinationDegrees);
    return {std::cos(dec) * std::sin(ra), std::cos(dec) * std::cos(ra), std::sin(dec)};
}

Vec3 rotate(const Rotation& r, const Vec3& v) {
    return {r[0][0] * v.x + r[0][1] * v.y + r[0][2] * v.z, r[1][0] * v.x + r[1][1] * v.y + r[1][2] * v.z,
            r[2][0] * v.x + r[2][1] * v.y + r[2][2] * v.z};
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

double source_ev(double illuminance, double evMin, double evMax, double autoCompensation) {
    const double ev = std::log2(std::max(illuminance, 1e-6) * 100.0 / 330.0)
        - (std::clamp(std::log10(illuminance + 1.0), 0.1, 5.2) - 3.0) / 2.0 * autoCompensation;
    return std::clamp(ev, evMin, evMax);
}

double source_exposure_bias(double ev100, double evMin, double evMax, double autoCompensation, double meterScale) {
    const double illuminance = kPi * 0.18 * std::exp2(ev100) / meterScale;
    return ev100 - std::log2(1.2) - source_ev(illuminance, evMin, evMax, autoCompensation);
}

namespace {
constexpr double kEarthRadius = 6368.0;
constexpr double kAtmosphere = 100.0;
constexpr double kRayleighHeight = 7.994;
constexpr double kMieHeight = 1.2;
constexpr int kShells = 32;

double shell_height(int index) {
    const double x = static_cast<double>(index) / static_cast<double>(kShells - 1);
    return kAtmosphere * std::exp(10.0 * (x - 1.0)) * x;
}

/** @return The distance along a ray from the radius r0 (km, from the earth center) with the cosine mu to the zenith at which the
 *  ray reaches the radius r (the outward root), or a negative value. */
double distance_to_radius(double r0, double mu, double r) {
    const double b = r0 * mu;
    const double disc = b * b - (r0 * r0 - r * r);
    return disc < 0.0 ? -1.0 : -b + std::sqrt(disc);
}

/** @return The optical depth (Rayleigh, Mie; km) from the radius r0 along the cosine mu to the top, or a negative Rayleigh value
 *  when the ray hits the earth. */
std::array<double, 2> optical_depth_up(double r0, double mu) {
    // The ray hits the earth when it goes down and comes nearer than the earth radius.
    if (mu < 0.0 && r0 * r0 * (1.0 - mu * mu) < kEarthRadius * kEarthRadius) {
        return {-1.0, 0.0};
    }
    const double h0 = r0 - kEarthRadius;
    double last = 0.0, sumR = 0.0, sumM = 0.0;
    double lastR = std::exp(-h0 / kRayleighHeight), lastM = std::exp(-h0 / kMieHeight);
    // The shells above the start height, then the top.
    for (int i = 0; i < kShells; ++i) {
        const double h = h0 + (kAtmosphere - h0) * shell_height(i) / kAtmosphere;
        const double t = distance_to_radius(r0, mu, kEarthRadius + h);
        if (t <= last) {
            continue;
        }
        // The height at the distance t (a ray that goes down first passes a lower point: the shells keep the step small).
        const double r = std::sqrt(r0 * r0 + t * t + 2.0 * r0 * t * mu);
        const double dR = std::exp(-(r - kEarthRadius) / kRayleighHeight), dM = std::exp(-(r - kEarthRadius) / kMieHeight);
        sumR += (lastR + dR) * 0.5 * (t - last);
        sumM += (lastM + dM) * 0.5 * (t - last);
        last = t;
        lastR = dR;
        lastM = dM;
    }
    return {sumR, sumM};
}
} // namespace

std::array<double, 2> source_sky_phases(double g, double cosine) {
    g = std::clamp(g, -0.9995, 0.9995);
    const double g2 = g * g;
    const double miePart = 1.5 * (1.0 - g2) / (2.0 + g2);
    // The source looks the phase up with -dot(sky direction, sun direction): with g < 0 the glow is about the sun.
    const double mie = miePart * (1.0 + cosine * cosine) / std::pow(1.0 + g2 + 2.0 * g * cosine, 1.5);
    return {mie, 0.75 * (1.0 + cosine * cosine)};
}

SkyParts source_sky(const SkyModel& model, const Vec3& view) {
    const double kr = 1e-4 * model.rayleigh, km = 1e-4 * model.mie;
    std::array<double, 3> inv4{};
    for (int c = 0; c < 3; ++c) {
        inv4[c] = std::pow(model.waveLengths[c] * 1e-3, -4.0);
    }
    const double horizontal = std::sqrt(view.x * view.x + view.y * view.y);
    const double mu = std::max(view.z, 0.0) / std::max(std::hypot(horizontal, std::max(view.z, 0.0)), 1e-9);
    const double sunLength = std::sqrt(model.sunToward.x * model.sunToward.x + model.sunToward.y * model.sunToward.y + model.sunToward.z * model.sunToward.z);
    const Vec3 sun{model.sunToward.x / sunLength, model.sunToward.y / sunLength, model.sunToward.z / sunLength};
    // The view direction above the horizon (the dome of the source has the upper half only).
    const double scale = horizontal > 1e-9 ? std::sqrt(1.0 - mu * mu) / horizontal : 0.0;
    const Vec3 dir{view.x * scale, view.y * scale, mu};
    const double r0 = kEarthRadius;
    std::array<double, 3> intM{}, intR{};
    double last = 0.0, viewR = 0.0, viewM = 0.0;
    std::array<double, 3> lastSampleM{}, lastSampleR{};
    for (int i = 0; i < kShells; ++i) {
        const double t = distance_to_radius(r0, mu, kEarthRadius + shell_height(i));
        if (t < 0.0) {
            continue;
        }
        const Vec3 p{dir.x * t, dir.y * t, r0 + dir.z * t};
        const double r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        const double h = r - kEarthRadius;
        const double dR = std::exp(-h / kRayleighHeight), dM = std::exp(-h / kMieHeight);
        if (i > 0) {
            // The optical depth of the view from the viewer to this sample (the trapezoid of the densities).
            const double prevH = shell_height(i - 1);
            viewR += (std::exp(-prevH / kRayleighHeight) + dR) * 0.5 * (t - last);
            viewM += (std::exp(-prevH / kMieHeight) + dM) * 0.5 * (t - last);
        }
        const double muSun = (p.x * sun.x + p.y * sun.y + p.z * sun.z) / r;
        const std::array<double, 2> toSun = optical_depth_up(r, muSun);
        std::array<double, 3> sampleM{}, sampleR{};
        if (toSun[0] >= 0.0) {
            for (int c = 0; c < 3; ++c) {
                const double tau = 4.0 * kPi * kr * inv4[c] * (toSun[0] + viewR) + 4.0 * kPi * km * (toSun[1] + viewM);
                const double a = std::exp(-tau);
                sampleM[c] = dM * a;
                sampleR[c] = dR * a;
            }
        }
        if (i > 0) {
            for (int c = 0; c < 3; ++c) {
                intM[c] += (lastSampleM[c] + sampleM[c]) * 0.5 * (t - last);
                intR[c] += (lastSampleR[c] + sampleR[c]) * 0.5 * (t - last);
            }
        }
        lastSampleM = sampleM;
        lastSampleR = sampleR;
        last = t;
    }
    SkyParts out;
    for (int c = 0; c < 3; ++c) {
        out.mie[c] = intM[c] * model.sunIntensity[c] * km;
        out.rayleigh[c] = intR[c] * model.sunIntensity[c] * kr * inv4[c];
    }
    return out;
}

Vec3 map_cube_to_sphere(const Vec3& v) {
    const double x2 = v.x * v.x, y2 = v.y * v.y, z2 = v.z * v.z;
    return {v.x * std::sqrt(std::max(0.0, 1.0 - 0.5 * y2 - 0.5 * z2 + 0.333 * y2 * z2)),
            v.y * std::sqrt(std::max(0.0, 1.0 - 0.5 * z2 - 0.5 * x2 + 0.333 * z2 * x2)),
            v.z * std::sqrt(std::max(0.0, 1.0 - 0.5 * x2 - 0.5 * y2 + 0.333 * x2 * y2))};
}

double probe_fade(const Vec3& boxSpace, double falloffMax, double uniform) {
    if (std::abs(boxSpace.x) >= 1.0 || std::abs(boxSpace.y) >= 1.0 || std::abs(boxSpace.z) >= 1.0) {
        return 0.0;
    }
    const Vec3 s = map_cube_to_sphere(boxSpace);
    const double edge = std::clamp(1.0 - (s.x * s.x + s.y * s.y + s.z * s.z), 0.0, 1.0);
    const double x = std::clamp(edge / std::max(falloffMax, 1e-6), 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x) * uniform;
}

std::vector<double> probe_weights(const std::vector<double>& fades) {
    std::vector<double> out(fades.size() + 1, 0.0);
    double sum = 0.0;
    for (std::size_t i = 0; i < fades.size() && sum < 1.0; ++i) {
        out[i] = (1.0 - sum) * std::clamp(fades[i], 0.0, 1.0);
        sum += out[i];
    }
    out.back() = std::max(0.0, 1.0 - sum);
    return out;
}

double snow_count_weight(const Vec3& camera, const Vec3& center, double radius) {
    const double r = std::max(radius, 0.001);
    const double dx = (center.x - camera.x) / r;
    const double dy = (center.y - camera.y) / r;
    const double dz = (center.z - camera.z) / r;
    return std::max(1.0 - (dx * dx + dy * dy + dz * dz), 0.0);
}

int snow_flake_count(int count, double weight) {
    return static_cast<int>(std::trunc(static_cast<double>(count) * std::clamp(weight, 0.0, 1.0)));
}

double snow_cluster_life(double gravityScale, double random) {
    return std::max(0.1, 4.0 / std::max(gravityScale, 1.0) + (random * 5.0 - 2.5));
}

double snow_fall_speed(double gravityScale, double multiplier) {
    return gravityScale * 9.8 * multiplier;
}

} // namespace swc::environment
