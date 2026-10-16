#pragma once
#include "recipe.h"
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <variant>

namespace devtools::recipes::detail {
constexpr std::size_t max_database = 64 * 1024 * 1024;
constexpr std::size_t max_recipe = 4 * 1024 * 1024;
constexpr std::array<unsigned, 3> core{0x46, 0x53, 0x50};
constexpr std::array<unsigned, 6> auxiliary{0x56, 0x57, 0x58, 0x5a, 0x5b, 0x5c};
constexpr std::array<unsigned, 6> id_order{0x57, 0x56, 0x58, 0x5a, 0x5b, 0x5c};
constexpr std::array<unsigned, 3> opaque_fields{66, 82, 90};
constexpr std::array<std::pair<unsigned, unsigned>, 28> scene_scalars{
    {{6, 4},   {10, 4},  {14, 4},  {18, 4},  {22, 4},  {26, 4},  {30, 4},
     {34, 4},  {38, 4},  {42, 4},  {46, 4},  {50, 4},  {54, 4},  {58, 4},
     {62, 4},  {106, 1}, {107, 1}, {108, 1}, {109, 1}, {110, 1}, {111, 1},
     {112, 1}, {113, 4}, {117, 1}, {118, 4}, {122, 4}, {126, 4}, {130, 2}}};
constexpr std::array<std::pair<unsigned, unsigned>, 5> scene_references{
    {{22, 0x56}, {26, 0x58}, {30, 0x5a}, {34, 0x5b}, {38, 0x5c}}};

inline void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Build {
    std::string_view name, hash;
    unsigned timer_width;
};

const Build& build_info(std::string_view name);
std::string digest(std::span<const std::uint8_t> bytes);
Bytes read_file(const std::filesystem::path& path, std::size_t limit);

struct Json {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    std::variant<std::monostate, bool, std::uint64_t, std::string, Array, Object> value;
    Json() = default;

    Json(bool v) : value(v) {}

    Json(unsigned v) : value(std::uint64_t(v)) {}

    Json(std::uint64_t v) : value(v) {}

    Json(const char* v) : value(std::string(v)) {}

    Json(std::string v) : value(std::move(v)) {}

    Json(Array v) : value(std::move(v)) {}

    Json(Object v) : value(std::move(v)) {}

    const Json& at(const char* key) const;
    const Array& array(std::size_t maximum) const;
    std::uint32_t integer(std::uint32_t maximum = UINT32_MAX) const;
    const std::string& string() const;
    bool boolean() const;
    void fields(std::initializer_list<const char*> names) const;
};

Json decode(std::string_view text);
std::string encode(const Json& value);

struct Class {
    unsigned count, index, tag;
};

struct Database {
    const Bytes& data;
    std::map<std::pair<unsigned, unsigned>, unsigned> records;
    std::map<unsigned, Class> classes;
    std::vector<unsigned> master_order;
    std::vector<std::pair<std::size_t, std::size_t>> nodes, occupied;
    explicit Database(const Bytes& bytes, bool baseline = false);
    std::span<const std::uint8_t> part(std::size_t at, std::size_t size) const;
    unsigned number(std::size_t at, unsigned width = 4, bool little = false) const;
    std::vector<std::vector<unsigned>> tree(unsigned root, unsigned width,
                                            std::set<unsigned>& seen);
    std::set<unsigned> identities(unsigned cls) const;
    std::span<const std::uint8_t> blob(unsigned at, unsigned limit = 65536) const;

    struct Variable {
        Bytes name, metadata;
        unsigned value;
    };

    Variable variable(unsigned identity) const;
    Json words(unsigned descriptor) const;
};

std::map<unsigned, unsigned> dynamic_ids(const Database& base);
std::set<unsigned> validate_baseline(const Database& base);
void validate_recipe(const Json& recipe, const Database& base);
void validate_timers(const Json& timers);
}
