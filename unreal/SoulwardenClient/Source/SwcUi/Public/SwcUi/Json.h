#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SwcUi/Export.h"

// A small JSON reader for the converted UI content (canvases, sprites, fonts, texts). The values keep the order of
// the object members. Numbers are doubles; the content writes 64-bit entity ids as texts.
namespace swc::ui::json {

class SWCUI_EXPORT Value {
public:
    enum class Type { null, boolean, number, string, array, object };
    using Member = std::pair<std::string, Value>;

    Value() = default;
    static Value make_bool(bool value);
    static Value make_number(double value);
    static Value make_string(std::string value);
    static Value make_array();
    static Value make_object();

    [[nodiscard]] Type type() const { return m_type; }
    [[nodiscard]] bool is_null() const { return m_type == Type::null; }
    [[nodiscard]] bool is_object() const { return m_type == Type::object; }
    [[nodiscard]] bool is_array() const { return m_type == Type::array; }
    [[nodiscard]] bool is_string() const { return m_type == Type::string; }
    [[nodiscard]] bool is_number() const { return m_type == Type::number; }

    [[nodiscard]] double number(double fallback = 0.0) const;
    [[nodiscard]] float real(float fallback = 0.0F) const { return static_cast<float>(number(fallback)); }
    [[nodiscard]] int integer(int fallback = 0) const { return static_cast<int>(number(fallback)); }
    [[nodiscard]] bool boolean(bool fallback = false) const;
    /** @return The text of a string value, or an empty text. */
    [[nodiscard]] const std::string& text() const;

    /** @return The member with this key, or a null value. */
    [[nodiscard]] const Value& operator[](std::string_view key) const;
    /** @return The element with this index, or a null value. */
    [[nodiscard]] const Value& operator[](std::size_t index) const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] const std::vector<Value>& elements() const;
    [[nodiscard]] const std::vector<Member>& members() const;

    std::vector<Value>& elements_mut();
    std::vector<Member>& members_mut();
    /** Sets a string value (the clone of a script property that names an entity id inside the clone). */
    void set_text(std::string value);

private:
    Type m_type{Type::null};
    bool m_bool{false};
    double m_number{0.0};
    std::string m_text;
    std::vector<Value> m_elements;
    std::vector<Member> m_members;
};

/** Reads one JSON document. @return False and the error text when the text is not JSON. */
SWCUI_EXPORT bool parse(std::string_view text, Value& out, std::string* error = nullptr);

/** Reads a file. @return False when the file is missing or not JSON. */
SWCUI_EXPORT bool parse_file(const std::string& path, Value& out, std::string* error = nullptr);

/** Writes a JSON text for a string (with quotes). */
SWCUI_EXPORT std::string quote(std::string_view text);

} // namespace swc::ui::json
