#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "SwcUi/Export.h"
#include "SwcUi/Json.h"

// The element tree of the loaded canvases and the LyShine rules for it (docs/UI.md). The data comes from the canvas
// converter (converter/swc_convert/ui.py, SWC_CONTENT/ui/canvases). The rules follow LyShine 1.12 (the O3DE source of
// LyShine is the reference, Apache-2.0 OR MIT, facts only): the rect of an element is the parent rect with the anchors
// and the offsets; rotation and scale act around the pivot and do not change the rect; the root element has the rect
// of the canvas; a disabled element hides its subtree; a fader multiplies the alpha of its subtree.
namespace swc::ui {

class TextMeasure;   // Draw.h

using Id = std::uint64_t;
/** The invalid entity id of Lumberyard 1.12 (the canvases use it for "no element"). */
inline constexpr Id kNoId = 0xFFFFFFFFull;

struct Vec2 {
    float x{0.0F};
    float y{0.0F};
};

struct Color {
    float r{1.0F};
    float g{1.0F};
    float b{1.0F};
    float a{1.0F};
};

struct Edges {
    float left{0.0F};
    float top{0.0F};
    float right{0.0F};
    float bottom{0.0F};
};

/** A 2D affine map: x' = a x + c y + tx, y' = b x + d y + ty. */
struct Affine {
    float a{1.0F}, b{0.0F}, c{0.0F}, d{1.0F}, tx{0.0F}, ty{0.0F};
    [[nodiscard]] Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    [[nodiscard]] Affine operator*(const Affine& o) const;
    [[nodiscard]] Affine inverse() const;
    static Affine translation(float x, float y) { return {1, 0, 0, 1, x, y}; }
    static Affine scaling(float x, float y) { return {x, 0, 0, y, 0, 0}; }
    static Affine rotation_degrees(float degrees);
};

// Enum orders of LyShine (O3DE UiImageBus.h, UiTextBus.h, UiTransformBus.h; the converter keeps the stored numbers).
enum ImageType { kImageStretched = 0, kImageSliced, kImageFixed, kImageTiled, kImageStretchedToFit, kImageStretchedToFill };
enum FillType { kFillNone = 0, kFillLinear, kFillRadial, kFillRadialCorner, kFillRadialEdge };
enum BlendMode { kBlendNormal = 0, kBlendAdd, kBlendScreen, kBlendDarken, kBlendLighten };
enum HAlign { kAlignLeft = 0, kAlignCenter, kAlignRight };
enum VAlign { kAlignTop = 0, kAlignMiddle, kAlignBottom };
enum Overflow { kOverflowText = 0, kOverflowClip, kOverflowEllipsis };
enum Wrap { kNoWrap = 0, kWrap, kWrapSplitWords };
enum Shrink { kShrinkNone = 0, kShrinkUniform, kShrinkWidthOnly };
enum TextCase { kCaseNormal = 0, kCaseLower, kCaseUpper };

struct TransformData {
    Edges anchors{0.0F, 0.0F, 1.0F, 1.0F};
    Edges offsets;
    Vec2 pivot{0.5F, 0.5F};
    float rotation{0.0F};
    Vec2 scale{1.0F, 1.0F};
    bool scaleToDevice{false};
};

struct ImageData {
    std::string sprite;   // texture path (".dds"), lower case; empty: a white rect
    int spriteType{0};
    int index{0};         // the cell of a sprite sheet
    Color color;          // r, g, b (the alpha of the color is not used)
    float alpha{1.0F};
    int type{kImageStretched};
    bool fillCenter{true};
    bool stretchSliced{true};
    int blend{kBlendNormal};
    // The overrides of the state actions of an interactable (hover, pressed, disabled, drag, drop): they win over the
    // values while the state lasts (LyShine UiVisualBus SetOverride...).
    std::optional<Color> overrideColor;
    std::optional<float> overrideAlpha;
    std::optional<std::string> overrideSprite;
    int overrideIndex{-1};
    int fillType{kFillNone};
    float fillAmount{1.0F};
    float fillStartAngle{0.0F};
    int fillCornerOrigin{0};
    int fillEdgeOrigin{0};
    bool fillClockwise{true};
    /** SetUVOverrides: a part of the texture (u0, v0, u1, v1). */
    std::optional<Edges> uvOverride;
};

struct TextData {
    std::string text;     // the text as the scripts set it (a "@key" is localized at the set)
    bool markup{false};
    Color color;
    float alpha{1.0F};
    std::string font;     // the .font path
    float size{32.0F};
    int effect{0};
    int hAlign{kAlignLeft};
    int vAlign{kAlignTop};
    float charSpacing{0.0F};   // 1/1000 em
    float lineSpacing{0.0F};   // pixels
    int overflow{kOverflowText};
    int wrap{kNoWrap};
    int shrink{kShrinkNone};
    float minShrink{0.0F};
    int textCase{kCaseNormal};
    std::optional<Color> overrideColor;   // the state actions of an interactable (as ImageData)
    std::optional<float> overrideAlpha;
    int cursor{-1};                       // a text input that edits: the cursor before this character (-1: none)
    Color cursorColor;
};

struct MaskData {
    bool enabled{true};
    bool interaction{true};
    Id childMask{kNoId};
    bool rtt{false};
    bool drawBehind{false};
    bool drawInFront{false};
    bool alphaTest{false};
};

struct FlipbookData {
    int start{0};
    int end{0};
    int loopStart{0};
    int loopType{0};      // 0 none, 1 linear, 2 ping-pong
    int rateUnit{0};      // 0 frames each second, 1 seconds each frame
    float rate{30.0F};
    float startDelay{0.0F};
    float loopDelay{0.0F};
    float reverseDelay{0.0F};
    bool autoPlay{false};
    // run-time state
    bool playing{false};
    int frame{0};
    int direction{1};
    float time{0.0F};
    float delay{0.0F};
};

struct SequenceData {
    int type{0};
    std::vector<std::string> images;
    std::string folder;
    int index{0};
    Color color;
    float alpha{1.0F};
    int blend{0};
};

/** A layout of the children (UiLayoutRowComponent, UiLayoutColumnComponent, UiLayoutGridComponent, and the fixed row and
 *  column of the fork). The layout sets the anchors of each child to 0 and its offsets (docs/UI.md, the layouts). */
struct LayoutData {
    enum class Kind : std::uint8_t { row, column, grid };
    Kind kind{Kind::row};
    bool fixed{false};            // the fork forms: the children keep their sizes; no padding, no layout cells
    Edges padding;                // left, top, right, bottom (pixels)
    Vec2 spacing;                 // row and column: x
    Vec2 cellSize{100.0F, 100.0F};   // grid
    int order{0};                 // row: 0 left to right, 1 right to left; column and grid rows: 0 top to bottom, 1 bottom to top
    int horizontalOrder{0};       // grid: 0 left to right, 1 right to left
    int startingWith{0};          // grid: 0 fill a row first, 1 fill a column first
    int hAlign{kAlignLeft};
    int vAlign{kAlignTop};
    bool ignoreDefaultCells{true};
    bool visibleOnly{false};      // grid and the fork forms: only the enabled children
};

/** The layout cell of an element (UiLayoutCellComponent): each value that the element overrides. */
struct CellData {
    std::optional<float> minWidth;
    std::optional<float> minHeight;
    std::optional<float> targetWidth;
    std::optional<float> targetHeight;
    std::optional<float> extraWidthRatio;
    std::optional<float> extraHeightRatio;
};

/** The cell of one child along one axis of a layout (min, target, extra ratio). */
struct LayoutCellSize {
    float min{0.0F};
    float target{0.0F};
    float extra{1.0F};
};

/** UiLayoutFitterComponent: the element takes the target size of its own layout cell. */
struct FitterData {
    bool horizontal{false};
    bool vertical{false};
};

struct ScriptData {
    std::string script;   // ".luac" path, lower case
    json::Value properties;
};

struct Element {
    Id id{kNoId};
    std::string name;
    Id parent{kNoId};
    Id canvas{kNoId};
    std::vector<Id> children;
    bool enabled{true};
    int renderPriority{0};
    bool childrenSortable{false};
    TransformData transform;
    std::optional<ImageData> image;
    std::optional<TextData> text;
    std::optional<float> fade;
    std::optional<MaskData> mask;
    std::optional<FlipbookData> flipbook;
    std::optional<SequenceData> sequence;
    std::optional<std::string> spawnerSlice;
    std::optional<float> saturation;
    std::optional<int> repeats;
    std::optional<LayoutData> layout;
    std::optional<CellData> cell;
    std::optional<FitterData> fitter;
    std::optional<json::Value> button;   // the action names of an interactable
    // The interactive components of the converted canvas (interactable, checkbox, radio, radioGroup, slider, scrollBar,
    // wheel, scrollBox, textInput, dropdown, draggable, dropTarget, markupButton, exitHover, dynamicScrollBox): their
    // data; the emulation (swc/interactables.lua) gives their behavior. A key that ends with "Entity" names an element.
    std::map<std::string, json::Value> components;
    bool handling{true};                 // UiInteractableBus IsHandlingEvents (an element with an interactable)
    std::vector<ScriptData> scripts;
    std::vector<std::string> other;      // the components that the runtime does not have
};

struct JavelinCanvasData {
    bool present{false};
    bool requiresMouse{false};
    bool escapable{false};
    bool replaceable{false};
    std::string drawLayer;
    std::vector<std::string> actionMapOverrides;
};

struct Canvas {
    Id entity{kNoId};        // the canvas entity: the "canvas id" of the scripts
    std::string path;        // the game path (".dynamicuicanvas")
    std::string name;        // the file name with no extension
    Vec2 authoredSize{1920.0F, 1080.0F};
    int drawOrder{0};
    bool enabled{true};
    bool pixelAligned{true};
    Id root{kNoId};
    JavelinCanvasData javelin;
    std::uint64_t loadSequence{0};
    /** The script components of the canvas entity itself (for example the globals canvas). */
    std::vector<ScriptData> scripts;
};

/** The texture data that the runtime needs: the size and the borders of the .sprite file next to the texture. */
struct Sprite {
    bool found{false};       // the game has the texture (a PNG, or an entry of textures/index.json)
    bool converted{false};   // the PNG is in the converted content (else nothing draws it)
    float width{0.0F};
    float height{0.0F};
    Edges borders{0.0F, 0.0F, 1.0F, 1.0F};   // fractions: left, top lines; right, bottom lines
    struct Cell {
        Edges uv{0.0F, 0.0F, 1.0F, 1.0F};
        Edges borders{0.0F, 0.0F, 1.0F, 1.0F};
        std::string alias;
    };
    std::vector<Cell> cells;
    std::string file;   // the PNG file
};

/** The font data of a .font file. */
struct FontInfo {
    std::string file;   // the font file (OTF or TTF), a full path
    float size{32.0F};
};

/** The passes of a font effect (LyShine font shader effects, _sharedfonteffects.xml). */
struct FontEffect {
    std::string name;
    struct Pass {
        std::optional<Color> color;   // none: the color of the text
        Vec2 offset;
    };
    std::vector<Pass> passes;
};

class SWCUI_EXPORT Model {
public:
    /** @param contentDir The folder SWC_CONTENT/ui. */
    explicit Model(std::string contentDir);

