#include "clip_notes.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace devtools {
namespace {

constexpr std::string_view header = "xfiles-clip-notes-v1\n";
constexpr std::size_t max_file_bytes = 4 * 1024 * 1024;
constexpr std::size_t max_entries = 10000;
constexpr std::size_t max_key_chars = 1024;
constexpr std::size_t max_label_chars = 1024;
constexpr std::size_t max_notes_chars = 65536;

std::string utf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Clip note text is too long");
    }
    const auto length =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!length) {
        throw std::runtime_error("Clip note contains invalid Unicode");
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length, nullptr,
                            nullptr) != length) {
        throw std::runtime_error("Could not encode clip note");
    }
    return result;
}

std::wstring wide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Clip note field is too long");
    }
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
    if (!length) {
        throw std::runtime_error("Clip notes file contains invalid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length) != length) {
        throw std::runtime_error("Could not decode clip notes file");
    }
    return result;
}

std::string escape(std::wstring_view value) {
    const auto encoded = utf8(value);
    std::string result;
    result.reserve(encoded.size());
    for (const char ch : encoded) {
        switch (ch) {
            case '\\':
                result += "\\\\";
                break;
            case '\t':
                result += "\\t";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            default:
                result += ch;
                break;
        }
    }
    return result;
}

std::wstring unescape(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '\\') {
            if (++i == value.size()) {
                throw std::runtime_error("Truncated clip notes escape");
            }
            switch (value[i]) {
                case '\\':
                    decoded += '\\';
                    break;
                case 't':
                    decoded += '\t';
                    break;
                case 'n':
                    decoded += '\n';
                    break;
                case 'r':
                    decoded += '\r';
                    break;
                default:
                    throw std::runtime_error("Unknown clip notes escape");
            }
        } else {
            if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7f) {
                throw std::runtime_error("Unescaped control in clip notes file");
            }
            decoded += ch;
        }
    }
    return wide(decoded);
}

void check_note(const ClipNote& note) {
    if (note.label.size() > max_label_chars || note.notes.size() > max_notes_chars) {
        throw std::runtime_error("Clip note exceeds size limit");
    }
    for (const auto* field : {&note.label, &note.notes}) {
        for (const wchar_t ch : *field) {
            if ((ch < 0x20 && ch != L'\t' && ch != L'\n' && ch != L'\r') || ch == 0x7f) {
                throw std::runtime_error("Clip note contains unsupported control character");
            }
        }
    }
    utf8(note.label);
    utf8(note.notes);
}

void write_atomic(const std::filesystem::path& file, const std::string& bytes) {
    const auto parent = file.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    static std::atomic<unsigned> sequence{0};
    std::filesystem::path temporary;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        temporary = file;
        temporary +=
            L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            break;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) {
            throw std::runtime_error("Could not create temporary clip notes file");
        }
    }
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Could not reserve temporary clip notes file");
    }
    DWORD written = 0;
    const bool complete =
        WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(handle);
    const bool closed = CloseHandle(handle) != 0;
    if (!complete || !closed ||
        !MoveFileExW(temporary.c_str(), file.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Could not replace clip notes file");
    }
}

}

std::wstring ClipNotes::key(const std::filesystem::path& relative_movie) {
    if (relative_movie.empty() || relative_movie.is_absolute() || relative_movie.has_root_name()) {
        throw std::invalid_argument("Clip movie path must be relative");
    }
    std::filesystem::path normalized;
    for (const auto& part : relative_movie) {
        if (part == L".." || part == L"." || part.empty()) {
            throw std::invalid_argument("Clip movie path contains invalid component");
        }
        normalized /= part;
    }
    auto stem = normalized.stem().wstring();
    if (!stem.empty() && stem.find_first_not_of(L"0123456789") == std::wstring::npos) {
        const auto first = stem.find_first_not_of(L'0');
        stem = first == std::wstring::npos ? L"0" : stem.substr(first);
    }
    const auto parent = normalized.parent_path().generic_wstring();
    auto result = parent.empty() ? stem + normalized.extension().wstring()
                                 : parent + L"/" + stem + normalized.extension().wstring();
    if (result.empty() || result.size() > max_key_chars) {
        throw std::invalid_argument("Clip movie key is invalid or too long");
    }
    for (const wchar_t ch : result) {
        if (ch < 0x20 || ch == 0x7f) {
            throw std::invalid_argument("Clip movie key contains control character");
        }
    }
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return std::towlower(ch); });
    utf8(result);
    return result;
}

