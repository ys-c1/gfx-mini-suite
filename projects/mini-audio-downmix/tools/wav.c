#include "wav.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16(const uint8_t* p) {
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}
static void wr_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void wr_u16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static int set_err(const char** err, const char* msg) {
    if (err) *err = msg;
    return -1;
}

int wav_read(const char* path, WavData* w, const char** err) {
    if (err) *err = NULL;
    if (!w || !path) return set_err(err, "null argument");
    memset(w, 0, sizeof(*w));

    FILE* f = fopen(path, "rb");
    if (!f) return set_err(err, "cannot open file");
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return set_err(err, "seek"); }
    long len = ftell(f);
    if (len < 12 || len > (long)(64u << 20)) {
        fclose(f);
        return set_err(err, "file size out of range");
    }
    rewind(f);
    uint8_t* buf = (uint8_t*)malloc((size_t)len);
    if (!buf) { fclose(f); return set_err(err, "out of memory"); }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf); fclose(f);
        return set_err(err, "short read");
    }
    fclose(f);

    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        free(buf);
        return set_err(err, "not a RIFF/WAVE file");
    }

    int have_fmt = 0;
    size_t off = 12;
    uint8_t* data = NULL;
    size_t data_len = 0;
    while (off + 8 <= (size_t)len) {
        const uint8_t* id = buf + off;
        uint32_t sz = rd_u32(buf + off + 4);
        size_t body = off + 8;
        if (sz > (size_t)len - body) break;          /* truncated chunk */
        if (memcmp(id, "fmt ", 4) == 0 && sz >= 16) {
            uint16_t tag = rd_u16(buf + body);
            w->channels = rd_u16(buf + body + 2);
            w->sample_rate = (int)rd_u32(buf + body + 4);
            w->bits = rd_u16(buf + body + 14);
            if (tag == 0xFFFE && sz >= 40) {          /* extensible */
                tag = rd_u16(buf + body + 24);
            }
            w->is_float = (tag == 3);
            if (tag != 1 && tag != 3) {
                free(buf);
                return set_err(err, "unsupported WAVE format tag");
            }
            have_fmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            data = buf + body;
            data_len = sz;
        }
        off = body + sz + (sz & 1u);                  /* chunks are padded */
    }

    if (!have_fmt || !data) { free(buf); return set_err(err, "missing fmt/data"); }
    if (w->channels < 1 || w->channels > 8) {
        free(buf);
        return set_err(err, "unsupported channel count in header");
    }
    if (!((w->bits == 16 && !w->is_float) || (w->bits == 32 && w->is_float))) {
        free(buf);
        return set_err(err, "only PCM16 and float32 are supported");
    }
    size_t bytes_per_samp = (size_t)w->bits / 8u;
    size_t frame_bytes = bytes_per_samp * (size_t)w->channels;
    w->frames = data_len / frame_bytes;
    if (w->frames == 0) { free(buf); return set_err(err, "no audio frames"); }

    w->samples = (float*)malloc(w->frames * (size_t)w->channels * sizeof(float));
    if (!w->samples) { free(buf); return set_err(err, "out of memory"); }
    for (size_t i = 0; i < w->frames * (size_t)w->channels; ++i) {
        if (w->is_float) {
            uint32_t u = rd_u32(data + i * 4);
            float v;
            memcpy(&v, &u, 4);
            w->samples[i] = v;
        } else {
            int16_t s = (int16_t)rd_u16(data + i * 2);
            w->samples[i] = (float)s / 32768.0f;
        }
    }
    free(buf);
    return 0;
}

void wav_free(WavData* w) {
    if (!w) return;
    free(w->samples);
    w->samples = NULL;
    w->frames = 0;
}

int wav_write_f32(const char* path, const WavData* w, const char** err) {
    if (err) *err = NULL;
    if (!w || !w->samples || !path || w->channels < 1 || w->frames == 0)
        return set_err(err, "bad argument");
    size_t n = w->frames * (size_t)w->channels;
    if (n > (0xFFFFFFFFu - 64u) / 4u) return set_err(err, "too large");

    uint32_t data_bytes = (uint32_t)(n * 4u);
    uint32_t riff_size = 4u + (8u + 16u) + (8u + data_bytes);
    uint8_t hdr[44];
    memcpy(hdr, "RIFF", 4);
    wr_u32(hdr + 4, riff_size);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    wr_u32(hdr + 16, 16);
    wr_u16(hdr + 20, 3);                       /* IEEE float */
    wr_u16(hdr + 22, (uint16_t)w->channels);
    wr_u32(hdr + 24, (uint32_t)w->sample_rate);
    wr_u32(hdr + 28, (uint32_t)(w->sample_rate * w->channels * 4));
    wr_u16(hdr + 32, (uint16_t)(w->channels * 4));
    wr_u16(hdr + 34, 32);
    memcpy(hdr + 36, "data", 4);
    wr_u32(hdr + 40, data_bytes);

    FILE* f = fopen(path, "wb");
    if (!f) return set_err(err, "cannot open output");
    if (fwrite(hdr, 1, sizeof hdr, f) != sizeof hdr) {
        fclose(f);
        return set_err(err, "short header write");
    }
    for (size_t i = 0; i < n; ++i) {
        uint8_t b[4];
        uint32_t u;
        memcpy(&u, &w->samples[i], 4);
        wr_u32(b, u);
        if (fwrite(b, 1, 4, f) != 4) {
            fclose(f);
            return set_err(err, "short data write");
        }
    }
    if (fclose(f) != 0) return set_err(err, "close failed");
    return 0;
}
