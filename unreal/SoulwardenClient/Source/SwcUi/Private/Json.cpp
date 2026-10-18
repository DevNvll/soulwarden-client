#include "SwcUi/Json.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace swc::ui::json {
namespace {

const Value& null_value() {
    static const Value value;
    return value;
}

const std::string& empty_text() {
    static const std::string text;
    return text;
}

class Reader {
public:
    explicit Reader(std::string_view text) : m_text(text) {}

    bool document(Value& out) {
        skip();
        if (!value(out, 0)) {
            return false;
        }
        skip();
        if (m_at != m_text.size()) {
            return fail("text after the value");
        }
        return true;
    }

    std::string error;

private:
    static constexpr int kMaxDepth = 512;

    bool fail(const char* what) {
        error = std::string(what) + " at byte " + std::to_string(m_at);
        return false;
    }

    void skip() {
        while (m_at < m_text.size() && (m_text[m_at] == ' ' || m_text[m_at] == '\n' || m_text[m_at] == '\r' || m_text[m_at] == '\t')) {
            ++m_at;
        }
    }

    bool literal(std::string_view word) {
        if (m_text.substr(m_at, word.size()) != word) {
            return fail("a word that is not JSON");
        }
        m_at += word.size();
        return true;
    }

    bool value(Value& out, int depth) {
        if (depth > kMaxDepth) {
            return fail("the document is too deep");
        }
        if (m_at >= m_text.size()) {
            return fail("the text ends before a value");
        }
        const char c = m_text[m_at];
        if (c == '{') {
            return object(out, depth);
        }
        if (c == '[') {
            return array(out, depth);
        }
        if (c == '"') {
            std::string text;
            if (!string(text)) {
                return false;
            }
            out = Value::make_string(std::move(text));
            return true;
        }
        if (c == 't') {
            out = Value::make_bool(true);
            return literal("true");
        }
        if (c == 'f') {
            out = Value::make_bool(false);
            return literal("false");
        }
        if (c == 'n') {
            out = Value();
            return literal("null");
        }
        return number(out);
    }

    bool number(Value& out) {
        const std::size_t start = m_at;
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
                ++m_at;
            } else {
                break;
            }
        }
        if (start == m_at) {
            return fail("a character that is not JSON");
        }
        const std::string text(m_text.substr(start, m_at - start));
        char* end = nullptr;
        const double parsed = std::strtod(text.c_str(), &end);
        if (end != text.c_str() + text.size()) {
            return fail("a number that is not JSON");
        }
        out = Value::make_number(parsed);
        return true;
    }

    static void append_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool hex4(unsigned& code) {
        if (m_at + 4 > m_text.size()) {
            return fail("a short escape");
        }
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_text[m_at++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                return fail("an escape that is not hexadecimal");
            }
        }
        return true;
    }

    bool string(std::string& out) {
        ++m_at;   // the quote
        while (m_at < m_text.size()) {
            const char c = m_text[m_at++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_at >= m_text.size()) {
                break;
            }
            const char e = m_text[m_at++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code = 0;
                if (!hex4(code)) {
                    return false;
                }
                if (code >= 0xD800 && code < 0xDC00 && m_at + 6 <= m_text.size() && m_text[m_at] == '\\' && m_text[m_at + 1] == 'u') {
                    m_at += 2;
                    unsigned low = 0;
                    if (!hex4(low)) {
                        return false;
                    }
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                append_utf8(out, code);
                break;
            }
            default:
                return fail("an escape that is not JSON");
            }
        }
        return fail("the text ends in a string");
    }

    bool array(Value& out, int depth) {
        ++m_at;
        out = Value::make_array();
        skip();
        if (m_at < m_text.size() && m_text[m_at] == ']') {
            ++m_at;
            return true;
        }
        while (true) {
            Value element;
            skip();
            if (!value(element, depth + 1)) {
                return false;
            }
            out.elements_mut().push_back(std::move(element));
            skip();
            if (m_at >= m_text.size()) {
                return fail("the text ends in an array");
            }
            const char c = m_text[m_at++];
            if (c == ']') {
                return true;
            }
            if (c != ',') {
                return fail("an array without a comma");
            }
        }
    }

    bool object(Value& out, int depth) {
        ++m_at;
        out = Value::make_object();
        skip();
        if (m_at < m_text.size() && m_text[m_at] == '}') {
            ++m_at;
            return true;
        }
        while (true) {
            skip();
            if (m_at >= m_text.size() || m_text[m_at] != '"') {
                return fail("an object key that is not a string");
            }
            std::string key;
            if (!string(key)) {
                return false;
            }
            skip();
            if (m_at >= m_text.size() || m_text[m_at] != ':') {
                return fail("an object without a colon");
            }
            ++m_at;
            skip();
            Value member;
            if (!value(member, depth + 1)) {
                return false;
            }
            out.members_mut().emplace_back(std::move(key), std::move(member));
            skip();
            if (m_at >= m_text.size()) {
                return fail("the text ends in an object");
            }
            const char c = m_text[m_at++];
            if (c == '}') {
                return true;
            }
            if (c != ',') {
                return fail("an object without a comma");
            }
        }
    }

    std::string_view m_text;
    std::size_t m_at{0};
};

} // namespace

