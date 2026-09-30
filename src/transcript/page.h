#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace transcript {
class Page {
public:
    Page();
    ~Page();
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    HDC dc() const {
        return dc_;
    }

    void draw(HDC background, const std::vector<std::wstring>& entries, const std::wstring& note,
              std::size_t page);

    std::size_t pages() const {
        return starts_.size();
    }

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    HFONT font_ = nullptr, heading_ = nullptr;
    std::vector<std::size_t> starts_;
};

inline constexpr RECT previous_button{40, 422, 178, 463};
inline constexpr RECT next_button{186, 422, 324, 463};
inline constexpr RECT close_button{456, 422, 600, 463};
std::wstring edition_note(const std::filesystem::path& root);
}
