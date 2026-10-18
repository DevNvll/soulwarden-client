#include "SwcUi/Draw.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

// The text of LyShine: the markup (O3DE TextMarkup: the text becomes XML; the tags b, i, a, font, img; a text that is
// not valid markup shows raw) and the layout of the draw batches (wrap at spaces, the fork WrapSplitWords, shrink to fit,
// ellipsis, inline images). O3DE Gems/LyShine is a reference only (facts, no code). The fork adds the tag g (the
// gestures of the conversation lines): it gives no text.

namespace swc::ui {
namespace {

// CryFont draws a glyph at 0.8 of the font size (the default size ratio of a font with no "sizeratio": all the .font
// files of the game); the line is the font size. The widths that the editor saved in the canvases agree: 0.79 to 0.84 of
// the em widths at the font size, for each font (tests/ui: the layouts with text cells). The character spacing stays in
// 1/1000 of the font size.
constexpr float kGlyphSizeRatio = 0.8F;

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

bool same_color(const std::optional<Color>& a, const std::optional<Color>& b) {
    if (a.has_value() != b.has_value()) return false;
    return !a || (a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a);
}

std::size_t utf8_length(unsigned char lead) {
    return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
}

/** Removes the gesture tags of the fork (<g ... />) from a raw text. */
std::string without_gestures(const std::string& text) {
    std::string out;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t open = text.find("<g ", at);
        if (open == std::string::npos) {
            break;
        }
        const std::size_t close = text.find("/>", open);
        if (close == std::string::npos) {
            break;
        }
        out.append(text, at, open - at);
        at = close + 2;
    }
    out.append(text, at, std::string::npos);
    return out;
}

/** The XML text rules: the five entities; a '&' with no entity is not valid. */
bool decode_entities(const std::string& text, std::string& out) {
    out.clear();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }
        static const std::pair<const char*, char> entities[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
        bool found = false;
        for (const auto& [name, value] : entities) {
            const std::size_t length = std::char_traits<char>::length(name);
            if (text.compare(i, length, name) == 0) {
                out += value;
                i += length - 1;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

struct Tag {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    bool closing{false};
    bool selfClosing{false};
};

/** Reads one tag (the text between '<' and '>'). @return False when it is not a valid XML tag. */
bool read_tag(const std::string& body, Tag& tag) {
    std::size_t at = 0;
    const auto skip_space = [&] {
        while (at < body.size() && std::isspace(static_cast<unsigned char>(body[at]))) ++at;
    };
    skip_space();
    if (at < body.size() && body[at] == '/') {
        tag.closing = true;
        ++at;
    }
    const std::size_t nameStart = at;
    while (at < body.size() && (std::isalnum(static_cast<unsigned char>(body[at])) || body[at] == '_' || body[at] == '-')) ++at;
    tag.name = body.substr(nameStart, at - nameStart);
    if (tag.name.empty()) {
        return false;
    }
    while (true) {
        skip_space();
        if (at >= body.size()) {
            return true;
        }
        if (body[at] == '/' && at + 1 == body.size()) {
            tag.selfClosing = true;
            return !tag.closing;
        }
        if (tag.closing) {
            return false;
        }
        const std::size_t keyStart = at;
        while (at < body.size() && (std::isalnum(static_cast<unsigned char>(body[at])) || body[at] == '_' || body[at] == '-')) ++at;
        const std::string key = body.substr(keyStart, at - keyStart);
        skip_space();
        if (key.empty() || at >= body.size() || body[at] != '=') {
            return false;
        }
        ++at;
        skip_space();
        if (at >= body.size() || (body[at] != '"' && body[at] != '\'')) {
            return false;
        }
        const char quote = body[at++];
        const std::size_t end = body.find(quote, at);
        if (end == std::string::npos) {
            return false;
        }
        std::string value;
        if (!decode_entities(body.substr(at, end - at), value)) {
            return false;
        }
        tag.attributes.emplace_back(key, value);
        at = end + 1;
    }
}

bool read_float(const std::string& text, float& out) {
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str()) {
        return false;
    }
    out = v;
    return true;
}

} // namespace

bool markup_spans(const std::string& text, std::vector<TextSpan>& out) {
    out.clear();
    struct Style {
        std::string name;
        std::string font;
        std::optional<Color> color;
        bool link{false};
        std::string action;
        std::string data;
    };
    std::vector<Style> stack{Style{}};
    std::vector<TextSpan> spans;
    const auto fail = [&]() {
        out.clear();
        TextSpan raw;
        raw.text = text;
        out.push_back(std::move(raw));
        return false;
    };
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] == '<') {
            const std::size_t close = text.find('>', at);
            if (close == std::string::npos) {
                return fail();
            }
            Tag tag;
            if (!read_tag(text.substr(at + 1, close - at - 1), tag)) {
                return fail();
            }
            at = close + 1;
            if (tag.closing) {
                if (stack.size() < 2 || stack.back().name != tag.name) {
                    return fail();
                }
                stack.pop_back();
                continue;
            }
            Style style = stack.back();
            style.name = tag.name;
            if (tag.name == "g") {
                // The fork gesture tag: no text (its attributes are for the conversation animations).
            } else if (tag.name == "b" || tag.name == "i") {
                if (!tag.attributes.empty()) {
                    return fail();
                }
                // A .font file is a family of one font: bold and italic do not change it.
            } else if (tag.name == "a") {
                style.link = true;
                for (const auto& [key, value] : tag.attributes) {
                    if (key == "action") style.action = value;
                    else if (key == "data") style.data = value;
                    else return fail();
                }
            } else if (tag.name == "font") {
                if (tag.attributes.empty()) {
                    return fail();
                }
                for (const auto& [key, value] : tag.attributes) {
                    if (key == "face") {
                        style.font = value;
                    } else if (key == "color") {
                        std::string v = value;
                        v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }), v.end());
                        if (v.size() == 7 && v[0] == '#') {
                            const auto channel = [&v](std::size_t i) {
                                return static_cast<float>(std::strtol(v.substr(i, 2).c_str(), nullptr, 16)) / 255.0F;
                            };
                            style.color = Color{channel(1), channel(3), channel(5), 1.0F};
                        }
                    } else {
                        return fail();
                    }
                }
            } else if (tag.name == "img") {
                TextSpan image;
                for (const auto& [key, value] : tag.attributes) {
                    float number = 0.0F;
                    if (key == "src") image.image = value;
                    else if (key == "height") image.imageHeight = value;
                    else if (key == "scale" && read_float(value, number)) image.imageScale = number;
                    else if (key == "vAlign") image.imageVAlign = value;
                    else if (key == "yOffset" && read_float(value, number)) image.imageYOffset = number;
                    else if (key == "xPadding" && read_float(value, number)) image.imageLeftPadding = image.imageRightPadding = number;
                    else if (key == "lPadding" && read_float(value, number)) image.imageLeftPadding = number;
                    else if (key == "rPadding" && read_float(value, number)) image.imageRightPadding = number;
                    else return fail();
                }
                if (image.image.empty()) {
                    return fail();
                }
                std::string& path = image.image;
                std::replace(path.begin(), path.end(), '\\', '/');
                const std::size_t slash = path.rfind('/');
                if (path.find('.', slash == std::string::npos ? 0 : slash) == std::string::npos) {
                    path += ".dds";
                }
                image.font = stack.back().font;
                spans.push_back(std::move(image));
            } else {
                return fail();
            }
            if (!tag.selfClosing) {
                stack.push_back(std::move(style));
            }
            continue;
        }
        const std::size_t next = text.find('<', at);
        std::string chars;
        if (!decode_entities(text.substr(at, next == std::string::npos ? std::string::npos : next - at), chars)) {
            return fail();
        }
        at = next == std::string::npos ? text.size() : next;
        const Style& style = stack.back();
        if (style.name == "img" || style.name == "g") {
            continue;   // no text inside an image or a gesture
        }
        TextSpan span;
        span.text = std::move(chars);
        span.font = style.font;
        span.color = style.color;
        span.link = style.link;
        span.linkAction = style.action;
        span.linkData = style.data;
        spans.push_back(std::move(span));
    }
    if (stack.size() != 1) {
        return fail();
    }
    // An escaped newline in the text of a tag is a newline.
    for (TextSpan& span : spans) {
        for (std::size_t p = span.text.find("\\n"); p != std::string::npos; p = span.text.find("\\n", p + 1)) {
            span.text.replace(p, 2, "\n");
        }
    }
    out = std::move(spans);
    return true;
}

