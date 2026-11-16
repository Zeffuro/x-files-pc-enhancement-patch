#include "browser_state.h"
#include "asset_io.h"
#include <commdlg.h>
#include <algorithm>
#include <climits>
#include <stdexcept>

namespace devtools::database_browser {
namespace {
std::string json_quote(const std::wstring& text) {
    if (text.size() > INT_MAX) {
        throw std::runtime_error("Reference text is too large");
    }
    const auto size = static_cast<int>(text.size());
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
                                           nullptr, 0, nullptr, nullptr);
    if (size && !count) {
        throw std::runtime_error("Reference text contains invalid Unicode");
    }
    std::string utf8(static_cast<std::size_t>(count), '\0');
    if (count && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, utf8.data(),
                                      count, nullptr, nullptr)) {
        throw std::runtime_error("Cannot encode reference text");
    }
    std::string result = "\"";
    constexpr char digits[] = "0123456789abcdef";
    for (const auto byte : utf8) {
        const auto ch = static_cast<unsigned char>(byte);
        if (ch == '"' || ch == '\\') {
            result += '\\';
            result += byte;
        } else if (ch < 32) {
            result += "\\u00";
            result += digits[ch >> 4];
            result += digits[ch & 15];
        } else {
            result += byte;
        }
    }
    return result + '"';
}

template <typename T> std::string number(const std::optional<T>& value) {
    return value ? std::to_string(*value) : "null";
}

const char* boolean(bool value) {
    return value ? "true" : "false";
}

std::string record(const Browser& state, std::uint32_t cls, std::uint32_t id) {
    const auto found = game_assets::find_database_record(state.stored, cls, id);
    return "{\"class_id\":" + std::to_string(cls) + ",\"record_id\":" + std::to_string(id) +
           ",\"offset\":" + (found ? std::to_string(state.stored.records[*found].offset) : "null") +
           "}";
}

std::string record(const Browser& state, std::uint32_t cls,
                   const std::optional<std::uint32_t>& id) {
    return id ? record(state, cls, *id) : "null";
}

bool unresolved(const ConversationRow& row) {
    const auto& status = row.columns[3];
    return status == L"Missing / unsupported" || status == L"Missing file" ||
           status == L"Null entry" || status == L"No movie path";
}
}

std::string conversation_reference_json(const Browser& state) {
    if (!state.database || !state.conversation_source) {
        throw std::runtime_error("Select a conversation, character or conversation list to export");
    }
    const bool index_incomplete = !state.stored.available || state.stored.truncated ||
                                  state.stored.skipped_nodes || state.stored.unsupported_indexes;
    const bool has_unresolved =
        std::any_of(state.conversation_rows.begin(), state.conversation_rows.end(), unresolved);
    const auto source = *state.conversation_source;
    Row source_name;
    if (const auto found =
            game_assets::find_database_record(state.stored, source.class_id, source.id)) {
        conversation_row_name(state, state.stored.records[*found], source_name);
    }
    std::string result =
        "{\n  \"schema\":\"xfiles-conversation-reference-v1\",\n  \"database_path\":" +
        json_quote(state.path.wstring()) +
        " ,\n  \"source_record\":" + record(state, source.class_id, source.id) +
        ",\n  \"source_name_preview\":" +
        (source_name.columns[3].empty() ? "null" : json_quote(source_name.columns[3])) +
        ",\n  \"scope\":\"displayed rows for selected source record\","
        "\n  \"candidate_only\":true,\n  \"eligibility_evaluated\":false,"
        "\n  \"name_preview_only\":true,\n  \"display_limit\":4096,"
        "\n  \"display_limit_reached\":" +
        boolean(state.conversations_limited) +
        " ,\n  \"index_incomplete\":" + boolean(index_incomplete) +
        " ,\n  \"has_unresolved\":" + boolean(has_unresolved) + " ,\n  \"incomplete\":" +
        boolean(state.conversations_limited || index_incomplete || has_unresolved) +
        " ,\n  \"rows\":[";
    for (std::size_t index = 0; index < state.conversation_rows.size(); ++index) {
        const auto& row = state.conversation_rows[index];
        result += index ? ",\n" : "\n";
        result += "    {\"row_index\":" + std::to_string(index) +
                  ",\"role\":" + json_quote(row.columns[0]) +
                  ",\"topic_name_preview\":" + json_quote(row.columns[1]) +
                  ",\"media_or_record\":" + json_quote(row.columns[2]) +
                  ",\"availability\":" + json_quote(row.columns[3]) +
                  ",\"unresolved\":" + boolean(unresolved(row)) +
                  ",\"topic_record\":" + record(state, 0x31, row.topic_id) +
                  ",\"candidate_list_record\":" + record(state, 0x36, row.list_id) +
                  ",\"candidate_record\":" + record(state, 0x35, row.candidate_id) +
                  ",\"stored_position\":" + number(row.position) + ",\"path\":" +
                  (row.media.empty() ? "null" : json_quote(row.media.generic_wstring())) +
                  ",\"details\":" + json_quote(row.detail) + "}";
    }
    return result + "\n  ]\n}\n";
}

void save_conversation_reference(const Browser& state, const std::filesystem::path& path) {
    const auto json = conversation_reference_json(state);
    write_new_asset(path, {reinterpret_cast<const std::uint8_t*>(json.data()), json.size()});
}

void export_conversation_reference(Browser& state) {
    if (!state.database || !state.conversation_source) {
        return;
    }
    const auto source = *state.conversation_source;
    const auto suggested = L"conversation-reference-" + std::to_wstring(source.class_id) + L"-" +
                           std::to_wstring(source.id) + L".json";
    std::wstring filename(32768, L'\0');
    std::copy(suggested.begin(), suggested.end(), filename.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = state.window;
    dialog.lpstrFilter = L"JSON reference (*.json)\0*.json\0";
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrTitle = L"Export conversation reference to a new file";
    dialog.lpstrDefExt = L"json";
    dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN;
    if (!GetSaveFileNameW(&dialog)) {
        if (CommDlgExtendedError()) {
            throw std::runtime_error("Cannot open the reference export dialog");
        }
        return;
    }
    save_conversation_reference(state, filename.c_str());
    SetWindowTextW(state.status, (L"Reference saved: " + std::wstring(filename.c_str())).c_str());
}
}
