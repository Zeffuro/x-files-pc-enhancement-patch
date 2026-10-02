#include "game/database/stored_action.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
    std::vector<std::uint8_t> bytes(535);
    word(bytes, 0, 5);
    word(bytes, 8, 32);
    word(bytes, 20, 0x501);
    word(bytes, 24, 0x40000);
    word(bytes, 28, 256);
    for (const auto at : {32u, 128u, 256u}) {
        bytes[at] = 0xc3;
        bytes[at + 1] = 0x71;
        word(bytes, at + 2, 1);
    }
    bytes[39] = 1;
    bytes[50] = 1;
    word(bytes, 51, 0x41);
    word(bytes, 67, 7);
    word(bytes, 71, 128);
    word(bytes, 75, 0x41);
    bytes[135] = 1;
    word(bytes, 136, 256);
    word(bytes, 140, 100);
    word(bytes, 262, 512);
    word(bytes, 266, 23);
    bytes[270] = 0x80;
    word(bytes, 512, 0xfedcba98);
    word(bytes, 516, 0x80000001);
    bytes[520] = 0;
    bytes[521] = 2;
    bytes[522] = 5;
    bytes[523] = 0xaf;
    word(bytes, 524, 0xffffffff);
    word(bytes, 528, 0x12345678);
    bytes[532] = 0;
    bytes[533] = 2;
    bytes[534] = 3;
    return bytes;
}

auto decode(const std::vector<std::uint8_t>& bytes) {
    return game_assets::parse_stored_action(bytes, game_assets::parse_database_index(bytes), 0x41,
                                            100);
}

void expression(std::ostream& out,
                const std::optional<game_assets::StoredActionExpression>& value) {
    if (!value) {
        out << "-";
        return;
    }
    out << value->left.raw << ',' << unsigned(value->left.kind) << ',' << value->right.raw << ','
        << unsigned(value->right.kind) << ',' << unsigned(value->operation);
}

void hex(std::ostream& out, std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    for (const auto byte : bytes) {
        out << digits[byte >> 4] << digits[byte & 15];
    }
}
}

