#include "devtools/database/source.h"
#include "game/database/database.h"
#include "game/database/stored_variable.h"
#include <algorithm>
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

void node(std::vector<std::uint8_t>& bytes, std::size_t at, unsigned count) {
    bytes[at] = 0xc3;
    word(bytes, at + 2, 1);
    bytes[at + 7] = static_cast<std::uint8_t>(count);
}

std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> bytes(512);
    word(bytes, 0, 5);
    word(bytes, 8, 32);
    word(bytes, 20, 0x501);
    word(bytes, 24, 0x40000);
    word(bytes, 28, 256);
    node(bytes, 32, 1);
    bytes[50] = 1;
    word(bytes, 51, 0x53);
    word(bytes, 67, 7);
    word(bytes, 71, 128);
    word(bytes, 75, 0x53);
    node(bytes, 128, 1);
    word(bytes, 136, 256);
    word(bytes, 140, 100);
    node(bytes, 256, 0);
    word(bytes, 262, 384);
    word(bytes, 266, 6);
    word(bytes, 270, 0xfffffffe);
    word(bytes, 274, 0x12b);
    bytes[278] = 0x27;
    bytes[279] = 0x81;
    std::copy_n("iTest", 5, bytes.begin() + 384);
    bytes[389] = 0;
    return bytes;
}

auto decode(const std::vector<std::uint8_t>& bytes) {
    return game_assets::parse_stored_variable(bytes, game_assets::parse_database_index(bytes), 0x53,
                                              100);
}
}

int main(int argc, char** argv) {
    using namespace game_assets;
    auto bytes = fixture();
    const auto saved = bytes;
    const auto value = decode(bytes);
    require(value && value->name == "iTest" && value->signed_value == -2 &&
                value->value_bits == 0xfffffffe && value->owner_id == 0x12b &&
                value->flag == 0x27 && value->type == 0x81 && value->offset == 256 &&
                value->name_offset == 384 && value->name_size == 6,
            "Stored variable fields decoded incorrectly");
    require(bytes == saved, "Read-only decoder changed source bytes");
    require(!parse_stored_variable(bytes, parse_database_index(bytes), 0x46, 100),
            "Unsupported class decoded as a variable");
    for (const auto size : {0u, 23u, 31u, 135u, 259u, 279u, 388u}) {
        bytes.assign(saved.begin(), saved.begin() + size);
        require(!decode(bytes), "Truncated GAM decoded a variable");
    }
    for (const auto& [at, data] : {std::pair{258u, 2u},
                                   {262u, 0xfffffff0u},
                                   {266u, 0xfffffff0u},
                                   {266u, 0u},
                                   {266u, 1025u},
                                   {71u, 0xfffffff0u},
                                   {20u, 0x500u}}) {
        bytes = saved;
        word(bytes, at, data);
        require(!decode(bytes), "Malformed GAM decoded a variable");
    }
    for (const auto bad : {0u, 9u, 0x80u}) {
        bytes = saved;
        bytes[385] = static_cast<std::uint8_t>(bad);
        require(!decode(bytes), "Invalid variable name accepted");
    }
    bytes = saved;
    bytes[389] = 'x';
    require(!decode(bytes), "Unterminated variable name accepted");
    bytes = saved;
    bytes[279] = 0xff;
    require(decode(bytes)->type == 0xff, "Unknown type was guessed or discarded");
    auto ambiguous = parse_database_index(saved);
    ambiguous.records.push_back(ambiguous.records.front());
    require(!parse_stored_variable(saved, ambiguous, 0x53, 100),
            "Ambiguous indexed variable decoded");
    ambiguous = parse_database_index(saved);
    ambiguous.truncated = true;
    require(!parse_stored_variable(saved, ambiguous, 0x53, 100),
            "Incomplete index decoded a variable");
    ambiguous = parse_database_index(saved);
    ambiguous.records.front().offset = 0;
    require(!parse_stored_variable(saved, ambiguous, 0x53, 100),
            "Cross-database zero mark decoded a variable");
    bytes = saved;
    word(bytes, 20, 0x500);
    require(!parse_stored_variable(bytes, parse_database_index(saved), 0x53, 100),
            "Wrong-schema bytes decoded with a previously valid index");
    for (int argument = 1; argument < argc; ++argument) {
        const auto path = std::filesystem::path(argv[argument]);
        const auto source = devtools::browser_source(path);
        require(source.database == std::filesystem::canonical(path) && source.asset.empty(),
                "GAM/save file was routed to raw asset mode");
        const auto database = Database::load(path);
        const auto index = parse_database_index(database.bytes());
        require(index.available && !index.truncated && !index.skipped_nodes &&
                    !index.duplicate_keys && !index.unsupported_indexes,
                "Real GAM index is incomplete");
        std::size_t variables = 0;
        for (const auto& record : index.records) {
            if (record.class_id != 0x53) {
                continue;
            }
            const auto variable =
                parse_stored_variable(database.bytes(), index, record.class_id, record.id);
            require(variable.has_value(), "Real GAM variable failed strict decoding");
            require(variable->offset == record.offset &&
                        variable->name_size == variable->name.size() + 1 &&
                        std::equal(variable->name.begin(), variable->name.end(),
                                   database.bytes().begin() + variable->name_offset),
                    "GAM variable lost its indexed identity or original name bytes");
            ++variables;
        }
        require(variables > 0, "Database variable enumeration lost indexed records");
        std::cout << path.string() << ": " << index.records.size() << " records, " << variables
                  << " variables, " << index.visited_nodes << " nodes\n";
    }
}
