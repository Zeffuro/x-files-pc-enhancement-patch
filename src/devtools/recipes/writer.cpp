#include "internal.h"

namespace devtools::recipes {
Bytes studio_visit_data(Bytes saved) {
    using namespace detail;
    const Database source(saved);
    for (unsigned cls : auxiliary) {
        require(source.identities(cls).size() == 1, "missing or ambiguous saved scene");
    }
    const auto scene = source.records.at({0x57, *source.identities(0x57).begin()});
    for (auto [offset, cls] : scene_references) {
        require(source.number(scene + offset) == *source.identities(cls).begin(),
                "invalid saved scene reference");
    }
    require(!source.number(scene + 122) && !source.number(scene + 126),
            "Close the game's device screens before visiting HyperBole Studios.");
    for (unsigned offset = 66; offset <= 98; offset += 8) {
        source.blob(scene + offset);
    }
    constexpr unsigned destination[]{0xa656, 0x106de, 0x12d7b, 0x12d84};
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            saved[scene + 6 + i * 4 + byte] =
                static_cast<std::uint8_t>(destination[i] >> (24 - byte * 8));
        }
    }
    // A different panorama must not inherit the source movie's saved seek position.
    std::fill(saved.begin() + scene + 113, saved.begin() + scene + 122, std::uint8_t{0});
    return saved;
}

Bytes reconstruct_data(const Bytes& baseline, std::string_view text, std::string_view build_name) {
    using namespace detail;
    const auto& build = build_info(build_name);
    Database base(baseline, true);
    Json recipe = decode(text);
    validate_recipe(recipe, base);
    require(recipe.at("native_build").string() == build_name,
            "recipe and executable builds differ");
    Bytes output = baseline;
    auto put = [&](std::size_t at, unsigned value, unsigned width = 4, bool little = false) {
        require(at <= output.size() && width <= output.size() - at, "output write out of bounds");
        for (unsigned i = 0; i < width; ++i) {
            output[at + i] = static_cast<std::uint8_t>(value >> (8 * (little ? i : width - i - 1)));
        }
    };
    auto allocate = [&](std::size_t size) {
        auto at = (output.size() + 7) & ~std::size_t(7);
        require(at <= max_database && size <= max_database - at, "output exceeds size limit");
        output.resize(at + size);
        return static_cast<unsigned>(at);
    };
    for (const auto& row : recipe.at("numeric_variables").array(4096)) {
        auto& a = row.array(3);
        put(base.records.at({0x53, a[0].integer()}) + 14, a[2].integer());
    }
    for (const auto& row : recipe.at("enabled_states").array(4096)) {
        auto& a = row.array(3);
        put(base.records.at({0x46, a[0].integer()}) + 10, a[2].integer(), 1);
    }
    auto classes = base.classes;
    auto ids = dynamic_ids(base);
    for (const auto& item : recipe.at("auxiliary").array(6)) {
        unsigned cls = item.at("class_id").integer(), at = allocate(cls == 0x57 ? 132 : 14);
        const auto& flags = item.at("flags").array(2);
        put(at, flags[0].integer(), 1);
        put(at + 1, flags[1].integer(), 1);
        put(at + 2, 1);
        const auto& scalars = item.at("scalars").array(scene_scalars.size());
        for (unsigned i = 0; i < scalars.size(); ++i) {
            put(at + scene_scalars[i].first, scalars[i].integer(), scene_scalars[i].second);
        }
        const auto& words = item.at("words").array(4096);
        if (!words.empty()) {
            unsigned blob = allocate(words.size() * 4);
            for (unsigned i = 0; i < words.size(); ++i) {
                put(blob + i * 4, words[i].integer(), 4, true);
            }
            unsigned descriptor = at + (cls == 0x57 ? 74 : 6);
            put(descriptor, blob);
            put(descriptor + 4, static_cast<unsigned>(words.size() * 4));
        }
        const auto& timers = recipe.at("timers").array(256);
        if (cls == 0x57 && !timers.empty()) {
            unsigned blob = allocate(timers.size() * build.timer_width);
            for (unsigned i = 0; i < timers.size(); ++i) {
                unsigned entry = blob + i * build.timer_width;
                put(entry, timers[i].at("timer_id").integer(), 4, true);
                put(entry + 4, timers[i].at("duration_ms").integer(), 4, true);
                put(entry + 8, timers[i].at("periodic").boolean() ? 1 : 0, build.timer_width - 8,
                    true);
            }
            put(at + 98, blob);
            put(at + 102, static_cast<unsigned>(timers.size() * build.timer_width));
        }
        unsigned index = allocate(16);
        put(index, 0xc0, 1);
        put(index + 2, 1);
        put(index + 6, 1, 2);
        put(index + 8, at);
        put(index + 12, ids.at(cls));
        classes[cls] = {1, index, 0x49442020};
    }
    auto ordered = base.master_order;
    ordered.insert(ordered.end(), auxiliary.begin(), auxiliary.end());
    unsigned master_list = allocate(8 + 4 * ordered.size());
    put(master_list, 0xc0, 1);
    put(master_list + 2, 1);
    put(master_list + 6, static_cast<unsigned>(ordered.size()), 2);
    for (unsigned i = 0; i < ordered.size(); ++i) {
        put(master_list + 8 + 4 * i, ordered[i]);
    }
    unsigned extent = 8 + 43 * (static_cast<unsigned>(classes.size()) + 1);
    // The native reader prefetches seven bytes beyond the final directory row.
    unsigned directory = allocate((extent + 14) & ~7u);
    put(directory, 0xc0, 1);
    put(directory + 2, 1);
    put(directory + 6, static_cast<unsigned>(classes.size() + 1), 2);
    unsigned master = directory + 8;
    put(master + 8, static_cast<unsigned>(classes.size()), 2);
    put(master + 10, 1, 1);
    put(master + 11, 1);
    put(master + 19, master_list);
    put(master + 27, 7);
    put(master + 35, 1);
    put(master + 39, 0x6e756c6c);
    unsigned i = 1;
    for (const auto& [cls, entry] : classes) {
        unsigned at = directory + 8 + 43 * i++;
        put(at + 10, 1, 1);
        put(at + 11, cls);
        put(at + 15, entry.count);
        put(at + 27, 7);
        put(at + 31, entry.index);
        put(at + 35, cls);
        put(at + 39, entry.tag);
    }
    put(8, directory);
    put(12, static_cast<unsigned>(classes.size() + 1));
    put(16, base.number(16) + static_cast<unsigned>(ids.size()));
    Database check(output);
    return output;
}
}
