#include <string.h>

#include "psp/sha256.h"

typedef struct RecvxSha256State {
    uint32_t words[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_size;
} RecvxSha256State;

static const uint32_t round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t rotate_right(uint32_t value, unsigned int shift)
{
    return (value >> shift) | (value << (32 - shift));
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void transform(RecvxSha256State *state, const uint8_t block[64])
{
    uint32_t schedule[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned int i;

    for (i = 0; i < 16; ++i)
        schedule[i] = read_be32(block + i * 4);
    for (; i < 64; ++i) {
        uint32_t s0 = rotate_right(schedule[i - 15], 7) ^
                      rotate_right(schedule[i - 15], 18) ^
                      (schedule[i - 15] >> 3);
        uint32_t s1 = rotate_right(schedule[i - 2], 17) ^
                      rotate_right(schedule[i - 2], 19) ^
                      (schedule[i - 2] >> 10);
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }

    a = state->words[0];
    b = state->words[1];
    c = state->words[2];
    d = state->words[3];
    e = state->words[4];
    f = state->words[5];
    g = state->words[6];
    h = state->words[7];
    for (i = 0; i < 64; ++i) {
        uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                        rotate_right(e, 25);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                        rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t1 = h + sum1 + choice + round_constants[i] + schedule[i];
        uint32_t t2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state->words[0] += a;
    state->words[1] += b;
    state->words[2] += c;
    state->words[3] += d;
    state->words[4] += e;
    state->words[5] += f;
    state->words[6] += g;
    state->words[7] += h;
}

static void update(RecvxSha256State *state, const uint8_t *bytes, size_t size)
{
    state->bit_count += (uint64_t)size * 8;
    while (size != 0) {
        size_t available = sizeof(state->block) - state->block_size;
        size_t count = size < available ? size : available;

        memcpy(state->block + state->block_size, bytes, count);
        state->block_size += count;
        bytes += count;
        size -= count;
        if (state->block_size == sizeof(state->block)) {
            transform(state, state->block);
            state->block_size = 0;
        }
    }
}

void recvxSha256(const void *bytes, size_t size, uint8_t digest[32])
{
    RecvxSha256State state = {
        {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19},
        0, {0}, 0
    };
    const uint8_t *input = bytes;
    uint64_t bit_count;
    unsigned int i;

    if (digest == NULL || (bytes == NULL && size != 0))
        return;
    update(&state, input, size);
    bit_count = state.bit_count;
    state.block[state.block_size++] = 0x80;
    if (state.block_size > 56) {
        memset(state.block + state.block_size, 0,
               sizeof(state.block) - state.block_size);
        transform(&state, state.block);
        state.block_size = 0;
    }
    memset(state.block + state.block_size, 0, 56 - state.block_size);
    for (i = 0; i < 8; ++i)
        state.block[63 - i] = (uint8_t)(bit_count >> (i * 8));
    transform(&state, state.block);
    for (i = 0; i < 8; ++i)
        write_be32(digest + i * 4, state.words[i]);
}
