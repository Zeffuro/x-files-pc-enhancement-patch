#include "disc_image.h"
#include "identity.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
constexpr std::uint64_t logical_sector = 2048;
using Bytes = std::vector<unsigned char>;

unsigned little(const unsigned char* value) {
    return unsigned(value[0]) | unsigned(value[1]) << 8 | unsigned(value[2]) << 16 |
           unsigned(value[3]) << 24;
}

unsigned big(const unsigned char* value) {
    return unsigned(value[3]) | unsigned(value[2]) << 8 | unsigned(value[1]) << 16 |
           unsigned(value[0]) << 24;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

struct CueSource {
    fs::path path;
    ImageGeometry geometry;
};

bool reparse_point(const fs::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

CueSource parse_cue(const fs::path& cue) {
    if (fs::file_size(cue) > 1024 * 1024) {
        throw std::runtime_error("The CUE sheet is unexpectedly large.");
    }
    std::ifstream input(cue);
    if (!input) {
        throw std::runtime_error("Cannot read the CUE sheet.");
    }
    const std::regex file_line(R"(^FILE[ \t]+\"([^\"]+)\"[ \t]+BINARY$)", std::regex::icase);
    const std::regex track_line(R"(^TRACK[ \t]+01[ \t]+([^ \t]+)$)", std::regex::icase);
    const std::regex index_line(R"(^INDEX[ \t]+01[ \t]+00:00:00$)", std::regex::icase);
    const std::regex metadata_line(R"(^(CATALOG[ \t]+[^ \t]+|REM([ \t].*)?)$)", std::regex::icase);
    enum class CueStage { file, track, index, complete };
    CueStage stage = CueStage::file;
    std::string filename, mode, line;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (line.empty() || std::regex_match(line, metadata_line)) {
            continue;
        }
        std::smatch match;
        if (std::regex_match(line, match, file_line)) {
            if (stage != CueStage::file) {
                throw std::runtime_error("Only one-file, one-track BIN/CUE images are supported.");
            }
            filename = match[1].str();
            stage = CueStage::track;
        } else if (std::regex_match(line, match, track_line)) {
            if (stage != CueStage::track) {
                throw std::runtime_error("Only one-file, one-track BIN/CUE images are supported.");
            }
            mode = match[1].str();
            std::transform(mode.begin(), mode.end(), mode.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            stage = CueStage::index;
        } else if (std::regex_match(line, index_line)) {
            if (stage != CueStage::index) {
                throw std::runtime_error("Only one-file, one-track BIN/CUE images are supported.");
            }
            stage = CueStage::complete;
        } else {
            throw std::runtime_error("Unsupported CUE directive or track layout.");
        }
    }
    if (stage != CueStage::complete || filename.empty()) {
        throw std::runtime_error("Only one-file, one-track BIN/CUE images are supported.");
    }
    if (mode == "MODE2/2352") {
        throw std::runtime_error("PlayStation MODE2 BIN/CUE images are not supported.");
    }
    if (mode != "MODE1/2352") {
        throw std::runtime_error("Only MODE1/2352 PC BIN/CUE images are supported.");
    }
    const fs::path name(filename);
    const auto extension = name.extension().wstring();
    if (name.empty() || name.is_absolute() || name.has_parent_path() ||
        _wcsicmp(extension.c_str(), L".bin") != 0) {
        throw std::runtime_error("The CUE sheet must name one BIN file in the same folder.");
    }
    const auto source = cue.parent_path() / name;
    if (!fs::is_regular_file(source)) {
        throw std::runtime_error("The BIN file named by the CUE sheet is missing.");
    }
    if (reparse_point(source)) {
        throw std::runtime_error("BIN/CUE source files must not be links.");
    }
    return {fs::canonical(source), {2352, 16}};
}

std::uint64_t logical_size(std::uint64_t physical, const ImageGeometry& geometry) {
    if (geometry.sector_size == logical_sector && !geometry.payload_offset) {
        return physical;
    }
    if (geometry.sector_size < logical_sector ||
        geometry.payload_offset > geometry.sector_size - logical_sector ||
        physical % geometry.sector_size) {
        throw std::runtime_error("The disc image is truncated or has invalid sector geometry.");
    }
    return physical / geometry.sector_size * logical_sector;
}

Bytes read_logical(std::ifstream& stream, std::uint64_t physical_size,
                   const ImageGeometry& geometry, std::uint64_t offset, std::uint64_t count) {
    const auto available = logical_size(physical_size, geometry);
    if (offset > available || count > available - offset || count > 16 * 1024 * 1024) {
        throw std::runtime_error("Disc image extent is outside the data track.");
    }
    Bytes result(static_cast<std::size_t>(count));
    if (!count) {
        return result;
    }
    if (geometry.sector_size == logical_sector && !geometry.payload_offset) {
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset));
        if (!stream.read(reinterpret_cast<char*>(result.data()),
                         static_cast<std::streamsize>(result.size()))) {
            throw std::runtime_error("Cannot read the disc image.");
        }
        return result;
    }
    const auto first_sector = offset / logical_sector;
    const auto within = offset % logical_sector;
    const auto sectors = (within + count + logical_sector - 1) / logical_sector;
    const auto raw_count = sectors * geometry.sector_size;
    Bytes raw(static_cast<std::size_t>(raw_count));
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(first_sector * geometry.sector_size));
    if (!stream.read(reinterpret_cast<char*>(raw.data()),
                     static_cast<std::streamsize>(raw.size()))) {
        throw std::runtime_error("Cannot read the BIN data track.");
    }
    for (std::uint64_t index = 0; index < sectors; ++index) {
        const auto start = static_cast<std::size_t>(index * geometry.sector_size);
        if (raw[start] || raw[start + 11] || raw[start + 15] != 1 ||
            !std::all_of(raw.begin() + static_cast<std::ptrdiff_t>(start + 1),
                         raw.begin() + static_cast<std::ptrdiff_t>(start + 11),
                         [](unsigned char value) { return value == 0xff; })) {
            throw std::runtime_error("The BIN sector headers do not match MODE1/2352.");
        }
    }
    std::uint64_t copied = 0;
    for (std::uint64_t index = 0; index < sectors && copied < count; ++index) {
        const auto skip = index ? 0 : within;
        const auto chunk = std::min(logical_sector - skip, count - copied);
        const auto start = index * geometry.sector_size + geometry.payload_offset + skip;
        std::copy_n(raw.begin() + static_cast<std::ptrdiff_t>(start),
                    static_cast<std::size_t>(chunk),
                    result.begin() + static_cast<std::ptrdiff_t>(copied));
        copied += chunk;
    }
    return result;
}

