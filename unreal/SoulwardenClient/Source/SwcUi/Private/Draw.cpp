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

std::string utf8_upper_or_lower(const std::string& text, bool upper) {
    std::string out = text;
    for (char& c : out) {
        if (upper && c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        } else if (!upper && c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    // Latin-1 letters in UTF-8 (2 bytes, 0xC3 lead): a small part of the case rules of the fork.
    for (std::size_t i = 0; i + 1 < out.size(); ++i) {
        if (static_cast<unsigned char>(out[i]) == 0xC3) {
            auto& next = reinterpret_cast<unsigned char&>(out[i + 1]);
            if (upper && next >= 0xA0 && next <= 0xBE && next != 0xB7) {
                next = static_cast<unsigned char>(next - 0x20);
            } else if (!upper && next >= 0x80 && next <= 0x9E && next != 0x97) {
                next = static_cast<unsigned char>(next + 0x20);
            }
            ++i;
        }
    }
    return out;
}

std::string strip_markup(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '<') {
            const std::size_t close = text.find('>', i);
            if (close != std::string::npos) {
                const std::string tag = lower(text.substr(i + 1, close - i - 1));
                if (tag == "br" || tag == "br/" || tag == "br /") {
                    out += '\n';
                }
                i = close;
                continue;
            }
        }
        if (c == '&') {
            static const std::pair<const char*, const char*> entities[] = {
                {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&nbsp;", " "}};
            bool replaced = false;
            for (const auto& [name, value] : entities) {
                const std::size_t length = std::char_traits<char>::length(name);
                if (text.compare(i, length, name) == 0) {
                    out += value;
                    i += length - 1;
                    replaced = true;
                    break;
                }
            }
            if (replaced) {
                continue;
            }
        }
        out += c;
    }
    return out;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    for (const char c : text) {
        if (c == '\n') {
            out.push_back(line);
            line.clear();
        } else if (c != '\r') {
            line += c;
        }
    }
    out.push_back(line);
    return out;
}

/** Wraps one line at spaces (or at characters for WrapSplitWords) to the width. */
std::vector<std::string> wrap_line(const std::string& line, float width, bool splitWords, const std::function<float(const std::string&)>& measure) {
    std::vector<std::string> out;
    if (measure(line) <= width) {
        out.push_back(line);
        return out;
    }
    std::string current;
    std::size_t at = 0;
    while (at < line.size()) {
        std::size_t next = line.find(' ', at);
        std::string word = line.substr(at, next == std::string::npos ? std::string::npos : next - at);
        const std::string candidate = current.empty() ? word : current + " " + word;
        if (measure(candidate) <= width || current.empty()) {
            if (current.empty() && measure(word) > width && splitWords) {
                // Split the word at the character that does not fit.
                std::string part;
                for (std::size_t i = 0; i < word.size();) {
                    std::size_t length = 1;
                    const auto lead = static_cast<unsigned char>(word[i]);
                    if (lead >= 0xF0) length = 4;
                    else if (lead >= 0xE0) length = 3;
                    else if (lead >= 0xC0) length = 2;
                    const std::string ch = word.substr(i, length);
                    if (!part.empty() && measure(part + ch) > width) {
                        out.push_back(part);
                        part.clear();
                    }
                    part += ch;
                    i += length;
                }
                current = part;
            } else {
                current = candidate;
            }
        } else {
            out.push_back(current);
            current = word;
        }
        if (next == std::string::npos) {
            break;
        }
        at = next + 1;
    }
    out.push_back(current);
    return out;
}

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
        const FontInfo* font = model.font(text.font);
        if (font == nullptr) {
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
        // The passes of the effect in reverse order: the first pass (the text itself) on top.
        for (auto pass = passes.rbegin(); pass != passes.rend(); ++pass) {
            Color color = pass->color ? *pass->color : text.color;
            const float passAlpha = pass->color ? pass->color->a : 1.0F;
            for (const TextLayout::Line& line : layout.lines) {
                DrawItem item;
                item.kind = DrawItem::Kind::text;
                item.element = e.id;
                item.fontFile = font->file;
                item.fontSize = layout.fontSize;
                item.letterSpacing = text.charSpacing;
                item.text = line.text;
                item.transform = transform * Affine::translation(line.x + pass->offset.x, line.y + pass->offset.y) *
                                 Affine::scaling(layout.scaleX, 1.0F);
                color.a = a * passAlpha;
                item.color = color;
                ++out.texts;
                out.items.push_back(std::move(item));
            }
        }
        if (clip) {
            DrawItem pop;
            pop.kind = DrawItem::Kind::pop_clip;
            pop.element = e.id;
            out.items.push_back(pop);
        }
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

std::string display_text(const TextData& text) {
    std::string out = text.markup ? strip_markup(text.text) : text.text;
    if (text.textCase == kCaseUpper) {
        out = utf8_upper_or_lower(out, true);
    } else if (text.textCase == kCaseLower) {
        out = utf8_upper_or_lower(out, false);
    }
    return out;
}

TextLayout layout_text(Model& model, Id id, TextMeasure& measure) {
    TextLayout layout;
    const Element* e = model.element(id);
    if (e == nullptr || !e->text) {
        return layout;
    }
    const TextData& text = *e->text;
    const FontInfo* font = model.font(text.font);
    const std::string file = font != nullptr ? font->file : std::string();
    const Edges r = model.rect(id);
    const float width = r.right - r.left;
    const float height = r.bottom - r.top;
    const std::string shown = display_text(text);

    const auto lay = [&](float size) {
        TextLayout out;
        out.fontSize = size;
        const auto measure_line = [&](const std::string& line) { return measure.width(file, size, text.charSpacing, line); };
        for (const std::string& line : split_lines(shown)) {
            if (text.wrap != kNoWrap && width > 0.0F) {
                for (std::string& part : wrap_line(line, width, text.wrap == kWrapSplitWords, measure_line)) {
                    out.lines.push_back({std::move(part), 0.0F, 0.0F, 0.0F});
                }
            } else {
                out.lines.push_back({line, 0.0F, 0.0F, 0.0F});
            }
        }
        const float lineHeight = measure.line_height(file, size);
        for (TextLayout::Line& line : out.lines) {
            line.width = measure_line(line.text);
            out.width = std::max(out.width, line.width);
        }
        out.height = lineHeight * static_cast<float>(out.lines.size()) + text.lineSpacing * static_cast<float>(out.lines.size() - 1);
        return out;
    };

    layout = lay(text.size);
    if (text.shrink == kShrinkUniform && (layout.width > width || layout.height > height) && layout.width > 0.0F) {
        float k = std::min(width / std::max(layout.width, 1e-6F), height / std::max(layout.height, 1e-6F));
        k = std::max(k, text.minShrink > 0.0F ? text.minShrink : 0.0F);
        layout = lay(text.size * std::min(k, 1.0F));
    } else if (text.shrink == kShrinkWidthOnly && layout.width > width && layout.width > 0.0F) {
        float k = width / layout.width;
        k = std::max(k, text.minShrink > 0.0F ? text.minShrink : 0.0F);
        layout.scaleX = std::min(k, 1.0F);
        for (TextLayout::Line& line : layout.lines) {
            line.width *= layout.scaleX;
        }
        layout.width *= layout.scaleX;
    }
    if (text.overflow == kOverflowEllipsis && text.wrap == kNoWrap) {
        const auto measure_line = [&](const std::string& line) { return measure.width(file, layout.fontSize, text.charSpacing, line) * layout.scaleX; };
        for (TextLayout::Line& line : layout.lines) {
            if (line.width <= width) {
                continue;
            }
            std::string cut = line.text;
            while (!cut.empty() && measure_line(cut + "...") > width) {
                cut.pop_back();
                while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) {
                    cut.pop_back();
                }
            }
            line.text = cut + "...";
            line.width = measure_line(line.text);
        }
    }
    const float lineHeight = measure.line_height(file, layout.fontSize);
    float y = r.top;
    if (text.vAlign == kAlignMiddle) {
        y = r.top + (height - layout.height) * 0.5F;
    } else if (text.vAlign == kAlignBottom) {
        y = r.bottom - layout.height;
    }
    for (TextLayout::Line& line : layout.lines) {
        float x = r.left;
        if (text.hAlign == kAlignCenter) {
            x = r.left + (width - line.width) * 0.5F;
        } else if (text.hAlign == kAlignRight) {
            x = r.right - line.width;
        }
        line.x = x;
        line.y = y;
        y += lineHeight + text.lineSpacing;
    }
    return layout;
}

Vec2 text_natural_size(Model& model, Id id, TextMeasure& measure) {
    const Element* e = model.element(id);
    if (e == nullptr || !e->text) {
        return {};
    }
    const TextData& text = *e->text;
    const FontInfo* font = model.font(text.font);
    const std::string file = font != nullptr ? font->file : std::string();
    const Edges r = model.rect(id);
    const float width = r.right - r.left;
    const auto measure_line = [&](const std::string& line) { return measure.width(file, text.size, text.charSpacing, line); };
    Vec2 size;
    std::size_t count = 0;
    for (const std::string& line : split_lines(display_text(text))) {
        size.x = std::max(size.x, measure_line(line));
        count += (text.wrap != kNoWrap && width > 0.0F) ? wrap_line(line, width, text.wrap == kWrapSplitWords, measure_line).size() : 1;
    }
    const float lineHeight = measure.line_height(file, text.size);
    size.y = count == 0 ? 0.0F : lineHeight * static_cast<float>(count) + text.lineSpacing * static_cast<float>(count - 1);
    return size;
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
                   ",\"text\":" + json::quote(item.text) + ",\"m\":" + buffer;
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
