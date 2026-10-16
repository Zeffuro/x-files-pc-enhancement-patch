#include "internal.h"
#include <algorithm>
#include <limits>

namespace devtools::recipes::detail {
std::span<const std::uint8_t> Database::part(std::size_t at, std::size_t size) const {
    require(at <= data.size() && size <= data.size() - at, "database read out of bounds");
    return std::span(data).subspan(at, size);
}

unsigned Database::number(std::size_t at, unsigned width, bool little) const {
    auto bytes = part(at, width);
    unsigned result = 0;
    for (unsigned i = 0; i < width; ++i) {
        result = (result << 8) | bytes[little ? width - i - 1 : i];
    }
    return result;
}

std::vector<std::vector<unsigned>> Database::tree(unsigned root, unsigned width,
                                                  std::set<unsigned>& seen) {
    std::vector<std::pair<unsigned, unsigned>> pending{{root, 0}};
    std::vector<std::vector<unsigned>> result;
    while (!pending.empty()) {
        auto [at, depth] = pending.back();
        pending.pop_back();
        require(at >= 32 && !seen.contains(at) && depth < 32 && seen.size() < 8192,
                "cyclic or oversized index tree");
        seen.insert(at);
        part(at, 8);
        require(number(std::size_t(at) + 2) == 1, "unsupported index version");
        unsigned count = number(std::size_t(at) + 6, 2);
        bool leaf = (data[at] & 0x80) != 0;
        require(count > 0 && count <= 32, "invalid index node count");
        unsigned extent = 8 + count * (leaf ? width : 4);
        part(at, extent);
        nodes.emplace_back(at, std::size_t(at) + extent);
        if (leaf) {
            for (unsigned i = 0; i < count; ++i) {
                std::vector<unsigned> row;
                for (unsigned j = 0; j < width; j += 4) {
                    row.push_back(number(std::size_t(at) + 8 + i * width + j));
                }
                result.push_back(std::move(row));
            }
            require(result.size() <= 200000, "oversized index");
        } else {
            for (unsigned i = count; i > 0; --i) {
                pending.emplace_back(number(std::size_t(at) + 8 + (i - 1) * 4), depth + 1);
            }
        }
    }
    return result;
}

Database::Database(const Bytes& bytes, bool baseline) : data(bytes) {
    require(data.size() >= 32 && data.size() <= max_database, "invalid database size");
    require(number(0) == 5 && number(20) == 0x501 && number(24) == 0x40000 && number(28) == 256,
            "unsupported database header");
    unsigned root = number(8), count = number(12);
    require(count >= 4 && count <= 11, "unsupported class count");
    require((part(root, 1)[0] & 0x80) != 0 && number(std::size_t(root) + 2) == 1,
            "unsupported class directory");
    require(number(std::size_t(root) + 6, 2) == count, "class directory count mismatch");
    part(root, 8 + count * 43);
    nodes.emplace_back(root, std::size_t(root) + 8 + count * 43);
    std::set<unsigned> seen{root}, object_ids, targets;
    unsigned master_list = 0;
    for (unsigned i = 0; i < count; ++i) {
        auto at = std::size_t(root) + 8 + 43 * i;
        unsigned cls = number(at + 11);
        require(part(at + 10, 1)[0] == 1 && number(at + 27) == 7 && number(at + 35) == cls,
                "unsupported class index");
        if (i == 0) {
            require(cls == 1 && number(at + 8, 2) == count - 1 && number(at + 31) == 0,
                    "invalid master class");
            master_list = number(at + 19);
            continue;
        }
        bool known = std::find(core.begin(), core.end(), cls) != core.end() ||
                     std::find(auxiliary.begin(), auxiliary.end(), cls) != auxiliary.end() ||
                     cls == 0x59;
        require(known && !classes.contains(cls), "unknown or duplicate class");
        unsigned index = number(at + 31), record_count = number(at + 15);
        require(record_count > 0 && record_count <= 200000, "invalid record count");
        classes.emplace(cls, Class{record_count, index, number(at + 39)});
        std::uint64_t previous = 0;
        bool first = true;
        unsigned actual = 0;
        for (auto& row : tree(index, 8, seen)) {
            unsigned target = row[0], identity = row[1];
            require((first || identity > previous) && !object_ids.contains(identity) &&
                        !targets.contains(target),
                    "duplicate or unordered record identity");
            require(target >= 32, "null record");
            part(target, 6);
            object_ids.insert(identity);
            targets.insert(target);
            records[{cls, identity}] = target;
            require(records.size() <= 200000, "too many database records");
            previous = identity;
            first = false;
            ++actual;
        }
        require(actual == record_count, "record count mismatch");
    }
    std::set<unsigned> listed;
    for (auto& row : tree(master_list, 4, seen)) {
        master_order.push_back(row[0]);
        listed.insert(row[0]);
    }
    require(master_order.size() == count - 1 && listed.size() == classes.size(),
            "master class list mismatch");
    for (auto [cls, value] : classes) {
        (void)value;
        require(listed.contains(cls), "master class list mismatch");
    }
    occupied = nodes;
    for (auto [key, at] : records) {
        unsigned cls = key.first, size = cls == 0x53   ? 24
                                         : cls == 0x46 ? 12
                                         : cls == 0x57 ? 132
                                         : cls == 0x59 ? 6
                                                       : 14;
        part(at, size);
        require((data[at] & 0x80) != 0 && number(std::size_t(at) + 2) == 1,
                "unsupported record header");
        occupied.emplace_back(at, std::size_t(at) + size);
    }
    std::sort(occupied.begin(), occupied.end());
    for (std::size_t i = 1; i < occupied.size(); ++i) {
        require(occupied[i - 1].second <= occupied[i].first, "overlapping record or index");
    }
    for (unsigned cls : core) {
        require(classes.contains(cls), "missing core classes");
    }
    require(identities(0x53).size() <= 4096, "too many variables");
    require(!object_ids.empty() && number(16) > *object_ids.rbegin(),
            "invalid dynamic identity highwater");
    if (baseline) {
        require(classes.size() == core.size(), "baseline must be an original database");
    }
}

std::set<unsigned> Database::identities(unsigned cls) const {
    std::set<unsigned> result;
    auto at = records.lower_bound({cls, 0});
    while (at != records.end() && at->first.first == cls) {
        result.insert(at->first.second);
        ++at;
    }
    return result;
}

std::span<const std::uint8_t> Database::blob(unsigned at, unsigned limit) const {
    unsigned pointer = number(at), size = number(std::size_t(at) + 4);
    require((pointer != 0) == (size != 0) && size <= limit, "invalid blob descriptor");
    if (size) {
        auto next = std::upper_bound(
            occupied.begin(), occupied.end(),
            std::pair<std::size_t, std::size_t>{pointer, std::numeric_limits<std::size_t>::max()});
        require(pointer >= 32 && (next == occupied.begin() || pointer >= std::prev(next)->second) &&
                    (next == occupied.end() || std::uint64_t(pointer) + size <= next->first),
                "blob overlaps record or index");
    }
    return part(pointer, size);
}

Database::Variable Database::variable(unsigned identity) const {
    unsigned at = records.at({0x53, identity});
    auto name = blob(at + 6, 1024);
    require(
        name.size() >= 2 && name.back() == 0 &&
            std::all_of(name.begin(), name.end() - 1, [](auto c) { return c >= 32 && c < 127; }),
        "invalid variable name");
    auto metadata = part(at + 18, 6);
    require((metadata.back() & 0x7f) <= 3, "unsupported variable type");
    return {Bytes(name.begin(), name.end()), Bytes(metadata.begin(), metadata.end()),
            number(at + 14)};
}

Json Database::words(unsigned descriptor) const {
    auto bytes = blob(descriptor, 4096 * 4);
    require(bytes.size() % 4 == 0, "unaligned typed word array");
    Json::Array result;
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        unsigned word = 0;
        for (unsigned j = 0; j < 4; ++j) {
            word |= unsigned(bytes[i + j]) << (8 * j);
        }
        result.emplace_back(word);
    }
    return result;
}

std::map<unsigned, unsigned> dynamic_ids(const Database& base) {
    unsigned start = base.number(16);
    require(start <= UINT32_MAX - id_order.size(), "dynamic identity overflow");
    std::map<unsigned, unsigned> ids;
    for (unsigned i = 0; i < id_order.size(); ++i) {
        ids[id_order[i]] = start + i;
    }
    return ids;
}

std::set<unsigned> validate_baseline(const Database& base) {
    auto ids = base.identities(0x53);
    for (unsigned id : ids) {
        auto v = base.variable(id);
        if ((v.metadata.back() & 0x7f) == 3) {
            require(base.records.contains({0x50, v.value}), "missing baseline string reference");
        }
    }
    for (unsigned id : base.identities(0x50)) {
        base.blob(base.records.at({0x50, id}) + 6);
    }
    for (unsigned id : base.identities(0x46)) {
        require(base.part(base.records.at({0x46, id}) + 10, 1)[0] <= 1,
                "invalid baseline enabled state");
    }
    return ids;
}
}
