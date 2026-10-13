#pragma once
#include "script.h"
#include "game/calls.h"

namespace native_game {
class ScrollTextGroup : public MainTextGroupRef {
public:
    ScrollTextGroup(std::byte* image_base, const Profile& addresses)
        : MainTextGroupRef(image_base ? image_base + addresses.main_text : nullptr, &addresses),
          functions_(image_base, addresses) {}

    void scroll(int direction, ScrollText up = nullptr, ScrollText down = nullptr) const {
        auto* list = reinterpret_cast<ChoiceList*>(data());
        if (direction < 0) {
            (up ? up : functions_.main_scroll_up_target())(list);
        } else {
            (down ? down : functions_.main_scroll_down_target())(list);
        }
    }

private:
    NativeFunctions functions_;
};

class ScrollCommandQueue : public CommandQueueRef {
public:
    ScrollCommandQueue(const CommandQueueRef& queue, const NativeFunctions& functions)
        : CommandQueueRef(queue), functions_(functions) {}

    void scroll_photos(int direction, ScrollPhotos up = nullptr,
                       ScrollPhotos down = nullptr) const {
        if (direction < 0) {
            (up ? up : functions_.photo_scroll_up_target())(data());
        } else {
            (down ? down : functions_.photo_scroll_down_target())(data());
        }
    }

private:
    NativeFunctions functions_;
};

class ScriptApplication : public ApplicationRef {
public:
    ScriptApplication(void* object, std::byte* image_base, const Profile& addresses)
        : ApplicationRef(object, &addresses), functions_(image_base, addresses) {}

    auto commands() const {
        return ScrollCommandQueue(ApplicationRef::commands(), functions_);
    }

    void process_commands(ProcessCommands override = nullptr) const {
        (override ? override : functions_.process_commands_target())(data());
    }

private:
    NativeFunctions functions_;
};

class ScriptRuntime {
public:
    ScriptRuntime(std::byte* image_base, const Profile& addresses)
        : image_(image_base), addresses_(addresses) {}

    auto main_text() const {
        return ScrollTextGroup(image_, addresses_);
    }

    auto application(void* object) const {
        return ScriptApplication(object, image_, addresses_);
    }

private:
    std::byte* image_;
    const Profile& addresses_;
};
}
