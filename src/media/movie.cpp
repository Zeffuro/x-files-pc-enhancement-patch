#include "movie.h"
#include <zlib.h>
#include "ima4.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace media {

namespace {

using Bytes = std::span<const std::uint8_t>;

[[noreturn]] void invalid(const char* message) {
    throw std::runtime_error(message);
}

Bytes slice(Bytes bytes, std::size_t offset, std::size_t length) {
    if (offset > bytes.size() || length > bytes.size() - offset) {
        invalid("Truncated movie data.");
    }
    return bytes.subspan(offset, length);
}

std::uint64_t integer(Bytes bytes, std::size_t offset, std::size_t length) {
    std::uint64_t result = 0;
    for (auto byte : slice(bytes, offset, length)) {
        result = (result << 8) | byte;
    }
    return result;
}

std::uint32_t u32(Bytes b, std::size_t offset) {
    return static_cast<std::uint32_t>(integer(b, offset, 4));
}

std::uint16_t u16(Bytes b, std::size_t offset) {
    return static_cast<std::uint16_t>(integer(b, offset, 2));
}

std::string tag(Bytes b, std::size_t offset) {
    const auto value = slice(b, offset, 4);
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

struct Atom {
    std::string type;
    Bytes body;
};

std::vector<Atom> atoms(Bytes bytes) {
    std::vector<Atom> result;
    while (!bytes.empty()) {
        auto size = static_cast<std::uint64_t>(u32(bytes, 0));
        std::size_t header = 8;
        const auto type = tag(bytes, 4);
        if (size == 1) {
            size = integer(bytes, 8, 8);
            header = 16;
        }
        if (size == 0) {
            size = bytes.size();
        }
        if (size < header || size > bytes.size()) {
            invalid("Invalid atom size.");
        }
        result.push_back({type, slice(bytes, header, static_cast<std::size_t>(size) - header)});
        bytes = bytes.subspan(static_cast<std::size_t>(size));
    }
    return result;
}

Bytes find(const std::vector<Atom>& list, const char* type, bool required = true) {
    Bytes found;
    bool seen = false;
    for (const auto& atom : list) {
        if (atom.type == type) {
            if (seen) {
                invalid("Duplicate structural atom.");
            }
            found = atom.body;
            seen = true;
        }
    }
    if (!seen && required) {
        throw std::runtime_error(std::string("Required movie atom missing: ") + type);
    }
    return found;
}

unsigned version(Bytes b) {
    const auto value = integer(b, 0, 1);
    if (value > 1) {
        invalid("Unsupported movie atom version.");
    }
    return static_cast<unsigned>(value);
}

bool same_movie(Bytes left, Bytes right, unsigned depth = 0) {
    if (depth > 16) {
        invalid("Movie header nesting is too deep.");
    }
    const auto a = atoms(left), b = atoms(right);
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& type = a[i].type;
        auto x = a[i].body, y = b[i].body;
        if (type != b[i].type || x.size() != y.size()) {
            return false;
        }
        if (type == "trak" || type == "mdia" || type == "minf" || type == "stbl" ||
            type == "edts") {
            if (!same_movie(x, y, depth + 1)) {
                return false;
            }
        } else if (type == "mvhd" || type == "tkhd" || type == "mdhd") {
            if (u32(x, 0) != u32(y, 0)) {
                return false;
            }
            const std::size_t prefix = version(x) ? 20 : 12;
            slice(x, 0, prefix);
            slice(y, 0, prefix);
            if (!std::equal(x.begin() + prefix, x.end(), y.begin() + prefix)) {
                return false;
            }
        } else if (!std::equal(x.begin(), x.end(), y.begin())) {
            return false;
        }
    }
    return true;
}

std::uint32_t table_count(Bytes b, std::size_t width, std::size_t header = 8) {
    if (version(b) != 0) {
        invalid("Unsupported sample table version.");
    }
    const auto count = u32(b, 4);
    if (b.size() < header || count > (b.size() - header) / width) {
        invalid("Truncated sample table.");
    }
    return count;
}

void read_edits(Track& track, Bytes bytes) {
    if (bytes.empty()) {
        return;
    }
    const auto b = find(atoms(bytes), "elst");
    const bool wide = version(b) == 1;
    const auto count = u32(b, 4);
    const std::size_t stride = wide ? 20 : 12;
    if (count > (b.size() - 8) / stride) {
        invalid("Truncated edit list.");
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto pos = 8 + i * stride;
        const auto duration = integer(b, pos, wide ? 8 : 4);
        const auto time = wide ? std::bit_cast<std::int64_t>(integer(b, pos + 8, 8))
                               : std::bit_cast<std::int32_t>(u32(b, pos + 4));
        track.edits.push_back(
            {duration, time, std::bit_cast<std::int32_t>(u32(b, pos + (wide ? 16 : 8)))});
    }
}

struct ChunkRun {
    std::uint32_t first, count, description;
};

void read_samples(Track& track, const std::vector<Atom>& list, Bytes file,
                  const std::vector<Bytes>& media_data) {
    auto stsd = find(list, "stsd");
    const auto description_count = u32(stsd, 4);
    const auto descriptions = atoms(stsd.subspan(8));
    if (version(stsd) != 0 || descriptions.size() != description_count) {
        invalid("Invalid sample descriptions.");
    }
    for (const auto& entry : descriptions) {
        Description desc;
        desc.codec = entry.type;
        desc.data_reference = u16(entry.body, 6);
        if (track.handler == "soun") {
            desc.channels = u16(entry.body, 16);
            desc.depth = u16(entry.body, 18);
            desc.sample_rate = u32(entry.body, 24) >> 16;
            if (u16(entry.body, 8) > 1 || !desc.sample_rate ||
                (desc.channels != 1 && desc.channels != 2)) {
                invalid("Unsupported audio sample description.");
            }
            if (desc.codec == "ima4") {
                desc.packet_frames = Ima4::packet_frames;
                desc.packet_bytes = Ima4::packet_bytes * desc.channels;
            } else if (desc.codec == "twos" && (desc.depth == 8 || desc.depth == 16)) {
                desc.packet_bytes = (desc.depth / 8) * desc.channels;
            } else if ((desc.codec == "QDMC" || desc.codec == "QDM2") && u16(entry.body, 8) == 1) {
                desc.packet_frames = u32(entry.body, 28);
                desc.packet_bytes = u32(entry.body, 36);
                if (!desc.packet_frames || !desc.packet_bytes) {
                    invalid("Invalid QDesign packet dimensions.");
                }
            } else {
                invalid("Unsupported audio codec.");
            }
        } else if (track.handler == "vide") {
            desc.width = u16(entry.body, 24);
            desc.height = u16(entry.body, 26);
            desc.depth = u16(entry.body, 74);
        }
        desc.bytes.assign(entry.body.begin(), entry.body.end());
        track.descriptions.push_back(std::move(desc));
    }

    auto offsets = find(list, "stco", false);
    const bool wide = offsets.empty();
    if (wide) {
        offsets = find(list, "co64");
    } else if (!find(list, "co64", false).empty()) {
        invalid("Duplicate chunk offset tables.");
    }
    const std::size_t offset_width = wide ? 8 : 4;
    const auto chunk_count = table_count(offsets, offset_width);
    const auto stsc = find(list, "stsc");
    const auto run_count = table_count(stsc, 12);
    std::vector<ChunkRun> runs;
    for (std::size_t i = 0; i < run_count; ++i) {
        const auto pos = 8 + i * 12;
        ChunkRun run{u32(stsc, pos), u32(stsc, pos + 4), u32(stsc, pos + 8)};
        if (!run.count || !run.description || run.description > track.descriptions.size() ||
            (!runs.empty() && run.first <= runs.back().first) || !run.first ||
            run.first > chunk_count) {
            invalid("Invalid sample-to-chunk mapping.");
        }
        runs.push_back(run);
    }
    const auto stsz = find(list, "stsz");
    if (version(stsz) != 0) {
        invalid("Unsupported sample-size table version.");
    }
    const auto fixed_size = u32(stsz, 4), logical_samples = u32(stsz, 8);
    const auto frames_per_packet =
        track.handler == "soun" ? track.descriptions.at(0).packet_frames : 1u;
    for (const auto& description : track.descriptions) {
        if (description.packet_frames != frames_per_packet) {
            invalid("Audio packet grouping changes within a track.");
        }
    }
    if (logical_samples % frames_per_packet || (track.handler == "soun" && fixed_size != 1)) {
        invalid("Unsupported audio sample sizing.");
    }
    const auto sample_count = logical_samples / frames_per_packet;
    if (!fixed_size && sample_count > (stsz.size() - 12) / 4) {
        invalid("Truncated sample-size table.");
    }
    if (sample_count > file.size()) {
        invalid("Unreasonable sample count.");
    }
    if (sample_count && (runs.empty() || runs[0].first != 1)) {
        invalid("Missing first chunk mapping.");
    }

    std::size_t run = 0;
    for (std::uint32_t chunk = 1; chunk <= chunk_count; ++chunk) {
        if (runs.empty()) {
            invalid("Missing chunk mapping.");
        }
        if (run + 1 < runs.size() && chunk >= runs[run + 1].first) {
            ++run;
        }
        auto offset = integer(offsets, 8 + (static_cast<std::size_t>(chunk) - 1) * offset_width,
                              offset_width);
        if (runs[run].count % frames_per_packet) {
            invalid("Audio chunk splits a compressed packet.");
        }
        for (std::uint32_t i = 0; i < runs[run].count / frames_per_packet; ++i) {
            if (track.samples.size() >= sample_count) {
                invalid("Chunk mapping exceeds sample count.");
            }
            const auto& description = track.descriptions[runs[run].description - 1];
            const auto size = description.packet_bytes ? description.packet_bytes
                              : fixed_size             ? fixed_size
                                                       : u32(stsz, 12 + track.samples.size() * 4);
            bool inside = false;
            for (const auto data : media_data) {
                const auto start = static_cast<std::uint64_t>(data.data() - file.data());
                if (offset >= start && offset - start <= data.size() &&
                    size <= data.size() - (offset - start)) {
                    inside = true;
                }
            }
            if (!inside) {
                invalid("Sample lies outside media data.");
            }
            track.samples.push_back({offset, 0, size, 0, runs[run].description - 1, true});
            offset += size;
        }
    }
    if (track.samples.size() != sample_count) {
        invalid("Chunk mapping does not cover every sample.");
    }
    const auto stts = find(list, "stts");
    const auto time_count = table_count(stts, 8);
    std::size_t index = 0;
    std::uint64_t time = 0;
    for (std::size_t i = 0; i < time_count; ++i) {
        const auto logical_count = u32(stts, 8 + i * 8);
        const auto logical_duration = u32(stts, 12 + i * 8);
        if (logical_count % frames_per_packet ||
            logical_duration > UINT32_MAX / frames_per_packet) {
            invalid("Timing splits or overflows a compressed packet.");
        }
        const auto count = logical_count / frames_per_packet;
        const auto duration = logical_duration * frames_per_packet;
        if (count > track.samples.size() - index) {
            invalid("Timing exceeds sample count.");
        }
        for (std::uint32_t j = 0; j < count; ++j) {
            track.samples[index].time = time;
            track.samples[index++].duration = duration;
            time += duration;
        }
    }
    if (index != track.samples.size()) {
        invalid("Missing sample timing.");
    }
    if (const auto stss = find(list, "stss", false); !stss.empty()) {
        const auto count = table_count(stss, 4);
        for (auto& sample : track.samples) {
            sample.keyframe = false;
        }
        std::uint32_t previous = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto number = u32(stss, 8 + i * 4);
            if (number <= previous || number > track.samples.size()) {
                invalid("Invalid keyframe index.");
            }
            track.samples[number - 1].keyframe = true;
            previous = number;
        }
    }
    if (!find(list, "ctts", false).empty()) {
        invalid("Composition offsets are not implemented yet.");
    }
}
}

