#include "audio.h"
#include "audio_edits.h"
#include "volume.h"
#include "media/ima4.h"
#include "media/compressed_audio.h"
#include "media/pcm.h"

#include <algorithm>
#include <stdexcept>

namespace playback {
namespace {

constexpr std::size_t maximum_samples = 64 * 1024 * 1024;

}

Audio::Audio(const media::Movie& movie, const media::Track& track) {
    const auto& description = track.descriptions.at(0);
    const bool compressed = description.codec == "QDMC" || description.codec == "QDM2";
    if ((!compressed && description.codec != "ima4" && description.codec != "twos") ||
        track.timescale != description.sample_rate) {
        throw std::runtime_error("Unsupported movie audio format");
    }
    format_.wFormatTag = WAVE_FORMAT_PCM;
    format_.nChannels = description.channels;
    format_.nSamplesPerSec = description.sample_rate;
    format_.wBitsPerSample = 16;
    format_.nBlockAlign = description.channels * sizeof(std::int16_t);
    format_.nAvgBytesPerSec = format_.nSamplesPerSec * format_.nBlockAlign;
    media::Ima4 decoder(description.channels);
    std::unique_ptr<media::CompressedAudio> compressed_decoder;
    if (compressed) {
        compressed_decoder = std::make_unique<media::CompressedAudio>(description);
    }
    std::vector<std::int16_t> decoded;
    for (const auto& sample : track.samples) {
        if (sample.description || sample.duration != description.packet_frames ||
            sample.time != decoded.size() / description.channels) {
            throw std::runtime_error("Unsupported audio codec or timing change");
        }
        std::vector<std::int16_t> packet;
        const auto bytes = movie.packet(sample);
        if (compressed_decoder) {
            packet = compressed_decoder->decode(bytes);
            if (packet.size() != static_cast<std::size_t>(sample.duration) * description.channels) {
                throw std::runtime_error("Decoded audio duration differs from its sample table");
            }
        } else if (description.codec == "ima4") {
            packet = decoder.decode(bytes);
        } else {
            if (bytes.size() != description.packet_bytes) {
                throw std::runtime_error("Invalid PCM packet size");
            }
            packet = media::decode_signed_pcm(bytes, description.depth);
        }
        if (packet.size() > maximum_samples || decoded.size() > maximum_samples - packet.size()) {
            throw std::runtime_error("Decoded audio exceeds the buffer limit");
        }
        decoded.insert(decoded.end(), packet.begin(), packet.end());
    }
    pcm_ = track.edits.empty() ? std::move(decoded) : edit_audio(decoded, track, movie.timescale);
    if (format_.nChannels == 1) {
        const auto count = pcm_.size();
        pcm_.resize(count * 2);
        for (auto index = count; index > 0; --index) {
            const auto sample = pcm_[index - 1];
            pcm_[(index - 1) * 2] = sample;
            pcm_[(index - 1) * 2 + 1] = sample;
        }
        format_.nChannels = 2;
        format_.nBlockAlign *= 2;
        format_.nAvgBytesPerSec *= 2;
    }
}

Audio::~Audio() = default;

void Audio::play(std::uint32_t time, std::uint32_t scale, std::int16_t level) {
    stop();
    if (pcm_.empty() || !scale) {
        return;
    }
    const auto offset =
        static_cast<std::uint64_t>(time) * format_.nSamplesPerSec / scale * format_.nChannels;
    if (offset >= pcm_.size()) {
        return;
    }
    if (!output_ || !output_->current_device()) {
        output_ = std::make_unique<Output>(format_);
    }
    volume(level);
    output_->play(std::span(pcm_).subspan(static_cast<std::size_t>(offset)));
}

void Audio::stop() {
    if (output_) {
        output_->stop();
    }
}

void Audio::volume(std::int16_t value) {
    volume_ = value;
    if (output_) {
        output_->volume(volume_, balance_);
    }
}

void Audio::balance(std::int16_t value) {
    balance_ = value;
    volume(volume_);
}

void Audio::refresh(std::uint32_t time, std::uint32_t scale) {
    if (output_ && !output_->current_device()) {
        play(time, scale, volume_);
    }
}

}
