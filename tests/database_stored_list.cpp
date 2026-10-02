#include "stored_list_fixture.h"
#include "game/database/database.h"
#include "game/database/stored_list.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

auto decode(const std::vector<std::uint8_t>& bytes, std::uint32_t type = 0x42,
            std::uint32_t id = 200) {
    return game_assets::parse_stored_reference_list(bytes, game_assets::parse_database_index(bytes),
                                                    type, id);
}
}

int main(int argc, char** argv) {
    using namespace stored_list_fixture;
    const auto saved = make();
    const auto index = game_assets::parse_database_index(saved);
    const auto list = decode(saved);
    require(list && list->offset == 512 && list->resource_mark == 1024 &&
                list->target_class == 0x41 && list->resource_nodes == 1 &&
                list->decoded_resource_bytes == 28 &&
                list->ids == std::vector<std::uint32_t>{300, 0, 300, 0xffffffff, 301},
            "Action IDs/order/nulls/duplicates/unsigned values changed");
    require(decode(saved, 0x52, 201)->ids == std::vector<std::uint32_t>{400, 0, 400},
            "Trigger list decode changed");
    require(!decode(saved, 0x51, 400) && !decode(saved, 0x42, 201),
            "Wrong class or ID decoded as a list");
    for (const auto type : {0u, 0x49442020u, 0xffffffffu}) {
        auto bytes = saved;
        word(bytes, 530, type);
        require(decode(bytes)->resource_type == type, "Raw resource type was interpreted");
    }
    for (std::size_t size = 0; size < saved.size(); ++size) {
        const std::vector bytes(saved.begin(), saved.begin() + size);
        require(!decode(bytes, 0x52, 201) &&
                    !game_assets::parse_stored_reference_list(bytes, index, 0x52, 201),
                "Truncated descriptor/resource decoded, including previous index");
    }
    for (const auto& [at, value] : {std::pair{0u, 4u},
                                    {20u, 0x500u},
                                    {24u, 0x20000u},
                                    {28u, 128u},
                                    {514u, 0u},
                                    {514u, 2u},
                                    {518u, 4u},
                                    {518u, 6u},
                                    {518u, 4097u},
                                    {522u, 0u},
                                    {522u, 31u},
                                    {522u, 512u},
                                    {522u, 0xffffffffu},
                                    {526u, 6u},
                                    {534u, 2u},
                                    {540u, 1u},
                                    {1026u, 2u}}) {
        auto bytes = saved;
        word(bytes, at, value);
        require(!decode(bytes), "Invalid list header/version/count/descriptor/mark decoded");
        require(!game_assets::parse_stored_reference_list(bytes, index, 0x42, 200),
                "Invalid bytes decoded with previous index");
    }
    auto bytes = saved;
    bytes[512] &= 0x7f;
    require(!decode(bytes), "Branch descriptor treated as a class42 leaf");
    bytes = saved;
    bytes[1030] = 1;
    require(!decode(bytes), "Node count high byte accepted despite native truncation");
    bytes = saved;
    word(bytes, 518, 1);
    word(bytes, 522, 288);
    require(!decode(bytes), "Index leaf reinterpreted as an ID resource");
    for (const auto mark : {280u, 296u}) {
        bytes = saved;
        word(bytes, 518, 1);
        word(bytes, 522, mark);
        node(bytes, mark, true, {300});
        require(!game_assets::parse_stored_reference_list(bytes, index, 0x42, 200),
                "Partially overlapping index/resource spans decoded");
    }
    auto unusable = index;
    unusable.records.insert(unusable.records.begin() + 2, unusable.records[2]);
    require(!game_assets::parse_stored_reference_list(saved, unusable, 0x42, 200),
            "Duplicate list identity decoded");
    for (unsigned condition = 0; condition < 5; ++condition) {
        unusable = index;
        if (condition == 0) {
            unusable.available = false;
        } else if (condition == 1) {
            unusable.truncated = true;
        } else if (condition == 2) {
            unusable.skipped_nodes = 1;
        } else if (condition == 3) {
            unusable.unsupported_indexes = 1;
        } else {
            unusable.records[2].offset = 0xffffffff;
        }
        require(!game_assets::parse_stored_reference_list(saved, unusable, 0x42, 200),
                "Unusable index decoded");
    }
    bytes = saved;
    word(bytes, 518, 0);
    word(bytes, 522, 0);
    require(decode(bytes) && decode(bytes)->ids.empty() && decode(bytes)->resource_nodes == 0,
            "Native constructed empty list rejected");
    bytes = saved;
    node(bytes, 1024, false, {800, 900});
    node(bytes, 800, true, {301, 300});
    node(bytes, 900, true, {0xffffffff, 0, 300});
    const auto tree = decode(bytes);
    require(tree && tree->ids == std::vector<std::uint32_t>{301, 300, 0xffffffff, 0, 300} &&
                tree->resource_nodes == 3 && tree->decoded_resource_bytes == 52,
            "Native tree order/descriptor aggregate count changed");
    for (const auto bad : {0u, 31u, 1024u, 800u, 0xffffffffu}) {
        auto corrupt = bytes;
        word(corrupt, 1036, bad);
        require(!decode(corrupt), "Invalid/reused/cyclic child mark decoded");
    }
    auto corrupt = bytes;
    node(corrupt, 900, true, {1024, 300, 400});
    require(decode(corrupt)->ids[2] == 1024, "Leaf ID was followed as a child mark");
    corrupt = bytes;
    word(corrupt, 902, 2);
    require(!decode(corrupt), "Unknown child version decoded");
    corrupt = bytes;
    word(corrupt, 518, 4);
    require(!decode(corrupt), "Aggregate tree length mismatch accepted");
    bytes = saved;
    bytes.resize(20000);
    word(bytes, 522, 2048);
    for (unsigned i = 0; i < 33; ++i) {
        node(bytes, 2048 + i * 16, false, {2048 + (i + 1) * 16});
    }
    node(bytes, 2048 + 33 * 16, true, {300, 0, 300, 0xffffffff, 301});
    require(!decode(bytes), "Unbounded resource depth accepted");
    word(bytes, 518, 0);
    node(bytes, 2048, false, {4096, 8192, 12288});
    for (const auto mark : {4096u, 8192u, 12288u}) {
        std::vector<std::uint32_t> children;
        for (unsigned i = 0; i < 170; ++i) {
            const auto child = mark + 1024 + i * 8;
            node(bytes, child, true, {});
            children.push_back(child);
        }
        node(bytes, mark, false, children);
    }
    require(!decode(bytes), "Unbounded resource node count accepted");
    require(saved == make(), "Stored list decoder changed input bytes");
    for (int argument = 1; argument < argc; ++argument) {
        const auto path = std::filesystem::path(argv[argument]);
        const auto database = game_assets::Database::load(path);
        const auto stored = game_assets::parse_database_index(database.bytes());
        require(stored.available && !stored.truncated && !stored.skipped_nodes &&
                    !stored.duplicate_keys && !stored.unsupported_indexes,
                "Installed database index is incomplete");
        std::size_t lists = 0, members = 0, nodes = 0;
        for (const auto& record : stored.records) {
            if (record.class_id != 0x42 && record.class_id != 0x52) {
                continue;
            }
            const auto decoded = game_assets::parse_stored_reference_list(
                database.bytes(), stored, record.class_id, record.id);
            require(decoded && decoded->offset == record.offset, "Installed list failed decoding");
            ++lists;
            members += decoded->ids.size();
            nodes += decoded->resource_nodes;
        }
        std::cout << path.string() << ": " << stored.records.size() << " records, " << lists
                  << " lists, " << members << " IDs, " << nodes << " resource nodes\n";
    }
}
