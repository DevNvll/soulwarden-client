#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "SwcUi/Export.h"
#include "SwcUi/Model.h"

// The draw list of the loaded canvases in viewport pixels: the renderer of the client (Slate) and the preview of the
// offline tests draw it. The geometry follows the render rules of LyShine (O3DE UiImageComponent, UiTextComponent,
// UiMaskComponent, UiFaderComponent; facts only).
namespace swc::ui {

struct Vertex {
    float x{0.0F};
    float y{0.0F};
    float u{0.0F};
    float v{0.0F};
    std::uint32_t rgba{0xFFFFFFFFU};   // 8-bit sRGB values and straight alpha: R in the high byte
};

struct DrawItem {
    enum class Kind : std::uint8_t { mesh, text, push_clip, pop_clip };
    Kind kind{Kind::mesh};
    Id element{kNoId};
    // mesh: a list of triangles with one texture
    std::string texture;   // the PNG file; empty: white
    int blend{kBlendNormal};
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
    // text: one line of one pass
    std::string fontFile;
    float fontSize{0.0F};        // canvas units (LyShine size: pixels of the canvas)
    float letterSpacing{0.0F};   // 1/1000 em
    std::string text;            // UTF-8
    Affine transform;            // text space (x right, y down, origin at the top left of the line box) -> viewport
    Color color;                 // with the alpha of the text, the faders, and the pass
    // push_clip: the clip quad in viewport space (top left, top right, bottom right, bottom left)
    Vec2 quad[4];
};

struct DrawList {
    Vec2 viewport;
    std::vector<DrawItem> items;
    std::uint64_t meshes{0};
    std::uint64_t texts{0};
};

/** The text metrics of the renderer: the client measures with the fonts of Slate, the offline tests with an estimate. */
class SWCUI_EXPORT TextMeasure {
public:
    virtual ~TextMeasure() = default;
    /** @return The width of one line in canvas units. */
    virtual float width(const std::string& fontFile, float size, float letterSpacing, std::string_view text) = 0;
    /** @return The height of a line box in canvas units. */
    virtual float line_height(const std::string& fontFile, float size) = 0;
};

/** The layout of the text of an element: its lines and their places in the rect (canvas space). */
struct TextLayout {
    struct Line {
        std::string text;
        float x{0.0F};
        float y{0.0F};
        float width{0.0F};
    };
    std::vector<Line> lines;
    float fontSize{0.0F};
    float scaleX{1.0F};   // shrink to fit (width only)
    float width{0.0F};
    float height{0.0F};
};

/** Lays out the text of an element in its rect (wrap, alignment, case, shrink to fit, ellipsis). */
SWCUI_EXPORT TextLayout layout_text(Model& model, Id id, TextMeasure& measure);

/** The size of the text of an element at its own font size with no shrink to fit (the default layout cell of a text):
 *  the widest line with no wrap, and the height of the lines wrapped at the width of the rect. */
SWCUI_EXPORT Vec2 text_natural_size(Model& model, Id id, TextMeasure& measure);

/** The text metrics with no fonts: width = characters x size x 0.5 (the offline tests and the fallback). */
SWCUI_EXPORT TextMeasure& estimate_text_measure();

/** The text with no markup tags, in its case, as the renderer draws it. */
SWCUI_EXPORT std::string display_text(const TextData& text);

/** Builds the draw list of each enabled canvas, in the draw order. */
SWCUI_EXPORT void build_draw_list(Model& model, TextMeasure& measure, DrawList& out);

/** @return The draw list as JSON (the format of the preview, converter/swc_convert/uipreview.py). */
SWCUI_EXPORT std::string draw_list_json(const DrawList& list);

/** @return 8-bit RGBA of a color (sRGB values) with an alpha. */
SWCUI_EXPORT std::uint32_t pack_color(const Color& color, float alpha);

} // namespace swc::ui
