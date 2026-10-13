#pragma once
#include "ui.h"

namespace quickdraw {
struct Port;
}

namespace native_game {

using RenderPoint = NativePoint;

struct PositionedDrawable {
    std::byte* drawable;
    RenderPoint position;
    void* effect;
};

struct CreditGroupVTable {
    using Update = int(__stdcall*)(void*, void*);
    using Draw = int(__stdcall*)(void*, void*, void*);

    void* reserved[0x30 / sizeof(void*)];
    Update update;
    Draw draw;
};

struct CreditGroupPrefix {
    CreditGroupVTable* vtable;
    List<PositionedDrawable> drawables;
};

struct MainViewCreditsPrefix {
    std::byte reserved[0x60c];
    CreditGroupPrefix credits;
};

struct CreditTextPrefix {
    void* vtable;
    std::byte before_rectangle[0x44];
    Rectangle rectangle;
};

struct CreditTextOwnerPrefix {
    void* vtable;
    std::byte before_text[0x14];
    CreditTextPrefix text;
};

enum class CanvasBackendKind : unsigned { quickdraw = 1 };

struct CanvasBackendPrefix {
    void* vtable;
    CanvasBackendKind kind;
    unsigned unknown;
    quickdraw::Port* port;
};

struct CanvasVTable {
    using Invalidate = int(__stdcall*)(void*, Rectangle*);

    void* reserved[0x4c / sizeof(void*)];
    Invalidate invalidate;
};

struct CanvasPrefix {
    CanvasVTable* vtable;
    std::byte before_backend[0x28];
    CanvasBackendPrefix* backend;
};

static_assert(sizeof(PositionedDrawable) == 0x14);
static_assert(offsetof(PositionedDrawable, position) == 4);
static_assert(offsetof(PositionedDrawable, effect) == 0x10);
static_assert(sizeof(CreditGroupVTable::Update) == sizeof(void*));
static_assert(sizeof(CreditGroupVTable::Draw) == sizeof(void*));
static_assert(offsetof(CreditGroupVTable, update) == 0x30);
static_assert(offsetof(CreditGroupVTable, draw) == 0x34);
static_assert(sizeof(CreditGroupPrefix) == 0x18);
static_assert(offsetof(CreditGroupPrefix, drawables) == 4);
static_assert(offsetof(MainViewCreditsPrefix, credits) == 0x60c);
static_assert(offsetof(CreditTextPrefix, rectangle) == 0x48);
static_assert(offsetof(CreditTextOwnerPrefix, text) == 0x18);
static_assert(sizeof(CanvasBackendKind) == sizeof(unsigned));
static_assert(offsetof(CanvasBackendPrefix, kind) == 4);
static_assert(offsetof(CanvasBackendPrefix, port) == 0x0c);
static_assert(sizeof(CanvasVTable::Invalidate) == sizeof(void*));
static_assert(offsetof(CanvasVTable, invalidate) == 0x4c);
static_assert(offsetof(CanvasPrefix, backend) == 0x2c);
}
