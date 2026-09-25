#include "store.h"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace preferences {
namespace {

[[noreturn]] void file_error(const char* message) {
    const auto error = GetLastError();
    throw std::system_error(static_cast<int>(error ? error : ERROR_WRITE_FAULT),
                            std::system_category(), message);
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

void validate_name(const std::string& name) {
    if (name.empty() || name != trim(name) || name.find_first_of("=\r\n[]") != std::string::npos ||
        name.front() == ';' || name.find('\0') != std::string::npos) {
        throw std::runtime_error("Invalid preference name.");
    }
}

}

bool IgnoreCase::operator()(const std::string& left, const std::string& right) const {
    return _stricmp(left.c_str(), right.c_str()) < 0;
}

Store::Store(const std::filesystem::path& path)
    : path_(path),
      lock_(CreateFileW((path.wstring() + L".lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) {
    if (lock_.get() == INVALID_HANDLE_VALUE) {
        file_error("Cannot lock preferences");
    }
    std::error_code error;
    if (!std::filesystem::exists(path_, error)) {
        if (error) {
            throw std::system_error(error);
        }
        return;
    }
    if (std::filesystem::file_size(path_) > 65536) {
        throw std::runtime_error("Preferences file is too large.");
    }
    std::ifstream input(path_);
    if (!input) {
        throw std::runtime_error("Cannot read preferences.");
    }
    bool section = false;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == ';') {
            continue;
        }
        if (line == "[Game]") {
            section = true;
            continue;
        }
        const auto separator = line.find('=');
        if (!section || separator == std::string::npos) {
            throw std::runtime_error("Invalid preferences file.");
        }
        auto name = trim(line.substr(0, separator));
        validate_name(name);
        const auto text = trim(line.substr(separator + 1));
        Value value;
        if (!text.empty() && text.front() == '"') {
            std::istringstream stream(text);
            std::string string;
            if (!(stream >> std::quoted(string)) || !(stream >> std::ws).eof()) {
                throw std::runtime_error("Invalid preference string.");
            }
            value = std::move(string);
        } else {
            DWORD number = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
                throw std::runtime_error("Invalid preference number.");
            }
            value = number;
        }
        if (!values_.emplace(std::move(name), std::move(value)).second) {
            throw std::runtime_error("Duplicate preference name.");
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("Cannot finish reading preferences.");
    }
}

const Value* Store::find(const std::string& name) const {
    const auto found = values_.find(name);
    return found == values_.end() ? nullptr : &found->second;
}

void Store::set(const std::string& name, Value value) {
    validate_name(name);
    if (const auto string = std::get_if<std::string>(&value);
        string && (string->find_first_of("\r\n") != std::string::npos ||
                   string->find('\0') != std::string::npos)) {
        throw std::runtime_error("Preference strings must occupy one line.");
    }
    auto changed = values_;
    changed.insert_or_assign(name, std::move(value));
    save(changed);
    values_ = std::move(changed);
}

bool Store::erase(const std::string& name) {
    auto changed = values_;
    if (!changed.erase(name)) {
        return false;
    }
    save(changed);
    values_ = std::move(changed);
    return true;
}

void Store::save(const Values& values) {
    std::ostringstream output;
    output << "[Game]\n";
    for (const auto& [name, value] : values) {
        output << name << " = ";
        if (const auto number = std::get_if<DWORD>(&value)) {
            output << *number;
        } else {
            output << std::quoted(std::get<std::string>(value));
        }
        output << '\n';
    }
    const auto bytes = output.str();
    if (bytes.size() > 65536) {
        throw std::runtime_error("Preferences file is too large.");
    }
    const auto temporary = path_.wstring() + L".tmp";
    {
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.get() == INVALID_HANDLE_VALUE) {
            file_error("Cannot write preferences");
        }
        DWORD written = 0;
        if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written,
                       nullptr) ||
            written != bytes.size() || !FlushFileBuffers(file.get())) {
            file_error("Cannot flush preferences");
        }
    }
    if (!MoveFileExW(temporary.c_str(), path_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        file_error("Cannot replace preferences");
    }
}

}
