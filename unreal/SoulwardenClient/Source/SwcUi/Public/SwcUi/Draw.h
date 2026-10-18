#pragma once

#include <cstdint>
#include <optional>
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
    float fontSize{0.0F};        // the em size of the glyphs in canvas units (0.8 of the LyShine size)
    float letterSpacing{0.0F};   // 1/1000 of the em size of the glyphs
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
    /** @return The ascent of the font (from the top of the line box to the baseline) in canvas units. */
    virtual float ascent(const std::string& fontFile, float size) { return line_height(fontFile, size) * 0.8F; }
};

/** A run of a text after the markup (LyShine TextMarkup: b, i, a, font, img; the fork tag g): plain text in one font
 *  and color, or an inline image. */
struct TextSpan {
    std::string text;               // UTF-8
    std::string font;               // the .font path of a <font face>, else empty (the font of the element)
    std::optional<Color> color;     // the color of a <font color> (the alpha of the text stays)
    std::string linkAction;         // an <a action data> link (UiMarkupButtonComponent)
    std::string linkData;
    bool link{false};
    std::string image;              // an <img src>: the texture path (".dds" when it has no ending)
    std::string imageHeight;        // "fontAscent" (default), "fontHeight", or a number of pixels
    float imageScale{1.0F};
    std::string imageVAlign;        // baseline (default), top, center, bottom
    float imageYOffset{0.0F};
    float imageLeftPadding{0.0F};
    float imageRightPadding{0.0F};
    [[nodiscard]] bool is_image() const { return !image.empty(); }
};

/** The spans of a markup text. A gesture tag of the fork (<g ... />, the gestures of the conversation lines) gives no
 *  span. @return False when the text is not valid markup: then `out` has one span with the raw text (as LyShine). */
SWCUI_EXPORT bool markup_spans(const std::string& text, std::vector<TextSpan>& out);

/** The spans of the text of an element (markup when the element has it on), in its case. */
SWCUI_EXPORT std::vector<TextSpan> text_spans(const TextData& text);

/** One piece of a laid-out line: a run of text in one font and color, or an inline image. */
struct TextPiece {
    std::string text;               // empty for an image
    std::string fontFile;
    std::optional<Color> color;
    std::string image;              // the texture of an inline image
    float x{0.0F};                  // from the start of the line, canvas units (before a width-only shrink)
    float width{0.0F};              // with the paddings of an image
    float imageLeft{0.0F};          // an image: its left padding, its size, and its top from the top of the line
    float imageTop{0.0F};
    float imageWidth{0.0F};
    float imageHeight{0.0F};
    bool link{false};
    std::string linkAction;
    std::string linkData;
};

/** The layout of the text of an element: its lines and their places in the rect (canvas space). */
struct TextLayout {
    struct Line {
        std::vector<TextPiece> pieces;
        std::string text;           // the text of the pieces
        float x{0.0F};
        float y{0.0F};
        float width{0.0F};
        float height{0.0F};
        float textTop{0.0F};        // the top of the glyph line box from the top of the line
    };
    std::vector<Line> lines;
    float fontSize{0.0F};    // the LyShine font size (the line height) after a shrink to fit
    float glyphSize{0.0F};   // the em size of the glyphs (0.8 of the font size, CryFont)
    float scaleX{1.0F};   // shrink to fit (width only)
    float width{0.0F};
    float height{0.0F};
};

/** Lays out the text of an element in its rect (markup, wrap, alignment, case, shrink to fit, ellipsis). */
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
