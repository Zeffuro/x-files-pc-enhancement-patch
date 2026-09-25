#include "playback/audio_edits.h"
#include <iostream>

void require(bool value) {
    if (!value) {
        throw std::runtime_error("Audio edit regression");
    }
}

int main() {
    try {
        media::Track track;
        media::Description description;
        description.channels = 2;
        description.sample_rate = 4;
        track.descriptions.push_back(description);
        const std::vector<std::int16_t> pcm{0, 0, 1000, -1000, 2000, -2000, 3000, -3000};
        track.edits = {{4, 0, 65536}};
        require(playback::edit_audio(pcm, track, 4) == pcm);
        track.edits = {{4, 0, 32768}};
        require(playback::edit_audio(pcm, track, 4) ==
                std::vector<std::int16_t>{0, 0, 500, -500, 1000, -1000, 1500, -1500});
        track.edits = {{2, 0, 131072}, {1, -1, 65536}, {1, 0, 0}};
        require(playback::edit_audio(pcm, track, 4) ==
                std::vector<std::int16_t>{0, 0, 2000, -2000, 0, 0, 0, 0});
        for (auto rate : {65526, 65619}) {
            track.edits = {{3, 1, rate}};
            const auto edited = playback::edit_audio(pcm, track, 4);
            require(edited.size() == 6 && edited[0] == 1000 && edited[1] == -1000);
            require(edited[2] == 1000 + 1000LL * rate / 65536);
        }
        for (auto edit :
             {media::Edit{4, 3, 65536}, media::Edit{4, 0, -1}, media::Edit{UINT64_MAX, 0, 65536}}) {
            track.edits = {edit};
            bool rejected = false;
            try {
                playback::edit_audio(pcm, track, 4);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            require(rejected);
        }
        std::cout << "Audio edit rates, channel alignment, gaps and bounds passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
