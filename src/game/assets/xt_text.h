#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace game_assets {
enum class XtEncoding { Unknown, Ascii, Windows1252, Utf8, Utf16Le, Utf16Be, Utf32Le, Utf32Be };

struct XtText {
    bool valid = false;
    std::wstring status;
    XtEncoding encoding = XtEncoding::Unknown;
    std::uint64_t file_size = 0;
    std::wstring text;
    std::vector<std::uint8_t> raw;
};

inline constexpr std::size_t xt_text_limit = 1024 * 1024;
inline constexpr std::size_t xt_text_raw_limit = 4096;
const wchar_t* xt_encoding_name(XtEncoding encoding);
XtText parse_xt_text(std::span<const std::uint8_t> bytes);
XtText load_xt_text(const std::filesystem::path& path);
}
