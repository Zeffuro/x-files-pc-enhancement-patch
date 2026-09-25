#include "movie.h"
#include <zlib.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <tuple>

using Data = std::vector<std::uint8_t>;

namespace {

void append(Data& target, const Data& value) {
    target.insert(target.end(), value.begin(), value.end());
}

void word(Data& data, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        data.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

Data words(std::initializer_list<std::uint32_t> values) {
    Data result;
    for (auto value : values) {
        word(result, value);
    }
    return result;
}

Data atom(const char* type, const Data& body) {
    Data result;
    word(result, static_cast<std::uint32_t>(body.size() + 8));
    result.insert(result.end(), type, type + 4);
    append(result, body);
    return result;
}

void append_atom(Data& data, const char* type, const Data& body) {
    append(data, atom(type, body));
}

Data fixture() {
    Data description(78);
    description[7] = 1;
    description[25] = 80;
    description[27] = 60;
    description[75] = 24;

    Data descriptions = words({0, 2});
    append_atom(descriptions, "jpeg", description);
    append_atom(descriptions, "cvid", description);

    Data sample_table;
    append_atom(sample_table, "stsd", descriptions);
    append_atom(sample_table, "stco", words({0, 2, 8, 16}));
    append_atom(sample_table, "stsc", words({0, 2, 1, 2, 1, 2, 2, 2}));
    append_atom(sample_table, "stsz", words({0, 4, 4}));
    append_atom(sample_table, "stts", words({0, 2, 2, 10, 2, 20}));
    append_atom(sample_table, "stss", words({0, 2, 1, 3}));

    Data handler = words({0, 0});
    append(handler, Data{'v', 'i', 'd', 'e'});

    Data media;
    append_atom(media, "mdhd", words({0, 0, 0, 100, 60}));
    append_atom(media, "hdlr", handler);
    append_atom(media, "minf", atom("stbl", sample_table));

    const auto edit_list = words({0, 2, 5, 0xffffffff, 65536, 60, 0, 65536});
    Data track;
    append_atom(track, "tkhd", words({15, 0, 0, 1, 0, 65}));
    append_atom(track, "edts", atom("elst", edit_list));
    append_atom(track, "mdia", media);

    Data movie;
    append_atom(movie, "mvhd", words({0, 0, 0, 100, 65}));
    append_atom(movie, "trak", track);

    const Data samples{0xff, 0xd8, 1, 2, 0xff, 0xd8, 3, 4, 0, 0, 0, 4, 0, 0, 0, 4};
    Data file;
    append_atom(file, "mdat", samples);
    append_atom(file, "moov", movie);
    return file;
}

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void replace(Data& data, const char* type, std::size_t body_offset, std::uint32_t value) {
    const auto position = std::search(data.begin(), data.end(), type, type + 4);
    require(position != data.end(), "Test atom absent.");
    const auto offset = static_cast<std::size_t>(position - data.begin()) + 4 + body_offset;
    const auto bytes = words({value});
    std::copy(bytes.begin(), bytes.end(), data.begin() + offset);
}

void rejected(Data data) {
    try {
        media::Movie movie(std::move(data));
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("Malformed file was accepted.");
}

Data compressed_fixture(bool wrapped = true, int size_adjustment = 0, bool corrupt = false) {
    const auto original = fixture();
    Data header(original.begin() + (wrapped ? 24 : 32), original.end());
    uLongf length = compressBound(static_cast<uLong>(header.size()));
    Data compressed(length);
    require(compress2(compressed.data(), &length, header.data(), static_cast<uLong>(header.size()),
                      Z_BEST_COMPRESSION) == Z_OK,
            "Cannot compress movie fixture.");
    compressed.resize(length);
    if (corrupt) {
        compressed.back() ^= 1;
    }
    auto payload = words({static_cast<std::uint32_t>(header.size() + size_adjustment)});
    append(payload, compressed);
    Data container;
    append_atom(container, "dcom", Data{'z', 'l', 'i', 'b'});
    append_atom(container, "cmvd", payload);
    Data result(original.begin(), original.begin() + 24);
    append_atom(result, "moov", atom("cmov", container));
    return result;
}

Data empty_text_track(std::uint32_t duration) {
    Data media;
    append_atom(media, "mdhd", words({0, 0, 0, 100, duration}));
    Data handler = words({0, 0});
    append(handler, Data{'t', 'e', 'x', 't'});
    append_atom(media, "hdlr", handler);
    append_atom(media, "minf", atom("stbl", {}));

    Data track;
    append_atom(track, "tkhd", words({15, 0, 0, 1, 0, duration}));
    append_atom(track, "edts", {});
    append_atom(track, "mdia", media);

    Data movie;
    append_atom(movie, "mvhd", words({0, 0, 0, 100, duration}));
    append_atom(movie, "trak", track);
    return atom("moov", movie);
}
}

int main() {
    try {
        const media::Movie empty(empty_text_track(0));
        require(empty.tracks.size() == 1 && empty.tracks[0].samples.empty(),
                "Empty text track was not preserved.");
        require(!empty.tracks[0].sample_at(0, 100), "Empty track produced a sample.");
        rejected(empty_text_track(10));
        auto data = fixture();
        for (const bool wrapped : {false, true}) {
            const media::Movie compressed(compressed_fixture(wrapped));
            require(compressed.tracks.size() == 1 && compressed.duration == 65 &&
                        compressed.packet(compressed.tracks[0].samples[1])[2] == 3,
                    "Compressed header changed metadata or sample offsets.");
        }
        rejected(compressed_fixture(true, -1));
        rejected(compressed_fixture(true, 1));
        rejected(compressed_fixture(true, 0, true));
        auto oversized = compressed_fixture();
        replace(oversized, "cmvd", 0, 0xffffffff);
        rejected(std::move(oversized));
        auto unknown = compressed_fixture();
        replace(unknown, "dcom", 0, 0);
        rejected(std::move(unknown));
        const media::Movie movie(data);
        require(movie.timescale == 100 && movie.tracks.size() == 1, "Movie metadata mismatch.");
        const auto& track = movie.tracks[0];
        require(track.samples.size() == 4 && track.descriptions.size() == 2, "Samples missing.");
        require(track.descriptions[0].width == 80 && track.descriptions[0].height == 60,
                "Dimensions mismatch.");
        require(track.samples[0].description == 0 && track.samples[1].description == 0 &&
                    track.samples[2].description == 1 && track.samples[3].description == 1,
                "Codec switch lost.");
        require(track.samples[3].time == 40 && track.samples[3].duration == 20, "Timing mismatch.");
        require(track.samples[0].keyframe && !track.samples[1].keyframe &&
                    track.samples[2].keyframe,
                "Keyframe mapping mismatch.");
        require(track.edits.size() == 2 && track.edits[0].media_time == -1 &&
                    track.edits[1].rate == 65536,
                "Edit list mismatch.");
        require(movie.packet(track.samples[1])[2] == 3, "Packet offset mismatch.");
        require(!track.sample_at(0, 100) && !track.sample_at(4, 100),
                "Empty edit must not display a sample.");
        require(track.sample_at(5, 100) == 0u && track.sample_at(15, 100) == 1u &&
                    track.sample_at(25, 100) == 2u && track.sample_at(64, 100) == 3u &&
                    !track.sample_at(65, 100),
                "Edit seek boundaries mismatch.");
        auto dwell = track;
        dwell.edits = {{100, 10, 0}};
        require(dwell.sample_at(0, 100) == 1u && dwell.sample_at(99, 100) == 1u &&
                    !dwell.sample_at(100, 100),
                "Dwell edit must hold the selected sample.");
        auto stretched = track;
        stretched.edits = {{120, 0, 32768}};
        require(stretched.sample_at(19, 100) == 0u && stretched.sample_at(20, 100) == 1u &&
                    stretched.sample_at(119, 100) == 3u && !stretched.sample_at(120, 100),
                "Half-speed edit boundaries mismatch.");
        stretched.edits = {{30, 0, 131072}};
        require(stretched.sample_at(4, 100) == 0u && stretched.sample_at(5, 100) == 1u,
                "Double-speed edit boundaries mismatch.");
        stretched.edits = {{120, 0, 98304}};
        require(stretched.sample_at(13, 200) == 0u && stretched.sample_at(14, 200) == 1u,
                "Fractional edit rate lost precision across timescales.");
        auto duplicated = data;
        Data duplicate_header(data.begin() + 24, data.end());
        replace(duplicate_header, "mvhd", 8, 12345);
        append(duplicated, duplicate_header);
        require(media::Movie(duplicated).tracks.size() == 1,
                "Timestamp-only duplicate movie rejected.");
        replace(duplicate_header, "mvhd", 12, 200);
        auto conflicting = data;
        append(conflicting, duplicate_header);
        rejected(std::move(conflicting));
        for (std::size_t n = 0; n < data.size(); ++n) {
            rejected(Data(data.begin(), data.begin() + n));
        }
        for (auto [type, offset, value] :
             {std::tuple{"stsc", 16u, 3u}, std::tuple{"stsc", 8u, 2u}, std::tuple{"stco", 8u, 0u},
              std::tuple{"stts", 8u, 5u}, std::tuple{"stss", 8u, 0u},
              std::tuple{"stsz", 8u, 0xffffffffu}, std::tuple{"mdhd", 12u, 0u},
              std::tuple{"mvhd", 0u, 0x02000000u}}) {
            auto broken = data;
            replace(broken, type, offset, value);
            rejected(std::move(broken));
        }
        std::cout
            << "Codec switching, timestamps, edits, keyframes, bounds and truncation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
