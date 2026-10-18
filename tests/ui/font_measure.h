#pragma once

#include <string>
#include <string_view>

#include "SwcUi/Draw.h"

/** The text metrics of the offline tests: the converted font files, read with stb_truetype (font_measure.cpp). */
class FontMeasure final : public swc::ui::TextMeasure {
public:
    float width(const std::string& fontFile, float size, float letterSpacing, std::string_view text) override;
    float line_height(const std::string& fontFile, float size) override;
    float ascent(const std::string& fontFile, float size) override;
};