    // ---- the view
    /** The viewport in pixels. The canvas has 1080 lines: its target size is the viewport divided by height / 1080
     *  (provisional rule, open point O8 of the research). */
    void set_viewport(float width, float height);
    [[nodiscard]] Vec2 viewport() const { return m_viewport; }
    [[nodiscard]] float view_scale() const { return m_viewScale; }
    [[nodiscard]] Vec2 target_canvas_size(const Canvas& canvas) const;

    // ---- canvases
    /** Loads a canvas (a game path with any of the endings .uicanvas, .dynamicuicanvas, or none). A canvas that is
     *  loaded already gives its id again. @return The canvas id, or kNoId. */
    Id load_canvas(const std::string& path, std::string* error);
    void unload_canvas(Id canvas, std::vector<Id>* destroyed);
    [[nodiscard]] Canvas* canvas(Id id);
    [[nodiscard]] Id find_canvas_by_path(const std::string& path) const;
    /** @return The canvases in their draw order (then in their load order). */
    [[nodiscard]] std::vector<Canvas*> canvases_in_order();
    /** @return The elements of a canvas that the load made, parents before children. */
    [[nodiscard]] std::vector<Id> subtree(Id root) const;

    // ---- elements
    [[nodiscard]] Element* element(Id id);
    [[nodiscard]] const Element* element(Id id) const;
    /** A copy of the subtree of `source` under `parent` (before `before`, or at the end). The ids in the copy are new;
     *  a script property or a child mask that names an element of the subtree names its copy.
     *  @return The id of the copy of `source`. `created` gets each new id, parents before children. */
    Id clone_element(Id source, Id parent, Id before, std::vector<Id>* created);
    /** A copy of the elements of a UI slice under `parent`. @return The id of the root of the copy. */
    Id spawn_slice(const std::string& slicePath, Id parent, std::vector<Id>* created, std::string* error,
                   std::unordered_map<Id, Id>* idMap = nullptr);
    /** A new empty element. */
    Id create_element(const std::string& name, Id parent, Id before);
    /** Removes an element and its subtree. `destroyed` gets each id, children before parents. */
    void destroy_element(Id id, std::vector<Id>* destroyed);
    bool reparent(Id id, Id parent, Id before);
    [[nodiscard]] Id find_child(Id parent, const std::string& name) const;
    [[nodiscard]] Id find_descendant(Id parent, const std::string& name) const;
    [[nodiscard]] bool enabled_in_hierarchy(Id id) const;

