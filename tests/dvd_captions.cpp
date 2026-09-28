#include "dvd/captions.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <typename Action> void rejects(Action action, const char* message) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, message);
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        dvd::Captions captions({{9250, 12767, L"first"}, {13050, 16834, L"second"}}, 1000);
        require(captions.at(-1).empty() && captions.at(828890).empty(),
                "DVD caption appeared before its audio-aligned beginning");
        require(captions.at(828891) == L"first" && captions.at(1145420) == L"first",
                "DVD caption lost its inclusive beginning or active interval");
        require(captions.at(1145421).empty() && captions.at(1170890).empty(),
                "DVD caption did not clear at its exclusive end");
        require(captions.at(1170891) == L"second" && captions.at(1511450) == L"second" &&
                    captions.at(1511451).empty(),
                "Second DVD cue lost the measured offset");
        require(captions.at(INT64_MAX).empty() && captions.at(900000) == L"first",
                "Caption lookup cannot seek backwards after completion");
        dvd::Captions early({{0, 100, L"clipped"}}, 1000);
        require(early.at(0) == L"clipped" && early.at(5391).empty(),
                "Initial caption offset did not clamp at the shared origin");
        dvd::Captions later({{0, 100, L"later"}}, 1000, 1744);
        require(later.at(1743).empty() && later.at(1744) == L"later" && later.at(10744).empty(),
                "Positive DVD caption offset lost its cue boundaries");
        require(dvd::Captions::supported(L"VOB/19650.VOB") &&
                    !dvd::Captions::supported(L"19650.XMV") &&
                    !dvd::Captions::supported(L"VOB/71914.VOB"),
                "DVD mapping lost the native binocular fallback or movie extension guard");
        rejects([] { dvd::Captions({}, 600); }, "Empty caption data accepted");
        rejects([] { dvd::Captions({{1, 2, L"text"}}, 0); }, "Zero timescale accepted");
        rejects([] { dvd::Captions({{1, 2, L"text"}}, 1000, INT64_MIN); },
                "Unbounded caption offset accepted");
        rejects([] { dvd::Captions({{1, 2, L"text"}}, 1000); },
                "Wholly pre-origin captions accepted");
        rejects([] { dvd::Captions({{2, 1, L"text"}}, 600); }, "Reversed caption accepted");
        rejects([] { dvd::Captions({{1, UINT64_MAX, L"text"}}, 600); },
                "Overflowing caption time accepted");
        rejects([] { dvd::Captions::load(L"missing/19650.vob"); },
                "Missing mapped source accepted");
        rejects([] { dvd::Captions::load(L"19652.vob"); }, "Unmapped numeric source accepted");
        const auto folder = std::filesystem::temp_directory_path() /
                            (L"xfiles-dvd-captions-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(folder / L"vob");

        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{folder};

        const auto bad_vob = folder / L"vob/19650.vob";
        std::ofstream(bad_vob, std::ios::binary) << "wrong source";
        rejects([&] { dvd::Captions::load(bad_vob); }, "Wrong mapped source accepted");
        if (argc == 2) {
            const auto real = dvd::Captions::load(argv[1]);
            require(real.at(828890).empty() && real.at(828891) == L"Yeah. It\u2019s him.",
                    "Proven source did not load the first native MOV cue");
            require(real.at(1170891) == L"Thank you." && real.at(1531530).empty(),
                    "Proven source did not load/finish the second native MOV cue");
            std::filesystem::copy_file(argv[1], bad_vob,
                                       std::filesystem::copy_options::overwrite_existing);
            rejects([&] { dvd::Captions::load(bad_vob); }, "Missing MOV source accepted");
            std::filesystem::create_directory(folder / L"XV");
            const auto bad_mov = folder / L"XV/19650.XMV";
            std::ofstream(bad_mov, std::ios::binary) << "wrong MOV";
            rejects([&] { dvd::Captions::load(bad_vob); }, "Mismatched MOV source accepted");
            std::filesystem::resize_file(bad_mov, 8783518);
            rejects([&] { dvd::Captions::load(bad_vob); }, "Same-size wrong MOV bypassed hash");
            std::fstream corrupt(bad_vob, std::ios::binary | std::ios::in | std::ios::out);
            corrupt.put('X');
            corrupt.close();
            rejects([&] { dvd::Captions::load(bad_vob); }, "Same-size wrong VOB bypassed hash");
        }
        std::cout << "DVD caption clock, boundaries, seeking and source fallback checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
