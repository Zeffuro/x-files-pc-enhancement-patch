#pragma once

#include "runtime.h"

#include <span>

std::span<const std::uint8_t> handle_bytes(std::uint8_t** handle);

Entry memory_entry(Selector selector);
void release_handles();