int main(int argc, char** argv) {
    using namespace game_assets;
    const auto saved = fixture();
    const auto index = parse_database_index(saved);
    const auto value = decode(saved);
    require(value && value->offset == 256 && value->resource_mark == 512 &&
                value->encoded_length == 23 && value->action_type == 0x80 &&
                value->payload == std::vector<std::uint8_t>(saved.begin() + 512, saved.end()) &&
                value->predicate && value->statement &&
                *value->predicate == StoredActionExpression{{0xfedcba98, 0}, {0x80000001, 2}, 5} &&
                *value->statement == StoredActionExpression{{0xffffffff, 0}, {0x12345678, 2}, 3},
            "Stored predicate/Statement words must decode from big endian wire bytes");
    require(!parse_stored_action(saved, index, 0x42, 100) &&
                !parse_stored_action(saved, index, 0x41, 101) &&
                !parse_stored_action(saved, index, 0x41, 0),
            "Wrong class or missing/null identity decoded");
    for (std::size_t size = 0; size < saved.size(); ++size) {
        const std::vector<std::uint8_t> bytes(saved.begin(), saved.begin() + size);
        require(!decode(bytes) && !parse_stored_action(bytes, index, 0x41, 100),
                "Truncated descriptor/payload decoded with new or previous index");
    }
    for (const auto& [at, data] : {std::pair{0u, 4u},
                                   {20u, 0x500u},
                                   {24u, 0x20000u},
                                   {28u, 128u},
                                   {258u, 0u},
                                   {258u, 2u},
                                   {136u, 0u},
                                   {136u, 24u},
                                   {136u, 0xfffffff0u},
                                   {262u, 0u},
                                   {262u, 24u},
                                   {262u, 256u},
                                   {262u, 270u},
                                   {262u, 128u},
                                   {262u, 0xfffffff0u},
                                   {266u, 4097u},
                                   {266u, 0xffffffffu}}) {
        auto bytes = saved;
        word(bytes, at, data);
        require(!decode(bytes), "Unsupported header/version/mark/length accepted");
        if (at != 136) {
            require(!parse_stored_action(bytes, index, 0x41, 100),
                    "Invalid bytes decoded with previous index");
        }
    }
    auto bytes = saved;
    bytes[256] &= 0x7f;
    require(!decode(bytes), "Non-leaf descriptor flags accepted");
    bytes = saved;
    bytes[270] = 0;
    word(bytes, 266, 11);
    const auto unconditional = decode(bytes);
    require(unconditional && !unconditional->predicate && unconditional->statement &&
                *unconditional->statement == *value->predicate,
            "Unconditional Statement boundary changed");
    for (unsigned type = 0; type < 256; ++type) {
        bytes = saved;
        bytes[270] = static_cast<std::uint8_t>(type);
        const auto result = decode(bytes);
        require(result && result->action_type == type &&
                    result->predicate.has_value() == ((type & 0x80) != 0) &&
                    result->statement.has_value() == (type == 0x80),
                "Raw subtype guessed or conditional flag lost");
    }
    for (unsigned kind = 0; kind < 256; ++kind) {
        bytes = saved;
        bytes[520] = bytes[521] = bytes[532] = bytes[533] = static_cast<std::uint8_t>(kind);
        bytes[522] = bytes[534] = static_cast<std::uint8_t>(kind);
        const auto result = decode(bytes);
        require(result && result->predicate->left.kind == kind &&
                    result->predicate->right.kind == kind && result->predicate->operation == kind &&
                    result->statement->left.kind == kind && result->statement->right.kind == kind &&
                    result->statement->operation == kind,
                "Unknown operand or operation guessed/narrowed");
    }
    for (unsigned length = 1; length < 40; ++length) {
        bytes = saved;
        bytes.resize(552);
        word(bytes, 266, length);
        const auto result = decode(bytes);
        require(result && result->predicate.has_value() == (length >= 12) &&
                    result->statement.has_value() == (length == 23),
                "Extended/truncated expression was silently accepted as exact Statement");
    }
    for (const bool conditional : {false, true}) {
        const std::size_t prefix = conditional ? 12 : 0;
        for (unsigned length = 1; length < 40; ++length) {
            bytes = saved;
            bytes.resize(552);
            bytes[270] = conditional ? 0x81 : 1;
            word(bytes, 266, length);
            const auto result = decode(bytes);
            require(result && result->asset.has_value() == (length == prefix + 5) &&
                        !result->statement &&
                        result->predicate.has_value() == (conditional && length >= 12),
                    "Asset body requires exactly five bytes after the conditional prefix");
        }
        for (unsigned selector = 0; selector < 256; ++selector) {
            bytes = saved;
            bytes[270] = conditional ? 0x81 : 1;
            word(bytes, 266, static_cast<std::uint32_t>(prefix + 5));
            word(bytes, 512 + prefix, 0xfedcba98);
            bytes[516 + prefix] = static_cast<std::uint8_t>(selector);
            const auto result = decode(bytes);
            require(result && result->asset == StoredAssetAction{0xfedcba98, bytes[516 + prefix]},
                    "Asset ID endian/high bits or raw selector changed");
        }
        for (const auto id : {0u, 1u, 0x80000000u, 0xffffffffu}) {
            word(bytes, 512 + prefix, id);
            require(decode(bytes)->asset->id == id, "Asset identity narrowed or null guessed");
        }
    }
    for (const bool conditional : {false, true}) {
        const std::size_t prefix = conditional ? 12 : 0;
        for (unsigned length = 1; length < 40; ++length) {
            bytes = saved;
            bytes.resize(552);
            bytes[270] = conditional ? 0x82 : 2;
            word(bytes, 266, length);
            const auto result = decode(bytes);
            require(result && result->timer.has_value() == (length == prefix + 6) &&
                        !result->statement && !result->asset &&
                        result->predicate.has_value() == (conditional && length >= 12),
                    "Timer body requires exactly six bytes after the conditional prefix");
        }
        word(bytes, 266, static_cast<std::uint32_t>(prefix + 6));
        for (unsigned byte = 0; byte < 256; ++byte) {
            word(bytes, 512 + prefix, 0xfedcba98);
            bytes[516 + prefix] = static_cast<std::uint8_t>(byte);
            bytes[517 + prefix] = static_cast<std::uint8_t>(255 - byte);
            const auto result = decode(bytes);
            require(result && result->timer == StoredTimerAction{0xfedcba98, bytes[516 + prefix],
                                                                 bytes[517 + prefix]},
                    "Timer duration endian/high bits or ID/control byte changed");
        }
        for (const auto duration : {0u, 1u, 0x80000000u, 0xffffffffu}) {
            word(bytes, 512 + prefix, duration);
            require(decode(bytes)->timer->duration == duration,
                    "Timer duration narrowed or null guessed");
        }
    }
    for (const bool conditional : {false, true}) {
        const std::size_t prefix = conditional ? 12 : 0;
        for (unsigned length = 1; length < 40; ++length) {
            bytes = saved;
            bytes.resize(552);
            bytes[270] = conditional ? 0x83 : 3;
            word(bytes, 266, length);
            const auto result = decode(bytes);
            require(result && result->enable.has_value() == (length == prefix + 9) &&
                        !result->statement && !result->asset && !result->timer &&
                        result->predicate.has_value() == (conditional && length >= 12),
                    "Enable body requires exactly nine bytes after the conditional prefix");
        }
        word(bytes, 266, static_cast<std::uint32_t>(prefix + 9));
        for (unsigned control = 0; control < 256; ++control) {
            word(bytes, 512 + prefix, 0xfedcba98);
            word(bytes, 516 + prefix, 0x80000001);
            bytes[520 + prefix] = static_cast<std::uint8_t>(control);
            const auto result = decode(bytes);
            require(result && result->enable ==
                                  StoredEnableAction{0xfedcba98, 0x80000001, bytes[520 + prefix]},
                    "Enable full words or raw control byte narrowed or interpreted");
        }
        for (const auto id : {0u, 1u, 0x80000000u, 0xffffffffu}) {
            for (const auto raw_word : {0u, 1u, 0x80000000u, 0xffffffffu}) {
                word(bytes, 512 + prefix, id);
                word(bytes, 516 + prefix, raw_word);
                require(decode(bytes)->enable == StoredEnableAction{id, raw_word, 255},
                        "Enable identity or unknown word lost complete bit representation");
            }
        }
    }
    for (const bool conditional : {false, true}) {
        const std::size_t prefix = conditional ? 12 : 0;
        for (unsigned length = 1; length < 40; ++length) {
            bytes = saved;
            bytes.resize(552);
            bytes[270] = conditional ? 0x84 : 4;
            word(bytes, 266, length);
            const auto result = decode(bytes);
            require(result && result->set_view.has_value() == (length == prefix + 16) &&
                        !result->statement && !result->asset && !result->timer && !result->enable &&
                        result->predicate.has_value() == (conditional && length >= 12),
                    "Set View body requires exactly sixteen bytes after the conditional prefix");
        }
        word(bytes, 266, static_cast<std::uint32_t>(prefix + 16));
        for (const auto id : {0u, 1u, 0x12345678u, 0x80000000u, 0xffffffffu}) {
            word(bytes, 512 + prefix, id);
            word(bytes, 516 + prefix, ~id);
            word(bytes, 520 + prefix, id ^ 0x12345678);
            word(bytes, 524 + prefix, id ^ 0x80000001);
            require(decode(bytes)->set_view ==
                        StoredSetViewAction{id, ~id, id ^ 0x12345678, id ^ 0x80000001},
                    "Set View full words narrowed, reordered or decoded with wrong endian");
        }
    }
    for (const bool conditional : {false, true}) {
        const std::size_t prefix = conditional ? 12 : 0;
        for (unsigned length = 1; length < 40; ++length) {
            bytes = saved;
            bytes.resize(552);
            bytes[270] = conditional ? 0x85 : 5;
            word(bytes, 266, length);
            const auto result = decode(bytes);
            require(result && result->interface_action.has_value() == (length == prefix + 5) &&
                        !result->statement && !result->asset && !result->timer && !result->enable &&
                        !result->set_view &&
                        result->predicate.has_value() == (conditional && length >= 12),
                    "Interface body requires exactly five bytes after the conditional prefix");
        }
        word(bytes, 266, static_cast<std::uint32_t>(prefix + 5));
        for (unsigned control = 0; control < 256; ++control) {
            for (const auto id : {0u, 1u, 0x12345678u, 0x80000000u, 0xffffffffu}) {
                word(bytes, 512 + prefix, id);
                bytes[516 + prefix] = static_cast<std::uint8_t>(control);
                require(decode(bytes)->interface_action ==
                            StoredInterfaceAction{id, static_cast<std::uint8_t>(control)},
                        "Interface full ID or raw control byte narrowed or interpreted");
            }
        }
    }
    for (const bool conditional : {false, true}) {
        const std::size_t prefix = conditional ? 12 : 0;
        for (unsigned count = 0; count < 256; ++count) {
            bytes = saved;
            bytes.resize(1820);
            bytes[270] = conditional ? 0x86 : 6;
            const auto body = 512 + prefix;
            StoredFunctionAction expected;
            for (unsigned i = 0; i < 15; ++i) {
                expected.name[i] = bytes[body + i] = static_cast<std::uint8_t>(count + i);
            }
            bytes[body + 15] = static_cast<std::uint8_t>(count);
            for (unsigned i = 0; i < count; ++i) {
                const auto raw = 0x80000001u ^ (0x01020408u * i);
                const auto kind = static_cast<std::uint8_t>(i + count);
                word(bytes, body + 16 + 4 * i, raw);
                bytes[body + 16 + 4 * count + i] = kind;
                expected.arguments.push_back({raw, kind});
            }
            const auto length = static_cast<std::uint32_t>(prefix + 16 + 5 * count);
            for (const auto n : {length - 1, length, length + 1}) {
                word(bytes, 266, n);
                const auto result = decode(bytes);
                require(result && result->function.has_value() == (n == length) &&
                            !result->statement && !result->asset && !result->timer &&
                            !result->enable && !result->set_view && !result->interface_action &&
                            result->predicate.has_value() == conditional,
                        "Function count must match exact body length after conditional prefix");
                if (result->function) {
                    require(*result->function == expected,
                            "Function raw name or separate value/kind arrays changed");
                }
            }
        }
        bytes = saved;
        bytes.resize(552);
        bytes[270] = conditional ? 0x86 : 6;
        for (unsigned n = 1; n < prefix + 16; ++n) {
            word(bytes, 266, n);
            const auto result = decode(bytes);
            require(result && !result->function &&
                        result->predicate.has_value() == (conditional && n >= 12),
                    "Short Function header must remain raw");
        }
    }
    for (const bool conditional : {false, true}) {
        bytes = saved;
        bytes.resize(564);
        const unsigned prefix = conditional ? 12 : 0;
        const auto body = 512 + prefix;
        bytes[270] = conditional ? 0x87 : 7;
        word(bytes, body, 0xffffffff);
        bytes[body + 4] = 255;
        bytes[body + 5] = 128;
        bytes[body + 6] = 0x80;
        bytes[body + 7] = 0;
        bytes[body + 8] = 0xff;
        bytes[body + 9] = 0xff;
        for (unsigned n = 1; n <= 40; ++n) {
            word(bytes, 266, n);
            const auto result = decode(bytes);
            require(result && result->sound.has_value() == (n == prefix + 10) &&
                        result->predicate.has_value() == (conditional && n >= 12),
                    "3D Sound body must have exactly ten bytes after conditional prefix");
        }
        word(bytes, 266, prefix + 10);
        for (unsigned raw = 0; raw <= 65535; ++raw) {
            bytes[body + 4] = static_cast<std::uint8_t>(raw);
            bytes[body + 5] = static_cast<std::uint8_t>(raw >> 8);
            bytes[body + 6] = static_cast<std::uint8_t>(raw >> 8);
            bytes[body + 7] = static_cast<std::uint8_t>(raw);
            bytes[body + 8] = static_cast<std::uint8_t>((raw ^ 65535) >> 8);
            bytes[body + 9] = static_cast<std::uint8_t>(raw ^ 65535);
            const auto result = parse_stored_action(bytes, index, 0x41, 100);
            require(result && result->sound ==
                                  StoredSoundAction{0xffffffff, static_cast<std::uint8_t>(raw),
                                                    static_cast<std::uint8_t>(raw >> 8),
                                                    static_cast<std::uint16_t>(raw),
                                                    static_cast<std::uint16_t>(raw ^ 65535)},
                    "3D Sound raw bytes or complete BE16 words changed");
        }
    }
    bytes = saved;
    word(bytes, 262, 0);
    word(bytes, 266, 0);
    require(decode(bytes) && decode(bytes)->payload.empty() && !decode(bytes)->predicate &&
                !decode(bytes)->statement && !decode(bytes)->asset && !decode(bytes)->timer &&
                !decode(bytes)->enable && !decode(bytes)->set_view &&
                !decode(bytes)->interface_action && !decode(bytes)->function &&
                !decode(bytes)->sound,
            "Empty native blob decoded incorrectly");
    for (unsigned condition = 0; condition < 8; ++condition) {
        auto unusable = index;
        if (condition == 0) {
            unusable.available = false;
        } else if (condition == 1) {
            unusable.truncated = true;
        } else if (condition == 2) {
            unusable.skipped_nodes = 1;
        } else if (condition == 3) {
            unusable.unsupported_indexes = 1;
        } else if (condition == 4) {
            unusable.records.push_back(unusable.records.front());
        } else if (condition == 5) {
            unusable.definition_offsets.push_back(256);
        } else if (condition == 6) {
            unusable.definition_offsets.push_back(264);
        } else {
            unusable.definition_offsets.push_back(520);
        }
        require(!parse_stored_action(saved, unusable, 0x41, 100),
                "Incomplete/ambiguous/overlapping definition index accepted");
    }
    for (const auto& range : {std::pair{255u, 257u}, {270u, 272u}, {511u, 513u}, {534u, 536u}}) {
        auto unusable = index;
        unusable.node_ranges.push_back(range);
        std::sort(unusable.node_ranges.begin(), unusable.node_ranges.end());
        require(!parse_stored_action(saved, unusable, 0x41, 100),
                "Partial index-node overlap accepted");
    }
    require(saved == fixture(), "Stored action parsing modified original bytes");
    std::cout << "Synthetic stored action checks passed\n";
    for (int argument = 1; argument < argc; ++argument) {
        const auto path = std::filesystem::path(argv[argument]);
        std::ifstream input(path, std::ios::binary);
        require(bool(input), "Installed database did not open");
        const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)), {});
        const auto stored = parse_database_index(data);
        require(stored.available && !stored.truncated && !stored.skipped_nodes &&
                    !stored.duplicate_keys && !stored.unsupported_indexes,
                "Installed index incomplete");
        std::size_t actions = 0, decoded = 0;
        for (const auto& record : stored.records) {
            if (record.class_id != 0x41) {
                continue;
            }
            ++actions;
            const auto result = parse_stored_action(data, stored, record.class_id, record.id);
            std::cout << "ROW\t" << argument << '\t' << record.id << '\t';
            if (!result) {
                std::cout << "RAW\n";
                continue;
            }
            ++decoded;
            std::cout << result->offset << '\t' << result->resource_mark << '\t'
                      << result->encoded_length << '\t' << unsigned(result->action_type) << '\t';
            hex(std::cout, result->payload);
            std::cout << '\t';
            expression(std::cout, result->predicate);
            std::cout << '\t';
            expression(std::cout, result->statement);
            std::cout << '\t';
            if (result->asset) {
                std::cout << result->asset->id << ',' << unsigned(result->asset->selector);
            } else {
                std::cout << '-';
            }
            std::cout << '\t';
            if (result->timer) {
                std::cout << result->timer->duration << ',' << unsigned(result->timer->id) << ','
                          << unsigned(result->timer->control);
            } else {
                std::cout << '-';
            }
            std::cout << '\t';
            if (result->enable) {
                std::cout << result->enable->id << ',' << result->enable->raw_word << ','
                          << unsigned(result->enable->control);
            } else {
                std::cout << '-';
            }
            std::cout << '\t';
            if (result->set_view) {
                const auto& view = *result->set_view;
                std::cout << view.view_id << ',' << view.node_id << ',' << view.location_id << ','
                          << view.viewpoint_id;
            } else {
                std::cout << '-';
            }
            std::cout << '\t';
            if (result->interface_action) {
                std::cout << result->interface_action->id << ','
                          << unsigned(result->interface_action->control);
            } else {
                std::cout << '-';
            }
            std::cout << '\t';
            if (result->function) {
                hex(std::cout, result->function->name);
                std::cout << ',' << result->function->arguments.size();
                for (const auto& parameter : result->function->arguments) {
                    std::cout << ',' << parameter.raw << ',' << unsigned(parameter.kind);
                }
            } else {
                std::cout << '-';
            }
            std::cout << '\t';
            if (result->sound) {
                const auto& sound = *result->sound;
                std::cout << sound.id << ',' << unsigned(sound.selector) << ','
                          << unsigned(sound.raw_byte5) << ',' << sound.word6 << ',' << sound.word8;
            } else {
                std::cout << '-';
            }
            std::cout << '\n';
        }
        std::cout << "FILE\t" << argument << '\t' << path.string() << '\t' << actions << '\t'
                  << decoded << '\n';
    }
}