std::vector<TextSpan> text_spans(const TextData& text) {
    std::vector<TextSpan> spans;
    if (text.markup) {
        markup_spans(text.text, spans);
    } else {
        TextSpan raw;
        raw.text = without_gestures(text.text);
        spans.push_back(std::move(raw));
    }
    if (text.textCase != kCaseNormal) {
        for (TextSpan& span : spans) {
            span.text = utf8_upper_or_lower(span.text, text.textCase == kCaseUpper);
        }
    }
    return spans;
}

std::string display_text(const TextData& text) {
    std::string out;
    for (const TextSpan& span : text_spans(text)) {
        out += span.text;
    }
    return out;
}

namespace {

/** The words, spaces, line ends, and images of the spans, measured at one font size. */
struct Token {
    enum class Kind : std::uint8_t { word, space, newline, image };
    Kind kind{Kind::word};
    std::size_t span{0};
    std::string text;
    float width{0.0F};
    float imageWidth{0.0F};
    float imageHeight{0.0F};
};

struct Shaper {
    Model& model;
    TextMeasure& measure;
    const TextData& data;
    std::vector<TextSpan> spans;
    std::vector<std::string> fonts;   // the font file of each span
    std::string baseFont;

    Shaper(Model& m, TextMeasure& t, const TextData& d) : model(m), measure(t), data(d), spans(text_spans(d)) {
        const FontInfo* font = model.font(data.font);
        baseFont = font != nullptr ? font->file : std::string();
        for (const TextSpan& span : spans) {
            const FontInfo* face = span.font.empty() ? nullptr : model.font(lower(span.font));
            fonts.push_back(face != nullptr ? face->file : baseFont);
        }
    }

