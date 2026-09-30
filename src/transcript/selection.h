#pragma once
#include <cstdint>
#include <string>
#include <utility>

namespace transcript {
class Selection {
public:
    void begin(std::wstring text, std::uint64_t now) {
        text_ = std::move(text);
        until_ = now + 1000;
        pressed_ = true;
    }

    void release(std::uint64_t now) {
        pressed_ = false;
        until_ = now + 1000;
    }

    void cancel() {
        text_.clear();
    }

    bool pending() const {
        return !text_.empty();
    }

    std::wstring update(bool accepted, std::uint64_t now) {
        if (!pressed_ && now >= until_) {
            cancel();
        }
        if (!accepted) {
            return {};
        }
        return std::exchange(text_, {});
    }

private:
    std::wstring text_;
    std::uint64_t until_ = 0;
    bool pressed_ = false;
};
}
