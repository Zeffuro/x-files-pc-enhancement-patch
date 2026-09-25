#include "destination.h"
#include "media.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
bool within(const fs::path& path, const fs::path& parent) {
    const auto lower = [](std::wstring text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return text;
    };
    auto child = path.begin();
    for (const auto& part : parent) {
        if (child == path.end() || lower(child->wstring()) != lower(part.wstring())) {
            return false;
        }
        ++child;
    }
    return true;
}
}

fs::path install_parent(const fs::path& destination) {
    auto parent = fs::absolute(destination).lexically_normal().parent_path();
    while (!parent.empty() && !fs::exists(parent)) {
        const auto next = parent.parent_path();
        if (next == parent) {
            break;
        }
        parent = next;
    }
    if (parent.empty() || !fs::is_directory(parent)) {
        throw std::runtime_error("Choose an installation folder on an available drive.");
    }
    return fs::canonical(parent);
}

void validate_destination(const MediaSource& source, const fs::path& destination) {
    const auto name = destination.filename();
    if (name.empty() || name == L"." || name == L".." ||
        fs::exists(fs::symlink_status(destination))) {
        throw std::runtime_error("Choose a new folder. To update, close the game and extract the "
                                 "new release ZIP into the existing game folder instead.");
    }
    const auto output = fs::weakly_canonical(fs::absolute(destination));
    if (within(output, fs::canonical(source.root))) {
        throw std::runtime_error("Choose a destination outside the source game/disc folder.");
    }
    const auto parent = install_parent(destination);
    constexpr std::uintmax_t reserve = 256ull * 1024 * 1024;
    const auto available = fs::space(parent).available;
    if (available < reserve || source.bytes > available - reserve) {
        throw std::runtime_error(
            "Not enough free space. Choose another drive or free space first.");
    }
}
