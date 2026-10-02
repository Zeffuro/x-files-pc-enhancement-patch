#include "devtools/database/payload.h"
#include <array>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void check_expression(const devtools::DatabaseExpression& expression, std::uint32_t left,
                      std::uint32_t right, std::uint8_t left_kind, std::uint8_t right_kind,
                      std::uint8_t operation) {
    require(expression.left.raw == left && expression.right.raw == right,
            "Operand word offset or byte order incorrect");
    require(expression.left.kind == left_kind && expression.right.kind == right_kind,
            "Operand kind offset incorrect");
    require(expression.operation == operation, "Operator offset incorrect");
}
}

int main() {
    using namespace devtools;
    const std::array<std::uint8_t, 11> body = {0x78, 0x56, 0x34, 0x12, 0xfe, 0xff,
                                               0xff, 0xff, 0,    2,    0};
    auto parsed = database_action_payload(0, body);
    require(!parsed.condition && parsed.statement.has_value(), "Plain statement missing");
    check_expression(*parsed.statement, 0x12345678u, 0xfffffffeu, 0, 2, 0);
    require(database_expression_text(*parsed.statement, false) ==
                L"Variable ID 305419896 = (opcode 0) -2",
            "Assignment text incorrect");

    const std::array<std::uint8_t, 12> prefix = {4, 3, 2, 1, 0, 0, 0, 0x80, 0, 2, 5, 0xff};
    std::vector<std::uint8_t> conditional(prefix.begin(), prefix.end());
    conditional.insert(conditional.end(), body.begin(), body.end());
    const auto original = conditional;
    parsed = database_action_payload(0x80, conditional);
    require(parsed.condition && parsed.statement, "Conditional statement missing");
    check_expression(*parsed.condition, 0x01020304u, 0x80000000u, 0, 2, 5);
    check_expression(*parsed.statement, 0x12345678u, 0xfffffffeu, 0, 2, 0);
    require(database_expression_text(*parsed.condition, true) ==
                L"Variable ID 16909060 <= (opcode 5) -2147483648",
            "Predicate text incorrect");
    require(conditional == original, "Decoder mutated its input");
    conditional[11] = 0;
    parsed = database_action_payload(0x80, conditional);
    require(parsed.condition && parsed.statement, "Predicate padding interpreted as structure");
    check_expression(*parsed.condition, 0x01020304u, 0x80000000u, 0, 2, 5);
    conditional = original;
    std::vector<std::uint8_t> unaligned{0xff};
    unaligned.insert(unaligned.end(), original.begin(), original.end());
    parsed = database_action_payload(0x80, std::span(unaligned).subspan(1));
    require(parsed.condition && parsed.statement, "Unaligned payload rejected");
    check_expression(*parsed.statement, 0x12345678u, 0xfffffffeu, 0, 2, 0);

    for (std::size_t length = 0; length < body.size(); ++length) {
        parsed = database_action_payload(0, std::span(body).first(length));
        require(!parsed.condition && !parsed.statement, "Truncated statement accepted");
    }
    for (std::size_t length = 0; length < conditional.size(); ++length) {
        parsed = database_action_payload(0x80, std::span(conditional).first(length));
        require(parsed.condition.has_value() == (length >= 12),
                "Incomplete predicate accepted or complete predicate lost");
        require(!parsed.statement, "Truncated conditional body accepted");
    }
    auto extended = std::vector<std::uint8_t>(body.begin(), body.end());
    extended.push_back(0);
    require(!database_action_payload(0, extended).statement, "Extended plain body accepted");
    conditional.push_back(0);
    parsed = database_action_payload(0x80, conditional);
    require(parsed.condition && !parsed.statement, "Extended conditional body accepted");
    parsed = database_action_payload(0, original);
    require(!parsed.condition && !parsed.statement, "Prefix inferred without type flag");
    parsed = database_action_payload(0x80, body);
    require(!parsed.condition && !parsed.statement, "Statement treated as truncated prefix");

    for (unsigned subtype = 1; subtype <= 127; ++subtype) {
        parsed = database_action_payload(static_cast<std::uint8_t>(subtype), body);
        require(!parsed.condition && !parsed.statement, "Unsupported plain subtype decoded");
        parsed = database_action_payload(static_cast<std::uint8_t>(subtype | 0x80), original);
        require(parsed.condition && !parsed.statement, "Unsupported body decoded or prefix lost");
        parsed = database_action_payload(static_cast<std::uint8_t>(subtype | 0x80), prefix);
        require(parsed.condition && !parsed.statement, "Complete standalone predicate lost");
        parsed = database_action_payload(static_cast<std::uint8_t>(subtype | 0x80), body);
        require(!parsed.condition && !parsed.statement, "Unsupported truncated prefix accepted");
    }

    const std::array<std::wstring, 8> condition_ops = {L"=",  L"!=", L">",   L"<",
                                                       L">=", L"<=", L"and", L"or"};
    const std::array<std::wstring, 8> statement_ops = {L"=",  L"++", L"--", L"+=",
                                                       L"-=", L"*=", L"/=", L"%="};
    for (unsigned operation = 0; operation <= 255; ++operation) {
        extended.assign(body.begin(), body.end());
        extended[10] = static_cast<std::uint8_t>(operation);
        parsed = database_action_payload(0, extended);
        require(parsed.statement && parsed.statement->operation == operation,
                "Operator raw byte lost");
        for (bool condition : {false, true}) {
            const auto expected = operation < 8
                                      ? (condition ? condition_ops : statement_ops)[operation] +
                                            L" (opcode " + std::to_wstring(operation) + L")"
                                      : L"Raw operator " + std::to_wstring(operation);
            require(database_expression_text(*parsed.statement, condition) ==
                        L"Variable ID 305419896 " + expected + L" -2",
                    "Operator label incorrect");
        }
    }
    for (unsigned kind = 0; kind <= 255; ++kind) {
        extended.assign(body.begin(), body.end());
        extended[8] = extended[9] = static_cast<std::uint8_t>(kind);
        parsed = database_action_payload(0, extended);
        require(parsed.statement && parsed.statement->left.kind == kind &&
                    parsed.statement->right.kind == kind,
                "Raw operand kind lost");
        if (kind == 1) {
            require(database_expression_text(*parsed.statement, false) ==
                        L"Unsupported parameter selector 305419896 = (opcode 0) "
                        L"Unsupported parameter selector 4294967294",
                    "Unsupported parameter selector acquired a slot");
        } else if (kind == 4) {
            require(database_expression_text(*parsed.statement, false) ==
                        L"Ignored Statement destination kind 4 (raw value 305419896). "
                        L"Encoded = (opcode 0) with right operand Trigger context index "
                        L"4294967294 (kind 4, value unavailable)",
                    "Context destination treated as writable or raw index lost");
        } else if (kind != 0 && kind != 2) {
            require(database_expression_text(*parsed.statement, false) ==
                        L"Raw operand kind " + std::to_wstring(kind) +
                            L" value 305419896 = (opcode 0) Raw operand kind " +
                            std::to_wstring(kind) + L" value 4294967294",
                    "Unknown operand interpreted or raw value lost");
        }
    }
    for (std::uint32_t raw = 0; raw <= 30000; ++raw) {
        const auto selector = database_parameter_selector(raw);
        const bool integer = raw < 8;
        const bool boolean = raw >= 10000 && raw < 10012;
        const bool byte = raw >= 20000 && raw < 20004;
        require(selector.has_value() == (integer || boolean || byte),
                "Selector gap or range boundary incorrect");
        const auto label = database_parameter_text(raw);
        if (selector) {
            const auto kind =
                integer ? DatabaseParameterKind::integer
                        : (boolean ? DatabaseParameterKind::boolean : DatabaseParameterKind::byte);
            const auto first = integer ? 0u : (boolean ? 10000u : 20000u);
            require(selector->kind == kind && selector->index == raw - first,
                    "Selector kind or index incorrect");
            require(label.find(L"[" + std::to_wstring(raw - first) + L"]") != std::wstring::npos &&
                        label.find(L"selector " + std::to_wstring(raw)) != std::wstring::npos,
                    "Parameter label lost slot or raw selector");
        } else {
            require(label == L"Unsupported parameter selector " + std::to_wstring(raw),
                    "Unsupported selector interpreted as native fallback slot zero");
        }
        const DatabaseExpression expression{{raw, 1}, {raw, 1}, 0};
        require(database_expression_text(expression, false) == label + L" = (opcode 0) " + label &&
                    database_expression_text(expression, true) == label + L" = (opcode 0) " + label,
                "Parameter operand lost its selector in expression");
    }
    for (const auto raw : {0x7fffffffu, 0x80000000u, 0xffffffffu}) {
        require(!database_parameter_selector(raw) &&
                    database_parameter_text(raw) ==
                        L"Unsupported parameter selector " + std::to_wstring(raw),
                "Large unsupported selector wrapped to a valid slot");
    }
    for (const auto raw : {0u, 1u, 4u, 5u, 10000u, 0x7fffffffu, 0x80000000u, 0xffffffffu}) {
        const auto label =
            L"Trigger context index " + std::to_wstring(raw) + L" (kind 4, value unavailable)";
        for (unsigned operation = 0; operation <= 255; ++operation) {
            const DatabaseExpression expression{
                {raw, 4}, {raw, 4}, static_cast<std::uint8_t>(operation)};
            const auto condition_op = operation < 8 ? condition_ops[operation] + L" (opcode " +
                                                          std::to_wstring(operation) + L")"
                                                    : L"Raw operator " + std::to_wstring(operation);
            require(database_expression_text(expression, true) ==
                        label + L" " + condition_op + L" " + label,
                    "Context read was signed, bounded, evaluated or treated as a parameter");
            const auto statement_op = operation < 8 ? statement_ops[operation] + L" (opcode " +
                                                          std::to_wstring(operation) + L")"
                                                    : L"Raw operator " + std::to_wstring(operation);
            const auto status = operation < 8 ? L"Ignored" : L"Unsupported";
            require(database_expression_text(expression, false) ==
                        std::wstring(status) + L" Statement destination kind 4 (raw value " +
                            std::to_wstring(raw) + L"). Encoded " + statement_op +
                            L" with right operand " + label,
                    "Ignored destination or unknown operation misrepresented");
            const DatabaseExpression right{{60, 0}, {raw, 4}, static_cast<std::uint8_t>(operation)};
            require(database_expression_text(right, false) ==
                        L"Variable ID 60 " + statement_op + L" " + label,
                    "Statement context source treated as an ignored destination");
        }
    }
    const std::array<std::uint32_t, 5> boundaries = {0, 0x7fffffffu, 0x80000000u, 0xffffffffu, 1};
    const std::array<std::wstring, 5> expected = {L"0", L"2147483647", L"-2147483648", L"-1", L"1"};
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        const DatabaseExpression expression{{boundaries[index], 2}, {boundaries[index], 0}, 0};
        require(database_expression_text(expression, false) ==
                    expected[index] + L" = (opcode 0) Variable ID " +
                        std::to_wstring(boundaries[index]),
                "Signed literal or unsigned variable boundary incorrect");
    }
}
