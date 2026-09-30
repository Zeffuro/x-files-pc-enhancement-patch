#include "transcript/capture.h"
#include "playback/movie.h"
#include "enhancements/game_ui.h"
#include "quickdraw/world.h"

#include <iostream>
#include <stdexcept>

namespace {
using Data = std::vector<std::uint8_t>;
Settings options;

struct NativeState {
    int session = 0, scene = 1;
} state;

std::uintptr_t native_input = 0;
bool attached = true;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void word(Data& data, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        data.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void atom(Data& data, const char* name, const Data& body) {
    word(data, static_cast<std::uint32_t>(body.size() + 8));
    data.insert(data.end(), name, name + 4);
    data.insert(data.end(), body.begin(), body.end());
}

std::shared_ptr<media::Movie> fixture() {
    Data packet;
    std::vector<media::Sample> samples;
    for (const auto text :
         {std::string("Shadow"), std::string("Future"), std::string("Corrected")}) {
        const auto offset = packet.size() + 8;
        packet.push_back(0);
        packet.push_back(static_cast<std::uint8_t>(text.size()));
        packet.insert(packet.end(), text.begin(), text.end());
        samples.push_back({offset, 100, static_cast<std::uint32_t>(text.size() + 2), 100, 0, true});
    }
    samples[1].time = 500;
    Data data, header, movie;
    atom(data, "mdat", packet);
    for (const auto value : {0u, 0u, 0u, 1000u, 1000u}) {
        word(header, value);
    }
    atom(movie, "mvhd", header);
    atom(data, "moov", movie);
    auto result = std::make_shared<media::Movie>(std::move(data));
    media::Track video;
    video.flags = 1;
    video.handler = "vide";
    video.timescale = 1000;
    video.samples = {{8, 0, 1, 500, 0, true}, {8, 500, 1, 500, 0, true}};
    result->tracks.push_back(std::move(video));
    media::Track original;
    original.flags = 1;
    original.handler = "text";
    original.timescale = 1000;
    original.samples = {samples[0], samples[1]};
    result->tracks.push_back(std::move(original));
    media::Track corrected;
    corrected.handler = "text";
    corrected.timescale = 1000;
    corrected.layer = -1;
    corrected.samples = {samples[2]};
    result->tracks.push_back(std::move(corrected));
    return result;
}
}

const Settings& settings() {
    return options;
}

void trace_value(const char*, std::uint32_t) {}

namespace enhancements {
void finish_dialogue_click(UINT) {}
}

namespace enhancements::game {
const Edition& edition() {
    static const Edition profile = [] {
        Edition result{};
        result.scene_active = offsetof(NativeState, scene);
        result.main_menu = 123;
        return result;
    }();
    return profile;
}

std::byte* executable_image() {
    return attached ? reinterpret_cast<std::byte*>(&state) : nullptr;
}

std::uintptr_t input_vtable() {
    return native_input;
}
}

namespace quickdraw {
HDC port_dc(Port*) {
    return nullptr;
}
}

int main() {
    try {
        options.captions = CaptionMode::Off;
        playback::Movie movie;
        movie.media = fixture();
        movie.relative_path = L"XV/12345.XMV";
        movie.inspection_id = 1;
        movie.rate = playback::unit_rate;
        quickdraw::Port port{};
        movie.port = &port;
        movie.last_frame_draw = 1;
        for (const auto& source : movie.media->tracks) {
            auto track = std::make_unique<playback::Track>();
            track->media = &source;
            track->enabled = (source.flags & 1) != 0;
            movie.tracks.push_back(std::move(track));
        }
        movie.time = 50;
        transcript::observe_movie(movie);
        require(transcript::history().entries().empty(), "Caption captured before its beginning");
        movie.time = 100;
        options.dialogue_transcript = false;
        transcript::observe_movie(movie);
        transcript::record_choice(L"Disabled choice");
        transcript::record_marker(L"Disabled marker");
        require(transcript::history().entries().empty(), "Disabled transcript captured a cue");
        options.dialogue_transcript = true;
        require(playback::current_caption(movie).empty(), "Captions Off changed rendering");
        require(playback::current_caption(movie, CaptionMode::On) == L"Corrected",
                "Independent resolver lost the corrected disabled layer");
        transcript::observe_movie(movie);
        require(transcript::history().entries().size() == 1 &&
                    transcript::history().entries()[0].text == L"Corrected",
                "Off-independent capture included a shadow track or lost dialogue");
        movie.time = 150;
        transcript::observe_movie(movie);
        movie.time = 600;
        transcript::observe_movie(movie);
        require(transcript::history().entries().size() == 1,
                "Seek/speed gap captured an unobserved future cue or duplicated dialogue");
        movie.time = 550;
        movie.active = false;
        transcript::observe_movie(movie);
        movie.active = true;
        state.scene = 0;
        transcript::observe_movie(movie);
        state.scene = 1;
        native_input = enhancements::game::edition().main_menu;
        transcript::observe_movie(movie);
        native_input = 0;
        movie.last_frame_draw = 0;
        transcript::observe_movie(movie);
        movie.last_frame_draw = 1;
        movie.rate = 0;
        transcript::observe_movie(movie);
        movie.rate = playback::unit_rate;
        attached = false;
        transcript::observe_movie(movie);
        attached = true;
        movie.relative_path = L"XG/12345.XMV";
        transcript::observe_movie(movie);
        require(transcript::history().entries().size() == 1,
                "Inactive, preview or unsupported context added dialogue");
        movie.relative_path = L"XV/12345.XMV";
        transcript::observe_movie(movie);
        require(transcript::history().entries().size() == 2 &&
                    transcript::history().entries()[1].text == L"Future",
                "Actually reached cue was not captured");
        movie.time = 100;
        transcript::observe_movie(movie);
        require(transcript::history().entries().size() == 2, "Backward replay duplicated its cue");
        std::cout << "Transcript caption mode, layering, reached-time and context checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
