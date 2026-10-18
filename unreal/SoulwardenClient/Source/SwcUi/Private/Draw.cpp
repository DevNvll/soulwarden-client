#include "SwcUi/Draw.h"
#include "SwcUi/Json.h"

#include <algorithm>
#include <cmath>

namespace swc::ui {
namespace {

struct Point {
    Vec2 position;
    Vec2 uv;
};

Point lerp(const Point& a, const Point& b, float t) {
    return {{a.position.x + (b.position.x - a.position.x) * t, a.position.y + (b.position.y - a.position.y) * t},
            {a.uv.x + (b.uv.x - a.uv.x) * t, a.uv.y + (b.uv.y - a.uv.y) * t}};
}

/** The mesh of one image: canvas-space points (no scale or rotation), transformed at the end. */
class MeshBuilder {
public:
    MeshBuilder(DrawItem& item, const Affine& transform, std::uint32_t color) : m_item(item), m_transform(transform), m_color(color) {}

    std::uint16_t add(const Point& p) {
        const Vec2 v = m_transform.apply(p.position);
        m_item.vertices.push_back({v.x, v.y, p.uv.x, p.uv.y, m_color});
        return static_cast<std::uint16_t>(m_item.vertices.size() - 1);
    }

    /** A quad: top left, top right, bottom right, bottom left (clockwise on the screen). */
    void quad(const Point q[4]) {
        const std::uint16_t a = add(q[0]);
        const std::uint16_t b = add(q[1]);
        const std::uint16_t c = add(q[2]);
        const std::uint16_t d = add(q[3]);
        m_item.indices.insert(m_item.indices.end(), {a, b, c, c, d, a});
    }

    void triangle(const Point& a, const Point& b, const Point& c) {
        const std::uint16_t ia = add(a);
        const std::uint16_t ib = add(b);
        const std::uint16_t ic = add(c);
        m_item.indices.insert(m_item.indices.end(), {ia, ib, ic});
    }

    /** A convex polygon (a fan of triangles). */
    void polygon(const std::vector<Point>& poly) {
        if (poly.size() < 3) {
            return;
        }
        std::vector<std::uint16_t> ids;
        ids.reserve(poly.size());
        for (const Point& p : poly) {
            ids.push_back(add(p));
        }
        for (std::size_t i = 1; i + 1 < ids.size(); ++i) {
            m_item.indices.insert(m_item.indices.end(), {ids[0], ids[i], ids[i + 1]});
        }
    }

    /** A quad with the fill of the image, or the full quad (O3DE UiImageComponent RenderFilledQuad). */
    void filled_quad(const Point q[4], const ImageData& image) {
        const float amount = std::clamp(image.fillAmount, 0.0F, 1.0F);
        switch (image.fillType) {
        case kFillNone:
            quad(q);
            break;
        case kFillLinear:
            if (amount > 0.0F) {
                linear(q, image.fillEdgeOrigin, amount);
            }
            break;
        case kFillRadial:
            if (amount > 0.0F) {
                radial(q, image, amount);
            }
            break;
        case kFillRadialCorner:
            if (amount > 0.0F) {
                radial_corner(q, image, amount);
            }
            break;
        case kFillRadialEdge:
            if (amount > 0.0F) {
                radial_edge(q, image, amount);
            }
            break;
        default:
            quad(q);
            break;
        }
    }

