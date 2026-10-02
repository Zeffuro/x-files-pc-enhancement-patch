#include "stored_asset_list_fixture.h"
#include "game/database/database.h"
#include "game/database/stored_asset_list.h"
#include <cstdlib>
#include <iostream>

namespace {
constexpr std::uint32_t classes[] = {0x2e, 0x32, 0x34, 0x36, 0x38, 0x3a, 0x3b, 0x4d, 0x4e, 0x55};

void require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void dump(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    for (const auto byte : bytes) {
        std::cout << digits[byte >> 4] << digits[byte & 15];
    }
}
}

int main(int argc, char** argv) {
    using namespace stored_list_fixture;
    for (const auto cls : classes) {
        auto bytes = stored_asset_list_fixture::make();
        word(bytes, 86, cls);
        word(bytes, 118, cls);
        word(bytes, 544, 0xffffffff);
        bytes[548] = 0xff;
        const auto decode = [&] {
            return game_assets::parse_stored_object_list(
                bytes, game_assets::parse_database_index(bytes), cls, 200);
        };
        auto value = decode();
        require(value && value->ids == std::vector<std::uint32_t>({300, 0, 300, 0xffffffff, 301}) &&
                    value->descriptor_extent == (cls == 0x4e ? 37u : 32u),
                "List class/order/full-width boundaries changed");
        if (cls == 0x4e) {
            require(value->trailing_word == 0xffffffff && value->trailing_byte == 255,
                    "Extended layout fields narrowed");
            word(bytes, 328, 546);
            require(!decode(), "Indexed definition inside extended tail accepted");
            word(bytes, 328, 704);
            word(bytes, 522, 544);
            node(bytes, 544, true, {300, 0, 300, 0xffffffff, 301});
            require(!decode(), "Resource overlapping extended tail accepted");
            word(bytes, 522, 1024);
        }
        word(bytes, 518, 4);
        require(!decode(), "List count conflict accepted");
        word(bytes, 518, 5);
        word(bytes, 526, 0x0c);
        require(!decode(), "Wrong resource class accepted");
        word(bytes, 526, 0x0b);
        word(bytes, 514, 2);
        require(!decode(), "List version conflict accepted");
    }
    std::cout << "Synthetic stored object-list checks passed\n";
    for (int argument = 1; argument < argc; ++argument) {
        const auto database = game_assets::Database::load(std::filesystem::path(argv[argument]));
        const auto index = game_assets::parse_database_index(database.bytes());
        require(index.available && !index.truncated && !index.skipped_nodes &&
                    !index.duplicate_keys && !index.unsupported_indexes,
                "Installed index incomplete");
        std::size_t count = 0;
        for (const auto& record : index.records) {
            if (std::find(std::begin(classes), std::end(classes), record.class_id) ==
                std::end(classes)) {
                continue;
            }
            std::cout << "LIST\t" << argument << '\t' << record.class_id << '\t' << record.id
                      << '\t';
            const auto value = game_assets::parse_stored_object_list(database.bytes(), index,
                                                                     record.class_id, record.id);
            if (!value) {
                std::cout << "RAW\n";
                continue;
            }
            ++count;
            std::cout << value->offset << '\t';
            dump(database.bytes().subspan(value->offset, value->descriptor_extent));
            std::cout << '\t' << value->resource_mark << ',' << value->resource_class << ','
                      << value->resource_type << ',' << value->resource_owner << ','
                      << value->resource_id << ',' << unsigned(value->needs_release_raw) << ','
                      << unsigned(value->duplicate_raw) << ',' << value->trailing_word << ','
                      << unsigned(value->trailing_byte) << '\t';
            for (const auto id : value->ids) {
                std::cout << id << ',';
            }
            std::cout << '\t';
            for (const auto& node : value->nodes) {
                std::cout << node.offset << ':';
                dump(database.bytes().subspan(node.offset, node.extent));
                std::cout << ',';
            }
            std::cout << '\n';
        }
        std::cout << "FILE\t" << argument << '\t' << index.records.size() << '\t' << count << '\n';
    }
}
