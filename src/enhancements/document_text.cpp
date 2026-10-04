#include "document_text.h"
#include "game_resources.h"
#include <algorithm>

namespace enhancements::documents {
std::optional<Document> current_document(const game::ScriptControls& controls, unsigned code_page) {
    if (controls.script_dialog || !controls.acknowledgement_buttons.empty()) {
        return std::nullopt;
    }
    const auto has = [&](unsigned id) {
        return std::find(controls.resources.begin(), controls.resources.end(), id) !=
               controls.resources.end();
    };
    Document result;
    RECT content{};
    if (has(resource::pda_message)) {
        result.resource = resource::pda_message;
        result.title = L"PDA message";
        content = {180, 40, 422, 358};
    } else if (has(resource::workstation_message) && !controls.buttons.empty()) {
        result.resource = resource::workstation_message;
        result.title = L"Message";
        content = {150, 80, 610, 455};
    } else {
        return std::nullopt;
    }
    std::vector<game::ScriptControls::Text> fields;
    for (const auto& field : controls.document_text) {
        RECT overlap{};
        if (field.bounds.bottom - field.bounds.top >= 30 &&
            field.bounds.right - field.bounds.left >= 100 &&
            IntersectRect(&overlap, &field.bounds, &content) &&
            EqualRect(&overlap, &field.bounds)) {
            fields.push_back(field);
        }
    }
    std::stable_sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) {
        return a.bounds.top == b.bounds.top ? a.bounds.left < b.bounds.left
                                            : a.bounds.top < b.bounds.top;
    });
    for (const auto& field : fields) {
        UnionRect(&result.bounds, &result.bounds, &field.bounds);
        const auto count = MultiByteToWideChar(code_page, 0, field.value.data(),
                                               static_cast<int>(field.value.size()), nullptr, 0);
        if (!count) {
            return std::nullopt;
        }
        std::wstring text(count, L'\0');
        MultiByteToWideChar(code_page, 0, field.value.data(), static_cast<int>(field.value.size()),
                            text.data(), count);
        if (!result.text.empty()) {
            result.text += L"\n\n";
        }
        result.text += text;
    }
    return result.text.empty() ? std::nullopt : std::optional{std::move(result)};
}
}
