#include "setup/catalog.h"

#include <iostream>
#include <stdexcept>

namespace {
using Hashes = std::map<std::wstring, std::set<std::string>>;
constexpr const wchar_t* names[] = {L"xfiles.exe",  L"xfiles.hdb",  L"xfiles.gam", L"xfilesc.dll",
                                    L"xfilese.dll", L"xfiless.dll", L"xfilest.dll"};

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

Hashes identity(MediaSetId set) {
    Hashes hashes;
    const auto catalog = media_catalog(set);
    for (const auto* name : names) {
        hashes[name].insert(catalog.at(name).sha256);
    }
    return hashes;
}

void reject(const Hashes& hashes) {
    try {
        identify_media_set(hashes);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("Invalid or ambiguous media identity accepted");
}
}

int main() {
    try {
        // Observed Release 1.00/US identity: the other six core files match UK.
        auto us = identity(MediaSetId::cd_en);
        us[L"xfilesc.dll"] = {"7d18aa903ee3800cb3a285141d8942b88100b6b030befa04e5762e9e672b49d4"};
        require(identify_media_set(us) == MediaSetId::cd_en_us,
                "US retail credits DLL prevented English CD identification");
        const auto uk_catalog = media_catalog(MediaSetId::cd_en);
        const auto us_catalog = media_catalog(MediaSetId::cd_en_us);
        require(us_catalog.size() == uk_catalog.size(), "US catalog lost required files");
        for (const auto& [name, record] : uk_catalog) {
            const auto& actual = us_catalog.at(name);
            require(actual.size == record.size, "US catalog changed a required size");
            require(actual.sha256 ==
                        (name == L"xfilesc.dll" ? *us.at(name).begin() : record.sha256),
                    "US installation catalog does not preserve exact checksums");
        }
        for (const auto& set : media_sets()) {
            const auto hashes = identity(set.id);
            require(identify_media_set(hashes) == set.id, "Supported edition regressed");
            for (const auto* name : names) {
                auto missing = hashes;
                missing.erase(name);
                reject(missing);
                auto damaged = hashes;
                damaged[name] = {std::string(64, '0')};
                reject(damaged);
            }
            for (const auto& other : media_sets()) {
                if (set.id == other.id) {
                    continue;
                }
                auto mixed = hashes;
                for (const auto& [name, values] : identity(other.id)) {
                    mixed[name].insert(values.begin(), values.end());
                }
                reject(mixed);
            }
        }
        std::cout << "All media catalog identity checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