Movie Movie::open(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto length = stream.tellg();
    if (!stream || length < 0 || length > 512 * 1024 * 1024) {
        invalid("Movie unreadable or exceeds 512 MiB.");
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(data.data()),
                     static_cast<std::streamsize>(data.size()))) {
        invalid("Movie read failed.");
    }
    return Movie(std::move(data));
}

Movie::Movie(std::vector<std::uint8_t> data) : data_(std::move(data)) {
    const auto root = atoms(data_);
    std::vector<Bytes> media_data;
    for (const auto& entry : root) {
        if (entry.type == "mdat") {
            media_data.push_back(entry.body);
        }
    }
    Bytes movie_bytes;
    for (const auto& entry : root) {
        if (entry.type == "moov") {
            if (!movie_bytes.empty() && !same_movie(movie_bytes, entry.body)) {
                invalid("Conflicting movie headers.");
            }
            if (movie_bytes.empty()) {
                movie_bytes = entry.body;
            }
        }
    }
    if (movie_bytes.empty()) {
        invalid("No movie header; image sequences need a separate loader.");
    }
    auto movie = atoms(movie_bytes);
    std::vector<std::uint8_t> expanded;
    if (const auto compressed = find(movie, "cmov", false); !compressed.empty()) {
        const auto parts = atoms(compressed);
        const auto method = find(parts, "dcom");
        const auto payload = find(parts, "cmvd");
        const auto size = u32(payload, 0);
        if (method.size() != 4 || tag(method, 0) != "zlib" || !size || size > 64 * 1024 * 1024) {
            invalid("Invalid compressed movie header.");
        }
        expanded.resize(size);
        uLongf output_size = size;
        uLong input_size = static_cast<uLong>(payload.size() - 4);
        if (uncompress2(expanded.data(), &output_size, payload.data() + 4, &input_size) != Z_OK ||
            output_size != size || input_size != payload.size() - 4) {
            invalid("Corrupt compressed movie header.");
        }
        movie = atoms(expanded);
        if (movie.size() == 1 && movie.front().type == "moov") {
            movie = atoms(movie.front().body);
        }
    }
    const auto mvhd = find(movie, "mvhd");
    timescale = u32(mvhd, version(mvhd) ? 20 : 12);
    duration = integer(mvhd, version(mvhd) ? 24 : 16, version(mvhd) ? 8 : 4);
    if (!timescale) {
        invalid("Zero movie timescale.");
    }
    for (const auto& entry : movie) {
        if (entry.type == "trak") {
            const auto list = atoms(entry.body);
            Track track;
            const auto tkhd = find(list, "tkhd");
            track.flags = u32(tkhd, 0) & 0xffffff;
            track.id = u32(tkhd, version(tkhd) ? 20 : 12);
            track.duration = integer(tkhd, version(tkhd) ? 28 : 20, version(tkhd) ? 8 : 4);
            const std::size_t layer_offset = version(tkhd) ? 44 : 32;
            if (tkhd.size() >= layer_offset + 52) {
                track.layer = static_cast<std::int16_t>(integer(tkhd, layer_offset, 2));
                for (std::size_t index = 0; index < track.placement.size(); ++index) {
                    track.placement[index] = u32(tkhd, layer_offset + 8 + index * 4);
                }
            }
            read_edits(track, find(list, "edts", false));
            const auto mdia = atoms(find(list, "mdia"));
            track.handler = tag(find(mdia, "hdlr"), 8);
            const auto mdhd = find(mdia, "mdhd");
            track.timescale = u32(mdhd, version(mdhd) ? 20 : 12);
            if (!track.timescale) {
                invalid("Zero track timescale.");
            }
            if (track.handler == "vide" || track.handler == "soun" || track.handler == "text") {
                const auto minf = atoms(find(mdia, "minf"));
                const auto table = atoms(find(minf, "stbl"));
                const auto media_duration =
                    integer(mdhd, version(mdhd) ? 24 : 16, version(mdhd) ? 8 : 4);
                if (!table.empty() || track.duration || media_duration) {
                    read_samples(track, table, data_, media_data);
                }
            }
            tracks.push_back(std::move(track));
        }
    }
}

