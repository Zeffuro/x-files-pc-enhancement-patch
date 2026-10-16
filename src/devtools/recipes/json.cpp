#include "internal.h"
#include <charconv>
#include <limits>

namespace devtools::recipes::detail {
const Json& Json::at(const char* key) const {
    const auto* object = std::get_if<Object>(&value);
    if (!object || !object->contains(key)) {
        throw std::runtime_error("missing recipe field");
    }
    return object->at(key);
}

const Json::Array& Json::array(std::size_t maximum) const {
    const auto* result = std::get_if<Array>(&value);
    if (!result || result->size() > maximum) {
        throw std::runtime_error("invalid or oversized array");
    }
    return *result;
}

std::uint32_t Json::integer(std::uint32_t maximum) const {
    const auto* result = std::get_if<std::uint64_t>(&value);
    if (!result || *result > maximum) {
        throw std::runtime_error("invalid unsigned integer");
    }
    return static_cast<std::uint32_t>(*result);
}

const std::string& Json::string() const {
    const auto* result = std::get_if<std::string>(&value);
    if (!result) {
        throw std::runtime_error("invalid string");
    }
    return *result;
}

bool Json::boolean() const {
    const auto* result = std::get_if<bool>(&value);
    if (!result) {
        throw std::runtime_error("invalid boolean");
    }
    return *result;
}

void Json::fields(std::initializer_list<const char*> names) const {
    const auto* object = std::get_if<Object>(&value);
    if (!object || object->size() != names.size()) {
        throw std::runtime_error("unexpected object fields");
    }
    for (auto key : names) {
        require(object->contains(key), "unexpected object fields");
    }
}

namespace {
class Parser {
    std::string_view text;
    std::size_t at = 0;

    void space() {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == '\r' || text[at] == '\n' || text[at] == '\t')) {
            ++at;
        }
    }

    char take() {
        require(at < text.size(), "incomplete JSON");
        return text[at++];
    }

    bool consume(char c) {
        space();
        if (at < text.size() && text[at] == c) {
            ++at;
            return true;
        }
        return false;
    }

