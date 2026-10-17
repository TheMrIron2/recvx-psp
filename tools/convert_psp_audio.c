/* Host asset cooker only. ADX -> Sony VAG; PSP playback uses sceSasCore. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned int be32(const unsigned char *p) { return (be16(p) << 16) | be16(p + 2); }
static void put32(unsigned char *p, unsigned int v) { for (int i = 0; i < 4; ++i) p[i] = v >> (i * 8); }
static int clamp(int s) { return s > 32767 ? 32767 : s < -32768 ? -32768 : s; }
static const int filter[5][2] = {{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
static void encode(const int16_t *pcm, unsigned int count, unsigned int loop_start,
                   int loop, unsigned char *output)
{
    int hist[2] = {0};
    unsigned int blocks = (count + 27) / 28;
    for (unsigned int block = 0; block < blocks; ++block) {
        double best = 1e30;
        int best_hist[2] = {0}, best_filter = 0, best_shift = 0;
        unsigned char best_data[14] = {0};
        for (int f = 0; f < 5; ++f) {
            int maximum = 0, h[2] = {hist[0], hist[1]}, shift = 0;
            if (loop && block == loop_start / 28 && f) continue;
            for (int i = 0; i < 28; ++i) {
                int s = block * 28u + i < count ? pcm[block * 28u + i] : 0;
                int r = s - ((h[0] * filter[f][0] + h[1] * filter[f][1]) >> 6);
                if (abs(r) > maximum) maximum = abs(r);
                h[1] = h[0]; h[0] = s;
            }
            while (shift < 12 && maximum < (7 * (4096 >> (shift + 1)))) ++shift;
            for (int trial = shift > 0 ? shift - 1 : 0; trial <= shift; ++trial) {
                unsigned char data[14] = {0}; double error = 0;
                int step = 4096 >> trial; h[0] = hist[0]; h[1] = hist[1];
                for (int i = 0; i < 28; ++i) {
                    int s = block * 28u + i < count ? pcm[block * 28u + i] : 0;
                    int prediction = (h[0] * filter[f][0] + h[1] * filter[f][1]) >> 6;
                    int residual = s - prediction;
                    int n = residual >= 0 ? (residual + step / 2) / step : -((-residual + step / 2) / step);
                    if (n > 7) n = 7; if (n < -8) n = -8;
                    int result = clamp(prediction + n * step);
                    error += (double)(s - result) * (s - result);
                    h[1] = h[0]; h[0] = result;
                    data[i / 2] |= (n & 15) << ((i & 1) * 4);
                }
                if (error < best) {
                    best = error; best_hist[0] = h[0]; best_hist[1] = h[1];
                    best_filter = f; best_shift = trial; memcpy(best_data, data, 14);
                }
            }
        }
        unsigned char *p = output + block * 16;
        p[0] = (best_filter << 4) | best_shift;
        p[1] = loop ? block == loop_start / 28 ? 6 : block + 1 == blocks ? 3 : 2 : 0;
        memcpy(p + 2, best_data, 14); hist[0] = best_hist[0]; hist[1] = best_hist[1];
    }
    output[blocks * 16 + 1] = 7; /* Native SAS terminator. */
}
/* Interleaved signed LE16 PCM from the movie build-host demux/resampler. */
static int encode_pcm(int argc, char **argv)
{
    unsigned long channels, rate;
    char *end;
    FILE *input, *output;
    long size;
    unsigned char header[64] = {0}, *raw, *encoded;
    int16_t *plane;
    unsigned int samples, bytes;
    if (argc != 6) return 1;
    channels = strtoul(argv[2], &end, 10); if (*end || channels < 1 || channels > 2) return 2;
    rate = strtoul(argv[3], &end, 10); if (*end || rate < 8000 || rate > 48000) return 2;
    input = fopen(argv[4], "rb"); if (!input) return 3;
    if (fseek(input, 0, SEEK_END) || (size = ftell(input)) <= 0 ||
        size > 48000L * 600 * 4 || size % (2 * channels) || fseek(input, 0, SEEK_SET)) { fclose(input); return 4; }
    samples = (unsigned int)(size / (2 * channels));
    bytes = ((samples + 27) / 28 + 1) * 16;
    raw = malloc((size_t)size); plane = malloc((size_t)samples * sizeof(*plane));
    encoded = calloc(1, bytes);
    if (!raw || !plane || !encoded) { fclose(input); free(raw); free(plane); free(encoded); return 5; }
    if (fread(raw, 1, (size_t)size, input) != (size_t)size) { fclose(input); free(raw); free(plane); free(encoded); return 6; }
    fclose(input);
    memcpy(header, "RVAG", 4); put32(header + 4, 1); put32(header + 8, (unsigned int)channels);
    put32(header + 12, (unsigned int)rate); put32(header + 16, bytes); put32(header + 24, samples);
    output = fopen(argv[5], "wb"); if (!output) { free(raw); free(plane); free(encoded); return 7; }
    int failed = fwrite(header, 1, 64, output) != 64;
    for (unsigned int ch = 0; ch < channels && !failed; ++ch) {
        for (unsigned int i = 0; i < samples; ++i) {
            size_t p = (i * channels + ch) * 2;
            plane[i] = (int16_t)((unsigned int)raw[p] | ((unsigned int)raw[p + 1] << 8));
        }
        encode(plane, samples, 0, 0, encoded);
        failed = fwrite(encoded, 1, bytes, output) != bytes;
    }
    if (fclose(output)) failed = 1;
    free(raw); free(plane); free(encoded); return failed ? 8 : 0;
}
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--pcm")) return encode_pcm(argc, argv);
    FILE *f; long size; unsigned char *data, *output;
    unsigned int offset, channels, rate, samples, loop = 0, loop_start = 0, blocks, bytes;
    int16_t *pcm; int hist[2][2] = {{0}}; int c0, c1;
    if (argc != 3 || !(f = fopen(argv[1], "rb"))) return 1;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 24 || size > 16 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) return 2;
    data = malloc((size_t)size); if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) return 3; fclose(f);
    offset = be16(data + 2) + 4; channels = data[7]; rate = be32(data + 8); samples = be32(data + 12);
    if (be16(data) != 0x8000 || data[4] != 3 || data[5] != 18 || data[6] != 4 ||
        channels < 1 || channels > 2 || rate < 8000 || rate > 48000 || !samples || samples > 48000u * 600 ||
        data[18] != 3 || data[19] || offset < 24 || offset > (unsigned long)size ||
        memcmp(data + offset - 6, "(c)CRI", 6) ||
        (uint64_t)offset + ((uint64_t)samples + 31) / 32 * 18 * channels > (unsigned long)size) return 4;
    if (offset >= 50 && be16(data + 22) == 1) {
        loop_start = be32(data + 28); unsigned int end = be32(data + 36);
        if (loop_start >= end || end > samples || be32(data + 32) != offset + loop_start / 32 * 18 * channels) return 5;
        samples = end; loop = 1;
    }
    pcm = malloc((size_t)samples * channels * sizeof(*pcm)); if (!pcm) return 6;
    double a = sqrt(2.0) - cos(6.283185307179586 * be16(data + 16) / rate), b = sqrt(2.0) - 1;
    double c = (a - sqrt((a + b) * (a - b))) / b;
    c0 = (int)(2 * c * 4096); c1 = (int)(-c * c * 4096);
    for (unsigned int block = 0; block < (samples + 31) / 32; ++block) for (unsigned int ch = 0; ch < channels; ++ch) {
        const unsigned char *p = data + offset + (block * channels + ch) * 18;
        unsigned int code = be16(p); int scale = (code & 0x1fff) + 1;
        if (code & 0x8000) return 7;
        for (unsigned int i = 0; i < 32 && block * 32 + i < samples; ++i) {
            int n = (p[2 + i / 2] >> ((i & 1) ? 0 : 4)) & 15; if (n >= 8) n -= 16;
            int s = clamp(n * scale + ((hist[ch][0] * c0 + hist[ch][1] * c1) >> 12));
            hist[ch][1] = hist[ch][0]; hist[ch][0] = s; pcm[ch * samples + block * 32 + i] = (int16_t)s;
        }
    }
    blocks = (samples + 27) / 28; bytes = (blocks + 1) * 16;
    output = calloc(1, 64u + bytes * channels); if (!output) return 8;
    memcpy(output, "RVAG", 4); put32(output + 4, 1); put32(output + 8, channels); put32(output + 12, rate);
    put32(output + 16, bytes); put32(output + 20, loop); put32(output + 24, samples); put32(output + 28, loop_start / 28 * 28);
    for (unsigned int ch = 0; ch < channels; ++ch) encode(pcm + ch * samples, samples, loop_start, loop, output + 64 + ch * bytes);
    f = fopen(argv[2], "wb"); if (!f || fwrite(output, 1, 64u + bytes * channels, f) != 64u + bytes * channels || fclose(f)) return 9;
    free(data); free(output); free(pcm); return 0;
}
