#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace enhancements {
struct SaveIdentity {
    std::uintptr_t state = 0, view = 0;
    std::wstring frame;
    bool operator==(const SaveIdentity&) const = default;
};

class AutosaveState {
public:
    bool update(std::uint64_t now, bool enabled, bool blocked,
                const std::optional<SaveIdentity>& identity) {
        if (!enabled) {
            candidate_.reset();
            last_.reset();
            transitioned_ = true;
            return false;
        }
        if (blocked) {
            candidate_.reset();
            return false;
        }
        if (!identity) {
            candidate_.reset();
            transitioned_ = true;
            return false;
        }
        if (candidate_ != identity) {
            candidate_ = identity;
            stable_since_ = now;
            return false;
        }
        if ((!transitioned_ && last_ == identity) || now < stable_since_ ||
            now - stable_since_ < 2000 ||
            (attempted_ && (now < attempted_at_ || now - attempted_at_ < 30000))) {
            return false;
        }
        attempted_ = true;
        attempted_at_ = now;
        transitioned_ = false;
        last_ = identity;
        return true;
    }

    void loaded(std::uint64_t now) {
        candidate_.reset();
        attempted_ = true;
        attempted_at_ = now;
        transitioned_ = true;
    }

private:
    std::optional<SaveIdentity> candidate_, last_;
    std::uint64_t stable_since_ = 0, attempted_at_ = 0;
    bool transitioned_ = true, attempted_ = false;
};
}