    /** The width of a text at a font size: the glyphs at 0.8 of the size, the spacing in 1/1000 of the size. */
    /** The top of the glyph line box in the line of the font size (centered), and the ascent from the line top. */
    float glyph_top(const std::string& font, float size) const {
        return (size - measure.line_height(font, size * kGlyphSizeRatio)) * 0.5F;
    }
    float glyph_ascent(const std::string& font, float size) const {
        return glyph_top(font, size) + measure.ascent(font, size * kGlyphSizeRatio);
    }

    float word_width(std::size_t span, float size, const std::string& text) const {
        return measure.width(fonts[span], size * kGlyphSizeRatio, data.charSpacing / kGlyphSizeRatio, text);
    }

    std::vector<Token> tokens(float size) const {
        std::vector<Token> out;
        const float scale = data.size > 0.0F ? size / data.size : 1.0F;
        for (std::size_t s = 0; s < spans.size(); ++s) {
            const TextSpan& span = spans[s];
            if (span.is_image()) {
                Token t;
                t.kind = Token::Kind::image;
                t.span = s;
                float height = glyph_ascent(fonts[s], size);
                if (span.imageHeight == "fontHeight") {
                    height = size;
                } else if (!span.imageHeight.empty() && span.imageHeight != "fontAscent") {
                    height = std::max(0.0F, std::strtof(span.imageHeight.c_str(), nullptr)) * scale;
                }
                const Sprite& sprite = model.sprite(span.image);
                const float aspect = sprite.found && sprite.height > 0.0F ? sprite.width / sprite.height : 1.0F;
                t.imageHeight = height * span.imageScale;
                t.imageWidth = height * aspect * span.imageScale;
                t.width = t.imageWidth + (span.imageLeftPadding + span.imageRightPadding) * scale;
                out.push_back(t);
                continue;
            }
            std::size_t at = 0;
            const std::string& text = span.text;
            while (at < text.size()) {
                Token t;
                t.span = s;
                if (text[at] == '\n') {
                    t.kind = Token::Kind::newline;
                    ++at;
                } else if (text[at] == '\r') {
                    ++at;
                    continue;
                } else if (text[at] == ' ') {
                    t.kind = Token::Kind::space;
                    const std::size_t end = text.find_first_not_of(' ', at);
                    t.text = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                    at = end == std::string::npos ? text.size() : end;
                } else {
                    t.kind = Token::Kind::word;
                    const std::size_t end = text.find_first_of(" \n\r", at);
                    t.text = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                    at = end == std::string::npos ? text.size() : end;
                }
                t.width = t.kind == Token::Kind::newline ? 0.0F : word_width(s, size, t.text);
                out.push_back(std::move(t));
            }
        }
        return out;
    }

