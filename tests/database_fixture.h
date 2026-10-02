#pragma once
#include "devtools/database/native.h"
#include "game/layouts/database/cache.h"
#include "game/layouts/database/story.h"
#include "game/layouts/database/variable.h"
#include <windows.h>
#include <array>
#include <cstring>
#include <stdexcept>

struct DatabaseFixture {
    std::byte* image;
    native_game::Profile profile;

    explicit DatabaseFixture(const native_game::Profile& value) : profile(value) {
        image = static_cast<std::byte*>(
            VirtualAlloc(nullptr, 0x300000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!image) {
            throw std::runtime_error("Fixture allocation failed");
        }
        put(profile.database_manager, address(0x10000));
        put(0x1001c, address(0x11000));
        put(0x100f0, address(0x12000));
        native_game::DatabaseNode node{};
        node.object.vtable = address(profile.database_object_node);
        node.count = 7;
        put(0x11000, node);
        const std::array<unsigned, 7> classes{0x27, 0x2f, 0x39, 0x41, 0x52, 0x53, 0x54};
        for (unsigned index = 0; index < classes.size(); ++index) {
            const unsigned offset = 0x14000 + index * 0x200;
            put(0x11030 + index * 12,
                native_game::DatabaseObjectEntry{(index + 1) * 10, address(offset), 0});
            native_game::PersistentObject object{};
            object.id = (index + 1) * 10;
            object.vtable = address(0x20000 + index * 0x20);
            object.references = 2;
            put(offset, object);
            put(0x20008 + index * 0x20, address(0x21000 + index * 0x10));
            put(0x21000 + index * 0x10,
                std::array<std::uint8_t, 6>{0xb8, static_cast<std::uint8_t>(classes[index]), 0, 0,
                                            0, 0xc3});
        }
        put(0x1409c, 20u);
        put(0x140a0, 50u);
        put(0x14228, native_game::StoryString{0, address(0x24000), 32, 0});
        put(0x14238, native_game::StoryString{0, address(0x24040), 32, 0});
        put(0x24000, std::array<char, 32>{'C', 'o', 'o', 'k'});
        put(0x24040, std::array<char, 32>{'E', 'n', 't', 'r', 'y'});
        put(0x14430, std::int32_t{-10});
        put(0x14434, 15);
        put(0x14438, 100);
        put(0x1443c, 90);
        put(0x14444, 20u);
        put(0x14a38, std::int32_t{1});
        put(0x14a41, std::uint8_t{1});
        put(0x14c28, 20u);
        put(0x14c2c, 99u);
        put(0x14c30, 40u);
        node.count = 1;
        put(0x12000, node);
        native_game::Variable variable{};
        variable.vtable = address(0x200a0);
        variable.id = 60;
        variable.raw_value = 5;
        variable.type_flags = 1;
        put(0x16000, variable);
        put(0x12030, native_game::DatabaseObjectEntry{60, address(0x16000), 0});
    }

    ~DatabaseFixture() {
        VirtualFree(image, 0, MEM_RELEASE);
    }

    DatabaseFixture(const DatabaseFixture&) = delete;
    DatabaseFixture& operator=(const DatabaseFixture&) = delete;

    unsigned address(unsigned offset) const {
        return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(image) + offset);
    }

    template <class T> void put(unsigned offset, const T& value) {
        std::memcpy(image + offset, &value, sizeof(value));
    }

    devtools::NativeDatabaseSnapshot snapshot() const {
        return devtools::inspect_database(image, profile);
    }
};
