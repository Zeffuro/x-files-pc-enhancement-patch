#include "database_conversation_fixture.h"
#include "game/database/database.h"
#include "game/database/stored_conversation.h"
#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

auto conversation(const std::vector<std::uint8_t>& bytes, std::uint32_t cls = 0x31,
                  std::uint32_t id = 300) {
    return game_assets::parse_stored_conversation(bytes, game_assets::parse_database_index(bytes),
                                                  cls, id);
}

auto conversations(const std::vector<std::uint8_t>& bytes, std::uint32_t cls = 0x2d,
                   std::uint32_t id = 400) {
    return game_assets::parse_stored_conversations(bytes, game_assets::parse_database_index(bytes),
                                                   cls, id);
}

void check_conversation() {
    using namespace stored_list_fixture;
    const auto original = database_conversation_fixture::make();
    auto bytes = original;
    auto value = conversation(bytes);
    require(value && value->id == 300 && value->offset == 640 && value->name_id == 200 &&
                value->icons_id == 0x80000001 && value->responses_id == 0xffffffff &&
                value->triggers_id == 0x80000002 && value->question_id == 0x80000003 &&
                value->atoms_id == 0 && value->raw_flag == 254 && value->name_available &&
                value->name == std::vector<std::uint8_t>({'A', 0, '\\', 0x80, 0xff}),
            "Conversation field roles or counted name bytes changed");
    for (unsigned flag = 0; flag < 256; ++flag) {
        bytes[670] = static_cast<std::uint8_t>(flag);
        value = conversation(bytes);
        require(value && value->raw_flag == flag, "Conversation raw flag narrowed to Boolean");
    }
    for (unsigned bit = 0; bit < 32; ++bit) {
        bytes = original;
        for (unsigned at = 646; at <= 666; at += 4) {
            word(bytes, at, std::uint32_t{1} << bit);
        }
        value = conversation(bytes);
        const auto expected = std::uint32_t{1} << bit;
        require(value && value->name_id == expected && value->icons_id == expected &&
                    value->responses_id == expected && value->triggers_id == expected &&
                    value->question_id == expected && value->atoms_id == expected,
                "Conversation reference word narrowed");
    }
    for (const auto id : {0u, 999u}) {
        bytes = original;
        word(bytes, 646, id);
        value = conversation(bytes);
        require(value && value->name_id == id && !value->name_available && value->name.empty(),
                "Null or missing title invented");
    }
    bytes = original;
    word(bytes, 518, 0);
    word(bytes, 522, 0);
    value = conversation(bytes);
    require(value && value->name_available && value->name.empty(),
            "Valid counted empty name treated as unavailable");
    bytes = original;
    word(bytes, 86, 0x50);
    word(bytes, 118, 0x50);
    value = conversation(bytes);
    require(value && !value->name_available, "Wrong-class title fallback accepted");
    bytes = original;
    word(bytes, 308, 200);
    value = conversation(bytes);
    require(value && value->name_available, "Same ID in another class made name ambiguous");
    bytes = original;
    bytes[295] = 2;
    word(bytes, 304, 800);
    word(bytes, 308, 200);
    bytes[800] = 0xc3;
    word(bytes, 802, 1);
    value = conversation(bytes);
    require(value && !value->name_available, "Ambiguous same-class title resolved");
    bytes = original;
    word(bytes, 272, 512);
    value = conversation(bytes);
    require(value && !value->name_available, "Aliased title descriptor resolved");
    for (const auto at : {514u, 526u, 530u}) {
        bytes = original;
        word(bytes, at, at == 530 ? 65537 : 2);
        value = conversation(bytes);
        require(value && !value->name_available, "Malformed title resolved");
    }
    for (const auto cls : {0x2fu, 0x2du, 0x32u, 0x36u, 0xffffffffu}) {
        require(!conversation(original, cls), "Wrong conversation class accepted");
    }
    for (const auto id : {0u, 999u}) {
        require(!conversation(original, 0x31, id), "Missing conversation accepted");
    }
    bytes = original;
    word(bytes, 642, 2);
    require(!conversation(bytes), "Conversation persistent version accepted");
    bytes = original;
    word(bytes, 272, 665);
    require(!conversation(bytes), "Conversation crossed indexed descriptor");
    bytes = original;
    word(bytes, 276, 300);
    require(!conversation(bytes), "Ambiguous conversation resolved");
    bytes = original;
    word(bytes, 0, 4);
    require(!conversation(bytes), "Database version accepted");
    const auto index = game_assets::parse_database_index(original);
    value =
        game_assets::parse_stored_conversation(std::span(original).first(918), index, 0x31, 300);
    require(value && !value->name_available, "Truncated second name blob escaped validation");
    for (std::size_t end = 0; end < 671; ++end) {
        require(!game_assets::parse_stored_conversation(std::span(original).first(end), index, 0x31,
                                                        300),
                "Truncated conversation accepted");
    }
    require(original == database_conversation_fixture::make(), "Conversation input changed");
}

