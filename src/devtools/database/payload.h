#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace devtools {
struct DatabaseOperand {
    std::uint32_t raw = 0;
    std::uint8_t kind = 0;
};

struct DatabaseExpression {
    DatabaseOperand left;
    DatabaseOperand right;
    std::uint8_t operation = 0;
};

enum class DatabaseParameterKind { integer, boolean, byte };

struct DatabaseParameterSelector {
    DatabaseParameterKind kind;
    std::uint32_t index;
};

constexpr std::optional<DatabaseParameterSelector> database_parameter_selector(std::uint32_t raw) {
    if (raw < 8) {
        return DatabaseParameterSelector{DatabaseParameterKind::integer, raw};
    }
    if (raw >= 10000 && raw < 10012) {
        return DatabaseParameterSelector{DatabaseParameterKind::boolean, raw - 10000};
    }
    if (raw >= 20000 && raw < 20004) {
        return DatabaseParameterSelector{DatabaseParameterKind::byte, raw - 20000};
    }
    return {};
}

std::wstring database_parameter_text(std::uint32_t raw);

struct DatabaseActionPayload {
    std::optional<DatabaseExpression> condition;
    std::optional<DatabaseExpression> statement;
};

DatabaseActionPayload database_action_payload(std::uint8_t type,
                                              std::span<const std::uint8_t> bytes);
std::wstring database_expression_text(const DatabaseExpression& expression, bool condition);
}
