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

    /** A quad with the fill of the image (linear or radial), or the full quad. */
    void filled_quad(const Point q[4], const ImageData& image) {
        if (image.fillType == kFillLinear) {
            linear(q, image);
        } else if (image.fillType == kFillRadial || image.fillType == kFillRadialCorner || image.fillType == kFillRadialEdge) {
            radial(q, image);
        } else {
            quad(q);
        }
    }

private:
    void linear(const Point q[4], const ImageData& image) {
        // The vertices turn with the origin edge (0 left, 1 top, 2 right, 3 bottom); the far edge moves with the amount.
        const int offset = std::clamp(image.fillEdgeOrigin, 0, 3);
        Point v[4];
        for (int i = 0; i < 4; ++i) {
            v[i] = q[(i + offset) % 4];
        }
        const float amount = std::clamp(image.fillAmount, 0.0F, 1.0F);
        if (amount <= 0.0F) {
            return;
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

    void radial(const Point q[4], const ImageData& image) {
        const float amount = std::clamp(image.fillAmount, 0.0F, 1.0F);
        if (amount <= 0.0F) {
            return;
        }
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

    DrawItem& m_item;
    Affine m_transform;
    std::uint32_t m_color;
};

struct Walker {
    Model& model;
    TextMeasure& measure;
    DrawList& out;

    void image(const Element& e, const ImageData& image, float alpha, const std::string& overrideSprite = std::string(), int overrideIndex = -1) {
        const std::string& path = overrideSprite.empty() ? image.sprite : overrideSprite;
        const Sprite* sprite = nullptr;
        if (!path.empty()) {
            sprite = &model.sprite(path);
            if (!sprite->converted) {
                return;   // missing converted texture: nothing (the log names it)
            }
        }
        const float a = image.alpha * alpha;
        if (a <= 0.0F) {
            return;
        }
        DrawItem item;
        item.kind = DrawItem::Kind::mesh;
        item.element = e.id;
        item.texture = sprite != nullptr ? sprite->file : std::string();
        item.blend = image.blend;
        const Affine transform = model.to_viewport(e.id);
        MeshBuilder mesh(item, transform, pack_color(image.color, a));
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
            const float xs[4] = {r.left, r.left + lb * textureSize.x, r.right - rb * textureSize.x, r.right};
            const float ys[4] = {r.top, r.top + tb * textureSize.y, r.bottom - bb * textureSize.y, r.bottom};
            const float uw = uv.right - uv.left;
            const float vh = uv.bottom - uv.top;
            const float us[4] = {uv.left, uv.left + borders.left * uw, uv.left + borders.right * uw, uv.right};
            const float vs[4] = {uv.top, uv.top + borders.top * vh, uv.top + borders.bottom * vh, uv.bottom};
            if (image.fillType != kFillNone) {
                // A filled sliced image: the fill cuts the whole rect (an approximation of the clip of each slice).
                rect_points(r, q);
                mesh.filled_quad(q, image);
                break;
            }
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    if (row == 1 && col == 1 && !image.fillCenter) {
                        continue;
                    }
                    if (xs[col + 1] <= xs[col] || ys[row + 1] <= ys[row]) {
                        continue;
                    }
                    Point s[4] = {{{xs[col], ys[row]}, {us[col], vs[row]}},
                                  {{xs[col + 1], ys[row]}, {us[col + 1], vs[row]}},
                                  {{xs[col + 1], ys[row + 1]}, {us[col + 1], vs[row + 1]}},
                                  {{xs[col], ys[row + 1]}, {us[col], vs[row + 1]}}};
                    mesh.quad(s);
                }
            }
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
            out.items.push_back(std::move(item));
        }
    }

    void text(const Element& e, const TextData& text, float alpha) {
        const float a = text.alpha * alpha;
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
                    Color color = pass->color ? *pass->color : (piece.color ? *piece.color : text.color);
                    color.a = a * passAlpha;
                    item.color = color;
                    ++out.texts;
                    out.items.push_back(std::move(item));
                }
            }
        }
        if (clip) {
            DrawItem pop;
            pop.kind = DrawItem::Kind::pop_clip;
            pop.element = e.id;
            out.items.push_back(pop);
        }
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
        if (masking) {
            // The mask element (or its child mask element) clips the children: a quad clip. A sprite mask (an alpha
            // shape) is not exact here (docs/UI.md).
            DrawItem push;
            push.kind = DrawItem::Kind::push_clip;
            push.element = id;
            const Id shape = (e->mask->childMask != kNoId && model.element(e->mask->childMask) != nullptr) ? e->mask->childMask : id;
            model.viewport_points(shape, push.quad);
            out.items.push_back(push);
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
            out += "{\"k\":\"mesh\",\"tex\":" + json::quote(item.texture) + ",\"blend\":" + std::to_string(item.blend) + ",\"v\":[";
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
