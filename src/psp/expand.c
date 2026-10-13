// Portable translation of the game's Expand decoder
#include <limits.h>
#include <stdint.h>

#include "psp/expand.h"
#ifdef RECVX_GAME_BOOT
#include "psp/game_boot.h"
#endif

EXPAND_CTRL_BUF ExpandCtrlBuf;

typedef struct ExpandReader {
    const unsigned char *source;
    size_t size;
    size_t position;
    unsigned int descriptor;
    unsigned int bits_left;
} ExpandReader;

static int read_byte(ExpandReader *reader, unsigned int *byte)
{
    if (reader->position == reader->size)
        return 0;
    *byte = reader->source[reader->position++];
    return 1;
}

static int read_bit(ExpandReader *reader, unsigned int *bit)
{
    if (reader->bits_left == 0) {
        if (!read_byte(reader, &reader->descriptor))
            return 0;
        reader->bits_left = 8;
    }
    *bit = reader->descriptor & 1;
    reader->descriptor >>= 1;
    --reader->bits_left;
    return 1;
}

void Init_Expand(void)
{
    ExpandCtrlBuf.flag.abort = 0;
}

RecvxExpandResult recvxExpandBounded(const void *source, size_t source_size,
                                   void *destination, size_t destination_size,
                                   size_t *consumed, size_t *written)
{
    ExpandReader reader = {source, source_size, 0, 0, 0};
    unsigned char *output = destination;
    size_t produced = 0;
    RecvxExpandResult result = RECVX_EXPAND_TRUNCATED;

    if (consumed != NULL)
        *consumed = 0;
    if (written != NULL)
        *written = 0;
    if (source == NULL || (destination == NULL && destination_size != 0))
        return RECVX_EXPAND_INVALID_ARGUMENT;

    for (;;) {
        unsigned int bit, byte, lower, upper;
        size_t distance, length;

        if (!read_bit(&reader, &bit))
            break;
        if (bit) {
            if (!read_byte(&reader, &byte))
                break;
            if (produced == destination_size) {
                result = RECVX_EXPAND_OUTPUT_FULL;
                break;
            }
            output[produced++] = (unsigned char)byte;
            continue;
        }

        if (!read_bit(&reader, &bit))
            break;
        if (!bit) {
            if (!read_bit(&reader, &bit))
                break;
            length = bit << 1;
            if (!read_bit(&reader, &bit) || !read_byte(&reader, &byte))
                break;
            length += bit + 2;
            distance = 0x100 - byte;
        } else {
            unsigned int word;
            if (!read_byte(&reader, &lower) || !read_byte(&reader, &upper))
                break;
            word = lower | (upper << 8);
            if (word == 0) {
                result = RECVX_EXPAND_OK;
                break;
            }
            distance = 0x2000 - (word >> 3);
            length = word & 7;
            if (length != 0) {
                length += 2;
            } else {
                if (!read_byte(&reader, &byte))
                    break;
                length = byte + 1;
            }
        }

        if (distance > produced) {
            result = RECVX_EXPAND_BAD_REFERENCE;
            break;
        }
        if (length > destination_size - produced) {
            result = RECVX_EXPAND_OUTPUT_FULL;
            break;
        }
        while (length-- != 0) {
            output[produced] = output[produced - distance];
            ++produced;
        }
    }

    if (consumed != NULL)
        *consumed = reader.position;
    if (written != NULL)
        *written = produced;
    return result;
}

// original API has no buffer sizes; new file-loading code should call recvxExpandBounded instead
int Expand(char *source, unsigned char *destination)
{
#ifdef RECVX_GAME_BOOT
    return recvxGameBootExpand(source, destination);
#else
    size_t written;
    RecvxExpandResult result = recvxExpandBounded(source, SIZE_MAX, destination,
                                                INT_MAX, NULL, &written);
    return result == RECVX_EXPAND_OK ? (int)written : -1;
#endif
}
