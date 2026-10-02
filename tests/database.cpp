#include "game/database/database.h"
#include <array>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

void require(bool value) {
    if (!value) {
        throw std::runtime_error("Database byte check failed");
    }
}

int main(int argc, char** argv) {
    using game_assets::Database;
    std::vector<std::uint8_t> bytes(320);
    bytes[3] = 5;
    bytes[30] = 1;
    const std::array<std::uint8_t, 7> text{'X', 'V', 0x7f, '1', '.', 'x', 0};
    std::copy(text.begin(), text.end(), bytes.begin() + 35);
    bytes[319] = 'X';
    const auto database = Database::parse(bytes);
    require(database.header()[0] == 5 && database.header()[7] == 256);
    require(database.strings().size() == 1);
    require(database.strings()[0].offset == 35 && database.strings()[0].size == 7);
    require(database.strings()[0].text == "XV/1.x");
    require(database.hex(35, 7).find(L"00000023") != std::wstring::npos);
    require(database.hex(319, std::numeric_limits<std::size_t>::max()).find(L"58") !=
            std::wstring::npos);
    require(database.hex(320) == L"Offset is outside the database.");
    auto multiline = bytes;
    const std::string html =
        "<HTML>\n\t<A HREF=\"messages/digest564\">\r\nOffice Tracker</A>\r</HTML>";
    std::copy(html.begin(), html.end(), multiline.begin() + 64);
    multiline[64 + html.size()] = 0xc2;
    const auto fragments = Database::parse(multiline).strings();
    require(fragments.size() == 2 && fragments[1].offset == 64 &&
            fragments[1].size == html.size() && fragments[1].text == html);
    require(Database::parse(multiline).bytes()[64 + html.size()] == 0xc2);
    for (const auto size : {0u, 31u}) {
        bool failed = false;
        try {
            Database::parse(std::span(bytes).first(size));
        } catch (const std::runtime_error&) {
            failed = true;
        }
        require(failed);
    }
    bytes[3] = 6;
    bool failed = false;
    try {
        Database::parse(bytes);
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed);
    for (int argument = 1; argument < argc; ++argument) {
        const auto actual = Database::load(argv[argument]);
        require(actual.header()[0] == 5 && actual.header()[7] == 256);
        require(std::any_of(actual.strings().begin(), actual.strings().end(), [](const auto& item) {
            return item.text.find("19808.xmv") != std::string::npos;
        }));
        std::cout << argv[argument] << ": " << actual.size() << " bytes, "
                  << actual.strings().size() << " text candidates\n";
    }
}