    /** A sliced image (the 9 quads of the slice values) with the fill of the image. A linear fill moves the slice
     *  values; a radial fill clips the quads by lines from the center (an angle, not a fraction of the border as on a
     *  single quad); a corner or edge fill clips them by one line from its origin (O3DE RenderSlicedLinearFilledSprite,
     *  ClipAndRenderForSlicedRadialFill, ClipAndRenderForSlicedRadialCornerOrEdgeFill). The lines are in canvas space:
     *  the same angles as O3DE (viewport space) unless the element has a scale that is not uniform. */
    void sliced(float xs[4], float ys[4], float us[4], float vs[4], const ImageData& image) {
        const float amount = std::clamp(image.fillAmount, 0.0F, 1.0F);
        if (image.fillType != kFillNone && amount <= 0.0F) {
            return;
        }
        if (image.fillType == kFillLinear) {
            const int origin = std::clamp(image.fillEdgeOrigin, 0, 3);
            clip_values(origin == 0 || origin == 2 ? xs : ys, origin == 0 || origin == 2 ? us : vs, origin >= 2, amount);
        }
        std::vector<std::vector<Point>> quads;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (row == 1 && col == 1 && !image.fillCenter) {
                    continue;
                }
                if (xs[col + 1] <= xs[col] || ys[row + 1] <= ys[row]) {
                    continue;
                }
                quads.push_back({{{xs[col], ys[row]}, {us[col], vs[row]}},
                                 {{xs[col + 1], ys[row]}, {us[col + 1], vs[row]}},
                                 {{xs[col + 1], ys[row + 1]}, {us[col + 1], vs[row + 1]}},
                                 {{xs[col], ys[row + 1]}, {us[col], vs[row + 1]}}});
            }
        }
        const Vec2 corners[4] = {{xs[0], ys[0]}, {xs[3], ys[0]}, {xs[3], ys[3]}, {xs[0], ys[3]}};
        const float sign = image.fillClockwise ? 1.0F : -1.0F;
        if (image.fillType == kFillRadial) {
            const Vec2 center{(xs[0] + xs[3]) * 0.5F, (ys[0] + ys[3]) * 0.5F};
            const Vec2 up{0.0F, (ys[0] - ys[3]) * 0.5F};
            const float start = image.fillStartAngle * kPi / 180.0F;
            // Clockwise: the first half is on the right of the line to the start; counter-clockwise the lines turn.
            const Vec2 first = offset(center, rotate(scale(up, -sign), start));
            const Vec2 second = offset(center, rotate(scale(up, sign), start));
            const Vec2 moving = offset(center, rotate(scale(up, sign), start + sign * 2.0F * kPi * amount));
            for (const std::vector<Point>& q : quads) {
                if (amount < 0.5F) {
                    polygon(clip(clip(q, center, first), center, moving));
                } else {
                    polygon(clip(q, center, first));
                    polygon(clip(clip(q, center, second), center, moving));
                }
            }
        } else if (image.fillType == kFillRadialCorner || image.fillType == kFillRadialEdge) {
            // The origin corner and the next corner clockwise: corner top left / edge top, then each quarter turn.
            const int which = image.fillType == kFillRadialCorner ? std::clamp(image.fillCornerOrigin, 0, 3)
                                                                  : (std::clamp(image.fillEdgeOrigin, 0, 3) + 3) % 4;
            const Vec2 from = corners[which];
            const Vec2 to = corners[(which + 1) % 4];
            const Vec2 origin = image.fillType == kFillRadialEdge ? Vec2{(from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F} : from;
            const float sweep = image.fillType == kFillRadialCorner ? kPi * 0.5F : kPi;
            const Vec2 along{(to.x - from.x) * sign, (to.y - from.y) * sign};
            const float angle = image.fillClockwise ? sweep * amount : sweep * (1.0F - amount);
            const Vec2 end = offset(origin, rotate(along, angle));
            for (const std::vector<Point>& q : quads) {
                polygon(clip(q, origin, end));
            }
        } else {
            for (const std::vector<Point>& q : quads) {
                polygon(q);
            }
        }
    }

private:
    static constexpr float kPi = 3.14159265358979F;

    static Vec2 rotate(Vec2 v, float angle) {
        // Positive angles turn clockwise on the screen (y goes down).
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return {v.x * c - v.y * s, v.x * s + v.y * c};
    }
    static Vec2 scale(Vec2 v, float k) { return {v.x * k, v.y * k}; }
    static Vec2 offset(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }

    /** The part of a convex polygon on one side of a line (O3DE ClipToLine keeps cross(line, point - origin) <= 0). */
    static std::vector<Point> clip(const std::vector<Point>& poly, Vec2 origin, Vec2 end) {
        const Vec2 d{end.x - origin.x, end.y - origin.y};
        const auto side = [&](const Point& p) { return d.x * (p.position.y - origin.y) - d.y * (p.position.x - origin.x); };
        constexpr float kEpsilon = 0.001F;
        std::vector<Point> out;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const Point& a = poly[(i + poly.size() - 1) % poly.size()];
            const Point& b = poly[i];
            const float sa = side(a);
            const float sb = side(b);
            const bool inA = sa < kEpsilon;
            const bool inB = sb < kEpsilon;
            if (inA != inB && std::fabs(sa - sb) > 1e-9F) {
                out.push_back(lerp(a, b, sa / (sa - sb)));
            }
            if (inB) {
                out.push_back(b);
            }
        }
        return out;
    }