    // ---- the LyShine transform rules
    /** The rect in canvas space with no scale or rotation (left, top, right, bottom). */
    [[nodiscard]] Edges rect(Id id) const;
    [[nodiscard]] Affine local_transform(Id id) const;
    [[nodiscard]] Affine to_canvas(Id id) const;
    [[nodiscard]] Affine to_viewport(Id id) const;
    [[nodiscard]] Vec2 pivot_point(Id id) const;                // canvas space, no scale or rotation
    [[nodiscard]] Vec2 local_position(Id id) const;
    void set_local_position(Id id, Vec2 position);
    [[nodiscard]] float local_width(Id id) const;
    [[nodiscard]] float local_height(Id id) const;
    void set_local_width(Id id, float width);
    void set_local_height(Id id, float height);
    [[nodiscard]] Vec2 viewport_position(Id id) const;
    void set_viewport_position(Id id, Vec2 position);
    [[nodiscard]] Vec2 canvas_position(Id id) const;
    void set_canvas_position(Id id, Vec2 position);
    /** The four corners in viewport space: top left, top right, bottom right, bottom left. */
    void viewport_points(Id id, Vec2 out[4]) const;
    [[nodiscard]] Vec2 device_scale(Id id) const;
    /** @return True when a viewport point is inside the rect of an element (with its scale and rotation). */
    [[nodiscard]] bool contains(Id id, Vec2 viewportPoint) const;
    /** @return The front-most element with an interactable that handles events at a viewport point, in the enabled
     *  canvases (the front canvas first; in a canvas, children before parents, the last child first). A mask
     *  with "mask interaction" hides the points outside it from its children. kNoId: none. */
    [[nodiscard]] Id interactable_at(Vec2 viewportPoint);

