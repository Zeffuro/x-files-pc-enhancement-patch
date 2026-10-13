#pragma once
#include "database.h"
#include "world.h"

namespace native_game {
class RuntimeActionPayloadRef : public MemoryRef<RuntimeActionExpression> {
public:
    using MemoryRef<RuntimeActionExpression>::MemoryRef;

    auto assignment(bool conditional) const {
        return RuntimeActionExpressionRef(
            offset_address(conditional ? sizeof(RuntimeActionExpression) : 0), profile());
    }
};
}
