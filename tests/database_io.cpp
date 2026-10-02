#include "game/database/io.h"
#include "media/files.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void seek(HANDLE handle, std::uint64_t offset) {
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    require(SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) != FALSE, "seek failed");
}

struct Temporary {
    std::filesystem::path folder;

    Temporary() {
        wchar_t temp[MAX_PATH]{};
        require(GetTempPathW(MAX_PATH, temp) != 0, "temp folder unavailable");
        folder = std::filesystem::path(temp) /
                 (L"xfiles-database-io-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(folder);
        const auto file = CreateFileW((folder / L"XFILES.HDB").c_str(), GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
        require(file != INVALID_HANDLE_VALUE, "cannot seed database");
        std::array<unsigned char, 256> data{};
        for (unsigned index = 0; index < data.size(); ++index) {
            data[index] = static_cast<unsigned char>(index);
        }
        DWORD written = 0;
        const auto result =
            WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
        CloseHandle(file);
        require(result && written == data.size(), "cannot write database fixture");
    }

    ~Temporary() {
        media::remove_file_hooks();
        std::error_code error;
        std::filesystem::remove_all(folder, error);
    }
};

HANDLE open(const std::string& path, DWORD flags = FILE_ATTRIBUTE_NORMAL) {
    return CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_EXISTING, flags, nullptr);
}

void verify() {
    Temporary temporary;
    const auto path = (temporary.folder / L"XFILES.HDB").string();
    const auto baseline = open(path);
    require(baseline != INVALID_HANDLE_VALUE, "baseline open failed");
    std::array<unsigned char, 8> bytes{};
    DWORD transferred = 0;
    seek(baseline, 17);
    SetLastError(0x1234);
    const auto baseline_result = ReadFile(baseline, bytes.data(), 8, &transferred, nullptr);
    const auto baseline_error = GetLastError();
    require(baseline_result && transferred == 8, "baseline read failed");
    CloseHandle(baseline);

    require(media::install_file_hooks(GetModuleHandleW(nullptr)), "hook installation failed");
    const auto first = open(path);
    require(first != INVALID_HANDLE_VALUE, "hooked open failed");
    auto snapshot = game_assets::database_io_snapshot();
    require(snapshot.open && snapshot.total_reads == 0 && !snapshot.path.empty(),
            "open observation missing");
    require(std::filesystem::equivalent(snapshot.path, path), "actual database path incorrect");
    seek(first, 17);
    SetLastError(0x1234);
    require(ReadFile(first, bytes.data(), 8, &transferred, nullptr) == baseline_result,
            "read return changed");
    require(GetLastError() == baseline_error && transferred == 8 && bytes[0] == 17,
            "read output or last error changed");
    snapshot = game_assets::database_io_snapshot();
    require(snapshot.total_reads == 1 && snapshot.total_bytes == 8 && snapshot.reads.size() == 1 &&
                snapshot.reads[0].offset == 17 && snapshot.reads[0].size == 8 &&
                snapshot.reads[0].caller_is_rva && snapshot.reads[0].caller != 0,
            "read offset or executable caller missing");

    const auto second = open(path);
    require(second != INVALID_HANDLE_VALUE, "second handle open failed");
    require(game_assets::database_io_snapshot().session > snapshot.session,
            "same-path reopen session missing");
    for (unsigned index = 0; index < 70; ++index) {
        const auto handle = index % 2 ? first : second;
        seek(handle, index);
        require(ReadFile(handle, bytes.data(), 1, &transferred, nullptr) && transferred == 1,
                "ring read failed");
    }
    snapshot = game_assets::database_io_snapshot();
    require(snapshot.total_reads == 70 && snapshot.total_bytes == 70 &&
                snapshot.reads.size() == 64 && snapshot.reads.front().sequence == 7 &&
                snapshot.reads.back().sequence == 70 && snapshot.reads.back().offset == 69,
            "bounded ring or multiple handles incorrect");
    require(CloseHandle(first), "first close failed");
    require(game_assets::database_io_snapshot().open, "second handle was forgotten");
    seek(second, 256);
    require(ReadFile(second, bytes.data(), 1, &transferred, nullptr) && !transferred,
            "EOF pass-through changed");
    require(game_assets::database_io_snapshot().total_reads == 70, "EOF recorded as data");
    SetLastError(0x4321);
    const auto close_generation = game_assets::begin_database_close(second);
    game_assets::observe_database_close(second, FALSE, close_generation);
    require(GetLastError() == 0x4321 && game_assets::database_io_snapshot().open,
            "failed close changed observation or last error");

    seek(second, 0);
    const auto stale = game_assets::begin_database_read(second, nullptr);
    game_assets::observe_database_open(second, path.c_str(), FILE_ATTRIBUTE_NORMAL);
    game_assets::observe_database_close(second, TRUE, close_generation);
    require(game_assets::database_io_snapshot().open, "reused handle accepted stale close");
    seek(second, 1);
    game_assets::end_database_read(stale, TRUE, 1, 123);
    require(game_assets::database_io_snapshot().total_reads == 0,
            "reused handle accepted stale read");
    require(CloseHandle(second), "second close failed");
    snapshot = game_assets::database_io_snapshot();
    require(!snapshot.open && !snapshot.path.empty(), "closed path evidence lost");

    const auto asynchronous = open(path, FILE_FLAG_OVERLAPPED);
    require(asynchronous != INVALID_HANDLE_VALUE, "overlapped open failed");
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    require(overlapped.hEvent != nullptr, "cannot create overlapped event");
    const auto result = ReadFile(asynchronous, bytes.data(), 8, &transferred, &overlapped);
    require(result || GetLastError() == ERROR_IO_PENDING, "overlapped read failed");
    require(GetOverlappedResult(asynchronous, &overlapped, &transferred, TRUE),
            "overlapped completion failed");
    require(game_assets::database_io_snapshot().total_reads == 0, "asynchronous read recorded");
    CloseHandle(overlapped.hEvent);
    CloseHandle(asynchronous);

    const auto missing = open((temporary.folder / L"missing" / L"XFILES.HDB").string());
    const auto error = GetLastError();
    require(missing == INVALID_HANDLE_VALUE && error == ERROR_PATH_NOT_FOUND,
            "failed create pass-through changed");
    media::remove_file_hooks();
}

}

int main() {
    try {
        verify();
        std::cout << "Database I/O hooks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