    /** The slice values of a linear fill: the value at the amount (of the full length) and all values after it
     *  clamped to it (O3DE ClipValuesForSlicedLinearFill). */
    static void clip_values(float positions[4], float uvs[4], bool reverse, float amount) {
        const int first = reverse ? 3 : 0;
        const int step = reverse ? -1 : 1;
        const float total = positions[first + 3 * step] - positions[first];
        float previousPercentage = 0.0F;
        int previous = first;
        int clamp = -1;
        for (int k = 1; k < 4; ++k) {
            const int current = first + k * step;
            const float percentage = total != 0.0F ? (positions[current] - positions[first]) / total : 1.0F;
            if (clamp != -1) {
                positions[current] = positions[clamp];
                uvs[current] = uvs[clamp];
            } else if (percentage > amount) {
                const float t = (amount - previousPercentage) / (percentage - previousPercentage);
                positions[current] = positions[previous] + t * (positions[current] - positions[previous]);
                uvs[current] = uvs[previous] + t * (uvs[current] - uvs[previous]);
                clamp = current;
            }
            previousPercentage = percentage;
            previous = current;
        }
    }

    void linear(const Point q[4], int edgeOrigin, float amount) {
        // The vertices turn with the origin edge (0 left, 1 top, 2 right, 3 bottom); the far edge moves with the amount.
        const int offset = std::clamp(edgeOrigin, 0, 3);
        Point v[4];
        for (int i = 0; i < 4; ++i) {
            v[i] = q[(i + offset) % 4];
        }
        v[1] = lerp(v[0], v[1], amount);
        v[2] = lerp(v[3], v[2], amount);
        quad(v);
    }

    /** A point of the border of the quad at a fraction of its perimeter: 0 is the middle of the top edge, then
     *  clockwise (the top right corner at 1/8). The fill amount and the start angle are fractions of the perimeter
     *  (LyShine radial fill). */
    static Point border(const Point q[4], float p) {
        p = p - std::floor(p);
        // The corners in the order of the walk, from the middle of the top edge.
        const Point top = lerp(q[0], q[1], 0.5F);
        const Point walk[6] = {top, q[1], q[2], q[3], q[0], top};
        const float marks[6] = {0.0F, 0.125F, 0.375F, 0.625F, 0.875F, 1.0F};
        for (int i = 0; i < 5; ++i) {
            if (p <= marks[i + 1]) {
                const float t = (p - marks[i]) / (marks[i + 1] - marks[i]);
                return lerp(walk[i], walk[i + 1], t);
            }
        }
        return top;
    }

    void radial(const Point q[4], const ImageData& image, float amount) {
        const Point center = lerp(lerp(q[0], q[1], 0.5F), lerp(q[3], q[2], 0.5F), 0.5F);
        const float start = std::fmod(image.fillStartAngle, 360.0F) / 360.0F;
        const float direction = image.fillClockwise ? 1.0F : -1.0F;
        // The walk passes each corner mark between the start and the end: one triangle for each segment.
        std::vector<float> stops{0.0F};
        for (const float mark : {0.125F, 0.375F, 0.625F, 0.875F}) {
            float d = direction > 0.0F ? mark - start : start - mark;
            d -= std::floor(d);   // the walk distance to the corner, in [0, 1)
            if (d > 0.0F && d < amount) {
                stops.push_back(d);
            }
        }
        stops.push_back(amount);
        std::sort(stops.begin(), stops.end());
        for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
            triangle(center, border(q, start + direction * stops[i]), border(q, start + direction * stops[i + 1]));
        }
    }

    /** A fill that turns about a corner (0 top left, then clockwise): two triangles, the moving point goes along the
     *  far edges (O3DE RenderRadialCornerFilledQuad). Clockwise it starts at the edge from the corner to the next
     *  corner clockwise. */
    void radial_corner(const Point q[4], const ImageData& image, float amount) {
        const int offset = std::clamp(image.fillCornerOrigin, 0, 3);
        Point v[4];
        for (int i = 0; i < 4; ++i) {
            v[i] = q[(i + offset) % 4];
        }
        const bool second = amount >= 0.5F;
        const float s = (amount - (second ? 0.5F : 0.0F)) * 2.0F;
        if (image.fillClockwise) {
            triangle(v[0], v[1], second ? v[2] : lerp(v[1], v[2], s));
            if (second) {
                triangle(v[0], v[2], lerp(v[2], v[3], s));
            }
        } else {
            triangle(v[0], v[3], second ? v[2] : lerp(v[3], v[2], s));
            if (second) {
                triangle(v[0], v[2], lerp(v[2], v[1], s));
            }
        }
    }

    /** A fill that turns about the middle of an edge (0 left, 1 top, 2 right, 3 bottom): three triangles, the moving
     *  point goes along the other edges (O3DE RenderRadialEdgeFilledQuad). */
    void radial_edge(const Point q[4], const ImageData& image, float amount) {
        const int offset = std::clamp(image.fillEdgeOrigin, 0, 3);
        Point v[5];
        v[0] = lerp(q[offset], q[(offset + 3) % 4], 0.5F);
        for (int i = 1; i < 5; ++i) {
            v[i] = q[(i - 1 + offset) % 4];
        }
        const int segment = std::min(static_cast<int>(amount * 3.0F), 2);
        const float s = (amount - 0.3333F * static_cast<float>(segment)) * 3.0F;
        for (int k = 0; k <= segment; ++k) {
            if (image.fillClockwise) {
                triangle(v[0], v[k + 1], k == segment ? lerp(v[k + 1], v[k + 2], s) : v[k + 2]);
            } else {
                triangle(v[0], v[4 - k], k == segment ? lerp(v[4 - k], v[3 - k], s) : v[3 - k]);
            }
        }
    }

    DrawItem& m_item;
    Affine m_transform;
    std::uint32_t m_color;
};