    // ---- the layouts (Layout.cpp; the rules of the LyShine layout manager)
    /** The text metrics for the default layout cells of the texts. It stays owned by the caller; null: an estimate. */
    void set_text_measure(TextMeasure* measure) { m_measure = measure; }
    [[nodiscard]] TextMeasure& text_measure();
    /** @return True when the element has a layout or a fitter (a layout controller). */
    [[nodiscard]] bool is_layout_controller(Id id) const;
    /** Marks the layout of an element (a controller) to compute again at the next update. */
    void mark_layout(Id id);
    /** Marks the layouts that use the layout cell of an element (its parents up to the first one that does not use
     *  the cells; a default cell counts only for a parent that does not ignore the default cells). */
    void mark_layout_cell_change(Id id, bool defaultCell);
    /** Computes the marked layouts and the layouts whose size changed. @return The number of computed roots. */
    int update_layouts();
    /** Computes the layout of an element and of its descendants now (widths first, then heights). */
    void compute_layout(Id id);
    [[nodiscard]] float cell_min_width(Id id, bool ignoreDefault);
    [[nodiscard]] float cell_min_height(Id id, bool ignoreDefault);
    [[nodiscard]] float cell_target_width(Id id, bool ignoreDefault);
    [[nodiscard]] float cell_target_height(Id id, bool ignoreDefault);
    [[nodiscard]] float cell_extra_width_ratio(Id id, bool ignoreDefault);
    [[nodiscard]] float cell_extra_height_ratio(Id id, bool ignoreDefault);

