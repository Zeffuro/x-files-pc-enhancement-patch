#include "devtools/recipes/internal.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <functional>

namespace {
using namespace devtools::recipes;
using namespace devtools::recipes::detail;

void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void rejects(const std::function<void()>& operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "malformed input accepted");
}

struct Fixture {
    Bytes data = Bytes(32);
    std::map<std::pair<unsigned, unsigned>, unsigned> records;

    unsigned allocate(unsigned size) {
        auto at = static_cast<unsigned>((data.size() + 7) & ~std::size_t(7));
        data.resize(at + size);
        return at;
    }

    void put(unsigned at, unsigned value, unsigned width = 4, bool little = false) {
        for (unsigned i = 0; i < width; ++i) {
            data.at(at + i) =
                static_cast<std::uint8_t>(value >> (8 * (little ? i : width - i - 1)));
        }
    }

    unsigned record(unsigned cls, unsigned id, unsigned size) {
        unsigned at = allocate(size);
        put(at, 0x80, 1);
        put(at + 2, 1);
        records[{cls, id}] = at;
        return at;
    }

    void blob(unsigned descriptor, const Bytes& value) {
        if (value.empty()) {
            return;
        }
        unsigned at = allocate(static_cast<unsigned>(value.size()));
        std::copy(value.begin(), value.end(), data.begin() + at);
        put(descriptor, at);
        put(descriptor + 4, static_cast<unsigned>(value.size()));
    }

    void text(unsigned descriptor, std::string_view value) {
        Bytes bytes(value.begin(), value.end());
        bytes.push_back(0);
        blob(descriptor, bytes);
    }

    Bytes finish(unsigned highwater) {
        std::map<unsigned, std::vector<std::pair<unsigned, unsigned>>> groups;
        for (auto [key, at] : records) {
            groups[key.first].emplace_back(key.second, at);
        }
        std::map<unsigned, Class> classes;
        for (auto& [cls, entries] : groups) {
            unsigned index = allocate(8 + 8 * static_cast<unsigned>(entries.size()));
            put(index, 0xc0, 1);
            put(index + 2, 1);
            put(index + 6, static_cast<unsigned>(entries.size()), 2);
            for (unsigned i = 0; i < entries.size(); ++i) {
                put(index + 8 + 8 * i, entries[i].second);
                put(index + 12 + 8 * i, entries[i].first);
            }
            classes[cls] = {static_cast<unsigned>(entries.size()), index, 0x6e756c6c};
        }
        unsigned count = static_cast<unsigned>(classes.size()), list = allocate(8 + 4 * count);
        put(list, 0xc0, 1);
        put(list + 2, 1);
        put(list + 6, count, 2);
        unsigned i = 0;
        for (auto [cls, c] : classes) {
            (void)c;
            put(list + 8 + 4 * i++, cls);
        }
        unsigned directory = allocate(8 + 43 * (count + 1) + 7);
        put(directory, 0xc0, 1);
        put(directory + 2, 1);
        put(directory + 6, count + 1, 2);
        unsigned master = directory + 8;
        put(master + 8, count, 2);
        put(master + 10, 1, 1);
        put(master + 11, 1);
        put(master + 19, list);
        put(master + 27, 7);
        put(master + 35, 1);
        put(master + 39, 0x6e756c6c);
        i = 1;
        for (auto [cls, c] : classes) {
            unsigned at = directory + 8 + 43 * i++;
            put(at + 10, 1, 1);
            put(at + 11, cls);
            put(at + 15, c.count);
            put(at + 27, 7);
            put(at + 31, c.index);
            put(at + 35, cls);
            put(at + 39, c.tag);
        }
        for (auto [offset, value] :
             std::initializer_list<std::pair<unsigned, unsigned>>{{0, 5},
                                                                  {8, directory},
                                                                  {12, count + 1},
                                                                  {16, highwater},
                                                                  {20, 0x501},
                                                                  {24, 0x40000},
                                                                  {28, 256}}) {
            put(offset, value);
        }
        return data;
    }
};

