#include "game/assets/xt_text.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

using Bytes = std::vector<std::uint8_t>;

void decoded(const Bytes& bytes, game_assets::XtEncoding encoding, const std::wstring& text) {
    const auto result = game_assets::parse_xt_text(bytes);
    require(result.valid && result.encoding == encoding && result.text == text &&
                result.file_size == bytes.size() && result.raw == bytes && !result.status.empty(),
            "Text decoding or metadata mismatch");
}

void invalid(const Bytes& bytes, game_assets::XtEncoding encoding) {
    const auto result = game_assets::parse_xt_text(bytes);
    require(!result.valid && result.encoding == encoding && result.text.empty() &&
                result.status.find(L"byte") != std::wstring::npos && result.raw == bytes,
            "Malformed text was accepted or silently truncated");
}

struct TempFile {
    std::filesystem::path root, path;

    TempFile() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() / ("xfiles-xt-" + std::to_string(stamp));
        require(std::filesystem::create_directory(root), "Temporary fixture creation failed");
        path = root / "test.XTX";
    }

    void write(const Bytes& bytes) const {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!bytes.empty()) {
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
        }
        require(bool(file), "Fixture write failed");
    }

    ~TempFile() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

void parser_checks() {
    using namespace game_assets;
    decoded({}, XtEncoding::Ascii, L"");
    decoded({'A', '\r', '\n', '\t', 'B', '\n', 'C', '\r'}, XtEncoding::Ascii, L"A\r\n\tB\nC\r");
    decoded({0x91, 'A', 0x92, 0x93, 0xe9, 0x94}, XtEncoding::Windows1252,
            L"\u2018A\u2019\u201c\u00e9\u201d");
    decoded({0xc3, 0xa9}, XtEncoding::Windows1252, L"\u00c3\u00a9");
    decoded({0xef, 0xbb, 0xbf, 'A', 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80}, XtEncoding::Utf8,
            L"A\u00e9\U0001f600");
    decoded({0xff, 0xfe, 'A', 0, 0xe9, 0, 0x3d, 0xd8, 0, 0xde}, XtEncoding::Utf16Le,
            L"A\u00e9\U0001f600");
    decoded({0xfe, 0xff, 0, 'A', 0, 0xe9, 0xd8, 0x3d, 0xde, 0}, XtEncoding::Utf16Be,
            L"A\u00e9\U0001f600");
    decoded({0xff, 0xfe, 0, 0, 'A', 0, 0, 0, 0, 0xf6, 1, 0}, XtEncoding::Utf32Le, L"A\U0001f600");
    decoded({0, 0, 0xfe, 0xff, 0, 0, 0, 'A', 0, 1, 0xf6, 0}, XtEncoding::Utf32Be, L"A\U0001f600");
    for (const auto& bytes : {Bytes{0xef, 0xbb, 0xbf}, Bytes{0xff, 0xfe}, Bytes{0xfe, 0xff},
                              Bytes{0xff, 0xfe, 0, 0}, Bytes{0, 0, 0xfe, 0xff}}) {
        require(parse_xt_text(bytes).valid && parse_xt_text(bytes).text.empty(),
                "BOM-only file rejected");
    }
    for (std::uint8_t byte : Bytes{0, 1, 8, 11, 12, 31, 127}) {
        invalid({'A', byte, 'B'}, XtEncoding::Ascii);
    }
    for (std::uint8_t byte : Bytes{0x81, 0x8d, 0x8f, 0x90, 0x9d}) {
        invalid({'A', byte, 'B'}, XtEncoding::Windows1252);
    }
    for (const auto& tail : {Bytes{0xc0, 0xaf}, Bytes{0xe0, 0x80, 0x80}, Bytes{0xed, 0xa0, 0x80},
                             Bytes{0xf4, 0x90, 0x80, 0x80}, Bytes{0xf5, 0x80, 0x80, 0x80},
                             Bytes{0x80}, Bytes{0xc2}, Bytes{0xe2, 0x82}, Bytes{0xf0, 0x9f, 0x98},
                             Bytes{0xc2, 'A'}, Bytes{0xc2, 0x80}, Bytes{0}, Bytes{0xff}}) {
        Bytes bytes{0xef, 0xbb, 0xbf, 'A'};
        bytes.insert(bytes.end(), tail.begin(), tail.end());
        invalid(bytes, XtEncoding::Utf8);
    }
    for (const auto& tail : {Bytes{'A'}, Bytes{0, 0xd8}, Bytes{0, 0xdc}, Bytes{0, 0xd8, 'B', 0},
                             Bytes{0, 0xd8, 0, 0xd8}, Bytes{0, 0}}) {
        Bytes bytes{0xff, 0xfe, 'A', 0};
        bytes.insert(bytes.end(), tail.begin(), tail.end());
        invalid(bytes, XtEncoding::Utf16Le);
    }
    invalid({0xfe, 0xff, 0, 'A', 0xd8, 0, 0, 'B'}, XtEncoding::Utf16Be);
    for (const auto& tail : {Bytes{0}, Bytes{0, 0}, Bytes{0, 0, 0}, Bytes{0, 0, 0, 0},
                             Bytes{0, 0xd8, 0, 0}, Bytes{0, 0, 0x11, 0}}) {
        Bytes bytes{0xff, 0xfe, 0, 0, 'A', 0, 0, 0};
        bytes.insert(bytes.end(), tail.begin(), tail.end());
        invalid(bytes, XtEncoding::Utf32Le);
    }
    invalid({0, 0, 0xfe, 0xff, 0, 0x11, 0, 0}, XtEncoding::Utf32Be);
    Bytes maximum(xt_text_limit, 'A');
    auto result = parse_xt_text(maximum);
    require(result.valid && result.text.size() == xt_text_limit &&
                result.raw.size() == xt_text_raw_limit,
            "Text safety limit boundary failed");
    maximum.push_back('A');
    result = parse_xt_text(maximum);
    require(!result.valid && result.text.empty() && result.file_size == maximum.size() &&
                result.raw.size() == xt_text_raw_limit,
            "Oversized text accepted or raw prefix unbounded");
    TempFile file;
    require(!load_xt_text(file.path).valid, "Missing text file accepted");
    file.write({});
    require(load_xt_text(file.path).valid, "Empty text file loader failed");
    file.write({0x93, 'A', 0x94});
    result = load_xt_text(file.path);
    require(result.valid && result.text == L"\u201cA\u201d" && result.file_size == 3,
            "Windows-1252 file loader failed");
    file.write({'A', 0, 'B'});
    require(!load_xt_text(file.path).valid, "Embedded NUL file silently truncated");
    file.write(maximum);
    result = load_xt_text(file.path);
    require(!result.valid && result.text.empty() && result.file_size == maximum.size() &&
                result.raw.size() == xt_text_raw_limit,
            "File size safety limit failed");
}

