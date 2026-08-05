#include "orpheus_runtime/wav_io.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

namespace orpheus {

static uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

static void write_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
}

static void write_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xff);
}

bool wav_read_f32(const std::string& path, std::vector<float>& samples, WavInfo& info) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    uint8_t header[44];
    if (std::fread(header, 1, 44, f) != 44) {
        std::fclose(f);
        return false;
    }

    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
        std::fclose(f);
        return false;
    }

    uint16_t audio_format = read_u16(header + 20);
    info.channels = read_u16(header + 22);
    info.sample_rate = read_u32(header + 24);
    uint16_t bits_per_sample = read_u16(header + 34);
    uint32_t data_size = read_u32(header + 40);

    uint32_t bytes_per_sample = bits_per_sample / 8;
    uint32_t total_samples = data_size / bytes_per_sample;
    info.total_frames = total_samples / info.channels;

    samples.resize(total_samples);
    std::vector<uint8_t> raw(data_size);
    if (std::fread(raw.data(), 1, data_size, f) != data_size) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    for (uint32_t i = 0; i < total_samples; ++i) {
        if (audio_format == 1) { // PCM
            if (bits_per_sample == 16) {
                int16_t v = static_cast<int16_t>(read_u16(raw.data() + i * 2));
                samples[i] = v / 32768.0f;
            } else if (bits_per_sample == 24) {
                const uint8_t* p = raw.data() + i * 3;
                int32_t v = (static_cast<int32_t>(p[0])
                    | (static_cast<int32_t>(p[1]) << 8)
                    | (static_cast<int32_t>(p[2]) << 16));
                if (v & 0x800000) v |= 0xff000000;
                samples[i] = v / 8388608.0f;
            } else if (bits_per_sample == 32) {
                int32_t v = static_cast<int32_t>(read_u32(raw.data() + i * 4));
                samples[i] = v / 2147483648.0f;
            } else {
                return false;
            }
        } else if (audio_format == 3 && bits_per_sample == 32) { // IEEE float
            std::memcpy(&samples[i], raw.data() + i * 4, 4);
        } else {
            return false;
        }
    }
    return true;
}

bool wav_write_i16(const std::string& path, const std::vector<float>& samples, const WavInfo& info) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    uint32_t total_samples = static_cast<uint32_t>(samples.size());
    uint32_t data_size = total_samples * 2;
    uint32_t byte_rate = info.sample_rate * info.channels * 2;
    uint16_t block_align = static_cast<uint16_t>(info.channels * 2);

    uint8_t header[44];
    std::memcpy(header, "RIFF", 4);
    write_u32(header + 4, 36 + data_size);
    std::memcpy(header + 8, "WAVE", 4);
    std::memcpy(header + 12, "fmt ", 4);
    write_u32(header + 16, 16);
    write_u16(header + 20, 1); // PCM
    write_u16(header + 22, info.channels);
    write_u32(header + 24, info.sample_rate);
    write_u32(header + 28, byte_rate);
    write_u16(header + 32, block_align);
    write_u16(header + 34, 16);
    std::memcpy(header + 36, "data", 4);
    write_u32(header + 40, data_size);

    std::fwrite(header, 1, 44, f);

    for (float s : samples) {
        float clamped = std::max(-1.0f, std::min(1.0f, s));
        int16_t v = static_cast<int16_t>(clamped * 32767.0f);
        uint8_t buf[2];
        write_u16(buf, static_cast<uint16_t>(v));
        std::fwrite(buf, 1, 2, f);
    }

    std::fclose(f);
    return true;
}

} // namespace orpheus
