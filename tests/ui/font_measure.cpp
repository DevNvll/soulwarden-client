// The text metrics of the offline tests: the font files of the converted content, read with stb_truetype (TrueType and
// CFF). The size is the em size in canvas pixels (the runtime gives the glyph size: 0.8 of the LyShine size). Width: the
// advances and the kerning of the font, plus the letter spacing (1/1000 em) after each character but the last. Line
// height: ascent - descent of the font (as the maximum character height of Slate).

#include "font_measure.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "vendor/stb/stb_truetype.h"

namespace {

struct Font {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok{false};
    int ascent{0};
    int descent{0};
    int lineGap{0};
};

float scale_of(Font* font, float size) {
    return stbtt_ScaleForMappingEmToPixels(&font->info, size);
}

std::map<std::string, std::unique_ptr<Font>>& fonts() {
    static std::map<std::string, std::unique_ptr<Font>> cache;
    return cache;
}

Font* font_of(const std::string& file) {
    auto& cache = fonts();
    auto found = cache.find(file);
    if (found != cache.end()) {
        return found->second->ok ? found->second.get() : nullptr;
    }
    auto font = std::make_unique<Font>();
    std::ifstream in(file, std::ios::binary);
    font->data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (!font->data.empty() && stbtt_InitFont(&font->info, font->data.data(), stbtt_GetFontOffsetForIndex(font->data.data(), 0))) {
        font->ok = true;
        stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, &font->lineGap);
    }
    Font* raw = font->ok ? font.get() : nullptr;
    cache.emplace(file, std::move(font));
    return raw;
}

std::vector<int> codepoints(std::string_view text) {
    std::vector<int> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        int cp = c;
        std::size_t length = 1;
        if (c >= 0xF0) { cp = c & 0x07; length = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; length = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; length = 2; }
        for (std::size_t k = 1; k < length && i + k < text.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        }
        out.push_back(cp);
        i += length;
    }
    return out;
}

} // namespace

float FontMeasure::width(const std::string& fontFile, float size, float letterSpacing, std::string_view text) {
    Font* font = font_of(fontFile);
    if (font == nullptr) {
        return static_cast<float>(text.size()) * size * 0.5F;
    }
    const float scale = scale_of(font, size);
    const std::vector<int> cps = codepoints(text);
    float units = 0.0F;
    for (std::size_t i = 0; i < cps.size(); ++i) {
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&font->info, cps[i], &advance, &bearing);
        units += static_cast<float>(advance);
        if (i + 1 < cps.size()) {
            units += static_cast<float>(stbtt_GetCodepointKernAdvance(&font->info, cps[i], cps[i + 1]));
        }
    }
    const float spacing = cps.size() > 1 ? letterSpacing / 1000.0F * size * static_cast<float>(cps.size() - 1) : 0.0F;
    return units * scale + spacing;
}

float FontMeasure::line_height(const std::string& fontFile, float size) {
    Font* font = font_of(fontFile);
    if (font == nullptr) {
        return size;
    }
    return static_cast<float>(font->ascent - font->descent) * scale_of(font, size);
}

float FontMeasure::ascent(const std::string& fontFile, float size) {
    Font* font = font_of(fontFile);
    if (font == nullptr) {
        return size * 0.8F;
    }
    return static_cast<float>(font->ascent) * scale_of(font, size);
}