struct Walker {
    Model& model;
    TextMeasure& measure;
    DrawList& out;

    /** A sprite mask that clips its children by the alpha of its texture (the mask element or its child mask element). */
    struct SpriteMask {
        std::string file;
        Affine toRect;    // viewport -> the canvas space of the rect of the mask element
        Edges rect;
        Edges uv;         // the part of the texture (a cell of a sprite sheet, else all)
        float alphaTest{0.0F};
    };
    std::vector<SpriteMask> masks;

    /** The mask UVs of the vertices of an item inside a sprite mask. */
    void apply_mask(DrawItem& item) const {
        if (masks.empty() || item.kind != DrawItem::Kind::mesh) {
            return;
        }
        const SpriteMask& m = masks.back();
        item.maskTexture = m.file;
        item.maskAlphaTest = m.alphaTest;
        const float w = m.rect.right - m.rect.left;
        const float h = m.rect.bottom - m.rect.top;
        for (Vertex& v : item.vertices) {
            const Vec2 p = m.toRect.apply({v.x, v.y});
            const float fx = w > 0.0F ? (p.x - m.rect.left) / w : 0.0F;
            const float fy = h > 0.0F ? (p.y - m.rect.top) / h : 0.0F;
            v.mu = m.uv.left + fx * (m.uv.right - m.uv.left);
            v.mv = m.uv.top + fy * (m.uv.bottom - m.uv.top);
        }
    }

