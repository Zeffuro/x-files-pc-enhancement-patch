#include "platform/audio_endpoint.h"

unsigned missing_endpoint_constructions = 0;
unsigned missing_endpoint_destructions = 0;

namespace platform {

AudioEndpoint::AudioEndpoint() : apartment_(E_NOTIMPL), result_(E_NOTFOUND) {
    ++missing_endpoint_constructions;
}

AudioEndpoint::~AudioEndpoint() {
    ++missing_endpoint_destructions;
}

}