Bytes fixture(bool saved = false, std::string_view build = "dvd_20000", unsigned variables = 2) {
    Fixture f;
    unsigned at = f.record(0x46, 20, 12);
    f.put(at + 6, 300);
    f.put(at + 10, saved ? 1 : 0, 1);
    for (unsigned i = 0; i < variables; ++i) {
        at = f.record(0x53, 10 + i, 24);
        f.text(at + 6, "variable" + std::to_string(i));
        f.put(at + 14, i == 1 ? (saved ? 41 : 40) : (saved ? UINT32_MAX : 3));
        f.put(at + 23, i == 1 ? 0x83 : 0x81, 1);
    }
    at = f.record(0x50, saved ? 41 : 40, 14);
    f.text(at + 6, saved ? "SAVED_SECRET_STRING" : "AUTHORED_BASELINE_STRING");
    if (saved) {
        std::map<unsigned, unsigned> ids;
        for (unsigned i = 0; i < id_order.size(); ++i) {
            ids[id_order[i]] = 150 + i;
        }
        for (unsigned cls : auxiliary) {
            at = f.record(cls, ids.at(cls), cls == 0x57 ? 132 : 14);
            if (cls == 0x57) {
                f.put(at + 18, 765);
                for (auto [offset, target] : scene_references) {
                    f.put(at + offset, ids.at(target));
                }
                f.blob(at + 74, {0xd2, 4, 0, 0, 0x2e, 0x16, 0, 0});
                f.text(at + 66, "OPAQUE_SECRET");
                Bytes timer(build_info(build).timer_width);
                timer[0] = 7;
                timer[4] = 5;
                timer[5] = 0xe1;
                f.blob(at + 98, timer);
            } else {
                f.blob(at + 6, {1, 2, 3, 4, 5, 6, 7, 8});
            }
        }
        at = f.record(0x59, 190, 14);
        f.text(at + 6, "SAVED_PHOTO_SECRET");
    }
    return f.finish(saved ? 200 : 100);
}

Json& field(Json& value, const char* key) {
    return std::get<Json::Object>(value.value).at(key);
}

Json::Array& array(Json& value) {
    return std::get<Json::Array>(value.value);
}

