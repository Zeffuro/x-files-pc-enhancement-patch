#include "resource_strings.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <set>

namespace game_assets {
namespace {
constexpr std::uint32_t directory_bit = 0x80000000;
constexpr std::size_t entry_limit = 65536;

bool contains(std::size_t size, std::size_t at, std::size_t count) {
    return at <= size && count <= size - at;
}

struct Section {
    std::uint32_t rva, virtual_size, raw_size, raw_offset;
};

struct Parser {
    std::span<const std::uint8_t> bytes;
    ResourceStrings result;
    std::vector<Section> sections;
    std::size_t resource_offset = 0, resource_size = 0;
    std::size_t entries = 0, slots = 0, units = 0;
    std::set<std::uint32_t> directories;
    std::set<std::uint64_t> identities;

    bool fail(const wchar_t* reason) {
        result.strings.clear();
        result.status = reason;
        return false;
    }

    std::uint16_t u16(std::size_t at) const {
        return static_cast<std::uint16_t>(bytes[at] | (std::uint16_t(bytes[at + 1]) << 8));
    }

    std::uint32_t u32(std::size_t at) const {
        return std::uint32_t(u16(at)) | (std::uint32_t(u16(at + 2)) << 16);
    }

    bool map(std::uint32_t rva, std::uint32_t size, std::size_t& offset) {
        const auto end = std::uint64_t(rva) + size;
        if (size == 0 || end > std::uint64_t(1) << 32) {
            return fail(L"Invalid resource RVA range");
        }
        std::size_t matches = 0;
        bool raw = false;
        for (const auto& section : sections) {
            const auto section_end =
                std::uint64_t(section.rva) + std::max(section.raw_size, section.virtual_size);
            if (rva < section_end && end > section.rva) {
                ++matches;
                if (rva >= section.rva && end <= std::uint64_t(section.rva) + section.raw_size) {
                    offset = std::size_t(section.raw_offset) + (rva - section.rva);
                    raw = contains(bytes.size(), offset, size);
                }
            }
        }
        if (matches != 1 || !raw) {
            return fail(L"Resource RVA is unmapped, truncated or ambiguous");
        }
        return true;
    }

    bool headers() {
        if (!contains(bytes.size(), 0, 64) || u16(0) != 0x5a4d) {
            return fail(L"Not a PE image with a complete DOS header");
        }
        const std::size_t pe = u32(0x3c);
        if (pe < 64 || !contains(bytes.size(), pe, 24) || u32(pe) != 0x00004550) {
            return fail(L"Invalid PE signature or COFF header");
        }
        const auto section_count = u16(pe + 6);
        const std::size_t optional_size = u16(pe + 20), optional = pe + 24;
        if (!contains(bytes.size(), optional, optional_size) || optional_size < 2) {
            return fail(L"Truncated PE optional header");
        }
        const auto magic = u16(optional);
        const std::size_t directory = magic == 0x10b ? 96 : 112;
        if (magic != 0x10b && magic != 0x20b) {
            return fail(L"Unsupported PE optional header");
        }
        if (optional_size < directory) {
            return fail(L"Truncated PE optional header fields");
        }
        const auto directory_count = u32(optional + directory - 4);
        if (directory_count > (optional_size - directory) / 8) {
            return fail(L"Truncated PE data directories");
        }
        const std::size_t section_table = optional + optional_size;
        if (section_count == 0 || section_count > 96 ||
            !contains(bytes.size(), section_table, std::size_t(section_count) * 40)) {
            return fail(L"Invalid or truncated PE section table");
        }
        const auto headers_size = u32(optional + 60);
        if (headers_size < section_table + std::size_t(section_count) * 40 ||
            headers_size > bytes.size()) {
            return fail(L"Invalid PE header size");
        }
        for (std::size_t index = 0; index < section_count; ++index) {
            const auto at = section_table + index * 40;
            Section section{u32(at + 12), u32(at + 8), u32(at + 16), u32(at + 20)};
            if (std::uint64_t(section.rva) + std::max(section.raw_size, section.virtual_size) >
                    (std::uint64_t(1) << 32) ||
                (section.raw_size &&
                 (section.raw_offset < headers_size ||
                  !contains(bytes.size(), section.raw_offset, section.raw_size)))) {
                return fail(L"Invalid or truncated PE section range");
            }
            for (const auto& prior : sections) {
                if (section.raw_size && prior.raw_size &&
                    section.raw_offset < std::uint64_t(prior.raw_offset) + prior.raw_size &&
                    prior.raw_offset < std::uint64_t(section.raw_offset) + section.raw_size) {
                    return fail(L"Overlapping PE raw sections");
                }
            }
            sections.push_back(section);
        }
        if (directory_count <= 2) {
            return fail(L"PE image has no resource directory");
        }
        const auto resource_rva = u32(optional + directory + 16);
        const auto size = u32(optional + directory + 20);
        if (resource_rva == 0 && size == 0) {
            return fail(L"PE image has no resource directory");
        }
        if (!map(resource_rva, size, resource_offset)) {
            return false;
        }
        resource_size = size;
        return true;
    }

