#include "variables.h"
#include "database/memory.h"
#include "game/layouts/database/variable.h"
#include "game/layouts/input/inventory_selection.h"
#include "game/profiles/variables.h"
#include <windows.h>
#include <sstream>

namespace devtools {
namespace {
template <class T> bool read(const void* address, T& value) {
    return database_copy(reinterpret_cast<std::uintptr_t>(address), &value, sizeof(value));
}
}

std::wstring inspect_variables(const std::byte* image, const native_game::Profile& profile) {
    std::wostringstream text;
    text << L"\r\n\r\nRegistered story variables (22 slots)\r\nRegistration / resource ID / raw "
            L"value / type";
    for (const auto& slot : native_game::registered_variables) {
        text << L"\r\n" << slot.registration;
        if (slot.label) {
            text << L" - " << slot.label;
        }
        text << L": ";
        std::uint32_t pointer = 0;
        native_game::Variable variable{};
        const auto rva = slot.rva(profile);
        if (!image || !rva || !read(image + rva, pointer)) {
            text << L"unavailable";
            continue;
        }
        if (!pointer) {
            text << L"not registered";
            continue;
        }
        if (!read(reinterpret_cast<const void*>(pointer), variable)) {
            text << L"unavailable (unreadable object)";
            continue;
        }
        text << variable.id << L" / " << variable.raw_value << L" (0x" << std::hex
             << static_cast<std::uint32_t>(variable.raw_value) << std::dec << L") / "
             << static_cast<unsigned>(variable.type_flags & 0x7f);
        if (std::wstring_view(slot.registration) == L"RegCurrInvVar [1]" &&
            (variable.type_flags & 0x7f) == 1) {
            if (const auto name = native_game::inventory_selection_name(variable.raw_value);
                !name.empty()) {
                text << L" - " << name;
            }
        }
    }
    text << L"\r\nThis is the registered subset. Raw values are not named quest flags.";
    return text.str();
}
}