    unsigned hex() {
        unsigned result = 0;
        for (unsigned i = 0; i < 4; ++i) {
            char c = take();
            unsigned v = c >= '0' && c <= '9'   ? unsigned(c - '0')
                         : c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10)
                         : c >= 'A' && c <= 'F' ? unsigned(c - 'A' + 10)
                                                : 16;
            require(v < 16, "invalid Unicode escape");
            result = result * 16 + v;
        }
        return result;
    }

    static void utf8(std::string& out, unsigned c) {
        if (c < 128) {
            out += char(c);
        } else if (c < 2048) {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 63));
        } else if (c < 65536) {
            out += char(0xe0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 63));
            out += char(0x80 | (c & 63));
        } else {
            out += char(0xf0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 63));
            out += char(0x80 | ((c >> 6) & 63));
            out += char(0x80 | (c & 63));
        }
    }

    std::string string() {
        require(take() == '"', "expected JSON string");
        std::string out;
        while (true) {
            unsigned c = static_cast<unsigned char>(take());
            if (c == '"') {
                return out;
            }
            require(c >= 32, "invalid JSON string");
            if (c == '\\') {
                char escape = take();
                if (escape == 'u') {
                    c = hex();
                    if (c >= 0xd800 && c <= 0xdbff) {
                        require(take() == '\\' && take() == 'u', "invalid surrogate");
                        unsigned low = hex();
                        require(low >= 0xdc00 && low <= 0xdfff, "invalid surrogate");
                        c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                    } else {
                        require(c < 0xdc00 || c > 0xdfff, "invalid surrogate");
                    }
                    utf8(out, c);
                } else {
                    switch (escape) {
                        case '"':
                        case '\\':
                        case '/':
                            out += escape;
                            break;
                        case 'b':
                            out += '\b';
                            break;
                        case 'f':
                            out += '\f';
                            break;
                        case 'n':
                            out += '\n';
                            break;
                        case 'r':
                            out += '\r';
                            break;
                        case 't':
                            out += '\t';
                            break;
                        default:
                            require(false, "invalid JSON escape");
                    }
                }
            } else if (c >= 128) {
                unsigned count = c >= 0xc2 && c <= 0xdf   ? 1
                                 : c >= 0xe0 && c <= 0xef ? 2
                                 : c >= 0xf0 && c <= 0xf4 ? 3
                                                          : 0;
                require(count != 0, "invalid UTF-8");
                unsigned code = c & ((1u << (6 - count)) - 1);
                for (unsigned i = 0; i < count; ++i) {
                    unsigned next = static_cast<unsigned char>(take());
                    require((next & 0xc0) == 0x80, "invalid UTF-8");
                    code = (code << 6) | (next & 63);
                }
                require(code >= (count == 1   ? 128u
                                 : count == 2 ? 2048u
                                              : 65536u) &&
                            code <= 0x10ffff && (code < 0xd800 || code > 0xdfff),
                        "invalid UTF-8");
                utf8(out, code);
            } else {
                out += char(c);
            }
        }
    }

    Json value(unsigned depth) {
        space();
        require(at < text.size(), "missing JSON value");
        char c = text[at];
        if (c == '{' || c == '[') {
            require(depth < 16, "recipe nesting exceeds limit");
            ++at;
            if (c == '[') {
                Json::Array result;
                if (consume(']')) {
                    return result;
                }
                do {
                    result.push_back(value(depth + 1));
                } while (consume(','));
                require(consume(']'), "invalid JSON array");
                return result;
            }
            Json::Object result;
            if (consume('}')) {
                return result;
            }
            do {
                space();
                auto key = string();
                require(consume(':'), "missing JSON colon");
                require(!result.contains(key), "duplicate JSON key");
                result.emplace(std::move(key), value(depth + 1));
            } while (consume(','));
            require(consume('}'), "invalid JSON object");
            return result;
        }
        if (c == '"') {
            return string();
        }
        for (auto token :
             {std::string_view("true"), std::string_view("false"), std::string_view("null")}) {
            if (text.substr(at, token.size()) == token) {
                at += token.size();
                if (token == "null") {
                    return {};
                }
                return Json(token == "true");
            }
        }
        require(c >= '0' && c <= '9', "non-integer JSON number");
        auto start = at++;
        if (c != '0') {
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
                ++at;
            }
        }
        std::uint64_t number = 0;
        auto result = std::from_chars(text.data() + start, text.data() + at, number);
        require(result.ec == std::errc{}, "integer overflow");
        return number;
    }

public:
    explicit Parser(std::string_view input) : text(input) {}

    Json parse() {
        auto result = value(0);
        space();
        require(at == text.size(), "invalid trailing JSON");
        return result;
    }
};

void render(std::string& out, const Json& json) {
    if (std::holds_alternative<std::monostate>(json.value)) {
        out += "null";
    } else if (auto b = std::get_if<bool>(&json.value)) {
        out += *b ? "true" : "false";
    } else if (auto n = std::get_if<std::uint64_t>(&json.value)) {
        out += std::to_string(*n);
    } else if (auto s = std::get_if<std::string>(&json.value)) {
        out += '"';
        constexpr char hex[] = "0123456789abcdef";
        for (unsigned char c : *s) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += char(c);
            } else if (c < 32) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else {
                out += char(c);
            }
        }
        out += '"';
    } else if (auto a = std::get_if<Json::Array>(&json.value)) {
        out += '[';
        bool first = true;
        for (auto& item : *a) {
            if (!first) {
                out += ',';
            }
            first = false;
            render(out, item);
        }
        out += ']';
    } else {
        out += '{';
        bool first = true;
        for (auto& [key, item] : std::get<Json::Object>(json.value)) {
            if (!first) {
                out += ',';
            }
            first = false;
            render(out, Json(key));
            out += ':';
            render(out, item);
        }
        out += '}';
    }
}
}

Json decode(std::string_view text) {
    require(text.size() <= max_recipe, "oversized recipe input");
    return Parser(text).parse();
}

std::string encode(const Json& value) {
    std::string out;
    render(out, value);
    out += '\n';
    require(out.size() <= max_recipe, "oversized recipe output");
    return out;
}
}
