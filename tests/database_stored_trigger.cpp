#include "game/database/database.h"
#include "game/database/stored_trigger.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void word(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(value >> (24 - 8 * i));
    }
}

std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> bytes(267);
    word(bytes, 0, 5);
    word(bytes, 8, 32);
    word(bytes, 20, 0x501);
    word(bytes, 24, 0x40000);
    word(bytes, 28, 256);
    for (const auto at : {32u, 128u, 256u}) {
        bytes[at] = 0xc3;
        bytes[at + 1] = 0x27;
        word(bytes, at + 2, 1);
    }
    bytes[39] = 1;
    bytes[50] = 1;
    word(bytes, 51, 0x51);
    word(bytes, 67, 7);
    word(bytes, 71, 128);
    word(bytes, 75, 0x51);
    bytes[135] = 1;
    word(bytes, 136, 256);
    word(bytes, 140, 100);
    word(bytes, 262, 0xfedcba98);
    bytes[266] = 8;
    return bytes;
}

auto decode(const std::vector<std::uint8_t>& bytes) {
    return game_assets::parse_stored_trigger(bytes, game_assets::parse_database_index(bytes), 0x51,
                                             100);
}
}

int main(int argc, char** argv) {
    using namespace game_assets;
    const auto saved = fixture();
    auto bytes = saved;
    const auto value = decode(bytes);
    require(value && value->offset == 256 && value->action_list_id == 0xfedcba98 &&
                value->event_type == 8,
            "Stored trigger fields decoded incorrectly");
    require(bytes == saved, "Stored trigger decoder changed bytes");
    const auto index = parse_database_index(saved);
    require(!parse_stored_trigger(saved, index, 0x42, 100) &&
                !parse_stored_trigger(saved, index, 0x51, 101),
            "Wrong class or missing ID decoded as a trigger");
    for (std::size_t size = 0; size < saved.size(); ++size) {
        bytes.assign(saved.begin(), saved.begin() + size);
        require(!decode(bytes) && !parse_stored_trigger(bytes, index, 0x51, 100),
                "Truncated trigger decoded, including with previous index");
    }
    for (const auto& [at, data] : {std::pair{0u, 4u},
                                   {20u, 0x500u},
                                   {24u, 0x20000u},
                                   {28u, 128u},
                                   {258u, 0u},
                                   {258u, 2u},
                                   {136u, 0u},
                                   {136u, 24u},
                                   {136u, 0xfffffff0u}}) {
        bytes = saved;
        word(bytes, at, data);
        require(!decode(bytes), "Unsupported trigger header/version/mark decoded");
        if (at != 136) {
            require(!parse_stored_trigger(bytes, index, 0x51, 100),
                    "Unsupported bytes decoded with previous index");
        }
    }
    for (unsigned event = 0; event < 256; ++event) {
        bytes = saved;
        bytes[266] = static_cast<std::uint8_t>(event);
        require(decode(bytes)->event_type == event, "Raw event type was lost or guessed");
    }
    for (const auto id : {0u, 1u, 0x80000000u, 0xffffffffu}) {
        bytes = saved;
        word(bytes, 262, id);
        require(decode(bytes)->action_list_id == id, "Raw action-list ID was narrowed");
    }
    auto unusable = index;
    unusable.records.push_back(unusable.records.front());
    require(!parse_stored_trigger(saved, unusable, 0x51, 100), "Duplicate trigger decoded");
    for (unsigned condition = 0; condition < 6; ++condition) {
        unusable = index;
        if (condition == 0) {
            unusable.available = false;
        }
        if (condition == 1) {
            unusable.truncated = true;
        }
        if (condition == 2) {
            unusable.skipped_nodes = 1;
        }
        if (condition == 3) {
            unusable.unsupported_indexes = 1;
        }
        if (condition == 4) {
            unusable.records.front().offset = 0;
        }
        if (condition == 5) {
            unusable.records.front().offset = 0xffffffff;
        }
        require(!parse_stored_trigger(saved, unusable, 0x51, 100),
                "Unusable index or overflowing mark decoded");
    }
    for (int argument = 1; argument < argc; ++argument) {
        const auto path = std::filesystem::path(argv[argument]);
        const auto database = Database::load(path);
        const auto stored = parse_database_index(database.bytes());
        require(stored.available && !stored.truncated && !stored.skipped_nodes &&
                    !stored.duplicate_keys && !stored.unsupported_indexes,
                "Installed database index is incomplete");
        std::size_t triggers = 0;
        for (const auto& record : stored.records) {
            if (record.class_id != 0x51) {
                continue;
            }
            const auto trigger = parse_stored_trigger(database.bytes(), stored, 0x51, record.id);
            require(trigger && trigger->offset == record.offset,
                    "Installed indexed trigger failed decoding");
            ++triggers;
        }
        std::cout << path.string() << ": " << stored.records.size() << " records, " << triggers
                  << " triggers\n";
    }
}