std::span<const std::uint8_t> Movie::packet(const Sample& sample) const {
    if (sample.offset > data_.size()) {
        invalid("Sample offset outside movie.");
    }
    return slice(data_, static_cast<std::size_t>(sample.offset), sample.size);
}

std::optional<std::size_t> Track::sample_at(std::uint64_t movie_time,
                                            std::uint32_t movie_scale) const {
    if (!movie_scale || !timescale) {
        invalid("Zero timescale in seek.");
    }
    std::uint64_t elapsed = movie_time, origin = 0;
    constexpr std::uint32_t unit_rate = 1 << 16;
    std::uint32_t rate = unit_rate;
    if (!edits.empty()) {
        const Edit* selected = nullptr;
        for (const auto& edit : edits) {
            if (elapsed < edit.duration) {
                selected = &edit;
                break;
            }
            elapsed -= edit.duration;
        }
        if (!selected || selected->media_time == -1) {
            return std::nullopt;
        }
        if (selected->media_time < 0) {
            invalid("Invalid negative edit time.");
        }
        if (selected->rate < 0) {
            invalid("Reverse edit rates are not implemented yet.");
        }
        rate = static_cast<std::uint32_t>(selected->rate);
        origin = static_cast<std::uint64_t>(selected->media_time);
    }
    std::uint64_t time = origin;
    if (rate) {
        std::uint64_t denominator = static_cast<std::uint64_t>(movie_scale) * unit_rate;
        std::uint64_t factors[] = {elapsed, timescale, rate};
        for (auto& factor : factors) {
            const auto divisor = std::gcd(factor, denominator);
            factor /= divisor;
            denominator /= divisor;
        }
        const auto limit = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t numerator = 1;
        for (const auto factor : factors) {
            if (factor && numerator > limit / factor) {
                invalid("Seek time overflow.");
            }
            numerator *= factor;
        }
        const auto offset = numerator / denominator;
        if (offset > limit - time) {
            invalid("Seek time overflow.");
        }
        time += offset;
    }
    auto next = std::upper_bound(
        samples.begin(), samples.end(), time,
        [](std::uint64_t value, const Sample& sample) { return value < sample.time; });
    if (next == samples.begin()) {
        return std::nullopt;
    }
    const auto& sample = *--next;
    if (time - sample.time >= sample.duration) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(next - samples.begin());
}
}