    void image(const Element& e, const ImageData& image, float alpha, const std::string& overrideSprite = std::string(), int overrideIndex = -1) {
        // A state action sprite wins over the sprite (a flipbook frame gives its own sprite and index).
        const std::string& path = !overrideSprite.empty() ? overrideSprite : image.overrideSprite ? *image.overrideSprite : image.sprite;
        if (overrideIndex < 0 && image.overrideSprite && image.overrideIndex >= 0) {
            overrideIndex = image.overrideIndex;
        }
        const Sprite* sprite = nullptr;
        if (!path.empty()) {
            sprite = &model.sprite(path);
            if (!sprite->converted) {
                return;   // missing converted texture: nothing (the log names it)
            }
        }
        const float a = image.overrideAlpha.value_or(image.alpha) * alpha;
        if (a <= 0.0F) {
            return;
        }
        DrawItem item;
        item.kind = DrawItem::Kind::mesh;
        item.element = e.id;
        item.texture = sprite != nullptr ? sprite->file : std::string();
        item.blend = image.blend;
        const Affine transform = model.to_viewport(e.id);
        MeshBuilder mesh(item, transform, pack_color(image.overrideColor.value_or(image.color), a));
        const Edges r = model.rect(e.id);

        // The cell of a sprite sheet (the flipbook frame), else the full texture.
        Edges uv{0.0F, 0.0F, 1.0F, 1.0F};
        Edges borders{0.0F, 0.0F, 1.0F, 1.0F};
        Vec2 textureSize{1.0F, 1.0F};
        if (sprite != nullptr) {
            textureSize = {sprite->width, sprite->height};
            borders = sprite->borders;
            const int cell = overrideIndex >= 0 ? overrideIndex : image.index;
            if (!sprite->cells.empty()) {
                const Sprite::Cell& c = sprite->cells[static_cast<std::size_t>(std::clamp(cell, 0, static_cast<int>(sprite->cells.size()) - 1))];
                uv = c.uv;
                borders = c.borders;
                textureSize = {sprite->width * (uv.right - uv.left), sprite->height * (uv.bottom - uv.top)};
            }
        }
        if (image.uvOverride) {
            uv = *image.uvOverride;
        }
        int type = image.type;
        if (sprite == nullptr) {
            type = kImageStretched;
        }
        const bool zeroBorders = borders.left <= 0.0F && borders.top <= 0.0F && borders.right >= 1.0F && borders.bottom >= 1.0F;
        if (type == kImageSliced && zeroBorders) {
            type = kImageStretched;
        }
        const auto rect_points = [&uv](const Edges& rect, Point q[4]) {
            q[0] = {{rect.left, rect.top}, {uv.left, uv.top}};
            q[1] = {{rect.right, rect.top}, {uv.right, uv.top}};
            q[2] = {{rect.right, rect.bottom}, {uv.right, uv.bottom}};
            q[3] = {{rect.left, rect.bottom}, {uv.left, uv.bottom}};
        };
        const Vec2 pivot = e.transform.pivot;
        Point q[4];
        switch (type) {
        case kImageSliced: {
            float lb = borders.left;
            float rb = 1.0F - borders.right;
            float tb = borders.top;
            float bb = 1.0F - borders.bottom;
            const float w = r.right - r.left;
            const float h = r.bottom - r.top;
            if ((lb + rb) * textureSize.x > w && (lb + rb) > 0.0F) {
                const float k = w / ((lb + rb) * textureSize.x);
                lb *= k;
                rb *= k;
            }
            if ((tb + bb) * textureSize.y > h && (tb + bb) > 0.0F) {
                const float k = h / ((tb + bb) * textureSize.y);
                tb *= k;
                bb *= k;
            }
            float xs[4] = {r.left, r.left + lb * textureSize.x, r.right - rb * textureSize.x, r.right};
            float ys[4] = {r.top, r.top + tb * textureSize.y, r.bottom - bb * textureSize.y, r.bottom};
            const float uw = uv.right - uv.left;
            const float vh = uv.bottom - uv.top;
            float us[4] = {uv.left, uv.left + borders.left * uw, uv.left + borders.right * uw, uv.right};
            float vs[4] = {uv.top, uv.top + borders.top * vh, uv.top + borders.bottom * vh, uv.bottom};
            mesh.sliced(xs, ys, us, vs, image);
            break;
        }
        case kImageFixed:
        case kImageStretchedToFit:
        case kImageStretchedToFill: {
            Vec2 size = textureSize;
            if (type != kImageFixed) {
                const float sx = (r.right - r.left) / std::max(textureSize.x, 1e-6F);
                const float sy = (r.bottom - r.top) / std::max(textureSize.y, 1e-6F);
                const float k = type == kImageStretchedToFit ? std::min(sx, sy) : std::max(sx, sy);
                size = {textureSize.x * k, textureSize.y * k};
            }
            const float dx = size.x - (r.right - r.left);
            const float dy = size.y - (r.bottom - r.top);
            const Edges placed{r.left - dx * pivot.x, r.top - dy * pivot.y, r.right + dx * (1.0F - pivot.x), r.bottom + dy * (1.0F - pivot.y)};
            rect_points(placed, q);
            mesh.filled_quad(q, image);
            break;
        }
        case kImageTiled: {
            // One quad for each tile (the UV of a tile stays in 0..1: the renderer needs no wrap sampler).
            const float tw = std::max(textureSize.x, 1.0F);
            const float th = std::max(textureSize.y, 1.0F);
            Edges area = r;
            if (image.fillType == kFillLinear) {
                const float amount = std::clamp(image.fillAmount, 0.0F, 1.0F);
                switch (image.fillEdgeOrigin) {
                case 0: area.right = r.left + (r.right - r.left) * amount; break;
                case 1: area.bottom = r.top + (r.bottom - r.top) * amount; break;
                case 2: area.left = r.right - (r.right - r.left) * amount; break;
                default: area.top = r.bottom - (r.bottom - r.top) * amount; break;
                }
            }
            int tiles = 0;
            for (float y = r.top; y < area.bottom && tiles < 4096; y += th) {
                for (float x = r.left; x < area.right && tiles < 4096; x += tw) {
                    const float x0 = std::max(x, area.left);
                    const float y0 = std::max(y, area.top);
                    const float x1 = std::min(x + tw, area.right);
                    const float y1 = std::min(y + th, area.bottom);
                    if (x1 <= x0 || y1 <= y0) {
                        continue;
                    }
                    const float u0 = uv.left + (x0 - x) / tw * (uv.right - uv.left);
                    const float u1 = uv.left + (x1 - x) / tw * (uv.right - uv.left);
                    const float v0 = uv.top + (y0 - y) / th * (uv.bottom - uv.top);
                    const float v1 = uv.top + (y1 - y) / th * (uv.bottom - uv.top);
                    Point s[4] = {{{x0, y0}, {u0, v0}}, {{x1, y0}, {u1, v0}}, {{x1, y1}, {u1, v1}}, {{x0, y1}, {u0, v1}}};
                    mesh.quad(s);
                    ++tiles;
                }
            }
            break;
        }
        default:
            rect_points(r, q);
            mesh.filled_quad(q, image);
            break;
        }
        if (!item.indices.empty()) {
            ++out.meshes;
            apply_mask(item);
            out.items.push_back(std::move(item));
        }
    }