void inventory(const std::filesystem::path& root) {
    using namespace game_assets;
    std::size_t files = 0, windows1252 = 0, total = 0, maximum = 0;
    for (const auto& file : std::filesystem::recursive_directory_iterator(root)) {
        auto extension = file.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (!file.is_regular_file() || (extension != ".xt" && extension != ".xtx")) {
            continue;
        }
        const auto result = load_xt_text(file.path());
        if (!result.valid) {
            std::cerr << file.path() << '\n';
        }
        require(result.valid, "Installed XT text failed parser validation");
        require(result.encoding == XtEncoding::Ascii || result.encoding == XtEncoding::Windows1252,
                "Installed encoding changed");
        std::ifstream input(file.path(), std::ios::binary);
        const Bytes bytes((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
        require(result.text.size() == bytes.size() && result.file_size == bytes.size() &&
                    result.raw.size() == std::min(bytes.size(), xt_text_raw_limit),
                "Installed text coverage mismatch");
        for (std::size_t at = 0; at < bytes.size(); ++at) {
            auto expected = static_cast<wchar_t>(bytes[at]);
            switch (bytes[at]) {
                case 0x91:
                    expected = L'\u2018';
                    break;
                case 0x92:
                    expected = L'\u2019';
                    break;
                case 0x93:
                    expected = L'\u201c';
                    break;
                case 0x94:
                    expected = L'\u201d';
                    break;
                default:
                    require(bytes[at] < 0x80 || bytes[at] == 0xe9, "New high byte in inventory");
            }
            require(result.text[at] == expected, "Installed text character mismatch");
        }
        ++files;
        windows1252 += result.encoding == XtEncoding::Windows1252 ? 1 : 0;
        total += bytes.size();
        maximum = std::max(maximum, bytes.size());
    }
    require(files == 467 && windows1252 == 7 && total == 169484 && maximum == 4122,
            "Installed XT inventory changed");
    std::cout << root << ": " << files << " XT files, " << windows1252 << " Windows-1252, " << total
              << " bytes, max " << maximum << '\n';
}
}

int main(int argc, char** argv) {
    try {
        parser_checks();
        for (int at = 1; at < argc; ++at) {
            inventory(argv[at]);
        }
    } catch (const std::exception& error) {
        std::cerr << "XT text test failed: " << error.what() << '\n';
        return 1;
    }
}