    /** The lines at a font size, wrapped at a width (no wrap when the width is 0 or less). */
    TextLayout lay(float size, float wrapWidth) const {
        TextLayout layout;
        layout.fontSize = size;
        layout.glyphSize = size * kGlyphSizeRatio;
        const float baseHeight = size;
        const float ascent = glyph_ascent(baseFont, size);
        const bool wrap = data.wrap != kNoWrap && wrapWidth > 0.0F;
        const float scale = data.size > 0.0F ? size / data.size : 1.0F;
        TextLayout::Line line;
        std::vector<Token> pending;   // the spaces after the last word of the line
        bool wrapped = false;         // the line started at a wrap (its first spaces do not count)
        float width = 0.0F;
        // The character spacing (1/1000 em) is after each character: a token measures it inside; between two tokens
        // of a line it comes once more.
        const float gap = data.charSpacing / 1000.0F * size;
        const auto append = [&](const Token& t) {
            if (!line.pieces.empty()) {
                width += gap;
                line.pieces.back().width += gap;
            }
            if (!line.pieces.empty() && t.kind != Token::Kind::image && line.pieces.back().image.empty() &&
                line.pieces.back().fontFile == fonts[t.span] && same_color(line.pieces.back().color, spans[t.span].color) &&
                line.pieces.back().link == spans[t.span].link) {
                line.pieces.back().text += t.text;
                line.pieces.back().width += t.width;
            } else {
                TextPiece p;
                p.x = width;
                p.width = t.width;
                p.fontFile = fonts[t.span];
                p.color = spans[t.span].color;
                p.link = spans[t.span].link;
                p.linkAction = spans[t.span].linkAction;
                p.linkData = spans[t.span].linkData;
                if (t.kind == Token::Kind::image) {
                    const TextSpan& span = spans[t.span];
                    p.image = span.image;
                    p.imageLeft = span.imageLeftPadding * scale;
                    p.imageWidth = t.imageWidth;
                    p.imageHeight = t.imageHeight;
                    // The vertical place from the top of the line box: baseline (default), top, center, bottom.
                    float top = ascent - t.imageHeight;
                    if (span.imageVAlign == "top") top = 0.0F;
                    else if (span.imageVAlign == "center") top = (size - t.imageHeight) * 0.5F;
                    else if (span.imageVAlign == "bottom") top = size - t.imageHeight;
                    p.imageTop = top + span.imageYOffset * scale;
                } else {
                    p.text = t.text;
                }
                line.pieces.push_back(std::move(p));
            }
            width += t.width;
        };
        const auto finish = [&]() {
            pending.clear();
            line.width = width;
            float top = 0.0F;
            float bottom = baseHeight;
            for (const TextPiece& p : line.pieces) {
                if (!p.image.empty()) {
                    top = std::min(top, p.imageTop);
                    bottom = std::max(bottom, p.imageTop + p.imageHeight);
                }
            }
            // A tall image makes the line taller: the text and the images move down by the part above the line top.
            line.textTop = -top + glyph_top(baseFont, size);
            for (TextPiece& p : line.pieces) {
                p.imageTop -= top;
            }
            line.height = bottom - top;
            for (const TextPiece& p : line.pieces) line.text += p.text;
            layout.lines.push_back(std::move(line));
            line = {};
            width = 0.0F;
        };
        for (const Token& t : tokens(size)) {
            if (t.kind == Token::Kind::newline) {
                finish();
                wrapped = false;
                continue;
            }
            if (t.kind == Token::Kind::space) {
                if (!(wrapped && line.pieces.empty())) {
                    pending.push_back(t);
                }
                continue;
            }
            float spaces = 0.0F;
            for (const Token& p : pending) spaces += p.width + gap;
            if (wrap && !line.pieces.empty() && width + spaces + gap + t.width > wrapWidth) {
                finish();
                wrapped = true;
                spaces = 0.0F;
            } else {
                for (const Token& p : pending) append(p);
            }
            pending.clear();
            if (wrap && line.pieces.empty() && t.kind == Token::Kind::word && t.width > wrapWidth && data.wrap == kWrapSplitWords) {
                // The fork WrapSplitWords: a word that does not fit in one line breaks at a character.
                std::string part;
                for (std::size_t i = 0; i < t.text.size();) {
                    const std::size_t length = utf8_length(static_cast<unsigned char>(t.text[i]));
                    const std::string ch = t.text.substr(i, length);
                    if (!part.empty() && word_width(t.span, size, part + ch) > wrapWidth) {
                        Token piece = t;
                        piece.text = part;
                        piece.width = word_width(t.span, size, part);
                        append(piece);
                        finish();
                        wrapped = true;
                        part.clear();
                    }
                    part += ch;
                    i += length;
                }
                Token piece = t;
                piece.text = part;
                piece.width = word_width(t.span, size, part);
                append(piece);
                continue;
            }
            append(t);
        }
        for (const Token& p : pending) append(p);   // spaces at the end of the text count (no wrap after them)
        finish();
        float total = 0.0F;
        for (std::size_t i = 0; i < layout.lines.size(); ++i) {
            layout.width = std::max(layout.width, layout.lines[i].width);
            total += layout.lines[i].height + (i > 0 ? data.lineSpacing : 0.0F);
        }
        layout.height = total;
        return layout;
    }
};

/** Cuts a line so that it and "..." fit in a width; the "..." takes the font of the last piece. */
void ellipsis_line(TextLayout::Line& line, float width, float scaleX, const Shaper& shaper, float size) {
    const std::string dots = "...";
    while (!line.pieces.empty()) {
        TextPiece& last = line.pieces.back();
        if (!last.image.empty()) {
            line.pieces.pop_back();
            continue;
        }
        const float dotsWidth = shaper.measure.width(last.fontFile, size * kGlyphSizeRatio, shaper.data.charSpacing / kGlyphSizeRatio, dots);
        std::string cut = last.text;
        float cutWidth = shaper.measure.width(last.fontFile, size * kGlyphSizeRatio, shaper.data.charSpacing / kGlyphSizeRatio, cut);
        while (!cut.empty() && (last.x + cutWidth + dotsWidth) * scaleX > width) {
            cut.pop_back();
            while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back();
            cutWidth = shaper.measure.width(last.fontFile, size * kGlyphSizeRatio, shaper.data.charSpacing / kGlyphSizeRatio, cut);
        }
        if (cut.empty() && line.pieces.size() > 1 && (last.x + dotsWidth) * scaleX > width) {
            line.pieces.pop_back();
            continue;
        }
        while (!cut.empty() && cut.back() == ' ') cut.pop_back();
        last.text = cut + dots;
        last.width = shaper.measure.width(last.fontFile, size * kGlyphSizeRatio, shaper.data.charSpacing / kGlyphSizeRatio, last.text);
        break;
    }
    line.width = 0.0F;
    line.text.clear();
    for (const TextPiece& p : line.pieces) {
        line.width = std::max(line.width, p.x + p.width);
        line.text += p.text;
    }
}

} // namespace

