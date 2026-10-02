#include "stored_list_fixture.h"
#include "game/database/database.h"
#include "game/database/stored_fields.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

auto fixture(std::uint32_t cls) {
    auto bytes = stored_list_fixture::make();
    stored_list_fixture::word(bytes, 86, cls);
    stored_list_fixture::word(bytes, 118, cls);
    stored_list_fixture::word(bytes, 518, 900);
    stored_list_fixture::word(bytes, 522, 4);
    stored_list_fixture::word(bytes, 526, 910);
    stored_list_fixture::word(bytes, 530, 3);
    bytes[900] = 'n';
    bytes[901] = 0xff;
    bytes[902] = 0;
    bytes[903] = 'x';
    bytes[910] = 0;
    bytes[911] = '\\';
    bytes[912] = 0x80;
    return bytes;
}

auto decode(const std::vector<std::uint8_t>& bytes, std::uint32_t cls) {
    return game_assets::parse_stored_fields(bytes, game_assets::parse_database_index(bytes), cls,
                                            200);
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
    const auto original = fixture(0x2f);
    auto bytes = original;
    auto value = decode(bytes, 0x2f);
    require(value && value->extent == 22 && value->scalars.empty() && value->blobs.size() == 2 &&
                value->blobs[0].bytes == std::vector<std::uint8_t>({'n', 0xff, 0, 'x'}) &&
                value->blobs[1].bytes == std::vector<std::uint8_t>({0, '\\', 0x80}),
            "Name counted bytes changed");
    for (const auto field : {518u, 526u}) {
        for (const auto mark :
             {0u, 1u, 31u, 32u, 256u, 512u, 520u, 576u, 704u, 1171u, 0xffffffffu}) {
            bytes = original;
            word(bytes, field, mark);
            require(!decode(bytes, 0x2f), "Name conflict accepted");
        }
        bytes = original;
        word(bytes, field, 0);
        word(bytes, field + 4, 0);
        require(bool(decode(bytes, 0x2f)), "Null empty name rejected");
    }
    bytes = original;
    word(bytes, 526, 900);
    word(bytes, 530, 4);
    require(bool(decode(bytes, 0x2f)), "Shared counted resource rejected");
    for (unsigned byte = 0; byte < 256; ++byte) {
        bytes = original;
        bytes[900] = static_cast<std::uint8_t>(byte);
        bytes[910] = static_cast<std::uint8_t>(255 - byte);
        value = decode(bytes, 0x2f);
        require(value && value->blobs[0].bytes[0] == byte && value->blobs[1].bytes[0] == 255 - byte,
                "Name byte domain restricted");
    }
    bytes = original;
    word(bytes, 514, 2);
    require(!decode(bytes, 0x2f), "Name version conflict accepted");
    bytes = original;
    word(bytes, 522, 65537);
    require(!decode(bytes, 0x2f), "Name safety cap bypassed");
    bytes = original;
    word(bytes, 264, 512);
    require(!decode(bytes, 0x2f), "Aliased descriptor accepted");
    const auto nav = fixture(0x33);
    for (unsigned bit = 0; bit < 32; ++bit) {
        bytes = nav;
        for (unsigned field = 0; field < 3; ++field) {
            word(bytes, 518 + 4 * field, std::uint32_t{1} << bit);
        }
        value = decode(bytes, 0x33);
        require(value && value->extent == 18 && value->blobs.empty() && value->scalars.size() == 3,
                "Navigation schema changed");
        for (const auto& scalar : value->scalars) {
            require(scalar.value == (std::uint32_t{1} << bit) && scalar.width == 4,
                    "Navigation word narrowed");
        }
    }
    bytes = nav;
    word(bytes, 320 + 8, 529);
    require(!decode(bytes, 0x33), "Definition inside navigation descriptor accepted");
    require(
        !game_assets::parse_stored_fields(nav, game_assets::parse_database_index(nav), 0x34, 200),
        "Unsupported class decoded");
    const auto enabled = fixture(0x46);
    const std::pair<std::uint32_t, std::vector<unsigned>> schemas[] = {
        {0x2b, {4, 4, 4, 4, 4, 4}},
        {0x2c, {4}},
        {0x37, {4, 2, 2, 2, 2, 4}},
        {0x39, {4, 2, 2, 2, 2, 4}},
        {0x3c, {4, 1}},
        {0x3d, {2, 2, 4, 4}},
        {0x4c, {4, 4}},
        {0x4f, {4, 2, 2, 2, 2, 4, 4, 2, 2}},
        {0x47, {4, 4, 4, 4, 4, 4, 4}},
        {0x48, {4, 4, 4, 4, 4, 4, 4}},
        {0x49, {4, 4, 4, 4, 4, 4, 4}},
        {0x4a, {4, 4, 4, 4, 4, 4, 4}},
        {0x4b, {4, 4, 4, 4, 4, 4, 4}},
        {0x3e, {4, 4, 4}},
        {0x40, {4, 4, 4}}};
    for (const auto& [cls, widths] : schemas) {
        for (unsigned bit = 0; bit < 32; ++bit) {
            bytes = fixture(cls);
            unsigned at = 6;
            for (const auto width : widths) {
                const auto bits = ~(std::uint32_t{1} << bit);
                for (unsigned byte = 0; byte < width; ++byte) {
                    bytes[512 + at + byte] =
                        static_cast<std::uint8_t>(bits >> (8 * (width - byte - 1)));
                }
                at += width;
            }
            value = decode(bytes, cls);
            require(value && value->extent == at && value->scalars.size() == widths.size(),
                    "Fixed field widths or extent changed");
            at = 6;
            for (std::size_t i = 0; i < widths.size(); ++i) {
                const auto mask = widths[i] == 4 ? 0xffffffffu : (1u << (8 * widths[i])) - 1;
                require(value->scalars[i].offset == at && value->scalars[i].width == widths[i] &&
                            value->scalars[i].value == (~(std::uint32_t{1} << bit) & mask),
                        "Fixed field unsigned basis narrowed");
                at += widths[i];
            }
        }
        bytes = fixture(cls);
        word(bytes, 328, 512 + value->extent - 1);
        require(!decode(bytes, cls), "Fixed field boundary crossed indexed definition");
        bytes = fixture(cls);
        word(bytes, 514, 2);
        require(!decode(bytes, cls), "Fixed field persistent version accepted");
        word(bytes, 514, 1);
        word(bytes, 0, 4);
        require(!decode(bytes, cls), "Format version gate confused with persistent version");
    }
    bytes = fixture(0x50);
    value = decode(bytes, 0x50);
    require(value && value->extent == 14 && value->scalars.empty() && value->blobs.size() == 1 &&
                value->blobs[0].bytes == std::vector<std::uint8_t>({'n', 0xff, 0, 'x'}),
            "Stored string counted resource changed");
    for (unsigned byte = 0; byte < 256; ++byte) {
        bytes = enabled;
        word(bytes, 518, 0xffffffff);
        bytes[522] = static_cast<std::uint8_t>(byte);
        bytes[523] = static_cast<std::uint8_t>(255 - byte);
        value = decode(bytes, 0x46);
        require(value && value->extent == 12 && value->scalars.size() == 3 &&
                    value->scalars[0].value == 0xffffffff && value->scalars[1].value == byte &&
                    value->scalars[2].value == 255 - byte && value->scalars[2].width == 1,
                "Enabled ID/class/raw byte narrowed");
    }
    const auto scene = [&](std::uint32_t cls) {
        auto data = stored_list_fixture::make();
        data.resize(2048);
        word(data, 86, cls);
        word(data, 118, cls);
        word(data, 296, 900);
        std::copy_n(data.begin() + 512, 32, data.begin() + 900);
        return data;
    };
    for (const auto cls : {0x27u, 0x28u, 0x29u, 0x2au}) {
        bytes = scene(cls);
        value = decode(bytes, cls);
        require(value && value->list && value->list->ids.size() == 5,
                "Composite scene lost ordered list");
        const auto valid = bytes;
        word(bytes, 910, 930);
        require(!decode(bytes, cls), "List resource overlaps scene suffix");
        bytes = valid;
        word(bytes, 1032, 900);
        bytes[1024] = 0x43;
        require(!decode(bytes, cls), "Composite scene list cycle accepted");
        if (cls == 0x27) {
            bytes = valid;
            word(bytes, 932, 1024);
            word(bytes, 936, 4);
            require(!decode(bytes, cls), "Counted string overlaps list resource");
            word(bytes, 932, 1500);
            require(bool(decode(bytes, cls)), "Disjoint scene string rejected");
            word(bytes, 936, 65537);
            require(!decode(bytes, cls), "Invalid scene string retained partial list");
        }
    }
    for (const auto cls : {0x56u, 0x58u, 0x5au, 0x5bu, 0x5cu}) {
        for (unsigned length = 0; length < 12; ++length) {
            bytes = scene(cls);
            word(bytes, 906, 1500);
            word(bytes, 910, length);
            for (unsigned i = 0; i < length; ++i) {
                bytes[1500 + i] = static_cast<std::uint8_t>(0x81 + i);
            }
            value = decode(bytes, cls);
            require(value && value->blobs[0].word_array && value->blobs[0].bytes.size() == length &&
                        value->blobs[0].little_endian_words.size() == length / 4,
                    "LE32 array lost trailing raw bytes");
            if (length >= 4) {
                require(value->blobs[0].little_endian_words[0] == 0x84838281,
                        "Raw array swapped native little-endian words");
            }
        }
    }
    bytes = scene(0x57);
    std::fill(bytes.begin() + 906, bytes.begin() + 1032, std::uint8_t{0});
    for (unsigned i = 0; i < 5; ++i) {
        word(bytes, 966 + 8 * i, 1500 + i * 16);
        word(bytes, 970 + 8 * i, 7);
    }
    bytes[1030] = 0xff;
    bytes[1031] = 0xff;
    value = decode(bytes, 0x57);
    require(value && value->extent == 132 && value->blobs.size() == 5 &&
                value->scalars.back().offset == 130 && value->scalars.back().value == 65535,
            "GameState unaligned suffix or extent changed");
    for (const auto& blob : value->blobs) {
        require(!blob.word_array && blob.little_endian_words.empty() && blob.bytes.size() == 7,
                "Opaque GameState resource interpreted as words");
    }
    word(bytes, 998, 0);
    require(!decode(bytes, 0x57), "Last GameState resource escaped validation");
    require(original == fixture(0x2f), "Input changed");
    std::cout << "Synthetic stored field checks passed\n";
    for (int argument = 1; argument < argc; ++argument) {
        const auto database = game_assets::Database::load(std::filesystem::path(argv[argument]));
        const auto index = game_assets::parse_database_index(database.bytes());
        require(index.available && !index.truncated && !index.skipped_nodes &&
                    !index.duplicate_keys && !index.unsupported_indexes,
                "Installed index incomplete");
        std::size_t count = 0;
        for (const auto& record : index.records) {
            if (!game_assets::supports_stored_fields(record.class_id)) {
                continue;
            }
            std::cout << "FIELDS\t" << argument << '\t' << record.class_id << '\t' << record.id
                      << '\t';
            value = game_assets::parse_stored_fields(database.bytes(), index, record.class_id,
                                                     record.id);
            if (!value) {
                std::cout << "RAW\n";
                continue;
            }
            ++count;
            std::cout << value->offset << '\t';
            dump(database.bytes().subspan(value->offset, value->extent));
            std::cout << '\t';
            for (const auto& scalar : value->scalars) {
                std::cout << scalar.offset << ':' << scalar.width << ':' << scalar.value << ',';
            }
            for (const auto& blob : value->blobs) {
                std::cout << '\t' << blob.mark << ':';
                dump(blob.bytes);
            }
            std::cout << '\t';
            if (value->list) {
                for (const auto member : value->list->ids) {
                    std::cout << member << ',';
                }
            }
            std::cout << '\t';
            if (value->list) {
                for (const auto& node : value->list->nodes) {
                    std::cout << node.offset << ':';
                    dump(database.bytes().subspan(node.offset, node.extent));
                    std::cout << ',';
                }
            }
            std::cout << '\t';
            for (const auto& blob : value->blobs) {
                std::cout << (blob.word_array ? 'W' : 'B') << ':';
                for (const auto word : blob.little_endian_words) {
                    std::cout << word << ',';
                }
                std::cout << ';';
            }
            std::cout << '\n';
        }
        std::cout << "FILE\t" << argument << '\t' << index.records.size() << '\t' << count << '\n';
    }
}