class DiscImage {
    fs::path path_;
    std::ifstream stream_;
    std::uint64_t size_;
    ImageGeometry geometry_;
    std::set<std::uint64_t> directories_;
    std::vector<MediaFile> files_;

    Bytes read(std::uint64_t offset, std::uint64_t count) {
        return read_logical(stream_, size_, geometry_, offset, count);
    }

    void directory(std::uint64_t offset, unsigned length, const fs::path& parent, unsigned depth) {
        if (depth > 16 || directories_.size() > 4096 || !directories_.insert(offset).second) {
            throw std::runtime_error("Invalid or recursive ISO directory.");
        }
        const auto data = read(offset, length);
        for (std::size_t index = 0; index < data.size();) {
            const auto record_size = data[index];
            if (!record_size) {
                index = (index / logical_sector + 1) * logical_sector;
                continue;
            }
            if (record_size < 34 || record_size > data.size() - index ||
                index % logical_sector + record_size > logical_sector) {
                throw std::runtime_error("Truncated ISO directory record.");
            }
            const auto record = data.data() + index;
            index += record_size;
            const unsigned name_size = record[32];
            if (!name_size || 33 + name_size > record_size) {
                throw std::runtime_error("Invalid ISO filename.");
            }
            if (name_size == 1 && record[33] <= 1) {
                continue;
            }
            if (record[25] & 4) {
                continue; // Associated Macintosh records duplicate the Windows data names.
            }
            if (record[1] || record[26] || record[27] || (record[25] & 0x80)) {
                throw std::runtime_error(
                    "This ISO layout is not supported. Mount the disc instead.");
            }
            std::wstring name;
            for (unsigned i = 0; i < name_size && record[33 + i] != ';'; ++i) {
                const auto character = record[33 + i];
                if (character < 32 || character >= 127 || character == '/' || character == '\\' ||
                    character == ':') {
                    throw std::runtime_error("Unsafe ISO filename.");
                }
                name += character;
            }
            if (name.empty() || name == L"." || name == L"..") {
                throw std::runtime_error("Invalid ISO filename.");
            }
            const auto extent = little(record + 2);
            const auto length_value = little(record + 10);
            if (extent != big(record + 6) || length_value != big(record + 14)) {
                throw std::runtime_error("ISO directory extent byte order does not match.");
            }
            const auto start = std::uint64_t(extent) * logical_sector;
            const auto available = logical_size(size_, geometry_);
            if (start > available || length_value > available - start) {
                throw std::runtime_error("ISO file extends beyond the image.");
            }
            const auto relative = parent / name;
            if (record[25] & 2) {
                directory(start, length_value, relative, depth + 1);
            } else {
                if (files_.size() >= 100000) {
                    throw std::runtime_error("Too many ISO files.");
                }
                files_.push_back({path_, relative, length_value, {}, start, geometry_});
            }
        }
    }

public:
    explicit DiscImage(const fs::path& source) {
        const auto extension = source.extension().wstring();
        if (_wcsicmp(extension.c_str(), L".cue") == 0) {
            if (reparse_point(source)) {
                throw std::runtime_error("BIN/CUE source files must not be links.");
            }
            const auto parsed = parse_cue(fs::canonical(source));
            path_ = parsed.path;
            geometry_ = parsed.geometry;
        } else if (_wcsicmp(extension.c_str(), L".iso") == 0) {
            path_ = fs::canonical(source);
        } else if (_wcsicmp(extension.c_str(), L".mdf") == 0 ||
                   _wcsicmp(extension.c_str(), L".mds") == 0) {
            throw std::runtime_error("MDF/MDS images are not supported; use BIN/CUE or ISO files.");
        } else {
            throw std::runtime_error(
                "Choose an ISO file, a CUE sheet, or a folder of disc images.");
        }
        size_ = fs::file_size(path_);
        logical_size(size_, geometry_);
        stream_.open(path_, std::ios::binary);
        if (!stream_) {
            throw std::runtime_error("Cannot open the disc image.");
        }
    }