ClipNotes ClipNotes::load(const std::filesystem::path& file) {
    std::error_code error;
    const auto status = std::filesystem::status(file, error);
    if (error == std::errc::no_such_file_or_directory ||
        (!error && status.type() == std::filesystem::file_type::not_found)) {
        return {};
    }
    if (error || !std::filesystem::is_regular_file(status)) {
        throw std::runtime_error("Could not inspect clip notes file");
    }
    const auto size = std::filesystem::file_size(file, error);
    if (error || size > max_file_bytes) {
        throw std::runtime_error("Clip notes file is unreadable or too large");
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open clip notes file");
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        throw std::runtime_error("Could not read complete clip notes file");
    }
    if (!bytes.starts_with(header)) {
        throw std::runtime_error("Unrecognized clip notes file header");
    }
    ClipNotes notes;
    auto cursor = header.size();
    while (cursor < bytes.size()) {
        const auto end = bytes.find('\n', cursor);
        if (end == std::string::npos) {
            throw std::runtime_error("Truncated clip notes row");
        }
        const std::string_view row(bytes.data() + cursor, end - cursor);
        const auto first = row.find('\t');
        const auto second = first == std::string_view::npos ? first : row.find('\t', first + 1);
        if (first == std::string_view::npos || second == std::string_view::npos ||
            row.find('\t', second + 1) != std::string_view::npos) {
            throw std::runtime_error("Malformed clip notes row");
        }
        const auto movie = unescape(row.substr(0, first));
        const auto canonical = key(std::filesystem::path(movie));
        const ClipNote note{unescape(row.substr(first + 1, second - first - 1)),
                            unescape(row.substr(second + 1))};
        check_note(note);
        if ((note.label.empty() && note.notes.empty()) ||
            !notes.entries_.emplace(canonical, note).second ||
            notes.entries_.size() > max_entries) {
            throw std::runtime_error("Empty, duplicate, or excessive clip notes row");
        }
        cursor = end + 1;
    }
    return notes;
}

std::optional<ClipNote> ClipNotes::get(const std::filesystem::path& relative_movie) const {
    const auto found = entries_.find(key(relative_movie));
    return found == entries_.end() ? std::nullopt : std::optional<ClipNote>(found->second);
}

void ClipNotes::set(const std::filesystem::path& relative_movie, std::wstring label,
                    std::wstring notes) {
    const auto canonical = key(relative_movie);
    ClipNote note{std::move(label), std::move(notes)};
    check_note(note);
    if (note.label.empty() && note.notes.empty()) {
        entries_.erase(canonical);
    } else {
        if (!entries_.contains(canonical) && entries_.size() >= max_entries) {
            throw std::runtime_error("Too many clip notes");
        }
        entries_[canonical] = std::move(note);
    }
}

void ClipNotes::save(const std::filesystem::path& file) const {
    std::string bytes(header);
    for (const auto& [movie, note] : entries_) {
        bytes += escape(movie);
        bytes += '\t';
        bytes += escape(note.label);
        bytes += '\t';
        bytes += escape(note.notes);
        bytes += '\n';
        if (bytes.size() > max_file_bytes) {
            throw std::runtime_error("Clip notes file would exceed size limit");
        }
    }
    write_atomic(file, bytes);
}

}
