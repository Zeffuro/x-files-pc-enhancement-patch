#pragma once
#include "game_ui.h"
#include <optional>

namespace enhancements::documents {
struct Document {
    unsigned resource = 0;
    std::wstring title;
    std::wstring text;
    RECT bounds{};

    bool operator==(const Document& other) const {
        return resource == other.resource && title == other.title && text == other.text &&
               EqualRect(&bounds, &other.bounds);
    }
};

std::optional<Document> current_document(const game::ScriptControls& controls,
                                         unsigned code_page = 1252);
}
