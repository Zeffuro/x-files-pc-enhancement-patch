#pragma once
#include "offline_index.h"
#include <filesystem>
#include <functional>

namespace game_assets {
enum class ReferenceNamespace { file, hdb, gam };

struct ReferenceSource {
    std::filesystem::path path;
    ReferenceNamespace space = ReferenceNamespace::file;
    std::span<const std::uint8_t> bytes;
    const OfflineDatabaseIndex* index = nullptr;
};

struct StoredReference {
    std::size_t source = 0;
    std::uint32_t source_class = 0, source_id = 0;
    std::optional<std::size_t> target_source;
    ReferenceNamespace target_space = ReferenceNamespace::file;
    std::uint32_t target_class = 0, target_id = 0;
    std::string field, function;
    bool resolved = false;
};

struct ReferenceIndex {
    std::vector<std::filesystem::path> files;
    std::vector<ReferenceNamespace> spaces;
    std::vector<StoredReference> references;
    std::vector<std::pair<std::size_t, StoredDatabaseRecord>> rejected;
    std::size_t scanned = 0, unsupported = 0, malformed = 0, unresolved = 0;
    std::size_t decoded_bytes = 0;
    bool incomplete = false, cancelled = false;
};

struct ReferenceLimits {
    std::size_t max_records = 200000, max_references = 250000;
    std::size_t max_malformed = 32;
    std::size_t max_decoded_bytes = 32u * 1024u * 1024u;
    std::function<bool()> cancelled;
};

ReferenceIndex build_reference_index(std::span<const ReferenceSource> sources,
                                     ReferenceLimits limits = {});
std::vector<const StoredReference*> references_to(const ReferenceIndex& index,
                                                  const std::filesystem::path& file,
                                                  std::uint32_t class_id, std::uint32_t id);
std::vector<const StoredReference*> function_callers(const ReferenceIndex& index,
                                                     const std::string& name);
std::string stored_function_name(std::span<const std::uint8_t> bytes);
}
