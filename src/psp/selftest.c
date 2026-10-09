#include <stddef.h>
#include <string.h>

#include "flag.h"
#include "ps2_NaMem.h"
#include "psp/expand.h"
#include "psp/selftest.h"

int recvxCheckFlags(void)
{
    unsigned int flags[2] = {0, 0};
    unsigned int bit;

    for (bit = 0; bit < 64; ++bit) {
        bhStFlg(flags, bit);
        if (!bhCkFlg(flags, bit))
            return 1;
    }
    if (flags[0] != 0xffffffffu || flags[1] != 0xffffffffu)
        return 2;
    for (bit = 0; bit < 64; ++bit) {
        bhCrFlg(flags, bit);
        if (bhCkFlg(flags, bit))
            return 3;
    }
    if (flags[0] != 0 || flags[1] != 0)
        return 4;
    bhStFlg(flags, 0);
    bhStFlg(flags, 63);
    return flags[0] == 0x80000000u && flags[1] == 1u ? 0 : 5;
}

int recvxCheckMemory(void)
{
    const unsigned char bytes[] = {0, 0x80, 0xff, 0x34, 0x56};
    unsigned char output[7];
    const unsigned int words[] = {0x12345678u, 0x89abcdefu, 0x76543210u};
    unsigned int copied[5];
    size_t i;

    memset(output, 0xa5, sizeof(output));
    njMemCopy(output + 1, bytes, 0);
    if (output[1] != 0xa5)
        return 1;
    njMemCopy(output + 1, bytes, sizeof(bytes));
    if (memcmp(output + 1, bytes, sizeof(bytes)) != 0 ||
        output[0] != 0xa5 || output[6] != 0xa5)
        return 2;

    for (i = 0; i < 5; ++i)
        copied[i] = 0xa5a5a5a5u;
    njMemCopy4(copied + 1, words, 0);
    if (copied[1] != 0xa5a5a5a5u)
        return 3;
    /* njMemCopy4's count is words, not bytes. */
    njMemCopy4(copied + 1, words, 3);
    return memcmp(copied + 1, words, sizeof(words)) == 0 &&
           copied[0] == 0xa5a5a5a5u && copied[4] == 0xa5a5a5a5u ? 0 : 4;
}

int recvxCheckExpand(void)
{
    /* Literal 'A', short distance-1/length-2 match, long length-3 match,
     * extended length-4 match, and an end marker: ten overlapping 'A's.
     */
    static const unsigned char packed[] = {
        0x41, 'A', 0xff, 0xf9, 0xff, 0x05, 0xf8, 0xff, 3, 0, 0
    };
    static const unsigned char empty[] = {0x02, 0, 0};
    static const unsigned char bad[] = {0x00, 0xff};
    unsigned char output[12];
    size_t consumed, written, i;
    RecvxExpandResult result;

    ExpandCtrlBuf.flag.abort = 1;
    Init_Expand();
    if (ExpandCtrlBuf.flag.abort != 0)
        return 1;
    memset(output, 0xa5, sizeof(output));
    result = recvxExpandBounded(packed, sizeof(packed), output + 1, 10,
                               &consumed, &written);
    if (result != RECVX_EXPAND_OK || written != 10 || consumed != sizeof(packed))
        return 2;
    for (i = 1; i <= 10; ++i)
        if (output[i] != 'A')
            return 3;
    if (output[0] != 0xa5 || output[11] != 0xa5)
        return 4;
    if (Expand((char *)packed, output + 1) != 10)
        return 5;
    if (recvxExpandBounded(empty, sizeof(empty), NULL, 0, &consumed, &written)
        != RECVX_EXPAND_OK || written != 0)
        return 6;
    if (recvxExpandBounded(packed, sizeof(packed), output, 9, NULL, NULL)
        != RECVX_EXPAND_OUTPUT_FULL)
        return 7;
    if (recvxExpandBounded(packed, sizeof(packed) - 1, output, sizeof(output),
                          NULL, NULL) != RECVX_EXPAND_TRUNCATED)
        return 8;
    if (recvxExpandBounded(bad, sizeof(bad), output, sizeof(output), NULL, NULL)
        != RECVX_EXPAND_BAD_REFERENCE)
        return 9;
    return 0;
}
