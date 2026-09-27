#include "clip_catalog.h"
#include "media/files.h"
#include "identity.h"
#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace devtools {

namespace {
std::wstring canonical_place(const std::wstring& name) {
    const auto key = lower(name);
    if (key == L"comity" || key == L"comity inn") {
        return L"Comity Inn";
    }
    if (key == L"rail yard" || key == L"rail-yard") {
        return L"Rail Yard";
    }
    if (key == L"apt" || key == L"apartment") {
        return L"Apartment";
    }
    if (key == L"field office" || key == L"fbi field office") {
        return L"FBI field office";
    }
    if (key == L"coroner" || key == L"coroner's office") {
        return L"Coroner's office";
    }
    if (key == L"gun battle" || key == L"smolnikoff") {
        return L"Smolnikoff";
    }
    if (key == L"wong" || key == L"wong dock") {
        return L"Wong dock";
    }
    return name;
}
}

std::set<std::wstring> ClipCatalog::places(const std::filesystem::path& path) const {
    std::set<std::wstring> result;
    const auto authored = defaults.find(catalog_key(path));
    if (authored != defaults.end() && authored->second.place) {
        if (!authored->second.place->empty()) {
            result.insert(canonical_place(*authored->second.place));
        }
        return result;
    }
    if (index) {
        for (const auto& label : index->labels(path)) {
            if (!label.location.empty()) {
                result.insert(canonical_place(label.location));
            }
        }
    }
    if (const auto found = defaults.find(catalog_key(path)); found != defaults.end()) {
        const auto label = lower(found->second.label);
        for (const auto* place :
             {L"Apartment", L"Comity Inn", L"Coroner's office", L"Crime Lab", L"FBI field office",
              L"Field Office", L"Gun Battle", L"Hangar", L"Hauling Yard", L"Hospital", L"Rail Yard",
              L"Rail-yard", L"Rauch", L"Secret Base", L"Smolnikoff", L"Tarakan", L"Warehouse",
              L"Wong dock", L"Woods"}) {
            const auto prefix = lower(place);
            if (label == prefix || label.starts_with(prefix + L" - ")) {
                result.insert(canonical_place(place));
            }
        }
    }
    return result;
}

std::set<std::wstring> ClipCatalog::places() const {
    std::set<std::wstring> result;
    for (const auto& path : paths) {
        const auto found = places(path);
        result.insert(found.begin(), found.end());
    }
    return result;
}

std::wstring ClipCatalog::comment(const std::filesystem::path& path) const {
    const auto found = defaults.find(catalog_key(path));
    return found == defaults.end() ? L"" : found->second.notes;
}

std::wstring ClipCatalog::metadata(const std::filesystem::path& path, bool comments) const {
    std::wstring result;
    if (const auto found = defaults.find(catalog_key(path)); found != defaults.end()) {
        result = found->second.label;
        if (comments && !found->second.notes.empty()) {
            result += L"\r\n" + found->second.notes;
        }
    }
    std::set<std::wstring> seen;
    if (index) {
        for (const auto& label : index->labels(path)) {
            const auto authored = defaults.find(catalog_key(path));
            const auto location = authored != defaults.end() && authored->second.place
                                      ? *authored->second.place
                                      : canonical_place(label.location);
            auto text = location.empty() ? label.scene : location + L" / " + label.scene;
            if (label.node) {
                text += L" / Node " + std::to_wstring(*label.node);
            }
            if (!seen.insert(text).second) {
                continue;
            }
            if (!result.empty()) {
                result += L"\r\n";
            }
            result += text;
        }
    }
    return result.empty() ? L"No authoring label found" : result;
}

void ClipCatalog::load() {
    paths.clear();

    installed_keys.clear();
    auto hdb = media::locate_file(L"XFILES.HDB");
    if (!hdb.empty()) {
        hdb = std::filesystem::absolute(hdb);
    }
    index = hdb.empty() ? std::nullopt : game_assets::ClipIndex::load(hdb);
    if (!hdb.empty()) {
        root = hdb.parent_path();
        notes_path = root / L"tools" / ("clip-notes-" + sha256(hdb).substr(0, 16) + ".tsv");
        for (const auto& directory : {L"XN", L"XV", L"XG", L"XS", L"XT", L""}) {
            std::error_code error;
            const auto folder = root / directory;
            for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end;
                 it.increment(error)) {
                const auto& entry = *it;
                if (!entry.is_regular_file(error)) {
                    continue;
                }
                const auto extension = lower(entry.path().extension().wstring());
                if (extension == L".xmv" || extension == L".amv" || extension == L".nmv" ||
                    extension == L".dmv") {
                    paths.push_back(entry.path().lexically_relative(root));
                    installed_keys.insert(catalog_key(paths.back()));
                    if (paths.size() > 10000) {
                        throw std::runtime_error("Too many movie files to index");
                    }
                }
            }
        }
    }
    const auto installed = paths.size();
    auto keys = installed_keys;
    if (index) {
        for (const auto& entry : index->entries()) {
            if (keys.insert(catalog_key(entry.movie)).second) {
                paths.emplace_back(entry.movie);
            }
        }
    }
    defaults.clear();
    for (const auto& [path, note] : load_defaults(root)) {
        defaults.emplace(catalog_key(path), note);
    }
    std::sort(paths.begin(), paths.end());
    status = std::to_wstring(installed) + L" installed movies, " + std::to_wstring(paths.size()) +
             L" catalog entries. ";
    status += index ? L"Authoring labels describe clips, not the live scene."
                    : L"Game database unavailable. Labels could not be loaded.";
}

}