TextLayout layout_text(Model& model, Id id, TextMeasure& measure) {
    TextLayout layout;
    const Element* e = model.element(id);
    if (e == nullptr || !e->text) {
        return layout;
    }
    const TextData& text = *e->text;
    const Shaper shaper(model, measure, text);
    const Edges r = model.rect(id);
    const float width = r.right - r.left;
    const float height = r.bottom - r.top;

    layout = shaper.lay(text.size, width);
    if (text.shrink == kShrinkUniform && (layout.width > width || layout.height > height) && layout.width > 0.0F) {
        float k = std::min(width / std::max(layout.width, 1e-6F), height / std::max(layout.height, 1e-6F));
        k = std::max(k, text.minShrink > 0.0F ? text.minShrink : 0.0F);
        layout = shaper.lay(text.size * std::min(k, 1.0F), width);
    } else if (text.shrink == kShrinkWidthOnly && layout.width > width && layout.width > 0.0F) {
        float k = width / layout.width;
        k = std::max(k, text.minShrink > 0.0F ? text.minShrink : 0.0F);
        layout.scaleX = std::min(k, 1.0F);
    }
    if (text.overflow == kOverflowEllipsis) {
        // The lines that fit in the height (one at least); the last one that stays and each line that is too wide
        // end with "...".
        std::size_t keep = layout.lines.size();
        float y = 0.0F;
        for (std::size_t i = 0; i < layout.lines.size(); ++i) {
            y += layout.lines[i].height + (i > 0 ? text.lineSpacing : 0.0F);
            if (y > height + 0.01F && i > 0) {
                keep = i;
                break;
            }
        }
        const bool cut = keep < layout.lines.size();
        layout.lines.resize(keep);
        for (std::size_t i = 0; i < layout.lines.size(); ++i) {
            TextLayout::Line& line = layout.lines[i];
            if (line.width * layout.scaleX > width + 0.01F || (cut && i + 1 == layout.lines.size())) {
                ellipsis_line(line, width, layout.scaleX, shaper, layout.fontSize);
            }
        }
        layout.width = 0.0F;
        layout.height = 0.0F;
        for (std::size_t i = 0; i < layout.lines.size(); ++i) {
            layout.width = std::max(layout.width, layout.lines[i].width);
            layout.height += layout.lines[i].height + (i > 0 ? text.lineSpacing : 0.0F);
        }
    }
    for (TextLayout::Line& line : layout.lines) {
        for (TextPiece& p : line.pieces) {
            p.x *= layout.scaleX;
            p.width *= layout.scaleX;
            p.imageLeft *= layout.scaleX;
            p.imageWidth *= layout.scaleX;
        }
        line.width *= layout.scaleX;
    }
    layout.width *= layout.scaleX;
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
        y += line.height + text.lineSpacing;
    }
    return layout;
}

Vec2 text_natural_size(Model& model, Id id, TextMeasure& measure) {
    const Element* e = model.element(id);
    if (e == nullptr || !e->text) {
        return {};
    }
    const TextData& text = *e->text;
    const Shaper shaper(model, measure, text);
    const Edges r = model.rect(id);
    return {shaper.lay(text.size, 0.0F).width, shaper.lay(text.size, r.right - r.left).height};
}

} // namespace swc::ui
