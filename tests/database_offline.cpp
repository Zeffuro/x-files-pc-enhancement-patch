#include "game/database/database.h"
#include "game/database/offline_index.h"
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
        bytes[at + i] = static_cast<std::uint8_t>(value >> (24 - i * 8));
    }
}

void node(std::vector<std::uint8_t>& bytes, unsigned at, bool leaf, unsigned count) {
    bytes[at] = leaf ? 0x8a : 0x4b;
    bytes[at + 1] = 0x31;
    word(bytes, at + 2, 1);
    bytes[at + 6] = static_cast<std::uint8_t>(count >> 8);
    bytes[at + 7] = static_cast<std::uint8_t>(count);
}

std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> bytes(512);
    word(bytes, 0, 5);
    word(bytes, 8, 32);
    word(bytes, 20, 0x501);
    word(bytes, 24, 0x40000);
    word(bytes, 28, 256);
    node(bytes, 32, false, 1);
    word(bytes, 40, 64);
    node(bytes, 64, true, 1);
    bytes[82] = 1;
    word(bytes, 83, 0x2f);
    word(bytes, 99, 7);
    word(bytes, 103, 128);
    word(bytes, 107, 0x2f);
    word(bytes, 111, 0x6e756c6c);
    node(bytes, 128, false, 1);
    word(bytes, 136, 160);
    node(bytes, 160, true, 2);
    word(bytes, 168, 256);
    word(bytes, 172, 298);
    word(bytes, 176, 272);
    word(bytes, 180, 300);
    node(bytes, 256, true, 0);
    node(bytes, 272, true, 0);
    return bytes;
}
}

int main(int argc, char** argv) {
    using namespace game_assets;
    const auto bytes = fixture();
    const auto database = Database::parse(bytes);
    require(database.bytes().size() == bytes.size() && database.bytes()[82] == 1,
            "Database byte accessor changed stored bytes");
    const auto index = parse_database_index(database.bytes());
    require(index.available && !index.truncated && !index.skipped_nodes &&
                !index.unsupported_indexes && index.visited_nodes == 4 && index.records.size() == 2,
            "Stored class and ID index traversal failed");
    require(index.records[0] == StoredDatabaseRecord{0x2f, 298, 256} &&
                index.records[1] == StoredDatabaseRecord{0x2f, 300, 272} &&
                find_database_record(index, 0x2f, 298) == std::size_t{0} &&
                !find_database_record(index, 0x41, 298),
            "Class-aware stored resolution failed");
    auto edited = bytes;
    word(edited, 180, 298);
    auto malformed = parse_database_index(edited);
    require(malformed.duplicate_keys == 1 && !find_database_record(malformed, 0x2f, 298),
            "Ambiguous stored key resolved");
    edited = bytes;
    word(edited, 168, 0);
    malformed = parse_database_index(edited);
    require(malformed.records.size() == 2 && malformed.records[0].offset == 0 &&
                !find_database_record(malformed, 0x2f, 298),
            "Cross-database zero mark resolved as stored bytes");
    edited = bytes;
    word(edited, 40, 32);
    malformed = parse_database_index(edited);
    require(malformed.available && malformed.skipped_nodes == 1 && malformed.records.empty(),
            "Stored index cycle not bounded");
    edited = bytes;
    word(edited, 99, 9);
    malformed = parse_database_index(edited);
    require(malformed.available && malformed.unsupported_indexes == 1 && malformed.records.empty(),
            "Unknown index class was guessed");
    edited = bytes;
    word(edited, 168, 32);
    malformed = parse_database_index(edited);
    require(malformed.skipped_nodes == 1 && !find_database_record(malformed, 0x2f, 298),
            "Record mark inside an index node was accepted");
    edited = bytes;
    word(edited, 168, 509);
    malformed = parse_database_index(edited);
    require(malformed.skipped_nodes == 1 && malformed.records.size() == 1,
            "Truncated record prefix was accepted");
    edited = bytes;
    edited[82] = 7;
    malformed = parse_database_index(edited);
    require(malformed.skipped_nodes == 1 && malformed.records.empty(),
            "Malformed class index count was accepted");
    edited = bytes;
    node(edited, 160, true, 256);
    malformed = parse_database_index(edited);
    require(malformed.skipped_nodes == 1 && malformed.records.empty(),
            "Wide node count was truncated to a byte");
    edited = bytes;
    word(edited, 8, 0xffffffffu);
    require(!parse_database_index(edited).available, "Invalid root offset accepted");
    edited = bytes;
    node(edited, 32, false, 2);
    word(edited, 44, 70);
    malformed = parse_database_index(edited);
    require(!malformed.available && malformed.records.empty(), "Overlapping index nodes accepted");
    edited = bytes;
    word(edited, 20, 0x500);
    require(!parse_database_index(edited).available, "Unsupported stored schema guessed");
    word(edited, 20, 0x501);
    word(edited, 24, 0x80000);
    require(!parse_database_index(edited).available, "Unsupported mark width guessed");
    for (const auto size : {0u, 31u, 39u, 110u, 179u}) {
        malformed = parse_database_index(std::span(bytes).first(size));
        require(!malformed.available || malformed.skipped_nodes, "Truncated node accepted");
    }
    OfflineDatabaseLimits limits;
    limits.max_records = 1;
    malformed = parse_database_index(bytes, limits);
    require(malformed.truncated && malformed.records.size() == 1, "Record bound failed");
    require(!find_database_record(malformed, 0x2f, 298),
            "Partial enumeration claimed a unique stored key");
    limits = {};
    limits.max_nodes = 2;
    malformed = parse_database_index(bytes, limits);
    require(malformed.truncated && malformed.visited_nodes <= 2, "Node bound failed");
    limits = {};
    limits.max_depth = 2;
    malformed = parse_database_index(bytes, limits);
    require(malformed.truncated && malformed.records.empty(), "Depth bound failed");
    const auto again = parse_database_index(bytes);
    require(again.records == index.records, "Stored ordering changed between parses");
    for (int argument = 1; argument < argc; ++argument) {
        const auto actual = Database::load(argv[argument]);
        const auto stored = parse_database_index(actual.bytes());
        require(stored.available && !stored.truncated && !stored.skipped_nodes &&
                    !stored.unsupported_indexes && !stored.duplicate_keys &&
                    stored.records.size() > 65000,
                "Installed HDB enumeration lost stored records");
        const auto name = find_database_record(stored, 0x2f, 298);
        const auto action = find_database_record(stored, 0x41, 68442);
        require(name && action && stored.records[*name].offset == 62872 &&
                    stored.records[*action].offset == 4353832,
                "Known uncached definitions are absent");
        std::cout << argv[argument] << ": " << stored.records.size() << " stored records, "
                  << stored.visited_nodes << " index nodes\n";
    }
}
