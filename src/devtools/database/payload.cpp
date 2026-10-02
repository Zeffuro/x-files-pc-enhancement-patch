#include "payload.h"
#include <array>
#include <bit>
#include <string_view>

namespace devtools {
namespace {
std::uint32_t little_endian_word(std::span<const std::uint8_t> bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

DatabaseExpression expression_at(std::span<const std::uint8_t> bytes) {
    return {{little_endian_word(bytes.first<4>()), bytes[8]},
            {little_endian_word(bytes.subspan<4, 4>()), bytes[9]},
            bytes[10]};
}

std::wstring operand_text(const DatabaseOperand& operand) {
    if (operand.kind == 0) {
        return L"Variable ID " + std::to_wstring(operand.raw);
    }
    if (operand.kind == 1) {
        return database_parameter_text(operand.raw);
    }
    if (operand.kind == 2) {
        return std::to_wstring(std::bit_cast<std::int32_t>(operand.raw));
    }
    if (operand.kind == 4) {
        return L"Trigger context index " + std::to_wstring(operand.raw) +
               L" (kind 4, value unavailable)";
    }
    return L"Raw operand kind " + std::to_wstring(operand.kind) + L" value " +
           std::to_wstring(operand.raw);
}
}

std::wstring database_parameter_text(std::uint32_t raw) {
    const auto selector = database_parameter_selector(raw);
    if (!selector) {
        return L"Unsupported parameter selector " + std::to_wstring(raw);
    }
    std::wstring_view kind;
    switch (selector->kind) {
        case DatabaseParameterKind::integer:
            kind = L"Integer";
            break;
        case DatabaseParameterKind::boolean:
            kind = L"Boolean32";
            break;
        case DatabaseParameterKind::byte:
            kind = L"Signed byte";
            break;
    }
    return std::wstring(kind) + L" parameter[" + std::to_wstring(selector->index) +
           L"] (selector " + std::to_wstring(raw) + L")";
}

DatabaseActionPayload database_action_payload(std::uint8_t type,
                                              std::span<const std::uint8_t> bytes) {
    DatabaseActionPayload result;
    const std::size_t prefix_size = (type & 0x80) ? 12 : 0;
    if (bytes.size() < prefix_size) {
        return result;
    }
    if (prefix_size) {
        result.condition = expression_at(bytes.first<12>());
    }
    // Keep unsupported and extended bodies raw even when their predicate is readable.
    if ((type & 0x7f) == 0 && bytes.size() == prefix_size + 11) {
        result.statement = expression_at(bytes.subspan(prefix_size));
    }
    return result;
}

std::wstring database_expression_text(const DatabaseExpression& expression, bool condition) {
    static constexpr std::array<std::wstring_view, 8> predicate_ops = {L"=",  L"!=", L">",   L"<",
                                                                       L">=", L"<=", L"and", L"or"};
    static constexpr std::array<std::wstring_view, 8> statement_ops = {L"=",  L"++", L"--", L"+=",
                                                                       L"-=", L"*=", L"/=", L"%="};
    const auto& operators = condition ? predicate_ops : statement_ops;
    std::wstring operation;
    if (expression.operation < operators.size()) {
        operation = operators[expression.operation];
        operation += L" (opcode " + std::to_wstring(expression.operation) + L")";
    } else {
        operation = L"Raw operator " + std::to_wstring(expression.operation);
    }
    if (!condition && expression.left.kind == 4) {
        const auto status =
            expression.operation < statement_ops.size() ? L"Ignored" : L"Unsupported";
        return std::wstring(status) + L" Statement destination kind 4 (raw value " +
               std::to_wstring(expression.left.raw) + L"). Encoded " + operation +
               L" with right operand " + operand_text(expression.right);
    }
    return operand_text(expression.left) + L" " + operation + L" " + operand_text(expression.right);
}
}
