#include "inspector_internal.h"
#include <algorithm>

namespace devtools::inspector {
namespace {
class Details {
public:
    void add(std::wstring key, std::wstring parent, std::wstring text, bool expanded = false) {
        rows.push_back({std::move(key), std::move(parent), std::move(text), expanded});
    }

    void field(const std::wstring& parent, const std::wstring& key, const std::wstring& label,
               std::wstring value) {
        std::replace(value.begin(), value.end(), L'\r', L'\n');
        if (value.size() + label.size() < 100 && value.find(L'\n') == std::wstring::npos) {
            add(key, parent, label + L": " + value);
            return;
        }
        add(key, parent, label);
        unsigned part = 0;
        for (std::size_t start = 0; start < value.size();) {
            if (value[start] == L'\n') {
                ++start;
                continue;
            }
            auto end = std::min(start + 90, value.size());
            const auto line = value.find(L'\n', start);
            if (line != std::wstring::npos && line < end) {
                end = line;
            } else if (end < value.size()) {
                const auto space = value.rfind(L' ', end);
                if (space != std::wstring::npos && space > start) {
                    end = space;
                }
            }
            add(key + L"/" + std::to_wstring(part++), key, value.substr(start, end - start));
            start = end;
            if (start < value.size() && value[start] == L' ') {
                ++start;
            }
        }
    }

    std::vector<PreviewDetail> rows;
};

std::wstring yes(bool value) {
    return value ? L"Yes" : L"No";
}

Details describe_clip() {
    Details result;
    result.add(L"clip", L"", L"Clip", true);
    result.add(L"labels", L"", L"Authoring labels");
    result.add(L"playback", L"", L"Playback");
    result.add(L"live", L"", L"Live activity");
    if (state.selected.empty()) {
        result.add(L"clip/empty", L"clip", L"Select a clip to inspect it.");
        return result;
    }
    result.field(L"clip", L"clip/path", L"Path", state.selected.generic_wstring());
    result.field(L"clip", L"clip/present", L"File",
                 state.catalog.installed_keys.contains(catalog_key(state.selected))
                     ? L"Installed"
                     : L"Missing from local game folders");
    result.field(L"clip", L"clip/format", L"Format", lower(state.selected.extension().wstring()));
    if (const auto note = state.annotations.get(state.selected); note && !note->label.empty()) {
        result.field(L"clip", L"clip/label", L"Your label", note->label);
    }
    const auto authored = state.catalog.defaults.find(catalog_key(state.selected));
    if (authored != state.catalog.defaults.end()) {
        if (!authored->second.label.empty()) {
            result.field(L"labels", L"labels/catalog", L"Catalog label", authored->second.label);
        }
        if (!authored->second.notes.empty()) {
            result.field(L"labels", L"labels/notes", L"Catalog notes", authored->second.notes);
        }
        if (authored->second.place && !authored->second.place->empty()) {
            result.field(L"labels", L"labels/place", L"Catalog place", *authored->second.place);
        }
    }
    const auto labels = state.catalog.index ? state.catalog.index->labels(state.selected)
                                            : std::vector<game_assets::ClipLabel>{};
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const auto& label = labels[i];
        const auto key = L"labels/authoring/" + std::to_wstring(i);
        result.add(key, L"labels", L"HDB label " + std::to_wstring(i + 1));
        for (const auto& [name, value] : {std::pair{L"Location", label.location},
                                          {L"Scene", label.scene},
                                          {L"Kind", label.kind},
                                          {L"Category", label.category}}) {
            if (!value.empty()) {
                result.field(key, key + L"/" + name, name, value);
            }
        }
        if (label.node) {
            result.field(key, key + L"/node", L"Node", std::to_wstring(*label.node));
        }
    }
    if (labels.empty() && authored == state.catalog.defaults.end()) {
        result.add(L"labels/empty", L"labels", L"No authoring label found.");
    }
    result.add(L"labels/source", L"labels", L"Labels describe stored clips.");
    result.add(L"labels/identity", L"labels", L"Current scene identity is unverified.");

    const auto* live = selected_movie();
    if (state.player) {
        result.field(L"playback", L"playback/source", L"Source", L"Independent preview");
        result.field(L"playback", L"playback/state", L"State",
                     state.player->playing() ? L"Playing" : L"Paused");
        result.field(L"playback", L"playback/video", L"Video", yes(state.player->has_video()));
        result.field(L"playback", L"playback/audio", L"Audio", yes(state.player->has_audio()));
        const auto& frame = state.player->frame();
        if (!frame.pixels.empty()) {
            result.field(L"playback", L"playback/size", L"Frame size",
                         std::to_wstring(frame.width) + L" x " + std::to_wstring(frame.height));
        }
        result.add(L"playback/hint", L"playback",
                   state.player->has_video() ? L"The game keeps running during preview."
                                             : L"Use Play preview to listen.");
    } else {
        result.field(L"playback", L"playback/source", L"Source",
                     live ? L"Last decoded game frame" : L"No open preview");
        result.add(L"playback/hint", L"playback",
                   state.browsing ? L"Use Play preview to open this clip."
                                  : L"Other game clips can cover the last decoded frame.");
    }
    if (media::navigation_archive(state.selected)) {
        const auto image = state.player ? state.player->image() : live ? live->image : std::nullopt;
        if (image) {
            result.field(L"playback", L"playback/image", L"Image",
                         std::to_wstring(image->sample + 1) + L" of " +
                             std::to_wstring(image->count));
            result.field(L"playback", L"playback/track", L"Track", std::to_wstring(image->track));
            result.field(L"playback", L"playback/key", L"Image key",
                         media::frame_key(state.selected, *image));
        } else {
            result.add(L"playback/image", L"playback", L"Navigation image has not been decoded.");
        }
    }
    if (live) {
        result.field(L"live", L"live/id", L"Movie ID", std::to_wstring(live->id));
        result.field(L"live", L"live/state", L"Activity", activity(*live));
        result.field(L"live", L"live/video", L"Video", yes(live->video));
        result.field(L"live", L"live/audio", L"Audio", yes(live->audio));
        result.field(L"live", L"live/size", L"Draw size",
                     std::to_wstring(live->width) + L" x " + std::to_wstring(live->height));
        result.field(L"live", L"live/position", L"Draw position",
                     std::to_wstring(live->left) + L", " + std::to_wstring(live->top));
        if (live->preview.pixels.empty()) {
            result.add(L"live/frame", L"live", L"No decoded video frame is available.");
        }
    } else {
        result.add(L"live/empty", L"live", L"This clip is not currently open in the game.");
    }
    return result;
}
}

void update_preview_details() {
    if (!state.details || state.showing_state || state.showing_database) {
        return;
    }
    state.preview_details.update(state.details, describe_clip().rows);
}
}