    std::vector<MediaFile> list() {
        for (unsigned block = 16; block < 64; ++block) {
            const auto data = read(block * logical_sector, logical_sector);
            if (std::string(data.begin() + 1, data.begin() + 6) != "CD001" || data[6] != 1) {
                break;
            }
            if (data[0] == 1) {
                if (data[128] != 0 || data[129] != 8 || data[156] < 34) {
                    break;
                }
                const auto root_extent = little(data.data() + 158);
                const auto root_length = little(data.data() + 166);
                if (root_extent != big(data.data() + 162) ||
                    root_length != big(data.data() + 170)) {
                    throw std::runtime_error("ISO root extent byte order does not match.");
                }
                directory(std::uint64_t(root_extent) * logical_sector, root_length, {}, 0);
                return std::move(files_);
            }
            if (data[0] == 255) {
                break;
            }
        }
        throw std::runtime_error("No supported PC ISO9660 filesystem was found in the image.");
    }
};

template <typename Callback> void read_file(const MediaFile& file, Callback callback) {
    if (!file.offset) {
        throw std::runtime_error("A disc-image file extent is required.");
    }
    std::ifstream input(file.source, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open the disc image.");
    }
    const auto physical_size = fs::file_size(file.source);
    const auto available = logical_size(physical_size, file.geometry);
    if (*file.offset > available || file.size > available - *file.offset) {
        throw std::runtime_error("Game file extent is outside the disc image.");
    }
    std::uint64_t position = 0;
    while (position < file.size) {
        const auto count = std::min<std::uint64_t>(1024 * 1024, file.size - position);
        const auto data =
            read_logical(input, physical_size, file.geometry, *file.offset + position, count);
        callback(data);
        position += count;
    }
}
}

std::vector<MediaFile> read_disc_image(const fs::path& path) {
    return DiscImage(path).list();
}

std::string media_file_sha256(const MediaFile& file) {
    if (!file.offset) {
        return sha256(file.source);
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        throw std::runtime_error("Cannot initialize SHA-256.");
    }

    struct Guard {
        BCRYPT_HASH_HANDLE value;

        ~Guard() {
            BCryptDestroyHash(value);
        }
    } guard{hash};

    read_file(file, [&](const Bytes& data) {
        if (BCryptHashData(hash, const_cast<unsigned char*>(data.data()),
                           static_cast<ULONG>(data.size()), 0) < 0) {
            throw std::runtime_error("SHA-256 update failed.");
        }
    });
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        throw std::runtime_error("SHA-256 finish failed.");
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}

void copy_media_file(const MediaFile& file, const fs::path& output,
                     const std::function<bool()>& keep_going) {
    if (!file.offset) {
        fs::copy_file(file.source, output, fs::copy_options::overwrite_existing);
        return;
    }
    std::ofstream destination(output, std::ios::binary);
    if (!destination) {
        throw std::runtime_error("Cannot create the imported game file.");
    }
    read_file(file, [&](const Bytes& data) {
        if (!keep_going()) {
            throw std::runtime_error("Setup cancelled; your source files were not changed.");
        }
        if (!destination.write(reinterpret_cast<const char*>(data.data()),
                               static_cast<std::streamsize>(data.size()))) {
            throw std::runtime_error("Cannot copy a file from the disc image. Check free space.");
        }
    });
    destination.close();
    if (!destination) {
        throw std::runtime_error("Cannot finish writing the imported file.");
    }
}
