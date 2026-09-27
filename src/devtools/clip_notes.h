#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace devtools {

struct ClipNote {
    std::wstring label;
    std::wstring notes;
};

class ClipNotes {
public:
    static ClipNotes load(const std::filesystem::path& file);

    std::optional<ClipNote> get(const std::filesystem::path& relative_movie) const;
    void set(const std::filesystem::path& relative_movie, std::wstring label, std::wstring notes);
    void save(const std::filesystem::path& file) const;

private:
    static std::wstring key(const std::filesystem::path& relative_movie);
    std::map<std::wstring, ClipNote> entries_;
};

}
