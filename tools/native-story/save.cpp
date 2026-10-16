#include "game/database/database.h"
#include "game/database/stored_fields.h"
#include "game/database/stored_variable.h"
#include "saves/compatibility.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    try {
        const bool baseline = argc == 3 && std::wstring_view(argv[1]) == L"--baseline";
        if (argc != 2 && !baseline) {
            throw std::runtime_error("Usage: xfiles-native-save [--baseline] <save.x|XFILES.GAM>");
        }
        const auto save = game_assets::Database::load(argv[baseline ? 2 : 1]);
        const auto index = game_assets::parse_database_index(save.bytes());
        if (!index.available || index.truncated || index.skipped_nodes ||
            index.unsupported_indexes || index.duplicate_keys) {
            throw std::runtime_error("Incomplete save index.");
        }
        std::optional<std::uint32_t> view;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> variables;
        for (const auto& record : index.records) {
            if (record.class_id == 0x57) {
                const auto fields =
                    game_assets::parse_stored_fields(save.bytes(), index, 0x57, record.id);
                if (!fields || view) {
                    throw std::runtime_error("Expected exactly one valid packed scene.");
                }
                for (const auto& field : fields->scalars) {
                    if (field.offset == 18 && field.width == 4) {
                        view = field.value;
                    }
                }
            } else if (record.class_id == 0x53) {
                const auto variable =
                    game_assets::parse_stored_variable(save.bytes(), index, 0x53, record.id);
                if (!variable) {
                    throw std::runtime_error("Malformed state variable.");
                }
                variables.emplace_back(record.id, variable->value_bits);
            }
        }
        if ((!view && !baseline) || variables.empty()) {
            throw std::runtime_error("Missing packed scene or state variables.");
        }
        const auto compatibility = saves::load_compatibility_error(argv[baseline ? 2 : 1], true);
        std::cout << "{\"current_database_error\":";
        if (compatibility) {
            std::cout << std::quoted(compatibility);
        } else {
            std::cout << "null";
        }
        std::cout << ",\"view\":" << (view ? std::to_string(*view) : "null")
                  << ",\"variable_count\":" << variables.size() << ",\"variables\":{";
        bool first = true;
        for (const auto& [id, value] : variables) {
            if (!first) {
                std::cout << ',';
            }
            first = false;
            std::cout << '\"' << id << "\":" << value;
        }
        std::cout << "}}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
