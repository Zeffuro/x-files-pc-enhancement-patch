#include "browser_state.h"
#include "game/database/stored_format.h"
#include <algorithm>

namespace devtools::database_browser {
namespace {
game_assets::ReferenceNamespace reference_namespace(const std::filesystem::path& path) {
    const auto extension = lower(path.extension().wstring());
    return extension == L".hdb"   ? game_assets::ReferenceNamespace::hdb
           : extension == L".gam" ? game_assets::ReferenceNamespace::gam
                                  : game_assets::ReferenceNamespace::file;
}

void index_references(Browser& state) {
    using namespace game_assets;
    if (state.references || !state.database) {
        return;
    }
    std::vector<ReferenceSource> sources{
        {state.path, reference_namespace(state.path), state.database->bytes(), &state.stored}};
    std::optional<Database> companion;
    OfflineDatabaseIndex companion_index;
    if (sources.front().space != ReferenceNamespace::file) {
        auto path = state.path;
        path.replace_extension(sources.front().space == ReferenceNamespace::hdb ? L".GAM"
                                                                                : L".HDB");
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (!error && size <= stored_format::max_database_bytes) {
            try {
                companion = Database::load(path);
                companion_index = parse_database_index(companion->bytes());
                sources.push_back(
                    {path, reference_namespace(path), companion->bytes(), &companion_index});
            } catch (const std::exception&) {
            }
        }
    }
    state.references = build_reference_index(sources);
}

HTREEITEM property(Browser& state, HTREEITEM parent, const std::wstring& text) {
    TVINSERTSTRUCTW node{};
    node.hParent = parent;
    node.hInsertAfter = TVI_LAST;
    node.item.mask = TVIF_TEXT;
    node.item.pszText = const_cast<LPWSTR>(text.c_str());
    return TreeView_InsertItem(state.properties, &node);
}

std::wstring widen(const std::string& text) {
    return {text.begin(), text.end()};
}
}

void reference_properties(Browser& state, std::uint32_t class_id, std::uint32_t id,
                          bool state_database, bool native_object) {
    using namespace game_assets;
    index_references(state);
    if (!state.references) {
        return;
    }
    const auto& index = *state.references;
    auto target_file = state.path;
    if (native_object) {
        const auto found =
            std::find(index.spaces.begin(), index.spaces.end(),
                      state_database ? ReferenceNamespace::gam : ReferenceNamespace::hdb);
        target_file = found == index.spaces.end()
                          ? std::filesystem::path{}
                          : index.files[static_cast<std::size_t>(found - index.spaces.begin())];
    }
    auto incoming = references_to(index, target_file, class_id, id);
    std::string function;
    if (!state_database && class_id == stored_format::class_id::action) {
        const auto decoded = std::find_if(
            index.references.begin(), index.references.end(), [&](const auto& reference) {
                return index.files[reference.source] == target_file &&
                       reference.source_class == class_id && reference.source_id == id &&
                       !reference.function.empty();
            });
        if (decoded != index.references.end()) {
            function = decoded->function;
            auto callers = function_callers(index, function);
            std::erase_if(callers, [&](const auto* reference) {
                return index.files[reference->source] == target_file &&
                       reference->source_class == class_id && reference->source_id == id;
            });
            incoming.insert(incoming.end(), callers.begin(), callers.end());
        }
    }
    const auto group = property(state, TVI_ROOT, L"References (stored definitions)");
    property(state, group, L"Selected namespace: " + target_file.wstring());
    property(state, group, L"Incoming references: " + std::to_wstring(incoming.size()));
    property(state, group,
             L"Indexed once per Refresh. Follows use the source file and exact class/ID.");
    property(
        state, group,
        L"Scope: action operands, enable targets, functions, lists, triggers and known bindings.");
    property(state, group,
             L"Stored links do not establish runtime eligibility or why an action is disabled.");
    property(state, group,
             L"Only kind 0 operands identify variables. Trigger-context operands stay unresolved.");
    if (!function.empty()) {
        property(state, group, L"Function callers: " + widen(function));
    }
    if (reference_namespace(state.path) != ReferenceNamespace::file && index.files.size() < 2) {
        property(state, group,
                 L"Companion HDB/GAM unavailable. Cross-file references are unresolved.");
    }
    property(state, group,
             L"Unresolved targets: " + std::to_wstring(index.unresolved) +
                 L" | Unsupported records: " + std::to_wstring(index.unsupported) +
                 L" | Malformed records: " + std::to_wstring(index.malformed));
    if (index.incomplete) {
        property(state, group,
                 L"Reference scan incomplete. Missing results are not proof of no use.");
    }
    for (const auto& [file, rejected] : index.rejected) {
        property(state, group,
                 L"Unresolved definition: " + index.files[file].wstring() + L" | " +
                     hex(rejected.class_id) + L" ID " + std::to_wstring(rejected.id));
    }
    for (const auto& reference : index.references) {
        if (index.files[reference.source] != target_file || reference.source_class != class_id ||
            reference.source_id != id || reference.target_space != ReferenceNamespace::gam) {
            continue;
        }
        const auto path = reference.target_source ? index.files[*reference.target_source]
                                                  : std::filesystem::path{};
        if (path.empty()) {
            continue;
        }
        state.links.push_back(
            {{L"To", L"Reference: " + widen(reference.field),
              L"GAM " + std::wstring(database_class_name(reference.target_class)) + L" ID " +
                  std::to_wstring(reference.target_id),
              reference.resolved ? path.wstring() + L" | Stored definition"
                                 : L"Unresolved GAM target"},
             {reference.resolved ? std::optional<DatabaseObjectKey>{{reference.target_class,
                                                                     reference.target_id, false}}
                                 : std::nullopt,
              {},
              true,
              path}});
    }
    for (const auto* reference : incoming) {
        const auto& path = index.files[reference->source];
        const auto label = std::wstring(database_class_name(reference->source_class)) + L" ID " +
                           std::to_wstring(reference->source_id);
        state.links.push_back(
            {{L"From", L"Reference: " + widen(reference->field), label,
              path.wstring() +
                  (reference->resolved ? L" | Stored definition" : L" | Unresolved target")},
             {DatabaseObjectKey{reference->source_class, reference->source_id, false},
              {},
              true,
              path}});
    }
    TreeView_Expand(state.properties, group, TVE_EXPAND);
}
}
