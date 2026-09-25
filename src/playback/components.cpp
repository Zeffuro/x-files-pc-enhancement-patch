#include "components.h"
#include "movie.h"

namespace playback {

std::int32_t __cdecl call_component(void* component, const ComponentParameters* parameters) {
    if (!parameters || parameters->flags || parameters->size != sizeof(parameters->argument)) {
        return static_cast<std::int32_t>(Error::Parameter);
    }
    auto& value = track(static_cast<TrackHandle>(component));
    constexpr std::int32_t bad_component_selector = -3000;
    if (value.media->handler != "soun") {
        return bad_component_selector;
    }
    switch (parameters->selector) {
        case MediaSelector::SetSoundBalance: {
            const auto balance = static_cast<std::int16_t>(parameters->argument);
            if (balance < -128 || balance > 127) {
                return static_cast<std::int32_t>(Error::Parameter);
            }
            value.balance = balance;
            auto& owner = movie(value.owner);
            if (value.enabled && owner.audio) {
                owner.audio->balance(balance);
            }
            return 0;
        }
        case MediaSelector::GetSoundBalance: {
            const auto result = reinterpret_cast<std::int16_t*>(parameters->argument);
            if (!result) {
                return static_cast<std::int32_t>(Error::Parameter);
            }
            *result = value.balance;
            return 0;
        }
        default:
            trace_value("media_component_selector",
                        static_cast<std::uint16_t>(parameters->selector));
            unsupported(Selector::CallComponent, "Unsupported media component operation", 0);
    }
}

}
