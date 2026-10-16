#include "internal.h"
#include <algorithm>

namespace devtools::recipes::detail {
const Build& build_info(std::string_view name) {
    static constexpr Build builds[]{
        {"cd_10012", "eae65c27f0c026530e363ebc3483a150c002e831e88ae6e04d3c2f3d4da859ce", 12},
        {"cd_10019", "d1cfec26c69c03a41d3a0e81783a11b7892bc6846f6326b306f99f2ed534e1d6", 9},
        {"cd_10020", "0f8b654cb78f6ae66d62bb173fae79614255ad46cdc25827bbaa98d7d0d79899", 9},
        {"dvd_20000", "1c7385b15bc11f6ff46b5a30ca383441df96be5be1c1dec31eae650f2243a2ad", 9}};
    for (const auto& build : builds) {
        if (build.name == name) {
            return build;
        }
    }
    throw std::runtime_error("unsupported native build");
}

void validate_timers(const Json& timers) {
    std::set<unsigned> seen;
    for (const auto& timer : timers.array(256)) {
        timer.fields({"timer_id", "duration_ms", "periodic"});
        unsigned id = timer.at("timer_id").integer(255);
        timer.at("duration_ms").integer();
        timer.at("periodic").boolean();
        require(seen.insert(id).second, "duplicate timer identity");
    }
}

void validate_recipe(const Json& recipe, const Database& base) {
    recipe.fields({"format", "version", "source_sha256", "baseline_sha256",
                   "baseline_variable_count", "baseline_next_id", "numeric_variables",
                   "enabled_states", "auxiliary", "omitted", "native_build", "executable_sha256",
                   "timers"});
    require(recipe.at("format").string() == "xfiles-numeric-save-recipe" &&
                recipe.at("version").integer() == 1,
            "unsupported recipe format");
    for (auto key : {"source_sha256", "baseline_sha256"}) {
        const auto& hash = recipe.at(key).string();
        require(hash.size() == 64 && std::all_of(hash.begin(), hash.end(),
                                                 [](char c) {
                                                     return (c >= '0' && c <= '9') ||
                                                            (c >= 'a' && c <= 'f');
                                                 }),
                "invalid SHA-256");
    }
    require(recipe.at("baseline_sha256").string() == digest(base.data),
            "baseline SHA-256 mismatch");
    const auto& build = build_info(recipe.at("native_build").string());
    require(recipe.at("executable_sha256").string() == build.hash,
            "recipe executable SHA-256 mismatch");
    validate_timers(recipe.at("timers"));
    auto variables = validate_baseline(base);
    require(recipe.at("baseline_variable_count").integer(4096) == variables.size() &&
                recipe.at("baseline_next_id").integer() == base.number(16),
            "baseline metadata mismatch");
    for (auto key : {"numeric_variables", "enabled_states"}) {
        unsigned cls = std::string_view(key) == "numeric_variables" ? 0x53 : 0x46,
                 maximum = cls == 0x53 ? UINT32_MAX : 1;
        bool first = true;
        unsigned previous = 0;
        for (const auto& row : recipe.at(key).array(4096)) {
            const auto& delta = row.array(3);
            require(delta.size() == 3, "invalid delta");
            unsigned id = delta[0].integer(), before = delta[1].integer(maximum),
                     after = delta[2].integer(maximum);
            require((first || id > previous) && base.records.contains({cls, id}) && before != after,
                    "duplicate, unordered or unknown delta");
            unsigned old;
            if (cls == 0x53) {
                auto v = base.variable(id);
                require((v.metadata.back() & 0x7f) != 3, "string variable delta is forbidden");
                old = v.value;
            } else {
                old = base.part(base.records.at({cls, id}) + 10, 1)[0];
            }
            require(old == before, "delta expected value mismatch");
            first = false;
            previous = id;
        }
    }
    const auto& items = recipe.at("auxiliary").array(6);
    require(items.size() == 6, "missing runtime records");
    auto ids = dynamic_ids(base);
    for (unsigned i = 0; i < items.size(); ++i) {
        unsigned cls = auxiliary[i];
        const auto& item = items[i];
        item.fields({"class_id", "flags", "scalars", "words"});
        require(item.at("class_id").integer() == cls, "unexpected runtime class order");
        const auto& flags = item.at("flags").array(2);
        require(flags.size() == 2, "invalid record flags");
        for (const auto& flag : flags) {
            flag.integer(255);
        }
        require((flags[0].integer() & 0x80) != 0, "invalid runtime record header");
        const auto& scalars = item.at("scalars").array(scene_scalars.size());
        require(scalars.size() == (cls == 0x57 ? scene_scalars.size() : 0),
                "invalid scalar layout");
        for (unsigned j = 0; j < scalars.size(); ++j) {
            auto [offset, width] = scene_scalars[j];
            unsigned value = scalars[j].integer(width == 4 ? UINT32_MAX : (1u << (width * 8)) - 1);
            for (auto [reference, target] : scene_references) {
                if (reference == offset) {
                    require(value == ids.at(target), "invalid remapped runtime reference");
                }
            }
        }
        for (const auto& word : item.at("words").array(4096)) {
            word.integer();
        }
    }
    const auto& omitted = recipe.at("omitted");
    omitted.fields({"photo_records", "string_variables", "packed_fields"});
    omitted.at("photo_records").integer(1024);
    bool first = true;
    unsigned previous = 0;
    for (const auto& value : omitted.at("string_variables").array(4096)) {
        unsigned id = value.integer();
        require(variables.contains(id) && (base.variable(id).metadata.back() & 0x7f) == 3 &&
                    (first || id > previous),
                "invalid omitted string identities");
        first = false;
        previous = id;
    }
    first = true;
    for (const auto& value : omitted.at("packed_fields").array(4)) {
        unsigned offset = value.integer();
        require(std::find(opaque_fields.begin(), opaque_fields.end(), offset) !=
                        opaque_fields.end() &&
                    (first || offset > previous),
                "invalid omitted packed fields");
        first = false;
        previous = offset;
    }
}
}

