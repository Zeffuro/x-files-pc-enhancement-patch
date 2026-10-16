#include "internal.h"
#include <algorithm>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace devtools::recipes::detail {
Bytes read_file(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "cannot open recipe input");
    Bytes output;
    std::vector<char> chunk(65536);
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        auto count = static_cast<std::size_t>(input.gcount());
        require(count <= limit - output.size(), "input exceeds size limit");
        output.insert(output.end(), chunk.begin(), chunk.begin() + count);
    }
    require(input.eof(), "cannot read recipe input");
    return output;
}
}

namespace devtools::recipes {
std::string identify_executable(const std::filesystem::path& path) {
    auto hash = detail::digest(detail::read_file(path, detail::max_database));
    for (auto name : {"cd_10012", "cd_10019", "cd_10020", "dvd_20000"}) {
        if (detail::build_info(name).hash == hash) {
            return name;
        }
    }
    throw std::runtime_error("unsupported executable SHA-256");
}

std::string extract(const std::filesystem::path& source, const std::filesystem::path& executable,
                    const std::filesystem::path& baseline) {
    auto build = identify_executable(executable);
    return extract_data(detail::read_file(baseline, detail::max_database),
                        detail::read_file(source, detail::max_database), build);
}

Bytes reconstruct(const std::filesystem::path& source, const std::filesystem::path& executable,
                  const std::filesystem::path& baseline) {
    auto build = identify_executable(executable);
    auto recipe = detail::read_file(source, detail::max_recipe);
    return reconstruct_data(
        detail::read_file(baseline, detail::max_database),
        std::string_view(reinterpret_cast<const char*>(recipe.data()), recipe.size()), build);
}

void write_new(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
               const std::vector<std::filesystem::path>& inputs) {
    detail::require(bytes.size() <= detail::max_database, "output exceeds size limit");
    auto target = std::filesystem::weakly_canonical(path);
    for (const auto& input : inputs) {
        detail::require(target != std::filesystem::weakly_canonical(input),
                        "output aliases an input");
    }
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    detail::require(file != INVALID_HANDLE_VALUE, "cannot create new output file");
    DWORD written = 0;
    bool success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
                             nullptr) != FALSE &&
                   written == bytes.size();
    bool closed = CloseHandle(file) != FALSE;
    if (!success || !closed) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("cannot write new output file");
    }
#else
    int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    detail::require(file >= 0, "cannot create new output file");
    std::size_t done = 0;
    bool success = true;
    while (done < bytes.size()) {
        auto count = ::write(file, bytes.data() + done, bytes.size() - done);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            success = false;
            break;
        }
        done += static_cast<std::size_t>(count);
    }
    bool closed = ::close(file) == 0;
    if (!success || !closed) {
        ::unlink(path.c_str());
        throw std::runtime_error("cannot write new output file");
    }
#endif
}
}