    void text(const Element& e, const TextData& text, float alpha) {
        const float a = text.overrideAlpha.value_or(text.alpha) * alpha;
        if (a <= 0.0F || text.text.empty()) {
            return;
        }
        if (model.font(text.font) == nullptr) {
            return;
        }
        const TextLayout layout = layout_text(model, e.id, measure);
        if (layout.lines.empty()) {
            return;
        }
        const Affine transform = model.to_viewport(e.id);
        const FontEffect* effect = model.font_effect(text.effect);
        std::vector<FontEffect::Pass> passes;
        if (effect != nullptr && !effect->passes.empty()) {
            passes = effect->passes;
        } else {
            passes.push_back({});
        }
        const Edges r = model.rect(e.id);
        bool clip = text.overflow == kOverflowClip;
        if (clip) {
            DrawItem push;
            push.kind = DrawItem::Kind::push_clip;
            push.element = e.id;
            push.quad[0] = transform.apply({r.left, r.top});
            push.quad[1] = transform.apply({r.right, r.top});
            push.quad[2] = transform.apply({r.right, r.bottom});
            push.quad[3] = transform.apply({r.left, r.bottom});
            out.items.push_back(push);
        }
        // The passes of the effect in reverse order: the first pass (the text itself) on top. A <font color> piece has
        // its own color in the first pass; the other passes (shadows, outlines) keep the color of the effect.
        for (auto pass = passes.rbegin(); pass != passes.rend(); ++pass) {
            const bool main = pass + 1 == passes.rend();
            const float passAlpha = pass->color ? pass->color->a : 1.0F;
            for (const TextLayout::Line& line : layout.lines) {
                for (const TextPiece& piece : line.pieces) {
                    if (!piece.image.empty()) {
                        if (main) {
                            inline_image(e, piece, line, transform, a);
                        }
                        continue;
                    }
                    if (piece.text.empty()) {
                        continue;
                    }
                    DrawItem item;
                    item.kind = DrawItem::Kind::text;
                    item.element = e.id;
                    item.fontFile = piece.fontFile;
                    item.fontSize = layout.glyphSize;
                    item.letterSpacing = layout.glyphSize > 0.0F ? text.charSpacing * layout.fontSize / layout.glyphSize : text.charSpacing;
                    item.text = piece.text;
                    item.transform = transform * Affine::translation(line.x + piece.x + pass->offset.x, line.y + line.textTop + pass->offset.y) *
                                     Affine::scaling(layout.scaleX, 1.0F);
                    Color color = pass->color ? *pass->color : (piece.color ? *piece.color : text.overrideColor.value_or(text.color));
                    color.a = a * passAlpha;
                    item.color = color;
                    ++out.texts;
                    out.items.push_back(std::move(item));
                }
            }
        }
        if (text.cursor >= 0) {
            cursor(e, text, layout, transform, a);
        }
        if (clip) {
            DrawItem pop;
            pop.kind = DrawItem::Kind::pop_clip;
            pop.element = e.id;
            out.items.push_back(pop);
        }
    }

    /** The cursor of a text input that edits: a line of 2 canvas units before the character at the cursor. */
    void cursor(const Element& e, const TextData& text, const TextLayout& layout, const Affine& transform, float alpha) {
        int left = text.cursor;
        const TextLayout::Line* at = &layout.lines.back();
        std::string prefix = at->text;
        for (const TextLayout::Line& line : layout.lines) {
            int count = 0;
            std::size_t bytes = 0;
            while (bytes < line.text.size() && count < left) {
                const auto lead = static_cast<unsigned char>(line.text[bytes]);
                bytes += lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
                ++count;
            }
            if (count == left || &line == &layout.lines.back()) {
                at = &line;
                prefix = line.text.substr(0, std::min(bytes, line.text.size()));
                break;
            }
            left -= count;
        }
        const std::string font = !at->pieces.empty() ? at->pieces.front().fontFile : std::string();
        const float spacing = layout.glyphSize > 0.0F ? text.charSpacing * layout.fontSize / layout.glyphSize : text.charSpacing;
        const float x = at->x + measure.width(font, layout.glyphSize, spacing, prefix) * layout.scaleX;
        const float top = at->y;
        const float bottom = at->y + at->height;
        DrawItem item;
        item.kind = DrawItem::Kind::mesh;
        item.element = e.id;
        const std::uint32_t rgba = pack_color(text.cursorColor, text.cursorColor.a * alpha);
        const Vec2 corners[4] = {{x, top}, {x + 2.0F, top}, {x + 2.0F, bottom}, {x, bottom}};
        for (const Vec2& corner : corners) {
            const Vec2 p = transform.apply(corner);
            item.vertices.push_back({p.x, p.y, 0.0F, 0.0F, rgba});
        }
        item.indices = {0, 1, 2, 0, 2, 3};
        ++out.meshes;
        out.items.push_back(std::move(item));
    }