    bool table(std::uint32_t relative, std::size_t& first, std::size_t& count) {
        if ((relative & 3) || !contains(resource_size, relative, 16)) {
            return fail(L"Invalid resource directory offset");
        }
        if (!directories.insert(relative).second) {
            return fail(L"Repeated or cyclic resource directory");
        }
        const auto at = resource_offset + relative;
        count = std::size_t(u16(at + 12)) + u16(at + 14);
        if (count > entry_limit - entries || !contains(resource_size, relative + 16, count * 8)) {
            return fail(L"Resource directory exceeds bounds or work limit");
        }
        entries += count;
        first = at + 16;
        const auto named = u16(at + 12);
        for (std::size_t index = 0; index < count; ++index) {
            if (bool(u32(first + index * 8) & directory_bit) != (index < named)) {
                return fail(L"Invalid resource name and ID entry ordering");
            }
        }
        return true;
    }

    bool text(std::size_t at, std::size_t count, std::wstring& output) {
        output.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            std::uint32_t value = u16(at + index * 2);
            if (value >= 0xd800 && value <= 0xdbff) {
                if (++index == count) {
                    return fail(L"Incomplete UTF-16 resource surrogate pair");
                }
                const auto low = u16(at + index * 2);
                if (low < 0xdc00 || low > 0xdfff) {
                    return fail(L"Invalid UTF-16 resource surrogate pair");
                }
                value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
            } else if (value >= 0xdc00 && value <= 0xdfff) {
                return fail(L"Unpaired UTF-16 resource surrogate");
            }
            if constexpr (sizeof(wchar_t) == 2) {
                if (value > 0xffff) {
                    value -= 0x10000;
                    output.push_back(static_cast<wchar_t>(0xd800 + (value >> 10)));
                    output.push_back(static_cast<wchar_t>(0xdc00 + (value & 0x3ff)));
                    continue;
                }
            }
            output.push_back(static_cast<wchar_t>(value));
        }
        return true;
    }

    bool bundle(std::uint32_t bundle_id, std::uint32_t language, std::uint32_t relative) {
        const auto identity = (std::uint64_t(bundle_id) << 32) | language;
        if (!identities.insert(identity).second) {
            return fail(L"Duplicate resource bundle and language");
        }
        if ((relative & 3) || !contains(resource_size, relative, 16)) {
            return fail(L"Invalid resource data entry offset");
        }
        const auto entry = resource_offset + relative;
        const auto size = u32(entry + 4), code_page = u32(entry + 8);
        std::size_t start = 0;
        if (!map(u32(entry), size, start)) {
            return false;
        }
        if (size % 2 || start < resource_offset ||
            !contains(resource_size, start - resource_offset, size)) {
            return fail(L"String resource data is outside the resource directory range");
        }
        if (slots > resource_string_limit - 16) {
            return fail(L"Resource strings exceed the 32768 slot safety limit");
        }
        slots += 16;
        std::size_t at = start, end = start + size;
        for (std::uint32_t slot = 0; slot < 16; ++slot) {
            if (!contains(end, at, 2)) {
                return fail(L"Truncated resource string length");
            }
            const auto offset = at, count = std::size_t(u16(at));
            at += 2;
            if (!contains(end, at, count * 2)) {
                return fail(L"Truncated counted resource string");
            }
            if (count > resource_unit_limit - units) {
                return fail(L"Resource text exceeds the UTF-16 safety limit");
            }
            units += count;
            if (count) {
                ResourceString string{(bundle_id - 1) * 16 + slot, language, code_page, offset, {}};
                if (!text(at, count, string.text)) {
                    return false;
                }
                result.strings.push_back(std::move(string));
            }
            at += count * 2;
        }
        if (at != end) {
            return fail(L"Unexpected trailing resource bundle bytes");
        }
        return true;
    }

