#pragma once
#include "ui.h"

namespace native_game {
struct CommandQueuePrefix {
    void* vtable;
    unsigned count, current;
    void* storage;
    unsigned capacity;
};

struct CommandQueue {
    CommandQueuePrefix data;
    unsigned last;
};

struct ApplicationQueuesPrefix {
    Application application;
    std::byte before_commands[0x268 - sizeof(Application)];
    CommandQueue commands, pointer_events;
};

namespace application_offset {
inline constexpr auto state = offsetof(Application, state);
inline constexpr auto database = offsetof(Application, unknown);
inline constexpr auto view = offsetof(Application, view);
inline constexpr auto commands = offsetof(ApplicationQueuesPrefix, commands);
inline constexpr auto pointer_events = offsetof(ApplicationQueuesPrefix, pointer_events);
}

static_assert(sizeof(Application) == 0xf8);
static_assert(application_offset::commands == 0x268);
static_assert(application_offset::pointer_events == 0x280);
static_assert(sizeof(CommandQueuePrefix) == 0x14);
static_assert(sizeof(CommandQueue) == 0x18);
static_assert(offsetof(CommandQueuePrefix, count) == 4);
static_assert(offsetof(CommandQueuePrefix, current) == 8);
static_assert(offsetof(CommandQueuePrefix, storage) == 0x0c);
static_assert(offsetof(CommandQueuePrefix, capacity) == 0x10);
static_assert(offsetof(CommandQueue, last) == 0x14);
}
