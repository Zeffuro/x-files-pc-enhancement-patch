#include "stored_asset_list_fixture.h"
#include "game/database/database.h"
#include "game/database/stored_asset_list.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

auto decode(const std::vector<std::uint8_t>& bytes) {
    return game_assets::parse_stored_asset_list(bytes, game_assets::parse_database_index(bytes),
                                                0x36, 200);
}

void dump_bytes(std::span<const std::uint8_t> bytes, std::size_t at, std::size_t count) {
    std::cout << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < count; ++i) {
        std::cout << std::setw(2) << unsigned(bytes[at + i]);
    }
    std::cout << std::dec;
}
}

int main(int argc, char** argv) {
    using namespace stored_list_fixture;
    const auto saved = stored_asset_list_fixture::make();
    const auto index = game_assets::parse_database_index(saved);
    const auto list = decode(saved);
    require(list && list->offset == 512 && list->resource_mark == 1024 &&
                list->resource_class == 0x0b && list->resource_type == 0x6e756c6c &&
                list->resource_owner == 1 && list->resource_id == 0 &&
                list->flags == std::array<std::uint8_t, 2>{0xc3, 0} &&
                list->needs_release_raw == 255 && list->duplicate_raw == 0 &&
                list->nodes.size() == 1 && list->decoded_resource_bytes == 28 &&
                list->nodes[0].offset == 1024 && list->nodes[0].extent == 28 &&
                list->nodes[0].flags == std::array<std::uint8_t, 2>{0xc3, 0x71} &&
                list->ids == std::vector<std::uint32_t>{300, 0, 300, 0xffffffff, 301},
            "Asset-reference descriptor or ordered unsigned IDs changed");
    require(!game_assets::parse_stored_asset_list(saved, index, 0x42, 200) &&
                !game_assets::parse_stored_asset_list(saved, index, 0x36, 0) &&
                !game_assets::parse_stored_asset_list(saved, index, 0x36, 201),
            "Wrong class or identity decoded");
    for (std::size_t size = 0; size < 1052; ++size) {
        const std::vector bytes(saved.begin(), saved.begin() + size);
        require(!decode(bytes) && !game_assets::parse_stored_asset_list(bytes, index, 0x36, 200),
                "Truncated descriptor or resource decoded with fresh or prior index");
    }
    for (const auto& [at, value] : {std::pair{0u, 4u},
                                    {20u, 0x500u},
                                    {24u, 0x20000u},
                                    {28u, 128u},
                                    {514u, 2u},
                                    {518u, 4u},
                                    {518u, 6u},
                                    {518u, 4097u},
                                    {522u, 0u},
                                    {522u, 31u},
                                    {522u, 512u},
                                    {522u, 0xffffffffu},
                                    {526u, 6u},
                                    {1026u, 2u}}) {
        auto bytes = saved;
        word(bytes, at, value);
        require(!decode(bytes) && !game_assets::parse_stored_asset_list(bytes, index, 0x36, 200),
                "Malformed schema/version/count/mark/class decoded");
    }
    for (const auto at : {530u, 534u, 540u}) {
        for (const auto value : {0u, 0x12345678u, 0xffffffffu}) {
            auto bytes = saved;
            word(bytes, at, value);
            const auto decoded = decode(bytes);
            require(decoded && (at == 530   ? decoded->resource_type
                                : at == 534 ? decoded->resource_owner
                                            : decoded->resource_id) == value,
                    "Raw descriptor metadata was interpreted or narrowed");
        }
    }
    for (unsigned value = 0; value < 256; ++value) {
        auto bytes = saved;
        bytes[512] = static_cast<std::uint8_t>(value | 0x80);
        bytes[513] = bytes[538] = bytes[539] = bytes[1025] = static_cast<std::uint8_t>(value);
        const auto decoded = decode(bytes);
        require(decoded && decoded->flags[0] == (value | 0x80) && decoded->flags[1] == value &&
                    decoded->needs_release_raw == value && decoded->duplicate_raw == value &&
                    decoded->nodes[0].flags[1] == value,
                "Raw flags or BOOL bytes were normalized");
    }
    auto bytes = saved;
    bytes[512] &= 0x7f;
    require(!decode(bytes), "Branch descriptor treated as a class36 leaf");
    bytes = saved;
    bytes[1030] = 1;
    require(!decode(bytes), "Native truncated node count was accepted");
    auto unusable = index;
    for (unsigned condition = 0; condition < 6; ++condition) {
        unusable = index;
        if (condition == 0) {
            unusable.available = false;
        } else if (condition == 1) {
            unusable.truncated = true;
        } else if (condition == 2) {
            unusable.skipped_nodes = 1;
        } else if (condition == 3) {
            unusable.unsupported_indexes = 1;
        } else if (condition == 4) {
            unusable.records.insert(unusable.records.begin(), unusable.records[0]);
        } else {
            unusable.definition_offsets.insert(std::lower_bound(unusable.definition_offsets.begin(),
                                                                unusable.definition_offsets.end(),
                                                                512u),
                                               512u);
        }
        require(!game_assets::parse_stored_asset_list(saved, unusable, 0x36, 200),
                "Unusable or ambiguous index decoded");
    }
    for (const auto at : {513u, 543u, 1024u, 1051u}) {
        unusable = index;
        unusable.definition_offsets.insert(std::lower_bound(unusable.definition_offsets.begin(),
                                                            unusable.definition_offsets.end(), at),
                                           at);
        require(!game_assets::parse_stored_asset_list(saved, unusable, 0x36, 200),
                "Indexed definition inside consumed bytes decoded");
    }
    for (const auto& span : {std::pair{511u, 513u}, {543u, 544u}, {1023u, 1025u}, {1051u, 1053u}}) {
        unusable = index;
        unusable.node_ranges.push_back(span);
        std::sort(unusable.node_ranges.begin(), unusable.node_ranges.end());
        require(!game_assets::parse_stored_asset_list(saved, unusable, 0x36, 200),
                "Partial index overlap decoded");
    }
    bytes = saved;
    word(bytes, 518, 0);
    word(bytes, 522, 0);
    require(decode(bytes) && decode(bytes)->ids.empty() && decode(bytes)->nodes.empty(),
            "Constructed empty list rejected");
    bytes = saved;
    node(bytes, 1024, false, {800, 900});
    node(bytes, 800, true, {301, 300});
    node(bytes, 900, true, {0xffffffff, 0, 300});
    const auto tree = decode(bytes);
    require(tree && tree->ids == std::vector<std::uint32_t>{301, 300, 0xffffffff, 0, 300} &&
                tree->nodes.size() == 3 && tree->decoded_resource_bytes == 52 &&
                tree->nodes[1].offset == 800 && tree->nodes[2].offset == 900,
            "Resource tree order or consumed extents changed");
    for (const auto mark : {0u, 31u, 1024u, 800u, 805u, 0xffffffffu}) {
        auto corrupt = bytes;
        word(corrupt, 1036, mark);
        require(!decode(corrupt), "Invalid/aliased/cyclic/overlapping child decoded");
    }
    bytes = saved;
    bytes.resize(20000);
    word(bytes, 522, 2048);
    for (unsigned i = 0; i < 33; ++i) {
        node(bytes, 2048 + i * 16, false, {2048 + (i + 1) * 16});
    }
    node(bytes, 2048 + 33 * 16, true, {300, 0, 300, 0xffffffff, 301});
    require(!decode(bytes), "Resource depth limit bypassed");
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
    require(!decode(bytes), "Resource node limit bypassed");
    require(saved == stored_asset_list_fixture::make(), "Parser mutated input");
    std::cout << "Synthetic stored asset-reference list checks passed\n";
    for (int argument = 1; argument < argc; ++argument) {
        const auto database = game_assets::Database::load(std::filesystem::path(argv[argument]));
        const auto stored = game_assets::parse_database_index(database.bytes());
        require(stored.available && !stored.truncated && !stored.skipped_nodes &&
                    !stored.duplicate_keys && !stored.unsupported_indexes,
                "Installed index incomplete");
        std::size_t lists = 0, members = 0;
        for (const auto& record : stored.records) {
            if (record.class_id != 0x36) {
                continue;
            }
            std::cout << "LIST\t" << argument << '\t' << record.id << '\t';
            const auto decoded =
                game_assets::parse_stored_asset_list(database.bytes(), stored, 0x36, record.id);
            if (!decoded) {
                std::cout << "RAW\n";
                continue;
            }
            ++lists;
            members += decoded->ids.size();
            std::cout << decoded->offset << '\t';
            dump_bytes(database.bytes(), decoded->offset, 32);
            std::cout << '\t';
            for (std::size_t i = 0; i < decoded->ids.size(); ++i) {
                if (i) {
                    std::cout << ',';
                }
                std::cout << decoded->ids[i];
            }
            std::cout << '\t';
            for (std::size_t i = 0; i < decoded->nodes.size(); ++i) {
                if (i) {
                    std::cout << ',';
                }
                const auto& node = decoded->nodes[i];
                std::cout << node.offset << ':';
                dump_bytes(database.bytes(), node.offset, node.extent);
            }
            std::cout << '\n';
        }
        std::cout << "FILE\t" << argument << '\t' << stored.records.size() << '\t' << lists << '\t'
                  << members << '\n';
    }
}
