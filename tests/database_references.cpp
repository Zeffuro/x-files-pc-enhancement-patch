#include "game/database/reference_index.h"
#include "stored_list_fixture.h"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace game_assets;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

auto hdb_fixture() {
    auto bytes = stored_list_fixture::make();
    using stored_list_fixture::word;
    word(bytes, 646, 800);
    word(bytes, 650, 33);
    bytes[654] = 0x86;
    word(bytes, 800, 200);
    word(bytes, 804, 200);
    bytes[808] = 0;
    bytes[809] = 2;
    bytes[810] = 1;
    const std::string name = "EndGame";
    std::copy(name.begin(), name.end(), bytes.begin() + 812);
    bytes[827] = 1;
    word(bytes, 828, 200);
    bytes[832] = 0;
    word(bytes, 662, 900);
    word(bytes, 666, 9);
    bytes[670] = 3;
    word(bytes, 900, 200);
    bytes[908] = 1;
    return bytes;
}

std::vector<std::uint8_t> gam_fixture() {
    std::vector<std::uint8_t> bytes(512);
    using stored_list_fixture::word;
    word(bytes, 0, 5);
    word(bytes, 20, 0x501);
    word(bytes, 24, 0x40000);
    word(bytes, 28, 256);
    bytes[128] = bytes[256] = 0xc3;
    word(bytes, 130, 1);
    word(bytes, 258, 1);
    word(bytes, 134, 300);
    bytes[139] = 0x41;
    return bytes;
}

OfflineDatabaseIndex gam_index() {
    OfflineDatabaseIndex result;
    result.available = true;
    result.records = {{0x46, 200, 128}, {0x53, 200, 256}};
    result.definition_offsets = {128, 256};
    return result;
}

void fixture_checks() {
    const auto hdb = hdb_fixture(), gam = gam_fixture();
    const auto hi = parse_database_index(hdb), gi = gam_index();
    const std::array sources{ReferenceSource{"story.HDB", ReferenceNamespace::hdb, hdb, &hi},
                             ReferenceSource{"story.GAM", ReferenceNamespace::gam, gam, &gi}};
    const auto index = build_reference_index(sources);
    const auto vars = references_to(index, "story.GAM", 0x53, 200);
    require(vars.size() == 2 && vars[0]->source_id == 300 && vars[0]->resolved &&
                vars[1]->source_id == 300,
            "Conditional function action lost its exact variable references");
    require(references_to(index, "story.HDB", 0x53, 200).empty() &&
                references_to(index, "other.GAM", 0x53, 200).empty(),
            "Numeric IDs crossed a database/file namespace");
    const auto enables = references_to(index, "story.GAM", 0x46, 200);
    if (enables.size() != 1 || enables[0]->source_id != 301) {
        std::cerr << "Enable references " << enables.size() << ", malformed " << index.malformed
                  << ", scanned " << index.scanned << '\n';
        for (const auto& reference : index.references) {
            std::cerr << reference.source_class << ':' << reference.source_id << " -> "
                      << reference.target_class << ':' << reference.target_id << " "
                      << reference.field << '\n';
        }
    }
    require(enables.size() == 1 && enables[0]->source_id == 301,
            "Enable action was confused with a same-ID variable");
    require(function_callers(index, "EndGame").size() == 1 &&
                function_callers(index, "endgame").empty(),
            "Function callers lost their exact decoded name");
    const auto owners = references_to(index, "story.HDB", 0x41, 300);
    require(owners.size() == 2 && references_to(index, "story.HDB", 0x42, 200).size() == 1 &&
                references_to(index, "story.HDB", 0x51, 400).size() == 1,
            "Validated list/trigger/enable links were lost or duplicates inflated ownership");
    const auto missing = build_reference_index(std::span(sources).first(1));
    require(missing.unresolved >= 3 && references_to(missing, "story.HDB", 0x53, 200).empty(),
            "Missing GAM targets were silently resolved in HDB");
    auto contextual = hdb;
    contextual[808] = contextual[832] = 4;
    auto context_sources = sources;
    context_sources[0].bytes = contextual;
    require(references_to(build_reference_index(context_sources), "story.GAM", 0x53, 200).empty(),
            "Trigger-context operand bits were treated as variable IDs");
    auto bad = hdb;
    stored_list_fixture::word(bad, 650, 32);
    auto altered = sources;
    altered[0].bytes = bad;
    const auto malformed = build_reference_index(altered);
    require(malformed.incomplete && malformed.malformed &&
                references_to(malformed, "story.GAM", 0x53, 200).empty() &&
                function_callers(malformed, "EndGame").empty(),
            "Malformed conditional function body produced references");
    auto duplicate = gi;
    duplicate.records.push_back({0x53, 200, 280});
    altered = sources;
    altered[1].index = &duplicate;
    const auto ambiguous = build_reference_index(altered);
    const auto ambiguous_vars = references_to(ambiguous, "story.GAM", 0x53, 200);
    require(ambiguous_vars.size() == 2 && !ambiguous_vars[0]->resolved &&
                !ambiguous_vars[1]->resolved && ambiguous.incomplete,
            "Duplicate target identity became a navigable unique reference");
    ReferenceLimits limits;
    limits.max_references = 1;
    require(build_reference_index(sources, limits).incomplete,
            "Reference count budget did not report incomplete results");
    limits = {};
    limits.max_records = 1;
    require(build_reference_index(sources, limits).incomplete,
            "Record scan budget did not report incomplete results");
    limits = {};
    limits.max_malformed = 1;
    altered = sources;
    altered[0].bytes = bad;
    const auto rejected = build_reference_index(altered, limits);
    require(rejected.incomplete && rejected.malformed == 1 && rejected.scanned == 1,
            "Malformed parser attempts did not stop at their work budget");
    limits = {};
    limits.cancelled = [] { return true; };
    const auto cancelled = build_reference_index(sources, limits);
    require(cancelled.cancelled && cancelled.incomplete && cancelled.references.empty(),
            "Cancellation published complete or fabricated results");
}

auto read(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    require(size <= 128u * 1024u * 1024u, "Database exceeds the read budget");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    require(bool(input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())),
            "Cannot read original database");
    return bytes;
}
}

int main(int argc, char** argv) {
    try {
        fixture_checks();
        if (argc == 3) {
            const auto hdb = read(argv[1]), gam = read(argv[2]);
            const auto hi = parse_database_index(hdb), gi = parse_database_index(gam);
            const std::array sources{ReferenceSource{argv[1], ReferenceNamespace::hdb, hdb, &hi},
                                     ReferenceSource{argv[2], ReferenceNamespace::gam, gam, &gi}};
            const auto index = build_reference_index(sources);
            const auto cover = references_to(index, argv[2], 0x53, 2383);
            const auto calls = function_callers(index, "EndGame");
            require(!cover.empty() && !calls.empty(), "Known authored references were not indexed");
            std::cout << "Original data: " << index.scanned << " records, "
                      << index.references.size() << " references, " << index.malformed
                      << " malformed, " << index.unresolved << " unresolved, Cover " << cover.size()
                      << ", EndGame " << calls.size() << '\n';
            for (const auto& [file, rejected] : index.rejected) {
                std::cout << "Unresolved definition: " << index.files[file].string() << " class "
                          << rejected.class_id << " ID " << rejected.id << " offset "
                          << rejected.offset << '\n';
            }
        }
        std::cout << "Typed, file-scoped reverse references and bounded failures pass.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