    // ---- content
    [[nodiscard]] const Sprite& sprite(const std::string& texturePath);
    [[nodiscard]] const FontInfo* font(const std::string& fontPath);
    [[nodiscard]] const FontEffect* font_effect(int index) const;
    [[nodiscard]] int font_effect_index(const std::string& name) const;
    /** @return The English text of a "@key" text, or the text itself. */
    [[nodiscard]] std::string localize(const std::string& text) const;
    [[nodiscard]] const std::string& content_dir() const { return m_contentDir; }
    /** The textures that the scripts asked for and the content does not have (tools/convert-ui.sh wanted). */
    [[nodiscard]] const std::set<std::string>& missing_textures() const { return m_missingTextures; }

    /** The lines of the runtime log (missing content, load failures). The host takes them. */
    std::vector<std::string> take_log();

private:
    struct Template {
        json::Value doc;
        bool isCanvas{false};
    };

    const Template* load_template(const std::string& path, std::string* error);
    void read_element(const json::Value& data, Element& out) const;
    Id new_id();
    void log(std::string line);
    void destroy_recursive(Id id, std::vector<Id>* destroyed);
    void remap_properties(json::Value& value, const std::unordered_map<Id, Id>& map) const;
    void remap_entities(json::Value& value, const std::unordered_map<Id, Id>& map) const;
    Id interactable_in(Id id, Vec2 point, Vec2 canvasPoint);
    void load_fonts();
    void load_texts();
    // Layout.cpp
    std::vector<Id> layout_children(Id id) const;
    std::vector<LayoutCellSize> cell_sizes(Id id, bool horizontal, bool ignoreDefault);
    float fitter_target(Id id, bool horizontal);
    bool default_cell(Id id, bool horizontal, int which, float maxSize, float& out);
    void apply_layout_width(Id id);
    void apply_layout_height(Id id);
    void add_layout_mark(Id id);
    /** A child came or went: the layout of the element and the layouts that use its cell (LyShine AddChild). */
    void child_list_changed(Id parent);
    void note_layout_element(Id id);

    std::string m_contentDir;
    Vec2 m_viewport{1920.0F, 1080.0F};
    float m_viewScale{1.0F};
    std::unordered_map<Id, Element> m_elements;
    std::map<Id, Canvas> m_canvases;
    std::unordered_map<std::string, Template> m_templates;
    std::unordered_map<std::string, Sprite> m_sprites;
    std::unordered_map<std::string, FontInfo> m_fonts;
    bool m_fontsLoaded{false};
    json::Value m_textureIndex;   // textures/index.json (loaded at the first missing texture)
    bool m_textureIndexLoaded{false};
    std::vector<FontEffect> m_effects;
    TextMeasure* m_measure{nullptr};
    std::vector<Id> m_layoutMarks;
    std::unordered_map<Id, Vec2> m_layoutSizes;   // the size of each layout element at its last computation
    std::set<Id> m_layoutElements;                // the elements with a layout
    std::unordered_map<std::string, std::string> m_texts;
    bool m_textsLoaded{false};
    std::set<std::string> m_missingTextures;
    std::vector<std::string> m_log;
    Id m_nextId{0x5357430000000000ull};   // "SWC" in the high bytes: ids that the runtime makes
    std::uint64_t m_loadSequence{0};
};

/** The CRC-32 of a text (the field names and the script property names of the object streams: lower case). */
SWCUI_EXPORT std::uint32_t crc32(std::string_view text);

/** @return The text in lower case (ASCII). */
SWCUI_EXPORT std::string lower(std::string_view text);

/** @return The id of a decimal text, or kNoId. */
SWCUI_EXPORT Id parse_id(const std::string& text);

} // namespace swc::ui
