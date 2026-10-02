#include "source.h"
#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace devtools {
BrowserSource browser_source(const std::filesystem::path& requested) {
    std::error_code error;
    const auto path = std::filesystem::canonical(requested, error);
    if (error) {
        throw std::runtime_error("Choose an existing folder, database or asset file");
    }
    BrowserSource result;
    if (std::filesystem::is_directory(path, error)) {
        result.root = path;
    } else if (std::filesystem::is_regular_file(path, error)) {
        result.root = path.parent_path();
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](wchar_t ch) { return wchar_t(std::towlower(ch)); });
        if (extension == L".hdb" || extension == L".gam" || extension == L".x") {
            result.database = path;
        } else {
            result.asset = path.filename();
        }
    } else {
        throw std::runtime_error("Choose an existing folder, database or asset file");
    }
    return result;
}
}