void suite() {
    check(digest({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty hash");
    Bytes abc{'a', 'b', 'c'};
    check(digest(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "abc hash");
    const auto base = fixture(), saved = fixture(true);
    rejects([&] { studio_visit_data(base); });
    rejects([&] { studio_visit_data({}); });
    const auto text = extract_data(base, saved, "dvd_20000");
    for (auto secret : {"SAVED_SECRET", "OPAQUE_SECRET", "SAVED_PHOTO", "AUTHORED_BASELINE"}) {
        check(text.find(secret) == std::string::npos, "recipe contains payload");
    }
    for (auto build : {"cd_10012", "cd_10019", "cd_10020", "dvd_20000"}) {
        const auto original = fixture(true, build);
        Database original_db(original);
        const auto scene = original_db.records.at({0x57, 150});
        auto positioned = original;
        std::fill(positioned.begin() + scene + 113, positioned.begin() + scene + 122,
                  std::uint8_t{0xff});
        const auto studio = studio_visit_data(positioned);
        Database studio_db(studio);
        check(studio.size() == positioned.size(), "studio visit changed save extent");
        for (std::size_t i = 0; i < studio.size(); ++i) {
            if ((i >= scene + 6 && i < scene + 22) || (i >= scene + 113 && i < scene + 122)) {
                continue;
            }
            check(studio[i] == positioned[i], "studio visit changed story or auxiliary data");
        }
        check(studio_db.number(scene + 6) == 0xa656 && studio_db.number(scene + 10) == 0x106de &&
                  studio_db.number(scene + 14) == 0x12d7b &&
                  studio_db.number(scene + 18) == 0x12d84 && studio_db.number(scene + 113) == 0 &&
                  studio_db.number(scene + 117, 1) == 0 && studio_db.number(scene + 118) == 0,
              "studio destination or media reset incorrect");
        for (const auto offset : {122u, 126u}) {
            auto overlay = original;
            overlay[scene + offset + 3] = 1;
            rejects([&] { studio_visit_data(overlay); });
        }
        auto value = decode(extract_data(base, fixture(true, build), build));
        auto& timers = array(field(value, "timers"));
        timers.emplace_back(Json::Object{
            {"timer_id", Json(255u)}, {"duration_ms", Json(UINT32_MAX)}, {"periodic", Json(true)}});
        auto rebuilt = reconstruct_data(base, encode(value), build);
        Database db(rebuilt);
        check(db.variable(10).value == UINT32_MAX && db.variable(11).value == 40,
              "numeric/string reconstruction");
        check(db.number(db.records.at({0x57, 100}) + 18) == 765, "scene reconstruction");
        check(db.blob(db.records.at({0x57, 100}) + 98).size() == 2 * build_info(build).timer_width,
              "timer layout");
        check(db.number(16) == 106 && db.identities(0x53).size() == 2, "identity preservation");
        auto roundtrip = decode(extract_data(base, rebuilt, build));
        check(encode(roundtrip.at("timers")) == encode(value.at("timers")), "timer roundtrip");
        for (auto [offset, target] : scene_references) {
            check(db.number(db.records.at({0x57, 100}) + offset) ==
                      dynamic_ids(Database(base)).at(target),
                  "reference remap");
        }
        check(db.blob(db.records.at({0x57, 100}) + 66).empty(), "opaque payload retained");
        check(rebuilt.size() - db.number(8) - 8 - db.number(12) * 43 >= 7,
              "directory prefetch padding");
    }
    auto extended = fixture(false, "dvd_20000", 3);
    auto extended_saved = fixture(true, "dvd_20000", 3);
    check(Database(reconstruct_data(extended, extract_data(extended, extended_saved, "dvd_20000"),
                                    "dvd_20000"))
                  .identities(0x53)
                  .size() == 3,
          "variable counts");
    rejects([&] { extract_data(base, extended_saved, "dvd_20000"); });
    rejects([&] { extract_data(base, saved, "cd_10012"); });
    rejects([&] { reconstruct_data(base, text, "cd_10012"); });
    std::vector<std::function<void(Json&)>> mutations{
        [](Json& j) { std::get<Json::Object>(j.value)["extra"] = Json(1u); },
        [](Json& j) { field(j, "baseline_sha256") = Json(std::string(64, '0')); },
        [](Json& j) { field(j, "baseline_variable_count") = Json(true); },
        [](Json& j) { field(j, "executable_sha256") = Json(std::string(64, '0')); },
        [](Json& j) { field(j, "native_build") = Json("unknown"); },
        [](Json& j) { array(array(field(j, "numeric_variables"))[0])[1] = Json(4u); },
        [](Json& j) { array(array(field(j, "numeric_variables"))[0])[0] = Json(11u); },
        [](Json& j) {
            auto row = array(field(j, "numeric_variables"))[0];
            array(field(j, "numeric_variables")).push_back(row);
        },
        [](Json& j) { array(array(field(j, "enabled_states"))[0])[2] = Json(2u); },
        [](Json& j) { field(array(field(j, "timers"))[0], "periodic") = Json(1u); },
        [](Json& j) { field(array(field(j, "timers"))[0], "timer_id") = Json(256u); },
        [](Json& j) {
            auto timer = array(field(j, "timers"))[0];
            array(field(j, "timers")).push_back(timer);
        },
        [](Json& j) { array(field(array(field(j, "auxiliary"))[1], "scalars"))[4] = Json(999u); },
        [](Json& j) { array(field(field(j, "omitted"), "packed_fields")).push_back(Json(74u)); }};
    for (auto& mutate : mutations) {
        auto j = decode(text);
        mutate(j);
        rejects([&] { reconstruct_data(base, encode(j), "dvd_20000"); });
    }
    for (auto invalid : {"{\"x\":1,\"x\":2}", "{\"x\":1.0}", "{\"x\":NaN}", "{\"x\":-1}",
                         "{\"x\":18446744073709551616}", "\"\xc0\x80\""}) {
        rejects([&] { decode(invalid); });
    }
    rejects([] { decode(std::string(17, '[') + std::string(17, ']')); });
    Database db(saved);
    std::vector<std::tuple<unsigned, unsigned, unsigned>> patches{
        {8, 0xfffffffc, 4},
        {db.records.at({0x53, 10}) + 6, 0xfffffff0, 4},
        {db.records.at({0x53, 10}) + 23, 0x82, 1},
        {db.records.at({0x57, 150}) + 22, 999, 4},
        {db.records.at({0x56, 151}) + 10, 7, 4},
        {db.classes.at(0x53).index, 0, 1},
        {db.classes.at(0x53).index + 6, 33, 2},
        {db.classes.at(0x53).index + 8, db.classes.at(0x53).index, 4},
        {db.classes.at(0x53).index + 20, 10, 4},
        {db.classes.at(0x53).index + 16, db.records.at({0x53, 10}), 4}};
    for (auto [at, value, width] : patches) {
        Fixture f;
        f.data = saved;
        f.put(at, value, width);
        rejects([&] { extract_data(base, f.data, "dvd_20000"); });
    }
    Database baseline(base);
    for (auto [at, value, width] : std::vector<std::tuple<unsigned, unsigned, unsigned>>{
             {baseline.records.at({0x53, 11}) + 14, 999, 4},
             {baseline.records.at({0x53, 11}) + 23, 0x87, 1},
             {baseline.records.at({0x46, 20}) + 10, 2, 1},
             {baseline.records.at({0x50, 40}) + 6, 0xfffffff0, 4}}) {
        Fixture f;
        f.data = base;
        f.put(at, value, width);
        auto j = decode(text);
        field(j, "baseline_sha256") = Json(digest(f.data));
        rejects([&] { reconstruct_data(f.data, encode(j), "dvd_20000"); });
        rejects([&] { extract_data(f.data, saved, "dvd_20000"); });
    }
    auto root =
        std::filesystem::temp_directory_path() / ("xfiles-recipe-" + digest(base).substr(0, 16));
    check(std::filesystem::create_directory(root), "test output directory already exists");
    auto target = root / "new.x", input = root / "input.gam";
    write_new(input, base);
    write_new(target, saved, {input});
    rejects([&] { write_new(target, base, {input}); });
    rejects([&] { write_new(input, saved, {input}); });
    auto alias = root / "alias.gam";
    std::filesystem::create_hard_link(input, alias);
    rejects([&] { write_new(alias, saved, {input}); });
    rejects([&] { write_new(root / "." / "input.gam", saved, {input}); });
    auto failed = root / "failed.x";
    rejects([&] { write_new(failed, reconstruct(input, input, input), {input}); });
    check(!std::filesystem::exists(failed), "failed validation created output");
    check(read_file(target, max_database) == saved && read_file(input, max_database) == base,
          "exclusive output overwrote input");
    rejects([&] { identify_executable(input); });
    std::filesystem::remove(alias);
    std::filesystem::remove(target);
    std::filesystem::remove(input);
    std::filesystem::remove(root);
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "studio-file") {
            auto output = studio_visit_data(read_file(argv[2], max_database));
            write_new(argv[3], output, {argv[2]});
            return 0;
        }
        if (argc == 6 && std::string_view(argv[1]) == "reconstruct-file") {
            auto output = reconstruct(argv[2], argv[3], argv[4]);
            write_new(argv[5], output, {argv[2], argv[3], argv[4]});
            return 0;
        }
        if (argc == 6 && std::string_view(argv[1]) == "extract-file") {
            auto text = extract(argv[2], argv[3], argv[4]);
            write_new(argv[5],
                      std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
                      {argv[2], argv[3], argv[4]});
            return 0;
        }
        if (argc == 6 && std::string_view(argv[1]) == "reconstruct") {
            auto base = read_file(argv[2], max_database), text = read_file(argv[3], max_recipe);
            auto output = reconstruct_data(
                base, std::string_view(reinterpret_cast<const char*>(text.data()), text.size()),
                argv[4]);
            write_new(argv[5], output, {argv[2], argv[3]});
            return 0;
        }
        if (argc == 6 && std::string_view(argv[1]) == "extract") {
            auto text = extract_data(read_file(argv[2], max_database),
                                     read_file(argv[3], max_database), argv[4]);
            write_new(argv[5],
                      std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
                      {argv[2], argv[3]});
            return 0;
        }
        suite();
        std::cout << "Native recipe checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
