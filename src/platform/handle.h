#pragma once

#include <utility>
#include <windows.h>

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}

    ~Handle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}

    Handle& operator=(Handle&&) = delete;

    HANDLE get() const {
        return value_;
    }

private:
    HANDLE value_;
};