Value Value::make_bool(bool value) {
    Value out;
    out.m_type = Type::boolean;
    out.m_bool = value;
    return out;
}

Value Value::make_number(double value) {
    Value out;
    out.m_type = Type::number;
    out.m_number = value;
    return out;
}

Value Value::make_string(std::string value) {
    Value out;
    out.m_type = Type::string;
    out.m_text = std::move(value);
    return out;
}

Value Value::make_array() {
    Value out;
    out.m_type = Type::array;
    return out;
}

Value Value::make_object() {
    Value out;
    out.m_type = Type::object;
    return out;
}

double Value::number(double fallback) const {
    if (m_type == Type::number) {
        return m_number;
    }
    if (m_type == Type::boolean) {
        return m_bool ? 1.0 : 0.0;
    }
    return fallback;
}

bool Value::boolean(bool fallback) const {
    if (m_type == Type::boolean) {
        return m_bool;
    }
    if (m_type == Type::number) {
        return m_number != 0.0;
    }
    return fallback;
}

const std::string& Value::text() const {
    return m_type == Type::string ? m_text : empty_text();
}

const Value& Value::operator[](std::string_view key) const {
    if (m_type != Type::object) {
        return null_value();
    }
    for (const Member& member : m_members) {
        if (member.first == key) {
            return member.second;
        }
    }
    return null_value();
}

const Value& Value::operator[](std::size_t index) const {
    if (m_type != Type::array || index >= m_elements.size()) {
        return null_value();
    }
    return m_elements[index];
}

std::size_t Value::size() const {
    if (m_type == Type::array) {
        return m_elements.size();
    }
    if (m_type == Type::object) {
        return m_members.size();
    }
    return 0;
}

const std::vector<Value>& Value::elements() const {
    static const std::vector<Value> none;
    return m_type == Type::array ? m_elements : none;
}

const std::vector<Value::Member>& Value::members() const {
    static const std::vector<Member> none;
    return m_type == Type::object ? m_members : none;
}

std::vector<Value>& Value::elements_mut() {
    return m_elements;
}

std::vector<Value::Member>& Value::members_mut() {
    return m_members;
}

void Value::set_text(std::string value) {
    m_type = Type::string;
    m_text = std::move(value);
}

bool parse(std::string_view text, Value& out, std::string* error) {
    Reader reader(text);
    if (!reader.document(out)) {
        if (error != nullptr) {
            *error = reader.error;
        }
        return false;
    }
    return true;
}

bool parse_file(const std::string& path, Value& out, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "no file " + path;
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parse(buffer.str(), out, error);
}

std::string quote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escape[8];
                std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += escape;
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

} // namespace swc::ui::json
