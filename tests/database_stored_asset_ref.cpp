#include "stored_asset_ref_fixture.h"
#include "game/database/database.h"
#include "game/database/stored_asset_ref.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

auto decode(const std::vector<std::uint8_t>& bytes) {
    return game_assets::parse_stored_asset_reference(
        bytes, game_assets::parse_database_index(bytes), 0x35, 200);
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
    const auto original = stored_asset_ref_fixture::make();
    auto bytes = original;
    const auto ref = decode(bytes);
    require(ref && ref->offset == 512 && ref->name_mark == 900 && ref->name_size == 6 &&
                ref->raw_words[0] == 0xffffffff && ref->raw_words[1] == 0x80000000 &&
                ref->raw_bytes[0] == 255 && ref->raw_bytes[1] == 2 &&
                ref->name == std::vector<std::uint8_t>({2, 'a', '\\', 0xff, 0, 'z'}),
            "Binary name or unsigned fields changed");
    const auto reject = [&](const std::vector<std::uint8_t>& data, const char* message) {
        require(!decode(data), message);
    };
    for (unsigned byte = 0; byte < 256; ++byte) {
        bytes = original;
        bytes[900] = static_cast<std::uint8_t>(byte);
        bytes[534] = static_cast<std::uint8_t>(byte);
        bytes[535] = static_cast<std::uint8_t>(255 - byte);
        bytes[513] = static_cast<std::uint8_t>(byte);
        const auto value = decode(bytes);
        require(value && value->name[0] == byte && value->raw_bytes[0] == byte &&
                    value->raw_bytes[1] == 255 - byte && value->flags[1] == byte,
                "Raw byte domain restricted");
        bytes[512] = static_cast<std::uint8_t>(byte);
        require(bool(decode(bytes)) == bool(byte & 128), "Descriptor leaf flags misread");
    }
    for (unsigned bit = 0; bit < 32; ++bit) {
        bytes = original;
        word(bytes, 526, std::uint32_t{1} << bit);
        word(bytes, 530, ~(std::uint32_t{1} << bit));
        const auto value = decode(bytes);
        require(value && value->raw_words[0] == (std::uint32_t{1} << bit) &&
                    value->raw_words[1] == ~(std::uint32_t{1} << bit),
                "Raw word narrowed");
    }
    for (const auto mark : {0u, 1u, 31u, 32u, 256u, 511u, 512u, 513u, 534u, 536u, 576u, 704u, 1171u,
                            1172u, 0xffffffffu}) {
        bytes = original;
        word(bytes, 518, mark);
        if (mark == 536) {
            require(bool(decode(bytes)), "Adjacent external name rejected");
        } else {
            reject(bytes, "Invalid name span accepted");
        }
    }
    bytes = original;
    word(bytes, 522, 0xffffffff);
    reject(bytes, "Unbounded name accepted");
    word(bytes, 518, 0);
    word(bytes, 522, 0);
    require(decode(bytes) && decode(bytes)->name.empty(), "Empty name rejected");
    bytes = original;
    word(bytes, 514, 2);
    reject(bytes, "Unsupported version accepted");
    bytes = original;
    word(bytes, 0, 4);
    reject(bytes, "Unsupported header accepted");
    bytes = original;
    word(bytes, 264, 512);
    reject(bytes, "Aliased descriptor accepted");
    bytes = original;
    word(bytes, 320 + 8, 520);
    reject(bytes, "Indexed definition inside descriptor accepted");
    bytes = original;
    word(bytes, 320 + 8, 901);
    reject(bytes, "Indexed definition inside name accepted");
    bytes = original;
    bytes.resize(536);
    reject(bytes, "Truncated name accepted");
    bytes = original;
    word(bytes, 518, 1172);
    word(bytes, 522, 65536);
    bytes.resize(1172 + 65536, 0xfe);
    require(decode(bytes) && decode(bytes)->name.size() == 65536, "Name safety boundary rejected");
    bytes.push_back(0);
    word(bytes, 522, 65537);
    reject(bytes, "Name safety limit bypassed");
    require(original == stored_asset_ref_fixture::make(), "Input changed");
    std::cout << "Synthetic stored asset-reference checks passed\n";
    for (int argument = 1; argument < argc; ++argument) {
        const auto database = game_assets::Database::load(std::filesystem::path(argv[argument]));
        const auto index = game_assets::parse_database_index(database.bytes());
        require(index.available && !index.truncated && !index.skipped_nodes &&
                    !index.duplicate_keys && !index.unsupported_indexes,
                "Installed index incomplete");
        std::size_t count = 0;
        for (const auto& record : index.records) {
            if (record.class_id != 0x35) {
                continue;
            }
            std::cout << "REF\t" << argument << '\t' << record.id << '\t';
            const auto value =
                game_assets::parse_stored_asset_reference(database.bytes(), index, 0x35, record.id);
            if (!value) {
                std::cout << "RAW\n";
                continue;
            }
            ++count;
            std::cout << value->offset << '\t' << value->name_mark << '\t' << value->name_size
                      << '\t' << value->raw_words[0] << ',' << value->raw_words[1] << ','
                      << unsigned(value->raw_bytes[0]) << ',' << unsigned(value->raw_bytes[1])
                      << ',' << unsigned(value->flags[0]) << ',' << unsigned(value->flags[1])
                      << '\t';
            dump(database.bytes().subspan(value->offset, 24));
            std::cout << '\t';
            dump(value->name);
            std::cout << '\n';
        }
        std::cout << "FILE\t" << argument << '\t' << index.records.size() << '\t' << count << '\n';
    }
}
