#include "saves/conversion.h"
#include "saves/compatibility.h"
#include "dispatch.h"

#include <windows.h>
#include <iostream>

namespace {
using namespace saves::database;

unsigned block(Bytes& bytes, unsigned size) {
    const auto offset = allocate(bytes, size);
    put(bytes, offset, 0xc0, 1);
    put(bytes, std::uint64_t(offset) + 2, 1);
    return offset;
}

void blob(Bytes& bytes, unsigned record, unsigned field, const std::string& text) {
    const auto offset = allocate(bytes, text.size() + 1);
    std::copy(text.begin(), text.end(), bytes.begin() + offset);
    put(bytes, record + field, offset);
    put(bytes, record + field + 4, static_cast<unsigned>(text.size() + 1));
}

unsigned index(Bytes& bytes, const Class& item) {
    std::vector<unsigned> nodes;
    auto record = item.records.begin();
    while (record != item.records.end()) {
        const auto node = block(bytes, 264);
        put(bytes, node, 0x80, 1);
        unsigned count = 0;
        while (count < 32 && record != item.records.end()) {
            put(bytes, node + 8 + count * 8, record->second);
            put(bytes, node + 12 + count * 8, record->first);
            ++record;
            ++count;
        }
        put(bytes, node + 6, count, 2);
        nodes.push_back(node);
    }
    while (nodes.size() > 1) {
        std::vector<unsigned> next;
        for (std::size_t i = 0; i < nodes.size(); i += 32) {
            const auto node = block(bytes, 136);
            put(bytes, node, 0, 1);
            const auto count = (std::min)(std::size_t(32), nodes.size() - i);
            put(bytes, node + 6, static_cast<unsigned>(count), 2);
            for (unsigned j = 0; j < count; ++j) {
                put(bytes, node + 8 + j * 4, nodes[i + j]);
            }
            next.push_back(node);
        }
        nodes = std::move(next);
    }
    put(bytes, nodes.front(), get(bytes, nodes.front(), 1) | 0x40, 1);
    return nodes.front();
}

Bytes fixture(bool current, bool colliding_photos = false) {
    Bytes bytes(1024);
    std::map<unsigned, Class> classes;
    const auto add = [&](unsigned cls, unsigned id, unsigned size) {
        const auto record = block(bytes, size);
        classes[cls].records.emplace(id, record);
        return record;
    };
    for (unsigned i = 0; i < 3115; ++i) {
        const auto record = add(0x46, 0x10000 + i, 12);
        put(bytes, record + 6, 1000 + i);
        put(bytes, record + 10, current ? 0 : 1, 1);
        put(bytes, record + 11, 0x48, 1);
    }
    for (unsigned i = 1; i <= 902; ++i) {
        const auto record = add(0x53, i, 24);
        blob(bytes, record, 6, "variable" + std::to_string(i));
        put(bytes, record + 14, current ? 0 : i);
        put(bytes, record + 18, 0x12b);
        put(bytes, record + 22, 0x27, 1);
        put(bytes, record + 23, i == 10 ? 3 : 1, 1);
        if (i == 10) {
            put(bytes, record + 14, current ? 0xf9c : 0x14d1);
        }
    }
    for (unsigned i = 0; i < 11; ++i) {
        const auto record = add(0x50, i == 0 ? (current ? 0xf9c : 0x14d1) : 0x6000 + i, 14);
        blob(bytes, record, 6, "string" + std::to_string(i));
    }
    if (current) {
        for (unsigned i = 0; i < 17; ++i) {
            const auto record = add(0x53, 0x14cb + i, 24);
            blob(bytes, record, 6, std::string(saves::conversion_detail::added_names[i]));
            put(bytes, record + 14, i == 11 ? 9 : 0);
            put(bytes, record + 18, 0x12b);
            put(bytes, record + 22, 0x27, 1);
            put(bytes, record + 23, saves::conversion_detail::added_types[i], 1);
        }
    } else {
        const auto state = add(0x57, 0x14cb, 132);
        unsigned i = 0;
        for (const auto cls : {0x56, 0x58, 0x5a, 0x5b, 0x5c}) {
            add(cls, 0x14cc + i, 14);
            put(bytes, state + 22 + i * 4, 0x14cc + i);
            ++i;
        }
        const auto photo = [&](unsigned id) {
            const auto record = add(0x59, id, 32);
            blob(bytes, record, 6, "photo" + std::to_string(id));
            put(bytes, record + 14, 0x14d2);
            put(bytes, record + 18, id);
            put(bytes, record + 22, 0x014000f0);
            put(bytes, record + 26, 0x025801e0);
            put(bytes, record + 30, 1, 1);
        };
        if (colliding_photos) {
            for (unsigned id = 0x14d2; id <= 0x14d4; ++id) {
                photo(id);
            }
        }
        photo(0x1500);
    }
    put(bytes, 0, 5);
    put(bytes, 8, 64);
    put(bytes, 12, static_cast<unsigned>(classes.size() + 1));
    put(bytes, 16, 0x20000);
    put(bytes, 20, 0x501);
    put(bytes, 66, 1);
    put(bytes, 70, static_cast<unsigned>(classes.size() + 1), 2);
    put(bytes, 80, static_cast<unsigned>(classes.size()), 2);
    put(bytes, 82, 1, 1);
    put(bytes, 83, 1);
    unsigned entry = 122;
    for (auto& [id, item] : classes) {
        put(bytes, entry, 1);
        put(bytes, entry + 4, id);
        put(bytes, entry + 8, static_cast<unsigned>(item.records.size()));
        put(bytes, entry + 24, index(bytes, item));
        put(bytes, entry + 28, id);
        entry += 43;
    }
    return bytes;
}

void write(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void verify(const Bytes& original, const Bytes& baseline, const Bytes& converted) {
    const Container before(original), initial(baseline), after(converted);
    test::require(after.classes.size() == before.classes.size(), "Saved classes changed");
    test::require(after.classes.at(0x53).records.size() == 919, "Missing migration variables");
    const auto state = after.classes.at(0x57).records.begin();
    test::require(state->first >= 0x20000, "Colliding scene ID was not relocated");
    unsigned field = 22;
    for (const auto cls : {0x56, 0x58, 0x5a, 0x5b, 0x5c}) {
        test::require(get(converted, state->second + field) ==
                          after.classes.at(cls).records.begin()->first,
                      "Scene collection reference was not relocated");
        field += 4;
    }
    for (const auto& [id, record] : before.classes.at(0x46).records) {
        test::require(saves::conversion_detail::equal(slice(original, record, 12),
                                                      slice(converted, record, 12)),
                      "Existing gameplay state changed");
    }
    for (const auto& [id, record] : before.classes.at(0x53).records) {
        test::require(after.classes.at(0x53).records.at(id) == record,
                      "Existing variable moved unexpectedly");
        if ((get(original, record + 23, 1) & 0x7f) != 3) {
            test::require(saves::conversion_detail::equal(slice(original, record, 24),
                                                          slice(converted, record, 24)),
                          "Existing variable value changed");
        } else {
            const auto old_string = before.classes.at(0x50).records.at(get(original, record + 14));
            const auto new_string = after.classes.at(0x50).records.at(get(converted, record + 14));
            test::require(old_string == new_string, "String contents were replaced");
        }
    }
    const auto& photos_before = before.classes.at(0x59).records;
    const auto& photos_after = after.classes.at(0x59).records;
    test::require(photos_before.size() == photos_after.size(), "Saved photos were lost");
    auto photo_after = photos_after.begin();
    for (const auto& [id, record] : photos_before) {
        test::require(photo_after->second == record, "Saved photo order changed");
        test::require(
            saves::conversion_detail::equal(slice(original, record, 32),
                                            slice(converted, record, 32)) &&
                saves::conversion_detail::equal(before.blob(record + 6), after.blob(record + 6)),
            "Saved photo data changed");
        test::require(photos_before.begin()->first > 0x14db ? photo_after->first == id
                                                            : photo_after->first >= 0x20000,
                      "Saved photo ID relocation was incomplete");
        ++photo_after;
    }
    for (unsigned id = 0x14cb; id <= 0x14db; ++id) {
        const auto record = after.classes.at(0x53).records.at(id);
        const auto expected = initial.classes.at(0x53).records.at(id);
        test::require(
            saves::conversion_detail::equal(after.blob(record + 6), initial.blob(expected + 6)) &&
                saves::conversion_detail::equal(slice(converted, record + 10, 14),
                                                slice(baseline, expected + 10, 14)),
            "New variable differs from the current initial database");
    }
    std::set<unsigned> allowed{16, 17, 18, 19};
    const auto allow = [&](unsigned offset, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            allowed.insert(offset + i);
        }
    };
    for (const auto& [id, item] : before.classes) {
        if (id != 0x46) {
            allow(item.directory + 8, 4);
            allow(item.directory + 24, 4);
        }
    }
    allow(state->second + 22, 20);
    allow(before.classes.at(0x53).records.at(10) + 14, 4);
    for (unsigned i = 0; i < original.size(); ++i) {
        test::require(original[i] == converted[i] || allowed.contains(i),
                      "Conversion changed bytes outside the explicit migration fields");
    }
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 4) {
            saves::write_converted_copy(argv[1], argv[2], argv[3]);
            const auto error = saves::load_compatibility_error(argv[3], true);
            test::require(!error, error ? error : "");
            std::cout << "Experimental copy created. Original preserved. Progression unverified.\n";
            return 0;
        }
        const auto original = fixture(false), baseline = fixture(true);
        const auto converted = saves::convert_old_save(original, baseline);
        verify(original, baseline, converted);
        const auto with_photos = fixture(false, true);
        verify(with_photos, baseline, saves::convert_old_save(with_photos, baseline));
        const auto rejected = [&](Bytes source, Bytes initial, const char* reason) {
            bool failed = false;
            try {
                saves::convert_old_save(std::move(source), std::move(initial));
            } catch (const std::exception&) {
                failed = true;
            }
            test::require(failed, reason);
        };
        rejected(converted, baseline, "Current save was converted a second time");
        auto damaged = original;
        damaged.resize(100);
        rejected(damaged, baseline, "Truncated source accepted");
        damaged = original;
        put(damaged, 8, 0xfffffff0);
        rejected(damaged, baseline, "Overflow directory accepted");
        const Container parsed(original), initial(baseline);
        const auto variable = parsed.classes.at(0x53).records.begin()->second;
        damaged = original;
        put(damaged, variable + 18, 777);
        rejected(damaged, baseline, "Variable ownership mismatch accepted");
        damaged = original;
        put(damaged, variable + 6, 0xfffffff0);
        rejected(damaged, baseline, "Invalid name pointer accepted");
        damaged = original;
        const auto state = parsed.classes.at(0x57).records.begin()->second;
        put(damaged, state + 22, 0x123456);
        rejected(damaged, baseline, "Unknown scene reference accepted");
        damaged = original;
        put(damaged, state + 66, 0xfffffff0);
        put(damaged, state + 70, 4);
        rejected(damaged, baseline, "Invalid scene blob accepted");
        damaged = baseline;
        put(damaged, initial.classes.at(0x53).records.at(0x14d6) + 14, 10);
        rejected(original, damaged, "Unknown target database version accepted");
        damaged = baseline;
        put(damaged, initial.classes.at(0x53).records.at(0x14d6) + 23, 2, 1);
        rejected(original, damaged, "Non-integer target database version accepted");
        damaged = original;
        put(damaged, parsed.classes.at(0x46).records.begin()->second + 6, 123);
        rejected(damaged, baseline, "Different game data accepted");
        damaged = with_photos;
        const Container photos(with_photos);
        put(damaged, photos.classes.at(0x59).records.begin()->second + 6, 0xfffffff0);
        rejected(damaged, baseline, "Invalid photo payload accepted");
        const auto directory = std::filesystem::temp_directory_path() /
                               (L"xfiles-conversion-" + std::to_wstring(GetCurrentProcessId()) +
                                L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(directory);
        const auto source = directory / "source.x", target = directory / "Xfiles.gam";
        const auto copy = directory / "copy.x";
        write(source, original);
        write(target, baseline);
        saves::write_converted_copy(source, target, copy);
        test::require(read(source) == original && read(target) == baseline,
                      "Conversion overwrote an input file");
        test::require(read(copy) == converted && !saves::load_compatibility_error(copy, true),
                      "Written copy did not pass native compatibility preflight");
        bool refused = false;
        try {
            saves::write_converted_copy(source, target, source);
        } catch (const std::exception&) {
            refused = true;
        }
        test::require(refused && read(source) == original, "Original destination was overwritten");
        refused = false;
        try {
            saves::write_converted_copy(source, target, copy);
        } catch (const std::exception&) {
            refused = true;
        }
        test::require(refused && read(copy) == converted, "Existing copy was overwritten");
        for (const auto& path : {source, target, copy}) {
            std::filesystem::remove(path);
        }
        std::filesystem::remove(directory);
        std::cout << "Save conversion preserves state, relocates IDs and refuses unsafe inputs.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
