#include "hooks.h"
#include "store.h"
#include "runtime.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace preferences {

wchar_t settings_path[32768]{};

namespace {

constexpr char key_path[] = "software\\fox interactive\\the x-files\\preferences";

struct Key {
    std::string path;
    REGSAM access;
};

struct State {
    std::mutex mutex;
    std::unordered_map<HKEY, std::unique_ptr<Key>> keys;
    std::unique_ptr<Store> file;

    Store& store() {
        if (!file) {
            file = std::make_unique<Store>(settings_path);
            trace_value("portable_preferences_active", 1);
        }
        return *file;
    }
};

State& state() {
    static State instance;
    return instance;
}

template <typename F> LSTATUS guarded(F action) {
    try {
        std::lock_guard lock(state().mutex);
        return action();
    } catch (const std::bad_alloc&) {
        return ERROR_NOT_ENOUGH_MEMORY;
    } catch (const std::system_error& error) {
        return static_cast<LSTATUS>(error.code().value());
    } catch (const std::exception&) {
        return ERROR_INVALID_DATA;
    }
}

Key* find(HKEY handle) {
    const auto found = state().keys.find(handle);
    return found == state().keys.end() ? nullptr : found->second.get();
}

std::string path_for(HKEY parent, const char* child) {
    std::string path;
    if (const auto key = find(parent)) {
        path = key->path;
    } else if (parent != HKEY_CURRENT_USER) {
        return {};
    }
    if (child && *child) {
        if (!path.empty()) {
            path += '\\';
        }
        path += child;
    }
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    });
    while (!path.empty() && path.back() == '\\') {
        path.pop_back();
    }
    return path;
}

bool redirect(const std::string& path) {
    const std::string_view target = key_path;
    return !path.empty() && target.starts_with(path) &&
           (path.size() == target.size() || target[path.size()] == '\\');
}

bool child_of_preferences(const std::string& path) {
    return path.starts_with(std::string(key_path) + '\\');
}

LSTATUS open_local(const std::string& path, REGSAM access, HKEY* output, DWORD* disposition) {
    if (!output) {
        return ERROR_INVALID_PARAMETER;
    }
    auto key = std::make_unique<Key>(Key{path, access});
    const auto handle = reinterpret_cast<HKEY>(key.get());
    state().keys.emplace(handle, std::move(key));
    *output = handle;
    if (disposition) {
        *disposition = REG_OPENED_EXISTING_KEY;
    }
    return ERROR_SUCCESS;
}

LSTATUS WINAPI open_key(HKEY parent, LPCSTR child, DWORD options, REGSAM access, PHKEY output) {
    return guarded([&]() -> LSTATUS {
        const auto path = path_for(parent, child);
        if (child_of_preferences(path)) {
            return ERROR_FILE_NOT_FOUND;
        }
        if (redirect(path)) {
            return options ? ERROR_INVALID_PARAMETER : open_local(path, access, output, nullptr);
        }
        if (const auto key = find(parent)) {
            if (key->path == key_path) {
                return ERROR_FILE_NOT_FOUND;
            }
            return RegOpenKeyExA(HKEY_CURRENT_USER, path.c_str(), options, access, output);
        }
        return RegOpenKeyExA(parent, child, options, access, output);
    });
}

LSTATUS WINAPI create_key(HKEY parent, LPCSTR child, DWORD reserved, LPSTR class_name,
                          DWORD options, REGSAM access, const LPSECURITY_ATTRIBUTES security,
                          PHKEY output, LPDWORD disposition) {
    if (reserved) {
        return ERROR_INVALID_PARAMETER;
    }
    return guarded([&]() -> LSTATUS {
        const auto path = path_for(parent, child);
        if (child_of_preferences(path)) {
            return ERROR_ACCESS_DENIED;
        }
        if (redirect(path)) {
            if (options || security || (class_name && *class_name)) {
                return ERROR_INVALID_PARAMETER;
            }
            return open_local(path, access, output, disposition);
        }
        if (const auto key = find(parent)) {
            if (key->path == key_path) {
                return ERROR_ACCESS_DENIED;
            }
            return RegCreateKeyExA(HKEY_CURRENT_USER, path.c_str(), 0, class_name, options, access,
                                   security, output, disposition);
        }
        return RegCreateKeyExA(parent, child, 0, class_name, options, access, security, output,
                               disposition);
    });
}

template <typename F> LSTATUS forward(HKEY handle, REGSAM access, F action) {
    const auto key = find(handle);
    if (!key) {
        return action(handle);
    }
    HKEY native = nullptr;
    const auto error = RegOpenKeyExA(HKEY_CURRENT_USER, key->path.c_str(), 0, access, &native);
    if (error != ERROR_SUCCESS) {
        return error;
    }
    const auto result = action(native);
    RegCloseKey(native);
    return result;
}

LSTATUS WINAPI close_key(HKEY key) {
    return guarded(
        [&]() -> LSTATUS { return state().keys.erase(key) ? ERROR_SUCCESS : RegCloseKey(key); });
}

