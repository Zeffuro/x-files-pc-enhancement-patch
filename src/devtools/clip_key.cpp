#include "clip_catalog.h"
#include <algorithm>
#include <cwctype>

namespace devtools {
std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), std::towlower);
    return text;
}

std::wstring catalog_key(const std::filesystem::path& path) {
    auto stem = path.stem().wstring();
    if (!stem.empty() && stem.find_first_not_of(L"0123456789") == std::wstring::npos) {
        const auto first = stem.find_first_not_of(L'0');
        stem = first == std::wstring::npos ? L"0" : stem.substr(first);
    }
    return lower(path.parent_path().generic_wstring() + L"/" + stem + path.extension().wstring());
}

}