    bool languages(std::uint32_t bundle_id, std::uint32_t relative) {
        std::size_t first = 0, count = 0;
        if (!table(relative, first, count)) {
            return false;
        }
        for (std::size_t index = 0; index < count; ++index) {
            const auto at = first + index * 8;
            const auto language = u32(at), target = u32(at + 4);
            if ((language & directory_bit) || (target & directory_bit)) {
                return fail(L"Unsupported named language or nested string resource");
            }
            if (!bundle(bundle_id, language, target)) {
                return false;
            }
        }
        return true;
    }

    bool bundles(std::uint32_t relative) {
        std::size_t first = 0, count = 0;
        if (!table(relative, first, count)) {
            return false;
        }
        std::set<std::uint32_t> ids;
        for (std::size_t index = 0; index < count; ++index) {
            const auto at = first + index * 8;
            const auto id = u32(at), target = u32(at + 4);
            if ((id & directory_bit) || id == 0 ||
                id > std::numeric_limits<std::uint32_t>::max() / 16 + 1 ||
                !(target & directory_bit)) {
                return fail(L"Unsupported resource bundle identity or directory structure");
            }
            if (!ids.insert(id).second) {
                return fail(L"Duplicate resource bundle identity");
            }
            if (!languages(id, target & ~directory_bit)) {
                return false;
            }
        }
        return true;
    }

    bool parse() {
        if (!headers()) {
            return false;
        }
        std::size_t first = 0, count = 0;
        if (!table(0, first, count)) {
            return false;
        }
        bool found = false;
        for (std::size_t index = 0; index < count; ++index) {
            const auto at = first + index * 8;
            if (u32(at) != 6) {
                continue;
            }
            const auto target = u32(at + 4);
            if (found || !(target & directory_bit)) {
                return fail(L"Duplicate or invalid RT_STRING resource type");
            }
            found = true;
            if (!bundles(target & ~directory_bit)) {
                return false;
            }
        }
        std::sort(result.strings.begin(), result.strings.end(),
                  [](const auto& left, const auto& right) {
                      return left.id < right.id ||
                             (left.id == right.id && left.language < right.language);
                  });
        result.status = result.strings.empty() ? L"PE image contains no nonempty RT_STRING entries"
                                               : L"PE resource strings decoded";
        return true;
    }
};
}

ResourceStrings parse_resource_strings(std::span<const std::uint8_t> bytes) {
    Parser parser{bytes, {}, {}, 0, 0, 0, 0, 0, {}, {}};
    parser.result.file_size = bytes.size();
    if (bytes.size() > resource_file_limit) {
        parser.fail(L"PE image exceeds the 32 MiB safety limit");
    } else {
        parser.result.valid = parser.parse();
    }
    return std::move(parser.result);
}

ResourceStrings load_resource_strings(const std::filesystem::path& path) {
    ResourceStrings result;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size < std::streampos(0)) {
        result.status = L"PE resource file could not be read";
        return result;
    }
    const auto file_size = static_cast<std::uint64_t>(size);
    if (file_size > resource_file_limit) {
        result.file_size = static_cast<std::size_t>(
            std::min(file_size, std::uint64_t(std::numeric_limits<std::size_t>::max())));
        result.status = L"PE image exceeds the 32 MiB safety limit";
        return result;
    }
    result.file_size = static_cast<std::size_t>(file_size);
    std::vector<std::uint8_t> bytes(result.file_size);
    file.seekg(0);
    if ((!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()),
                                      static_cast<std::streamsize>(bytes.size()))) ||
        file.peek() != std::char_traits<char>::eof() || file.bad()) {
        result.status = L"PE resource file changed or could not be read completely";
        return result;
    }
    return parse_resource_strings(bytes);
}
}