    /** An inline image of a text (markup <img>): a quad of its texture at its place in the line. */
    void inline_image(const Element& e, const TextPiece& piece, const TextLayout::Line& line, const Affine& transform, float alpha) {
        const Sprite& sprite = model.sprite(piece.image);
        if (!sprite.converted || piece.imageWidth <= 0.0F || piece.imageHeight <= 0.0F) {
            return;
        }
        DrawItem item;
        item.kind = DrawItem::Kind::mesh;
        item.element = e.id;
        item.texture = sprite.file;
        item.blend = kBlendNormal;
        const float left = line.x + piece.x + piece.imageLeft;
        const float top = line.y + piece.imageTop;
        const std::uint32_t rgba = pack_color(Color{1.0F, 1.0F, 1.0F, 1.0F}, alpha);
        const Vec2 corners[4] = {{left, top}, {left + piece.imageWidth, top}, {left + piece.imageWidth, top + piece.imageHeight},
                                 {left, top + piece.imageHeight}};
        const Vec2 uvs[4] = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
        for (int i = 0; i < 4; ++i) {
            const Vec2 p = transform.apply(corners[i]);
            item.vertices.push_back({p.x, p.y, uvs[i].x, uvs[i].y, rgba});
        }
        item.indices = {0, 1, 2, 0, 2, 3};
        ++out.meshes;
        apply_mask(item);
        out.items.push_back(std::move(item));
    }

    void element(Id id, float alpha) {
        const Element* e = model.element(id);
        if (e == nullptr || !e->enabled) {
            return;
        }
        if (e->fade) {
            alpha *= std::clamp(*e->fade, 0.0F, 1.0F);
        }
        if (alpha <= 0.0F) {
            return;
        }
        const bool masking = e->mask && e->mask->enabled;
        const auto own = [&] {
            if (e->image) {
                if (e->flipbook) {
                    image(*e, *e->image, alpha, std::string(), e->flipbook->frame);
                } else {
                    image(*e, *e->image, alpha);
                }
            } else if (e->sequence && !e->sequence->images.empty()) {
                ImageData data;
                data.type = e->sequence->type;
                data.color = e->sequence->color;
                data.alpha = e->sequence->alpha;
                data.blend = e->sequence->blend;
                const std::size_t index = static_cast<std::size_t>(std::clamp(e->sequence->index, 0, static_cast<int>(e->sequence->images.size()) - 1));
                image(*e, data, alpha, e->sequence->images[index]);
            }
            if (e->text) {
                text(*e, *e->text, alpha);
            }
        };
        if (!masking) {
            own();
        } else if (e->mask->drawBehind) {
            own();
        }
        bool spriteMask = false;
        if (masking) {
            // The mask element (or its child mask element) clips the children: a quad clip; with a sprite, the images of
            // the children also take the alpha of the sprite (the mask UVs of their vertices; texts keep the quad).
            DrawItem push;
            push.kind = DrawItem::Kind::push_clip;
            push.element = id;
            const Id shape = (e->mask->childMask != kNoId && model.element(e->mask->childMask) != nullptr) ? e->mask->childMask : id;
            model.viewport_points(shape, push.quad);
            out.items.push_back(push);
            const Element* s = model.element(shape);
            if (s != nullptr && s->image && !s->image->sprite.empty()) {
                const Sprite& sprite = model.sprite(s->image->sprite);
                if (sprite.converted) {
                    SpriteMask m;
                    m.file = sprite.file;
                    m.toRect = model.to_viewport(shape).inverse();
                    m.rect = model.rect(shape);
                    m.uv = {0.0F, 0.0F, 1.0F, 1.0F};
                    if (!sprite.cells.empty()) {
                        m.uv = sprite.cells[static_cast<std::size_t>(std::clamp(s->image->index, 0, static_cast<int>(sprite.cells.size()) - 1))].uv;
                    }
                    m.alphaTest = e->mask->alphaTest ? 0.5F : 0.0F;
                    masks.push_back(m);
                    spriteMask = true;
                }
            }
        }
        std::vector<Id> children = e->children;
        if (e->childrenSortable) {
            std::stable_sort(children.begin(), children.end(), [this](Id a, Id b) {
                const Element* ea = model.element(a);
                const Element* eb = model.element(b);
                return (ea != nullptr ? ea->renderPriority : 0) < (eb != nullptr ? eb->renderPriority : 0);
            });
        }
        for (const Id child : children) {
            if (masking && child == e->mask->childMask) {
                continue;   // the child mask element is the shape, not a visual
            }
            element(child, alpha);
        }
        if (spriteMask) {
            masks.pop_back();
        }
        if (masking) {
            DrawItem pop;
            pop.kind = DrawItem::Kind::pop_clip;
            pop.element = id;
            out.items.push_back(pop);
            if (e->mask->drawInFront) {
                own();
            }
        }
    }
};

} // namespace

