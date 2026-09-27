#pragma once
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace devtools {
inline std::uint64_t subtitle_time(std::wstring_view text) {
    const auto fail = []() {
        throw std::runtime_error("Enter seconds, minutes:seconds or hours:minutes:seconds.");
    };
    const auto first = text.find_first_not_of(L" \t");
    if (first == text.npos) {
        fail();
    }
    text = text.substr(first, text.find_last_not_of(L" \t") - first + 1);
    std::uint64_t total = 0, part = 0;
    unsigned fields = 0, digits = 0;
    std::size_t at = 0;
    for (; at < text.size() && text[at] != L'.' && text[at] != L','; ++at) {
        const auto c = text[at];
        if (c == L':') {
            if (!digits || fields >= 2 || (fields && part >= 60)) {
                fail();
            }
            total = total * 60 + part;
            part = 0;
            digits = 0;
            ++fields;
        } else {
            if (c < L'0' || c > L'9' || digits >= 7) {
                fail();
            }
            part = part * 10 + c - L'0';
            ++digits;
        }
    }
    if (!digits || (fields && part >= 60)) {
        fail();
    }
    total = (total * 60 + part) * 1000;
    if (at < text.size()) {
        const auto fraction = text.substr(at + 1);
        if (fraction.empty() || fraction.size() > 3) {
            fail();
        }
        unsigned multiplier = 100;
        for (const auto c : fraction) {
            if (c < L'0' || c > L'9') {
                fail();
            }
            total += (c - L'0') * multiplier;
            multiplier /= 10;
        }
    }
    return total;
}
}
