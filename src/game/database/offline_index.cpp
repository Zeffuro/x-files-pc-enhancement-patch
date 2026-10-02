#include "offline_index.h"
#include <algorithm>
#include <set>
#include <tuple>

namespace game_assets {
namespace {
struct Node {
    std::uint32_t offset, leaf_class, object_class;
    unsigned depth;
};

class Reader {
public:
    Reader(std::span<const std::uint8_t> bytes, OfflineDatabaseLimits limits)
        : bytes(bytes), limits(limits) {}

    OfflineDatabaseIndex read() {
        if (bytes.size() < 32 || bytes.size() > 128u * 1024u * 1024u || u32(0) != 5 ||
            u32(20) != 0x501 || u16(24) != 4 || u32(28) != 256) {
            result.status = L"Stored index unavailable. Unsupported database header.";
            return result;
        }
        const auto root = u32(8);
        if (!root) {
            result.available = true;
            result.status = L"Stored database has no class index.";
            return result;
        }
        pending.push_back({root, 3, 0, 0});
        for (std::size_t next = 0; next < pending.size(); ++next) {
            if (result.truncated) {
                break;
            }
            node(pending[next]);
        }
        std::sort(ranges.begin(), ranges.end());
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i].first < ranges[i - 1].second) {
                result.records.clear();
                result.status = L"Stored index unavailable. Index nodes overlap.";
                return result;
            }
        }
        result.node_ranges = ranges;
        std::erase_if(result.records, [&](const auto& record) {
            if (!record.offset) {
                return false;
            }
            const auto found =
                std::upper_bound(ranges.begin(), ranges.end(), record.offset,
                                 [](auto value, const auto& range) { return value < range.first; });
            if (found != ranges.begin() && record.offset < std::prev(found)->second) {
                ++result.skipped_nodes;
                return true;
            }
            return false;
        });
        std::sort(result.records.begin(), result.records.end(), [](const auto& a, const auto& b) {
            return std::tie(a.class_id, a.id, a.offset) < std::tie(b.class_id, b.id, b.offset);
        });
        for (const auto& record : result.records) {
            if (record.offset) {
                result.definition_offsets.push_back(record.offset);
            }
        }
        std::sort(result.definition_offsets.begin(), result.definition_offsets.end());
        for (std::size_t i = 1; i < result.records.size(); ++i) {
            if (result.records[i].class_id == result.records[i - 1].class_id &&
                result.records[i].id == result.records[i - 1].id) {
                ++result.duplicate_keys;
            }
        }
        result.available = visited.contains(root) && root_valid;
        result.status = std::to_wstring(result.records.size()) + L" stored index entries, " +
                        std::to_wstring(result.visited_nodes) + L" index nodes.";
        if (!result.available) {
            result.status = L"Stored index unavailable. Class root is invalid.";
        } else if (result.truncated || result.skipped_nodes || result.unsupported_indexes ||
                   result.duplicate_keys) {
            result.status += L" Enumeration is incomplete or contains ambiguous entries.";
        }
        return std::move(result);
    }

private:
    bool has(std::size_t at, std::size_t count) const {
        return at <= bytes.size() && count <= bytes.size() - at;
    }

    std::uint16_t u16(std::size_t at) const {
        return static_cast<std::uint16_t>((unsigned(bytes[at]) << 8) | bytes[at + 1]);
    }

    std::uint32_t u32(std::size_t at) const {
        return (std::uint32_t(bytes[at]) << 24) | (std::uint32_t(bytes[at + 1]) << 16) |
               (std::uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
    }

    void child(std::uint32_t offset, std::uint32_t leaf, std::uint32_t object_class,
               unsigned depth) {
        if (!offset) {
            return;
        }
        if (pending.size() >= limits.max_nodes) {
            result.truncated = true;
        } else {
            pending.push_back({offset, leaf, object_class, depth});
        }
    }

    void node(Node current) {
        if (current.depth >= limits.max_depth || visited.size() >= limits.max_nodes) {
            result.truncated = true;
            return;
        }
        if (!visited.insert(current.offset).second) {
            ++result.skipped_nodes;
            return;
        }
        ++result.visited_nodes;
        std::size_t at = current.offset;
        if (at < 32 || !has(at, 8)) {
            ++result.skipped_nodes;
            return;
        }
        const auto count = u16(at + 6);
        const bool leaf = (bytes[at] & 0x80) != 0;
        if (count > 255 || (leaf && current.leaf_class == 3 && count > 16)) {
            ++result.skipped_nodes;
            return;
        }
        at += 8;
        if (!leaf) {
            if (!has(at, std::size_t(count) * 4)) {
                ++result.skipped_nodes;
                return;
            }
            for (unsigned i = 0; i < count; ++i) {
                child(u32(at), current.leaf_class, current.object_class, current.depth + 1);
                at += 4;
            }
        } else if (current.leaf_class == 3) {
            std::vector<Node> indexes;
            for (unsigned i = 0; i < count; ++i) {
                if (!has(at, 27) || bytes[at + 10] > 6) {
                    ++result.skipped_nodes;
                    return;
                }
                const auto index_count = bytes[at + 10];
                at += 27;
                if (!has(at, std::size_t(index_count) * 16)) {
                    ++result.skipped_nodes;
                    return;
                }
                for (unsigned j = 0; j < index_count; ++j) {
                    if (u32(at) == 7) {
                        indexes.push_back({u32(at + 4), 7, u32(at + 8), current.depth + 1});
                    } else {
                        ++result.unsupported_indexes;
                    }
                    at += 16;
                }
            }
            for (const auto& index : indexes) {
                child(index.offset, index.leaf_class, index.object_class, index.depth);
            }
        } else {
            if (!has(at, std::size_t(count) * 8)) {
                ++result.skipped_nodes;
                return;
            }
            for (unsigned i = 0; i < count; ++i) {
                const auto offset = u32(at), id = u32(at + 4);
                at += 8;
                if (offset && (offset < 32 || !has(offset, 6))) {
                    ++result.skipped_nodes;
                    continue;
                }
                if (result.records.size() >= limits.max_records) {
                    result.truncated = true;
                    break;
                }
                result.records.push_back({current.object_class, id, offset});
            }
        }
        ranges.emplace_back(current.offset, at);
        if (current.offset == u32(8)) {
            root_valid = true;
        }
    }

    std::span<const std::uint8_t> bytes;
    OfflineDatabaseLimits limits;
    OfflineDatabaseIndex result;
    std::vector<Node> pending;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    std::set<std::uint32_t> visited;
    bool root_valid = false;
};
}

OfflineDatabaseIndex parse_database_index(std::span<const std::uint8_t> bytes,
                                          OfflineDatabaseLimits limits) {
    return Reader(bytes, limits).read();
}

std::optional<std::size_t> find_database_record(const OfflineDatabaseIndex& index,
                                                std::uint32_t class_id, std::uint32_t id) {
    if (!index.available || index.truncated || index.skipped_nodes || index.unsupported_indexes) {
        return std::nullopt;
    }
    const auto key = std::pair{class_id, id};
    const auto found = std::lower_bound(index.records.begin(), index.records.end(), key,
                                        [](const auto& record, const auto& target) {
                                            return std::pair{record.class_id, record.id} < target;
                                        });
    if (found == index.records.end() || found->class_id != class_id || found->id != id ||
        !found->offset) {
        return std::nullopt;
    }
    const auto next = std::next(found);
    if (next != index.records.end() && next->class_id == class_id && next->id == id) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - index.records.begin());
}
}