std::uint32_t pack_color(const Color& color, float alpha) {
    const auto byte = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    return (byte(color.r) << 24) | (byte(color.g) << 16) | (byte(color.b) << 8) | byte(alpha);
}

TextMeasure& estimate_text_measure() {
    struct Estimate final : TextMeasure {
        float width(const std::string&, float size, float spacing, std::string_view text) override {
            return static_cast<float>(text.size()) * size * (0.5F + spacing / 1000.0F);
        }
        float line_height(const std::string&, float size) override { return size; }
    };
    static Estimate estimate;
    return estimate;
}

void build_draw_list(Model& model, TextMeasure& measure, DrawList& out) {
    out.items.clear();
    out.meshes = 0;
    out.texts = 0;
    out.viewport = model.viewport();
    Walker walker{model, measure, out};
    for (Canvas* canvas : model.canvases_in_order()) {
        if (!canvas->enabled) {
            continue;
        }
        walker.element(canvas->root, 1.0F);
    }
}

// The draw list as JSON for the preview (converter/swc_convert/uipreview.py): meshes, text lines, and clip quads.
std::string draw_list_json(const DrawList& list) {
    std::string out = "{\"viewport\":[" + std::to_string(list.viewport.x) + "," + std::to_string(list.viewport.y) + "],\"items\":[";
    bool first = true;
    char buffer[128];
    for (const DrawItem& item : list.items) {
        out += first ? "" : ",";
        first = false;
        switch (item.kind) {
        case DrawItem::Kind::mesh: {
            out += "{\"k\":\"mesh\",\"tex\":" + json::quote(item.texture) + ",\"blend\":" + std::to_string(item.blend);
            if (!item.maskTexture.empty()) {
                out += ",\"mask\":" + json::quote(item.maskTexture) + ",\"at\":" + std::to_string(item.maskAlphaTest) + ",\"m\":[";
                for (std::size_t i = 0; i < item.vertices.size(); ++i) {
                    std::snprintf(buffer, sizeof(buffer), "%s%.4f,%.4f", i == 0 ? "" : ",", item.vertices[i].mu, item.vertices[i].mv);
                    out += buffer;
                }
                out += "]";
            }
            out += ",\"v\":[";
            for (std::size_t i = 0; i < item.vertices.size(); ++i) {
                const Vertex& vx = item.vertices[i];
                std::snprintf(buffer, sizeof(buffer), "%s%.2f,%.2f,%.4f,%.4f,%u", i == 0 ? "" : ",", vx.x, vx.y, vx.u, vx.v, vx.rgba);
                out += buffer;
            }
            out += "],\"i\":[";
            for (std::size_t i = 0; i < item.indices.size(); ++i) {
                out += (i == 0 ? "" : ",") + std::to_string(item.indices[i]);
            }
            out += "]}";
            break;
        }
        case DrawItem::Kind::text:
            std::snprintf(buffer, sizeof(buffer), "[%.4f,%.4f,%.4f,%.4f,%.2f,%.2f]", item.transform.a, item.transform.b, item.transform.c,
                          item.transform.d, item.transform.tx, item.transform.ty);
            out += "{\"k\":\"text\",\"font\":" + json::quote(item.fontFile) + ",\"size\":" + std::to_string(item.fontSize) +
                   ",\"ls\":" + std::to_string(item.letterSpacing) + ",\"text\":" + json::quote(item.text) + ",\"m\":" + buffer;
            std::snprintf(buffer, sizeof(buffer), ",\"color\":[%.3f,%.3f,%.3f,%.3f]}", item.color.r, item.color.g, item.color.b, item.color.a);
            out += buffer;
            break;
        case DrawItem::Kind::push_clip:
            std::snprintf(buffer, sizeof(buffer), "{\"k\":\"push\",\"q\":[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]}", item.quad[0].x,
                          item.quad[0].y, item.quad[1].x, item.quad[1].y, item.quad[2].x, item.quad[2].y, item.quad[3].x, item.quad[3].y);
            out += buffer;
            break;
        case DrawItem::Kind::pop_clip:
            out += "{\"k\":\"pop\"}";
            break;
        }
    }
    out += "]}\n";
    return out;
}

} // namespace swc::ui