void check_lists() {
    using namespace stored_list_fixture;
    const auto original = database_conversation_fixture::make();
    auto bytes = original;
    auto value = conversations(bytes);
    const std::vector<std::uint32_t> expected{300, 0, 300, 0xffffffff, 301};
    require(value && value->id == 201 && value->conversations == expected,
            "Character conversation list lost identity/order/nulls/duplicates");
    value = conversations(bytes, 0x32, 201);
    require(value && value->id == 201 && value->conversations == expected,
            "Direct conversation list differs from association");
    bytes = original;
    word(bytes, 726, 0);
    value = conversations(bytes);
    require(value && value->id == 0 && value->conversations.empty(),
            "Null character association not an empty list");
    bytes = original;
    word(bytes, 726, 999);
    require(!conversations(bytes), "Missing associated list accepted");
    bytes = original;
    word(bytes, 582, 0);
    word(bytes, 586, 0);
    value = conversations(bytes);
    require(value && value->id == 201 && value->conversations.empty(),
            "Stored empty list lost its identity");
    bytes = original;
    word(bytes, 172, 0x36);
    word(bytes, 204, 0x36);
    require(!conversations(bytes), "Wrong-class associated list accepted");
    for (const auto at : {578u, 582u, 590u, 1026u}) {
        bytes = original;
        word(bytes, at, 2);
        require(!conversations(bytes) && !conversations(bytes, 0x32, 201),
                "Malformed list accepted through association or direct access");
    }
    bytes = original;
    word(bytes, 706, 2);
    require(!conversations(bytes), "Character version conflict accepted");
    bytes = original;
    bytes[327] = 2;
    word(bytes, 336, 800);
    word(bytes, 340, 400);
    require(!conversations(bytes), "Ambiguous character source resolved");
    bytes = original;
    word(bytes, 272, 730);
    require(!conversations(bytes), "Character suffix crossed indexed definition");
    bytes = original;
    bytes[359] = 2;
    word(bytes, 368, 800);
    word(bytes, 372, 201);
    require(!conversations(bytes), "Ambiguous associated list resolved");
    for (const auto cls : {0x31u, 0x2fu, 0x36u, 0xffffffffu}) {
        require(!conversations(original, cls, 201), "Unsupported list source class accepted");
    }
    require(!conversations(original, 0x32, 0) && !conversations(original, 0x2d, 0) &&
                !conversations(original, 0x2d, 999),
            "Missing list source accepted");
    const auto index = game_assets::parse_database_index(original);
    for (std::size_t end = 0; end < 742; ++end) {
        require(!game_assets::parse_stored_conversations(std::span(original).first(end), index,
                                                         0x2d, 400),
                "Truncated character/list accepted");
    }
    bytes = original;
    word(bytes, 726, 0);
    const auto null_index = game_assets::parse_database_index(bytes);
    require(!game_assets::parse_stored_conversations(std::span(bytes).first(741), null_index, 0x2d,
                                                     400),
            "Truncated character suffix accepted with null association");
    require(original == database_conversation_fixture::make(), "List input changed");
}

void corpus(const std::filesystem::path& path, int argument) {
    const auto database = game_assets::Database::load(path);
    const auto index = game_assets::parse_database_index(database.bytes());
    require(index.available && !index.truncated && !index.skipped_nodes && !index.duplicate_keys &&
                !index.unsupported_indexes,
            "Installed index incomplete");
    std::size_t total = 0, readable = 0, named = 0, lists = 0, lists_readable = 0;
    std::size_t characters = 0, associations_readable = 0, null_associations = 0;
    for (const auto& record : index.records) {
        if (record.class_id == 0x31) {
            ++total;
            const auto value = game_assets::parse_stored_conversation(database.bytes(), index,
                                                                      record.class_id, record.id);
            if (value) {
                ++readable;
                named += value->name_available;
            } else if (total - readable <= 16) {
                std::cout << "CONVERSATION_RAW\t" << argument << '\t' << record.id << '\t'
                          << record.offset << '\n';
            }
        } else if (record.class_id == 0x32 || record.class_id == 0x2d) {
            const auto value = game_assets::parse_stored_conversations(database.bytes(), index,
                                                                       record.class_id, record.id);
            if (record.class_id == 0x32) {
                ++lists;
                lists_readable += bool(value);
            } else {
                ++characters;
                associations_readable += bool(value);
                null_associations += value && !value->id;
            }
        }
    }
    std::cout << "FILE\t" << argument << "\tconversations\t" << total << "\treadable\t" << readable
              << "\tnamed\t" << named << "\tlists\t" << lists << "\tlists_readable\t"
              << lists_readable << "\tcharacters\t" << characters << "\tassociations_readable\t"
              << associations_readable << "\tnull_associations\t" << null_associations << '\n';
}
}

int main(int argc, char** argv) {
    check_conversation();
    check_lists();
    std::cout << "Synthetic stored conversation checks passed\n";
    for (int argument = 1; argument < argc; ++argument) {
        corpus(std::filesystem::path(argv[argument]), argument);
    }
}
