#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace saves::database {
using Bytes = std::vector<unsigned char>;
constexpr std::size_t maximum_size = 64 * 1024 * 1024;

inline void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("This save does not match the supported conversion format.");
    }
}

inline std::span<const unsigned char> slice(const Bytes& bytes, std::uint64_t offset,
                                            std::uint64_t length) {
    require(offset <= bytes.size() && length <= bytes.size() - offset);
    return std::span(bytes).subspan(static_cast<std::size_t>(offset),
                                    static_cast<std::size_t>(length));
}

inline std::uint32_t get(const Bytes& bytes, std::uint64_t offset, unsigned width = 4) {
    std::uint32_t value = 0;
    for (const auto byte : slice(bytes, offset, width)) {
        value = (value << 8) | byte;
    }
    return value;
}

inline void put(Bytes& bytes, std::uint64_t offset, std::uint32_t value, unsigned width = 4) {
    slice(bytes, offset, width);
    for (unsigned i = 0; i < width; ++i) {
        bytes[static_cast<std::size_t>(offset) + width - i - 1] = static_cast<unsigned char>(value);
        value >>= 8;
    }
}

inline Bytes read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    require(size >= 24 && size <= maximum_size);
    Bytes bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    require(bool(input.read(reinterpret_cast<char*>(bytes.data()), size)));
    return bytes;
}

struct Class {
    std::uint32_t directory = 0;
    std::map<std::uint32_t, std::uint32_t> records;
};

class Container {
    std::set<std::uint32_t> nodes_;
    std::set<std::uint32_t> targets_;
    std::set<std::uint32_t> ids_;

    void index(std::uint32_t offset, Class& item, std::uint32_t& previous, unsigned depth) {
        require(depth <= 8 && nodes_.size() < 4096 && nodes_.insert(offset).second);
        require(get(bytes, std::uint64_t(offset) + 2) == 1);
        const auto count = get(bytes, std::uint64_t(offset) + 6, 2);
        require(count && count <= 32);
        const bool leaf = (get(bytes, offset, 1) & 0x80) != 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto entry = std::uint64_t(offset) + 8 + i * (leaf ? 8 : 4);
            const auto target = get(bytes, entry);
            if (!leaf) {
                index(target, item, previous, depth + 1);
                continue;
            }
            const auto id = get(bytes, entry + 4);
            require(id > previous && item.records.size() < 65536 && ids_.insert(id).second &&
                    targets_.insert(target).second);
            require(get(bytes, std::uint64_t(target) + 2) == 1);
            previous = id;
            item.records.emplace(id, target);
        }
    }

public:
    Bytes bytes;
    std::map<std::uint32_t, Class> classes;

    explicit Container(Bytes input) : bytes(std::move(input)) {
        require(bytes.size() <= maximum_size && get(bytes, 0) == 5 && get(bytes, 20) == 0x501);
        const auto directory = get(bytes, 8);
        const auto count = get(bytes, 12);
        require(count >= 4 && count <= 11 && get(bytes, std::uint64_t(directory) + 2) == 1 &&
                get(bytes, std::uint64_t(directory) + 6, 2) == count &&
                get(bytes, std::uint64_t(directory) + 16, 2) == count - 1 &&
                get(bytes, std::uint64_t(directory) + 18, 1) == 1 &&
                get(bytes, std::uint64_t(directory) + 19) == 1);
        for (unsigned i = 1; i < count; ++i) {
            const auto entry = std::uint64_t(directory) + 58 + (i - 1) * 43;
            const auto id = get(bytes, entry + 4);
            require(get(bytes, entry) == 1 && get(bytes, entry + 28) == id &&
                    (id == 0x46 || id == 0x50 || id == 0x53 || (id >= 0x56 && id <= 0x5c)) &&
                    !classes.contains(id));
            auto& item = classes[id];
            item.directory = static_cast<std::uint32_t>(entry);
            std::uint32_t previous = 0;
            index(get(bytes, entry + 24), item, previous, 0);
            require(item.records.size() == get(bytes, entry + 8));
        }
        require(!ids_.empty() && get(bytes, 16) > *ids_.rbegin());
        for (const auto& [id, item] : classes) {
            for (const auto& [key, record] : item.records) {
                validate_record(id, record);
            }
        }
    }

    std::span<const unsigned char> blob(std::uint64_t offset) const {
        const auto target = get(bytes, offset);
        const auto size = get(bytes, offset + 4);
        require(bool(target) == bool(size));
        return slice(bytes, target, size);
    }

    void validate_record(unsigned id, std::uint64_t record) const {
        if (id == 0x46) {
            slice(bytes, record, 12);
        } else if (id == 0x53) {
            slice(bytes, record, 24);
            const auto name = blob(record + 6);
            require(!name.empty() && name.size() <= 1024 && name.back() == 0);
        } else if (id == 0x57) {
            slice(bytes, record, 132);
            for (unsigned i = 0; i < 5; ++i) {
                require(blob(record + 66 + i * 8).size() % 4 == 0);
            }
        } else if (id == 0x59) {
            slice(bytes, record, 32);
            blob(record + 6);
        } else {
            slice(bytes, record, 14);
            const auto data = blob(record + 6);
            require(id == 0x50 || data.size() % 4 == 0);
        }
    }
};

inline std::uint32_t allocate(Bytes& bytes, std::size_t size) {
    const auto offset = (bytes.size() + 7) & ~std::size_t(7);
    require(offset <= maximum_size && size <= maximum_size - offset);
    bytes.resize(offset + size);
    return static_cast<std::uint32_t>(offset);
}

inline void rebuild_index(Bytes& bytes, const Class& item) {
    require(!item.records.empty() && item.records.size() <= 1024);
    std::vector<std::uint32_t> leaves;
    auto current = item.records.begin();
    while (current != item.records.end()) {
        const auto offset = allocate(bytes, 264);
        leaves.push_back(offset);
        put(bytes, offset, 0x80, 1);
        put(bytes, std::uint64_t(offset) + 2, 1);
        unsigned count = 0;
        for (; count < 32 && current != item.records.end(); ++count, ++current) {
            put(bytes, std::uint64_t(offset) + 8 + count * 8, current->second);
            put(bytes, std::uint64_t(offset) + 12 + count * 8, current->first);
        }
        put(bytes, std::uint64_t(offset) + 6, count, 2);
    }
    auto root = leaves.front();
    if (leaves.size() == 1) {
        put(bytes, root, 0xc0, 1);
    } else {
        root = allocate(bytes, 136);
        put(bytes, root, 0x40, 1);
        put(bytes, std::uint64_t(root) + 2, 1);
        put(bytes, std::uint64_t(root) + 6, static_cast<unsigned>(leaves.size()), 2);
        for (unsigned i = 0; i < leaves.size(); ++i) {
            put(bytes, std::uint64_t(root) + 8 + i * 4, leaves[i]);
        }
    }
    put(bytes, std::uint64_t(item.directory) + 8, static_cast<unsigned>(item.records.size()));
    put(bytes, std::uint64_t(item.directory) + 24, root);
}
}
