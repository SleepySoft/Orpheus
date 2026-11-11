#include "component_state.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define ORPHEUS_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define ORPHEUS_MKDIR(p) mkdir(p, 0755)
#endif

static void write_u16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static int write_file(const orpheus_builtin_wav_out_State* s) {
    const char* slash = strrchr(s->file_path, '/');
    const char* backslash = strrchr(s->file_path, '\\');
    const char* separator = backslash && (!slash || backslash > slash) ? backslash : slash;
    if (separator && separator != s->file_path) {
        char directory[512];
        size_t length = (size_t)(separator - s->file_path);
        memcpy(directory, s->file_path, length);
        directory[length] = '\0';
        if (ORPHEUS_MKDIR(directory) && errno != EEXIST) return ORPHEUS_ERR_NOT_FOUND;
    }
    FILE* file = fopen(s->file_path, "wb");
    if (!file) return ORPHEUS_ERR_NOT_FOUND;
    uint32_t size = s->algorithm.count * 2;
    uint8_t header[44];
    memcpy(header, "RIFF", 4);
    write_u32(header + 4, 36 + size);
    memcpy(header + 8, "WAVEfmt ", 8);
    write_u32(header + 16, 16);
    write_u16(header + 20, 1);
    write_u16(header + 22, (uint16_t)s->channels);
    write_u32(header + 24, s->algorithm.sample_rate);
    write_u32(header + 28, s->algorithm.sample_rate * (uint32_t)s->channels * 2);
    write_u16(header + 32, (uint16_t)(s->channels * 2));
    write_u16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    write_u32(header + 40, size);
    int result = fwrite(header, 1, sizeof(header), file) == sizeof(header) ? ORPHEUS_OK : ORPHEUS_ERR_PROCESSING;
    for (uint32_t i = 0; result == ORPHEUS_OK && i < s->algorithm.count; ++i) {
        float value = s->algorithm.samples[i];
        if (value > 1.0f) value = 1.0f;
        if (value < -1.0f) value = -1.0f;
        uint8_t pcm[2];
        write_u16(pcm, (uint16_t)(int16_t)(value * 32767.0f));
        if (fwrite(pcm, 1, sizeof(pcm), file) != sizeof(pcm)) result = ORPHEUS_ERR_PROCESSING;
    }
    if (fclose(file)) result = ORPHEUS_ERR_PROCESSING;
    return result;
}

int orpheus_builtin_wav_out_prepare(orpheus_builtin_wav_out_State* s, const OrpheusConfig* config) {
    double rate = s->sample_rate > 0 ? s->sample_rate : config->sample_rate > 0 ? config->sample_rate : 48000;
    if (!isfinite(rate) || rate < 1 || rate > UINT32_MAX / ((uint32_t)s->channels * 2))
        return ORPHEUS_ERR_INVALID_ARG;
    s->algorithm.sample_rate = (uint32_t)rate;
    s->algorithm.count = 0;
    s->algorithm.samples = malloc((size_t)ORPHEUS_WAV_OUT_CAPACITY * sizeof(float));
    return s->algorithm.samples ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

int orpheus_builtin_wav_out_destroy(orpheus_builtin_wav_out_State* s) {
    int result = ORPHEUS_OK;
    if (s->algorithm.samples && s->file_path[0]) {
        result = write_file(s);
        if (result != ORPHEUS_OK)
            fprintf(stderr, "WAV输出落盘失败：%s（错误码%d）\n", s->file_path, result);
    }
    free(s->algorithm.samples);
    s->algorithm.samples = NULL;
    s->algorithm.count = 0;
    return result;
}

int orpheus_builtin_wav_out_reset(orpheus_builtin_wav_out_State* s) {
    s->algorithm.count = 0;
    return ORPHEUS_OK;
}

int orpheus_builtin_wav_out_algorithm(orpheus_builtin_wav_out_State* s,
    orpheus_builtin_wav_out_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!s->algorithm.samples || !io->in.buffer || io->in.buffer->frame_count < ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t count = (size_t)ctx->frame_count * s->channels;
    if (s->algorithm.count > ORPHEUS_WAV_OUT_CAPACITY || count > ORPHEUS_WAV_OUT_CAPACITY - s->algorithm.count)
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    for (size_t i = 0; i < count; ++i)
        if (isnan(io->in.samples[i])) return ORPHEUS_ERR_INVALID_ARG;
    memcpy(s->algorithm.samples + s->algorithm.count, io->in.samples, count * sizeof(float));
    s->algorithm.count += (uint32_t)count;
    return ORPHEUS_OK;
}