namespace devtools::recipes {
std::string extract_data(const Bytes& baseline, const Bytes& saved, std::string_view build_name) {
    using namespace detail;
    const auto& build = build_info(build_name);
    Database base(baseline, true), source(saved);
    validate_baseline(base);
    require(source.classes.size() == 9 ||
                (source.classes.size() == 10 && source.classes.contains(0x59)),
            "save must contain the six supported runtime classes");
    for (unsigned cls : auxiliary) {
        require(source.classes.contains(cls), "missing runtime class");
    }
    Json::Array numeric, enabled, strings;
    for (unsigned cls : {0x46, 0x53}) {
        require(base.identities(cls) == source.identities(cls),
                "save and baseline identities differ");
    }
    for (unsigned id : base.identities(0x53)) {
        auto old = base.variable(id), next = source.variable(id);
        require(old.name == next.name && old.metadata == next.metadata,
                "variable metadata differs");
        if ((old.metadata.back() & 0x7f) == 3) {
            require(base.records.contains({0x50, old.value}) &&
                        source.records.contains({0x50, next.value}),
                    "missing string reference");
            auto a = base.blob(base.records.at({0x50, old.value}) + 6),
                 b = source.blob(source.records.at({0x50, next.value}) + 6);
            if (old.value != next.value || !std::equal(a.begin(), a.end(), b.begin(), b.end())) {
                strings.emplace_back(id);
            }
        } else if (old.value != next.value) {
            numeric.emplace_back(Json::Array{Json(id), Json(old.value), Json(next.value)});
        }
    }
    for (unsigned id : base.identities(0x46)) {
        unsigned a = base.records.at({0x46, id}), b = source.records.at({0x46, id});
        require(base.number(a + 6) == source.number(b + 6) &&
                    base.part(a + 11, 1)[0] == source.part(b + 11, 1)[0],
                "enabled metadata differs");
        unsigned old = base.part(a + 10, 1)[0], next = source.part(b + 10, 1)[0];
        require(old <= 1 && next <= 1, "invalid enabled state");
        if (old != next) {
            enabled.emplace_back(Json::Array{Json(id), Json(old), Json(next)});
        }
    }
    auto ids = dynamic_ids(base);
    std::map<unsigned, unsigned> source_ids;
    for (unsigned cls : auxiliary) {
        auto set = source.identities(cls);
        require(set.size() == 1, "runtime class must have one record");
        source_ids[cls] = *set.begin();
    }
    Json::Array items, omitted, timers;
    for (unsigned cls : auxiliary) {
        unsigned at = source.records.at({cls, source_ids.at(cls)});
        Json::Array scalars;
        if (cls == 0x57) {
            for (auto [offset, width] : scene_scalars) {
                unsigned value = source.number(at + offset, width);
                for (auto [reference, target] : scene_references) {
                    if (reference == offset) {
                        require(value == source_ids.at(target), "invalid runtime reference");
                        value = ids.at(target);
                    }
                }
                scalars.emplace_back(value);
            }
            for (unsigned offset : opaque_fields) {
                if (!source.blob(at + offset).empty()) {
                    omitted.emplace_back(offset);
                }
            }
            auto blob = source.blob(at + 98, 256 * build.timer_width);
            require(blob.size() % build.timer_width == 0, "incomplete timer record");
            unsigned pointer = source.number(at + 98);
            for (unsigned i = 0; i < blob.size(); i += build.timer_width) {
                unsigned id = source.number(pointer + i, 4, true),
                         duration = source.number(pointer + i + 4, 4, true),
                         periodic = source.number(pointer + i + 8, build.timer_width - 8, true);
                require(periodic <= 1, "invalid saved timer periodic flag");
                timers.emplace_back(Json::Object{{"timer_id", Json(id)},
                                                 {"duration_ms", Json(duration)},
                                                 {"periodic", Json(periodic != 0)}});
            }
            validate_timers(Json(timers));
        }
        items.emplace_back(Json::Object{{"class_id", Json(cls)},
                                        {"flags", Json::Array{Json(unsigned(source.data[at])),
                                                              Json(unsigned(source.data[at + 1]))}},
                                        {"scalars", Json(std::move(scalars))},
                                        {"words", source.words(at + (cls == 0x57 ? 74 : 6))}});
    }
    Json result(Json::Object{
        {"format", Json("xfiles-numeric-save-recipe")},
        {"version", Json(1u)},
        {"source_sha256", Json(digest(saved))},
        {"baseline_sha256", Json(digest(baseline))},
        {"native_build", Json(std::string(build.name))},
        {"executable_sha256", Json(std::string(build.hash))},
        {"baseline_variable_count", Json(static_cast<unsigned>(base.identities(0x53).size()))},
        {"baseline_next_id", Json(base.number(16))},
        {"numeric_variables", Json(std::move(numeric))},
        {"enabled_states", Json(std::move(enabled))},
        {"auxiliary", Json(std::move(items))},
        {"timers", Json(std::move(timers))},
        {"omitted", Json::Object{{"photo_records",
                                  Json(static_cast<unsigned>(source.identities(0x59).size()))},
                                 {"string_variables", Json(std::move(strings))},
                                 {"packed_fields", Json(std::move(omitted))}}}});
    validate_recipe(result, base);
    return encode(result);
}
}