LSTATUS WINAPI query_value(HKEY handle, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data,
                           LPDWORD size) {
    return guarded([&]() -> LSTATUS {
        const auto key = find(handle);
        if (!key || key->path != key_path) {
            return forward(handle, KEY_QUERY_VALUE, [&](HKEY native) {
                return RegQueryValueExA(native, name, reserved, type, data, size);
            });
        }
        if (!(key->access & KEY_QUERY_VALUE)) {
            return ERROR_ACCESS_DENIED;
        }
        if (reserved || (data && !size)) {
            return ERROR_INVALID_PARAMETER;
        }
        const auto value_name = name ? name : "";
        auto value = state().store().find(value_name);
        Value display;
        if (GetEnvironmentVariableW(L"XFILES_PATCH_DISPLAY", nullptr, 0)) {
            // The native No/Ask paths bypass the virtual DirectDraw surface.
            if (!_stricmp(value_name, "Auto Set Resolution")) {
                display = DWORD{'Y'};
                value = &display;
            } else if (!_stricmp(value_name, "Auto Set Depth")) {
                display = DWORD{'H'};
                value = &display;
            }
        }
        if (!value) {
            return ERROR_FILE_NOT_FOUND;
        }
        const auto number = std::get_if<DWORD>(value);
        const auto string = std::get_if<std::string>(value);
        const DWORD length = number ? sizeof(DWORD) : static_cast<DWORD>(string->size() + 1);
        if (type) {
            *type = number ? REG_DWORD : REG_SZ;
        }
        const DWORD capacity = size ? *size : 0;
        if (size) {
            *size = length;
        }
        if (data) {
            if (capacity < length) {
                return ERROR_MORE_DATA;
            }
            std::memcpy(data, number ? static_cast<const void*>(number) : string->c_str(), length);
        }
        return ERROR_SUCCESS;
    });
}

LSTATUS WINAPI set_value(HKEY handle, LPCSTR name, DWORD reserved, DWORD type, const BYTE* data,
                         DWORD size) {
    if (reserved) {
        return ERROR_INVALID_PARAMETER;
    }
    return guarded([&]() -> LSTATUS {
        const auto key = find(handle);
        if (!key || key->path != key_path) {
            return forward(handle, KEY_SET_VALUE, [&](HKEY native) {
                return RegSetValueExA(native, name, 0, type, data, size);
            });
        }
        if (!(key->access & KEY_SET_VALUE)) {
            return ERROR_ACCESS_DENIED;
        }
        if (!name || !data || size > 65536) {
            return ERROR_INVALID_PARAMETER;
        }
        if (type == REG_DWORD && size == sizeof(DWORD)) {
            DWORD value = 0;
            std::memcpy(&value, data, sizeof(value));
            state().store().set(name, value);
        } else if (type == REG_SZ && size && data[size - 1] == 0) {
            state().store().set(name, std::string(reinterpret_cast<const char*>(data), size - 1));
        } else {
            return ERROR_INVALID_DATA;
        }
        return ERROR_SUCCESS;
    });
}

LSTATUS WINAPI delete_value(HKEY handle, LPCSTR name) {
    return guarded([&]() -> LSTATUS {
        const auto key = find(handle);
        if (!key || key->path != key_path) {
            return forward(handle, KEY_SET_VALUE,
                           [&](HKEY native) { return RegDeleteValueA(native, name); });
        }
        if (!(key->access & KEY_SET_VALUE)) {
            return ERROR_ACCESS_DENIED;
        }
        return state().store().erase(name ? name : "") ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
    });
}

LSTATUS WINAPI delete_key(HKEY parent, LPCSTR child) {
    return guarded([&]() -> LSTATUS {
        const auto path = path_for(parent, child);
        if (redirect(path) || path.starts_with(std::string(key_path) + '\\')) {
            return ERROR_ACCESS_DENIED;
        }
        return forward(parent, DELETE, [&](HKEY native) { return RegDeleteKeyA(native, child); });
    });
}

LSTATUS WINAPI enum_key(HKEY handle, DWORD index, LPSTR name, LPDWORD length, LPDWORD reserved,
                        LPSTR class_name, LPDWORD class_length, PFILETIME time) {
    return guarded([&]() -> LSTATUS {
        if (const auto key = find(handle); key && key->path == key_path) {
            return ERROR_NO_MORE_ITEMS;
        }
        return forward(handle, KEY_ENUMERATE_SUB_KEYS, [&](HKEY native) {
            return RegEnumKeyExA(native, index, name, length, reserved, class_name, class_length,
                                 time);
        });
    });
}

}

FARPROC registry_hook(const char* name) {
    if (!std::strcmp(name, "RegOpenKeyExA")) {
        return reinterpret_cast<FARPROC>(open_key);
    }
    if (!std::strcmp(name, "RegCreateKeyExA")) {
        return reinterpret_cast<FARPROC>(create_key);
    }
    if (!std::strcmp(name, "RegCloseKey")) {
        return reinterpret_cast<FARPROC>(close_key);
    }
    if (!std::strcmp(name, "RegQueryValueExA")) {
        return reinterpret_cast<FARPROC>(query_value);
    }
    if (!std::strcmp(name, "RegSetValueExA")) {
        return reinterpret_cast<FARPROC>(set_value);
    }
    if (!std::strcmp(name, "RegDeleteValueA")) {
        return reinterpret_cast<FARPROC>(delete_value);
    }
    if (!std::strcmp(name, "RegDeleteKeyA")) {
        return reinterpret_cast<FARPROC>(delete_key);
    }
    if (!std::strcmp(name, "RegEnumKeyExA")) {
        return reinterpret_cast<FARPROC>(enum_key);
    }
    return nullptr;
}

}
