#include "reference_index.h"
#include "stored_action.h"
#include "stored_fields.h"
#include "stored_format.h"
#include "stored_list.h"
#include "stored_trigger.h"
#include <algorithm>
#include <set>
#include <tuple>

namespace game_assets {
namespace format = stored_format;

namespace {
constexpr std::size_t max_sources = 2;
constexpr std::size_t max_rejected_samples = 32;

bool descriptor(const ReferenceSource& source, const StoredDatabaseRecord& record,
                std::size_t size) {
    const auto at = std::size_t(record.offset);
    if (at < format::header_size || at > source.bytes.size() || size > source.bytes.size() - at ||
        !(source.bytes[at] & format::definition_flag)) {
        return false;
    }
    const auto& index = *source.index;
    const auto aliases = std::equal_range(index.definition_offsets.begin(),
                                          index.definition_offsets.end(), record.offset);
    if (std::distance(aliases.first, aliases.second) != 1) {
        return false;
    }
    const auto next = std::upper_bound(index.definition_offsets.begin(),
                                       index.definition_offsets.end(), record.offset);
    if (next != index.definition_offsets.end() && *next < at + size) {
        return false;
    }
    const auto range =
        std::lower_bound(index.node_ranges.begin(), index.node_ranges.end(), at,
                         [](const auto& value, auto start) { return value.second <= start; });
    return range == index.node_ranges.end() || range->first >= at + size;
}
}

std::string stored_function_name(std::span<const std::uint8_t> bytes) {
    std::string result;
    for (const auto byte : bytes) {
        if (!byte) {
            break;
        }
        if (byte < ' ' || byte > '~') {
            return {};
        }
        result += static_cast<char>(byte);
    }
    return result;
}

ReferenceIndex build_reference_index(std::span<const ReferenceSource> sources,
                                     ReferenceLimits limits) {
    ReferenceIndex result;
    if (sources.empty() || sources.size() > max_sources) {
        result.incomplete = true;
        return result;
    }
    for (const auto& source : sources) {
        result.files.push_back(source.path);
        result.spaces.push_back(source.space);
    }
    if (sources[0].path.empty() ||
        (sources.size() == max_sources &&
         (sources[1].path.empty() || sources[0].path == sources[1].path))) {
        result.incomplete = true;
        return result;
    }
    const auto binding = [&](std::size_t current, ReferenceNamespace space) {
        if (space == ReferenceNamespace::file) {
            return std::optional{current};
        }
        std::optional<std::size_t> found;
        for (std::size_t i = 0; i < sources.size(); ++i) {
            if (sources[i].space == space) {
                if (found) {
                    return std::optional<std::size_t>{};
                }
                found = i;
            }
        }
        return found;
    };
    using Key =
        std::tuple<std::size_t, std::uint32_t, std::uint32_t, std::optional<std::size_t>,
                   ReferenceNamespace, std::uint32_t, std::uint32_t, std::string, std::string>;
    std::set<Key> seen;
    bool stopped = false;
    for (std::size_t file = 0; file < sources.size() && !stopped; ++file) {
        const auto& source = sources[file];
        if (!source.index || !source.index->available || source.index->truncated ||
            source.index->skipped_nodes || source.index->unsupported_indexes ||
            source.bytes.size() > format::max_database_bytes) {
            result.incomplete = true;
            continue;
        }
        for (const auto& record : source.index->records) {
            if (limits.cancelled && limits.cancelled()) {
                result.cancelled = result.incomplete = true;
                stopped = true;
                break;
            }
            if (result.scanned >= limits.max_records ||
                result.decoded_bytes >= limits.max_decoded_bytes ||
                result.malformed >= limits.max_malformed) {
                result.incomplete = stopped = true;
                break;
            }
            ++result.scanned;
            const auto reject = [&] {
                ++result.malformed;
                if (result.rejected.size() < max_rejected_samples) {
                    result.rejected.emplace_back(file, record);
                }
            };
            if (!find_database_record(*source.index, record.class_id, record.id)) {
                reject();
                continue;
            }
            const auto add = [&](ReferenceNamespace space, std::uint32_t cls, std::uint32_t id,
                                 std::string field, std::string function = {}) {
                if (!id && function.empty()) {
                    return;
                }
                const auto target = function.empty() ? binding(file, space) : std::nullopt;
                const auto key =
                    Key{file, record.class_id, record.id, target, space, cls, id, field, function};
                if (!seen.insert(key).second) {
                    return;
                }
                if (result.references.size() >= limits.max_references) {
                    result.incomplete = stopped = true;
                    return;
                }
                const bool resolved =
                    !function.empty() ||
                    (target && sources[*target].index &&
                     bool(find_database_record(*sources[*target].index, cls, id)));
                result.unresolved += !resolved;
                result.references.push_back({file, record.class_id, record.id, target, space, cls,
                                             id, std::move(field), std::move(function), resolved});
            };
            const auto local = source.space == ReferenceNamespace::hdb ? ReferenceNamespace::hdb
                                                                       : ReferenceNamespace::file;
            if (record.class_id == format::class_id::action) {
                const auto action = parse_stored_action(source.bytes, *source.index,
                                                        format::class_id::action, record.id);
                if (!action) {
                    reject();
                    continue;
                }
                result.decoded_bytes += format::action::descriptor_size + action->payload.size();
                if ((action->action_type & format::action::type_mask) > format::action::sound ||
                    action->payload.empty()) {
                    ++result.unsupported;
                    continue;
                }
                if (!action->statement && !action->asset && !action->timer && !action->enable &&
                    !action->set_view && !action->interface_action && !action->function &&
                    !action->sound) {
                    reject();
                    continue;
                }
                const auto operand = [&](const StoredActionOperand& value, std::string field) {
                    if (value.kind == format::action::variable_operand) {
                        add(ReferenceNamespace::gam, format::class_id::variable, value.raw,
                            std::move(field));
                    }
                };
                const auto expression = [&](const auto& value, const std::string& field) {
                    if (value) {
                        operand(value->left, field + " left");
                        operand(value->right, field + " right");
                    }
                };
                expression(action->predicate, "Condition");
                expression(action->statement, "Statement");
                if (action->enable) {
                    add(ReferenceNamespace::gam, format::class_id::enabled_object,
                        action->enable->id, "Enable target");
                }
                if (action->function) {
                    const auto name = stored_function_name(action->function->name);
                    if (name.empty()) {
                        reject();
                    } else {
                        add(ReferenceNamespace::file, 0, 0, "Function caller", name);
                    }
                    for (std::size_t i = 0; i < action->function->arguments.size(); ++i) {
                        operand(action->function->arguments[i],
                                "Function argument " + std::to_string(i));
                    }
                }
            } else if (record.class_id == format::class_id::action_list ||
                       record.class_id == format::class_id::trigger_list) {
                const auto list =
                    descriptor(source, record, format::reference_list::descriptor_size)
                        ? parse_stored_reference_list(source.bytes, *source.index, record.class_id,
                                                      record.id)
                        : std::nullopt;
                if (!list) {
                    reject();
                    continue;
                }
                result.decoded_bytes +=
                    format::reference_list::descriptor_size + list->decoded_resource_bytes;
                for (const auto id : list->ids) {
                    add(local, list->target_class, id, "List member");
                }
            } else if (record.class_id == format::class_id::trigger) {
                const auto trigger = descriptor(source, record, format::trigger::descriptor_size)
                                         ? parse_stored_trigger(source.bytes, *source.index,
                                                                record.class_id, record.id)
                                         : std::nullopt;
                if (!trigger) {
                    reject();
                    continue;
                }
                result.decoded_bytes += format::trigger::descriptor_size;
                add(local, format::class_id::action_list, trigger->action_list_id,
                    "Trigger action list");
            } else if (record.class_id == format::class_id::title ||
                       record.class_id == format::class_id::standard_action ||
                       (record.class_id == format::class_id::enabled_object &&
                        source.space == ReferenceNamespace::gam)) {
                const auto fields =
                    parse_stored_fields(source.bytes, *source.index, record.class_id, record.id);
                if (!fields) {
                    reject();
                    continue;
                }
                result.decoded_bytes += fields->extent;
                for (const auto& blob : fields->blobs) {
                    result.decoded_bytes += blob.bytes.size();
                }
                if (fields->list) {
                    result.decoded_bytes += fields->list->decoded_resource_bytes;
                }
                if (record.class_id == format::class_id::enabled_object) {
                    if (fields->scalars.size() == format::enabled_object::scalar_count) {
                        add(ReferenceNamespace::hdb,
                            fields->scalars[format::enabled_object::class_scalar].value,
                            fields->scalars[format::enabled_object::object_scalar].value,
                            "Enabled object");
                    }
                    continue;
                }
                const auto offset = record.class_id == format::class_id::title
                                        ? format::title::trigger_list_offset
                                        : format::standard_action::action_offset;
                for (const auto& field : fields->scalars) {
                    if (field.offset == offset) {
                        add(local,
                            record.class_id == format::class_id::title
                                ? format::class_id::trigger_list
                                : format::class_id::action,
                            field.value,
                            record.class_id == format::class_id::title ? "Title trigger list"
                                                                       : "Action binding");
                    }
                }
            } else {
                ++result.unsupported;
            }
            if (stopped) {
                break;
            }
        }
    }
    result.incomplete = result.incomplete || result.malformed != 0;
    return result;
}

std::vector<const StoredReference*> references_to(const ReferenceIndex& index,
                                                  const std::filesystem::path& file,
                                                  std::uint32_t class_id, std::uint32_t id) {
    std::vector<const StoredReference*> result;
    for (const auto& reference : index.references) {
        if (reference.target_source && index.files[*reference.target_source] == file &&
            reference.target_class == class_id && reference.target_id == id &&
            reference.function.empty()) {
            result.push_back(&reference);
        }
    }
    return result;
}

std::vector<const StoredReference*> function_callers(const ReferenceIndex& index,
                                                     const std::string& name) {
    std::vector<const StoredReference*> result;
    for (const auto& reference : index.references) {
        if (!name.empty() && reference.function == name) {
            result.push_back(&reference);
        }
    }
    return result;
}
}
