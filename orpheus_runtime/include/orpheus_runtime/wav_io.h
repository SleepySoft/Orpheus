#ifndef ORPHEUS_RUNTIME_WAV_IO_H
#define ORPHEUS_RUNTIME_WAV_IO_H

#include <string>
#include <vector>
#include <cstdint>

namespace orpheus {

struct WavInfo {
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t total_frames;
};

// Read entire WAV file as interleaved f32.
// Supports PCM 16-bit, 24-bit, 32-bit integer and 32-bit float.
bool wav_read_f32(const std::string& path, std::vector<float>& samples, WavInfo& info);

// Write interleaved f32 samples as 16-bit PCM WAV.
bool wav_write_i16(const std::string& path, const std::vector<float>& samples, const WavInfo& info);

} // namespace orpheus

#endif
