#include "devtools/clip_notes.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Fixture {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (L"xfiles-clip-notes-" +
         std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path french = directory / L"fr" / L"tools" / L"clip-notes.tsv";
    std::filesystem::path english = directory / L"en" / L"tools" / L"clip-notes.tsv";

    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    void write(const std::string& bytes) const {
        std::filesystem::create_directories(french.parent_path());
        std::ofstream output(french, std::ios::binary | std::ios::trunc);
        require(bool(output), "Could not create clip notes fixture");
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        require(bool(output), "Could not write clip notes fixture");
    }

    std::string read() const {
        std::ifstream input(french, std::ios::binary);
        require(bool(input), "Could not read clip notes fixture");
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
};

void expect_bad(const Fixture& fixture, const std::string& bytes) {
    fixture.write(bytes);
    try {
        (void)devtools::ClipNotes::load(fixture.french);
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Malformed clip notes file was accepted");
}

void verify() {
    Fixture fixture;
    auto notes = devtools::ClipNotes::load(fixture.french);
    require(!notes.get(L"XN/38844.xmv"), "Missing notes file should be empty");
    require(!std::filesystem::exists(fixture.french), "Load should not create a notes file");

    notes.set(L"XN/038844.XMV", L"Caf\u00e9 \u6a2a\u6d5c",
              L"First line\r\nSecond\tline\\end\n\u96ea");
    notes.set(L"XV/38844.xmv", L"Same number in another directory", L"");
    const auto normalized = notes.get(L"xn\\38844.xmv");
    require(normalized && normalized->label == L"Caf\u00e9 \u6a2a\u6d5c" &&
                normalized->notes == L"First line\r\nSecond\tline\\end\n\u96ea",
            "Unicode, multiline text, or numeric normalization failed");
    require(!notes.get(L"XT/38844.xmv"), "Movie directories must remain distinct");
    notes.save(fixture.french);
    require(std::filesystem::exists(fixture.french), "Save did not create edition notes file");
    require(!std::filesystem::exists(fixture.english), "Save crossed edition paths");

    auto reloaded = devtools::ClipNotes::load(fixture.french);
    const auto roundtrip = reloaded.get(L"Xn/00038844.XmV");
    require(roundtrip && roundtrip->label == normalized->label &&
                roundtrip->notes == normalized->notes,
            "Save and reload changed clip notes");
    require(bool(reloaded.get(L"XV/038844.xmv")), "Second directory key was lost");
    reloaded.set(L"XN/38844.xmv", L"", L"");
    require(!reloaded.get(L"XN/38844.xmv"), "Clear did not remove entry");
    reloaded.save(fixture.french);
    require(!devtools::ClipNotes::load(fixture.french).get(L"XN/38844.xmv"),
            "Cleared entry reappeared after reload");
    require(bool(devtools::ClipNotes::load(fixture.french).get(L"XV/38844.xmv")),
            "Clearing one directory removed another");

    const auto previous = fixture.read();
    const std::wstring large(65536, L'x');
    for (unsigned i = 0; i < 65; ++i) {
        reloaded.set(L"XV/" + std::to_wstring(50000 + i) + L".xmv", L"", large);
    }
    try {
        reloaded.save(fixture.french);
        throw std::runtime_error("Oversized save was accepted");
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()) != "Oversized save was accepted",
                "Oversized save was accepted");
    }
    require(fixture.read() == previous, "Failed save changed previous notes file");

    expect_bad(fixture, "wrong-version\n");
    expect_bad(fixture, "xfiles-clip-notes-v1\nXN/1.xmv\tlabel\tunterminated");
    expect_bad(fixture, "xfiles-clip-notes-v1\nXN/1.xmv\tlabel\tbad\\q\n");
    expect_bad(fixture, "xfiles-clip-notes-v1\nXN/1.xmv\tlabel\tnote\textra\n");
    expect_bad(fixture, "xfiles-clip-notes-v1\nXN/01.xmv\ta\tb\nXN/1.xmv\tc\td\n");
    expect_bad(fixture, "xfiles-clip-notes-v1\nXN/1.xmv\t\t\n");
    expect_bad(fixture, "xfiles-clip-notes-v1\nXN/1.xmv\t\xff\tnote\n");
    expect_bad(fixture, "xfiles-clip-notes-v1\n../XN/1.xmv\tlabel\tnote\n");
}

}

int main() {
    verify();
}
