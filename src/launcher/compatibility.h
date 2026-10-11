#pragma once

#include "identity.h"
#include "session.h"
#include "setup/catalog.h"

#include <span>
#include <string_view>
#include <vector>

struct InstallationCatalog {
    MediaSet set;
    std::map<std::wstring, MediaRecord> files;
};

struct CompatibilityReport {
    Identity identity{};
    Session session;
    std::wstring media_set;
    std::size_t checked_files = 0;
    std::size_t changed_sizes = 0;
    std::size_t dvd_files = 0;
    std::size_t unavailable_dvd_files = 0;
    std::vector<std::wstring> problems;
    std::vector<std::wstring> warnings;

    std::wstring text() const;
};

CompatibilityReport inspect_installation(const std::filesystem::path& directory);
CompatibilityReport inspect_installation_files(const std::filesystem::path& directory,
                                               const Identity& identity, const Session& session,
                                               std::span<const InstallationCatalog> catalogs);
int show_compatibility(const std::filesystem::path& directory);
bool launcher_check_requested(std::wstring_view command_line);
