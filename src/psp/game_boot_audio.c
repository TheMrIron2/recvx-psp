#include <math.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_audio.h"
#include "psp/game_boot_movie_audio.h"

#define VOICES 32
#define BANKS 5
#define MIDI_SLOTS 8
typedef struct Bank {
    unsigned char *data;
    size_t size;
    unsigned int hd, hs, bd, bs, vagi, smpl, sset, prog;
} Bank;
typedef struct Voice {
    const unsigned char *data;
    size_t size;
    unsigned int rate, position;
    int active, paused, codec, slot, owner, key, volume, target, fade, pan;
} Voice;
typedef struct Sequence {
    const unsigned char *data;
    size_t size, pc;
    int active, bank, volume, pan, running, immediate, program[16], cv[16], cp[16];
    unsigned int ticks, tempo, division;
    uint64_t phase;
    int master, target, fade, stop_when_done;
} Sequence;
static Bank banks[2][BANKS];
static unsigned char *sequences[BANKS];
static size_t sequence_sizes[BANKS];
static Voice voices[VOICES];
static Sequence midi[MIDI_SLOTS];
static char directory[512], fault[192];
static unsigned int pending, bank_key;
static unsigned short keys[1024];
static int mounted;
static unsigned char *stream_data[2];
static int stream_volume[2];
static int stream_pan[2][2];
static int8_t environment[16];
static unsigned int le16(const unsigned char *p) { return p[0] | ((unsigned int)p[1] << 8); }
static unsigned int le32(const unsigned char *p) { return le16(p) | (le16(p + 2) << 16); }
static int span(size_t size, size_t offset, size_t length) { return offset <= size && length <= size - offset; }
static int fail(const char *reason) { if (!fault[0]) snprintf(fault, sizeof(fault), "%s", reason); return -1; }
const char *recvxAudioFault(void)
{ const char *result; recvxAudioLock(); result = fault[0] ? fault : NULL; recvxAudioUnlock(); return result; }
static int gain(int volume) { return volume <= -127 ? 0 : volume >= 0 ? 4096 : (int)(4096.0 * pow(10.0, volume / 40.0)); }
static unsigned char *read_asset(const char *relative, size_t *size)
{
    char path[768];
    FILE *file;
    long length;
    unsigned char *data;
    if (snprintf(path, sizeof(path), "%s/%s", directory, relative) >= (int)sizeof(path)) {
        fail("audio asset path is too long"); return NULL;
    }
    file = fopen(path, "rb");
    if (!file) { fail(relative); return NULL; }
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file); fail("invalid audio asset size"); return NULL;
    }
    data = memalign(64, (size_t)length);
    if (!data) { fclose(file); fail("audio allocation failed"); return NULL; }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data); fclose(file); fail("truncated audio asset"); return NULL;
    }
    fclose(file); *size = (size_t)length; return data;
}
static void stop(Voice *v) { recvxAudioNativeStop((unsigned int)(v - voices)); memset(v, 0, sizeof(*v)); }
static int native_volume(const Voice *v)
{
    return v->owner >= 0 ? v->volume * midi[v->owner].master / 4096 : v->volume;
}
static unsigned int record(const Bank *b, unsigned int base, unsigned int index, unsigned int minimum)
{
    const unsigned char *h = b->data + b->hd;
    unsigned int offset;
    if (!span(b->hs, base, 16) || index >= le32(h + base + 12) ||
        !span(b->hs, base + 16, (index + 1u) * 4u)) return 0;
    offset = le32(h + base + 16 + index * 4u);
    if (offset < 16 || !span(b->hs, base, offset) || !span(b->hs, base + offset, minimum)) return 0;
    return base + offset;
}
static int install_bank(int kind, unsigned int id, const unsigned char *data, size_t size)
{
    Bank b = {0};
    const unsigned char *h;
    unsigned int i, base;
    if (id >= BANKS || size < 16) return fail("invalid sound bank identity");
    b.hd = le32(data); b.hs = le32(data + 4); b.bd = le32(data + 8); b.bs = le32(data + 12);
    if (!span(size, b.hd, b.hs) || !span(size, b.bd, b.bs) || b.hs < 80 ||
        memcmp(data + b.hd, "IECSsreV", 8) || memcmp(data + b.hd + 16, "IECSdaeH", 8))
        return fail("invalid Sony sound bank header");
    b.data = (unsigned char *)data; b.size = size; h = data + b.hd;
    b.prog = le32(h + 36); b.sset = le32(h + 40); b.smpl = le32(h + 44); b.vagi = le32(h + 48);
    for (i = 0; i < 4; ++i) {
        const unsigned int offsets[] = {b.prog, b.sset, b.smpl, b.vagi};
        const char *const names[] = {"IECSgorP", "IECStesS", "IECSlpmS", "IECSigaV"};
        unsigned int j, n;
        base = offsets[i];
        if (!span(b.hs, base, 16) || memcmp(h + base, names[i], 8)) return fail("invalid sound bank section");
        n = le32(h + base + 12);
        if (n > 512 || !span(b.hs, base + 16, n * 4u)) return fail("sound bank table exceeds header");
        for (j = 0; j < n; ++j) if (le32(h + base + 16 + j * 4u) != 0xffffffffu &&
            !record(&b, base, j, i == 0 ? 36 : i == 1 ? 4 : i == 2 ? 24 : 8))
            return fail("sound bank record exceeds header");
    }
    for (i = 0; i < le32(h + b.vagi + 12); ++i) {
        base = record(&b, b.vagi, i, 8);
        if (!base) continue; /* Sparse Sony tables use 0xffffffff for vacant records. */
        if (le32(h + base) > b.bs || !le16(h + base + 4) || le16(h + base + 4) > 48000)
            return fail("invalid bank waveform range or rate");
    }
    b.data = memalign(64, size);
    if (!b.data) return fail("sound bank allocation failed");
    memcpy(b.data, data, size);
    for (i = 4; i < VOICES; ++i) if (voices[i].codec == 1 && banks[kind][id].data &&
        (uintptr_t)voices[i].data >= (uintptr_t)banks[kind][id].data &&
        (uintptr_t)voices[i].data - (uintptr_t)banks[kind][id].data < banks[kind][id].size) stop(&voices[i]);
    free(banks[kind][id].data); banks[kind][id] = b; return 0;
}
static int mount(void)
{
    unsigned char *data;
    size_t size;
    if (mounted) return 0;
    data = read_asset("files/COMMON.MLT", &size);
    if (!data) return -1;
    if (install_bank(0, 0, data, size)) { free(data); return -1; }
    free(data);
    data = read_asset("files/MULTSPQ1.IDX", &size);
    if (!data) return -1;
    if (size != sizeof(keys)) { free(data); return fail("invalid sound bank index"); }
    for (size_t i = 0; i < 1024; ++i) keys[i] = (unsigned short)le16(data + i * 2);
    free(data); mounted = 1; return 0;
}
int recvxAudioMount(void)
{ int result; recvxAudioLock(); result = mount(); recvxAudioUnlock(); return result; }
int recvxAudioInit(const char *root)
{
    recvxAudioShutdown();
    if (!root || strlen(root) >= sizeof(directory)) return -1;
    snprintf(directory, sizeof(directory), "%s", root); fault[0] = 0; return 0;
}
void recvxAudioShutdown(void)
{
    recvxAudioLock();
    recvxMovieAudioStopLocked();
    for (int i = 0; i < VOICES; ++i) stop(&voices[i]);
    for (int k = 0; k < 2; ++k) for (int i = 0; i < BANKS; ++i) { free(banks[k][i].data); memset(&banks[k][i], 0, sizeof(Bank)); }
    for (int i = 0; i < BANKS; ++i) { free(sequences[i]); sequences[i] = NULL; sequence_sizes[i] = 0; }
    memset(midi, 0, sizeof(midi)); memset(environment, 0, sizeof(environment)); mounted = 0; pending = 0;
    for (int i = 0; i < 2; ++i) { free(stream_data[i]); stream_data[i] = NULL; stream_volume[i] = 4096; }
    memset(stream_pan, 0, sizeof(stream_pan));
    recvxAudioUnlock();
}
int recvxAudioRequestBank(unsigned int key)
{
    int result = 0;
    recvxAudioLock();
    if (pending) result = fail("sound bank request overlaps a pending transfer");
    else if (mount()) result = -1;
    else { pending = 1; bank_key = key; }
    recvxAudioUnlock(); return result;
}
int recvxAudioBankBusy(void)
{ int result; recvxAudioLock(); result = (int)pending; recvxAudioUnlock(); return result; }
int recvxAudioEnvironment(unsigned int field) { return field < 16 ? environment[field] : 0; }
void recvxAudioService(void)
{
    unsigned char *data;
    size_t size, header;
    unsigned int i;
    char path[64];
    recvxAudioLock();
    if (!pending || fault[0]) { recvxAudioUnlock(); return; }
    for (i = 0; i < 1024 && keys[i] != 65535 && keys[i] != bank_key; ++i) {}
    if (i == 1024 || keys[i] == 65535) { fail("requested sound bank key is absent"); recvxAudioUnlock(); return; }
    snprintf(path, sizeof(path), "archives/MULTSPQ1/%u.bin", i);
    data = read_asset(path, &size);
    if (!data) { recvxAudioUnlock(); return; }
    for (header = 0; span(size, header, 16); header += 16) {
        unsigned int offset = le32(data + header), length = le32(data + header + 4);
        unsigned int type = le32(data + header + 8), bank = le32(data + header + 12);
        if (!offset) { pending = 0; break; }
        if (!span(size, offset, length) || !length || type > 5 || (type < 3 && bank >= BANKS)) {
            fail("invalid SPQ sound bank record"); break;
        }
        if (type == 1 || type == 2) {
            if (install_bank(type == 1, bank, data + offset, length)) break;
        } else if (type == 0) {
            unsigned char *copy;
            if (length < 48 || memcmp(data + offset, "IECSsreV", 8) || memcmp(data + offset + 16, "IECSuqeS", 8)) {
                fail("invalid Sony sequence bank"); break;
            }
            copy = memalign(64, length);
            if (!copy) { fail("sequence allocation failed"); break; }
            memcpy(copy, data + offset, length);
            for (i = 0; i < MIDI_SLOTS; ++i) if (midi[i].bank == (int)bank) midi[i].active = 0;
            free(sequences[bank]); sequences[bank] = copy; sequence_sizes[bank] = length;
        } else if (type == 5) {
            if (length < 16) { fail("invalid room sound environment"); break; }
            memcpy(environment, data + offset, sizeof(environment));
        }
        else if (type == 3 || type == 4) { fail("PS2 reverb program bank is unsupported"); break; }
    }
    if (pending && !fault[0]) fail("unterminated SPQ sound pack");
    free(data); recvxAudioUnlock();
}
int recvxAudioAdx(unsigned int slot, unsigned int partition, unsigned int id)
{
    const char *name = partition == 0 ? "BGM1" : partition == 1 ? "VOICE1" : partition == 3 ? "ADV" : NULL;
    char path[64]; unsigned char *data; size_t size; unsigned int channels, rate, bytes, loop;
    if (slot >= 2 || !name) return fail("invalid ADX slot or partition");
    recvxAudioLock(); snprintf(path, sizeof(path), "audio/%s/%u.vag", name, id);
    data = read_asset(path, &size);
    if (!data) { recvxAudioUnlock(); return -1; }
    channels = size >= 24 ? le32(data + 8) : 0;
    rate = size >= 24 ? le32(data + 12) : 0;
    bytes = size >= 24 ? le32(data + 16) : 0;
    loop = size >= 24 ? le32(data + 20) : 0;
    if (size < 64 || memcmp(data, "RVAG", 4) || le32(data + 4) != 1 ||
        channels < 1 || channels > 2 || rate < 8000 || rate > 48000 ||
        !bytes || (bytes & 15) || bytes > (size - 64) / channels || loop > 1) {
        free(data); recvxAudioUnlock(); return fail("invalid cooked native ADPCM stream");
    }
    stop(&voices[slot * 2]); stop(&voices[slot * 2 + 1]); free(stream_data[slot]); stream_data[slot] = data;
    for (unsigned int c = 0; c < channels; ++c) {
        unsigned int index = slot * 2 + c; Voice *v = &voices[index];
        v->data = data + 64 + c * bytes; v->size = bytes; v->rate = rate;
        v->volume = v->target = stream_volume[slot];
        v->pan = stream_pan[slot][c] + (channels == 1 ? 0 : c == 0 ? -127 : 127);
        if (v->pan < -127) v->pan = -127;
        if (v->pan > 127) v->pan = 127;
        v->slot = (int)slot; v->owner = -2;
        if (recvxAudioNativeStart(index, v->data, bytes, rate, (int)loop, -1, -1, v->volume, v->pan)) {
            fail("native ADPCM stream start failed"); break;
        }
        v->active = 1;
    }
    recvxAudioUnlock(); return fault[0] ? -1 : 0;
}
void recvxAudioStop(unsigned int slot)
{
    recvxAudioLock();
    if (slot < 2) { stop(&voices[slot * 2]); stop(&voices[slot * 2 + 1]); free(stream_data[slot]); stream_data[slot] = NULL; }
    recvxAudioUnlock();
}
void recvxAudioPause(unsigned int slot, int paused)
{
    recvxAudioLock();
    if (slot < 2) for (unsigned int i = slot * 2; i < slot * 2 + 2; ++i) {
        voices[i].paused = !!paused; recvxAudioNativePause(i, !!paused);
    }
    recvxAudioUnlock();
}
void recvxAudioVolume(unsigned int slot, int volume, int frames)
{
    recvxAudioLock();
    if (slot < 2) {
        stream_volume[slot] = gain(volume);
        for (unsigned int i = slot * 2; i < slot * 2 + 2; ++i) {
            Voice *v = &voices[i]; v->target = stream_volume[slot]; v->fade = frames > 0 ? frames : 0;
            if (!frames) { v->volume = v->target; recvxAudioNativeVolume(i, v->volume, v->pan); }
        }
    }
    recvxAudioUnlock();
}
void recvxAudioPan(unsigned int slot, unsigned int channel, int pan)
{
    recvxAudioLock();
    if (slot < 2 && channel < 2) {
        Voice *v = &voices[slot * 2 + channel];
        if (pan < -127) pan = -127;
        if (pan > 127) pan = 127;
        stream_pan[slot][channel] = pan;
        /* Voice streams are mono; stereo preserves the other channel. */
        v->pan = pan;
        if (v->active) recvxAudioNativeVolume((unsigned int)(v - voices), v->volume, v->pan);
    }
    recvxAudioUnlock();
}
int recvxAudioStatus(unsigned int slot)
{ int result; recvxAudioLock(); result = slot < 2 && (voices[slot * 2].active || voices[slot * 2 + 1].active); recvxAudioUnlock(); return result; }
unsigned int recvxAudioTime(unsigned int slot)
{ unsigned int result; recvxAudioLock(); result = slot < 2 ? (unsigned int)((uint64_t)voices[slot * 2].position * 1000 / RECVX_AUDIO_RATE) : 0; recvxAudioUnlock(); return result; }
void recvxAudioStopSounds(int frames)
{
    recvxAudioLock();
    for (int i = 0; i < MIDI_SLOTS; ++i) { midi[i].active = 0; midi[i].fade = 0; }
    for (int i = 4; i < VOICES; ++i) {
        if (frames <= 0) stop(&voices[i]);
        else { voices[i].target = 0; voices[i].fade = frames; }
    }
    recvxAudioUnlock();
}
void recvxAudioStopAll(int frames)
{
    recvxAudioLock();
    for (int i = 0; i < MIDI_SLOTS; ++i) { midi[i].active = 0; midi[i].fade = 0; }
    for (int i = 0; i < VOICES; ++i) {
        if (frames <= 0) stop(&voices[i]);
        else { voices[i].target = 0; voices[i].fade = frames; }
    }
    recvxAudioUnlock();
}
static int play_note(int kind, int bank, int program, int key, int velocity, int volume, int pan, int slot, int owner)
{
    Bank *b;
    const unsigned char *h;
    unsigned int p, tone, set, sample, wave, offset, end;
    int started = 0;
    if (bank < 0 || bank >= BANKS || !(b = &banks[kind][bank])->data) return fail("requested sound bank is not loaded");
    h = b->data + b->hd; p = record(b, b->prog, (unsigned int)program, 36);
    if (!p) return fail("sound program exceeds loaded bank");
    tone = le32(h + p);
    if (h[p + 4] > 32 || !span(b->hs, p + tone, h[p + 4] * 20u)) return fail("invalid sound program tones");
    for (unsigned int t = 0; t < h[p + 4]; ++t) {
        unsigned int entry = p + tone + t * 20;
        if (key < h[entry + 2] || key > h[entry + 4]) continue;
        set = record(b, b->sset, le16(h + entry), 4);
        if (!set || h[set + 3] > 32 || !span(b->hs, set + 4, h[set + 3] * 2u)) return fail("invalid sample set");
        for (unsigned int layer = 0; layer < h[set + 3]; ++layer) {
            Voice *v = NULL;
            sample = record(b, b->smpl, le16(h + set + 4 + layer * 2), 24);
            if (!sample) return fail("invalid sound sample");
            wave = record(b, b->vagi, le16(h + sample), 8);
            if (!wave) return fail("invalid sound waveform");
            offset = le32(h + wave); end = b->bs;
            for (unsigned int i = 0; i < le32(h + b->vagi + 12); ++i) {
                unsigned int w = record(b, b->vagi, i, 8), next;
                if (!w) continue;
                next = le32(h + w);
                if (next > offset && next < end) end = next;
            }
            for (int i = 4; i < VOICES; ++i) if (!voices[i].active) { v = &voices[i]; break; }
            if (!v) continue; /* Finite hardware voice pool: drop lower priority new notes. */
            stop(v); v->data = b->data + b->bd + offset; v->size = end - offset;
            v->rate = (unsigned int)(le16(h + wave + 4) * pow(2, (key - h[sample + 11]) / 12.0));
            if (!v->rate || v->rate > 192000) return fail("invalid sound pitch");
            v->codec = 1;
            v->volume = v->target = gain(volume) * velocity / 127 * h[p + 6] / 127 * h[sample + 4] / 127;
            v->pan = pan; v->slot = slot; v->owner = owner; v->key = key;
            if ((v->size & 15) || recvxAudioNativeStart((unsigned int)(v - voices), v->data, v->size,
                v->rate, h[wave + 6] & 1, le16(h + sample + 19), le16(h + sample + 21), native_volume(v), v->pan))
                return fail("native bank sample start failed");
            v->active = 1; ++started;
        }
    }
    return started;
}
int recvxAudioPlaySe(int slot, int sound, int volume, int pan)
{
    int result;
    recvxAudioLock();
    for (int i = 4; i < VOICES; ++i) if (voices[i].owner == -1 && voices[i].slot == slot) stop(&voices[i]);
    result = mount() ? -1 : play_note(0, (sound >> 8) & 15, sound & 255, 60, 127, volume, pan, slot, -1);
    recvxAudioUnlock(); return result;
}
static int variable(Sequence *s, unsigned int *out)
{
    unsigned int value = 0;
    for (int i = 0; i < 4; ++i) {
        unsigned int c;
        if (s->pc >= s->size) return fail("truncated sequence variable integer");
        c = s->data[s->pc++]; value = (value << 7) | (c & 127);
        if (!(c & 128)) { *out = value; return 0; }
    }
    return fail("invalid sequence variable integer");
}
static int sequence_event(int slot)
{
    Sequence *s = &midi[slot];
    unsigned int status, channel, a, b = 0, length;
    if (s->pc >= s->size) return fail("sequence event exceeds stream");
    status = s->data[s->pc];
    if (status & 128) { ++s->pc; if (status < 0xf0) s->running = (int)status; }
    else status = (unsigned int)s->running;
    if (status == 255) {
        if (s->pc >= s->size) return fail("truncated sequence meta event");
        a = s->data[s->pc++];
        if (variable(s, &length) || !span(s->size, s->pc, length)) return fail("invalid sequence meta extent");
        if (a == 0x2f) s->active = 0;
        else if (a == 0x51 && length == 3) { s->tempo = (s->data[s->pc] << 16) | (s->data[s->pc + 1] << 8) | s->data[s->pc + 2]; if (!s->tempo) return fail("invalid sequence tempo"); }
        else if (a != 0x58 && a != 0x59) return fail("unsupported Sony sequence meta event");
        s->pc += length; s->immediate = 0;
    } else {
        channel = status & 15;
        if (status < 0x80 || status >= 0xf0 || s->pc >= s->size) return fail("invalid sequence status");
        a = s->data[s->pc++];
        if ((status & 0xf0) == 0x90 || (status & 0xf0) == 0xb0 || (status & 0xf0) == 0xe0) {
            if (s->pc >= s->size) return fail("truncated sequence event");
            b = s->data[s->pc++]; s->immediate = !!(b & 128); b &= 127;
        } else s->immediate = !!(a & 128);
        a &= 127;
        switch (status & 0xf0) {
        case 0x80: case 0x90:
            if ((status & 0xf0) == 0x80 || !b) {
                for (int i = 4; i < VOICES; ++i) if (voices[i].owner == slot && voices[i].slot == (int)channel && voices[i].key == (int)a) {
                    voices[i].target = 0; voices[i].fade = 2;
                }
            } else if (play_note(1, s->bank, s->program[channel], (int)a, (int)b * s->cv[channel] / 127, s->volume, s->pan + s->cp[channel] - 64, (int)channel, slot) < 0) return -1;
            break;
        case 0xc0: s->program[channel] = (int)a; break;
        case 0xb0:
            if (a == 7) s->cv[channel] = (int)b;
            else if (a == 10) s->cp[channel] = (int)b;
            else if (a == 120 || a == 123) for (int i = 4; i < VOICES; ++i) { if (voices[i].owner == slot) stop(&voices[i]); }
            else return fail("unsupported Sony sequence controller");
            break;
        default: return fail("unsupported Sony sequence event");
        }
    }
    s->ticks = 0;
    if (s->active && !s->immediate && variable(s, &s->ticks)) return -1;
    return 0;
}
int recvxAudioPlayMidiFade(int slot, int sound, int volume, int pan, int frames)
{
    Sequence s = {0}; unsigned int bank = (sound >> 8) & 15, base, offset, end, count;
    const unsigned char *data; size_t size;
    if (slot < 0 || slot >= MIDI_SLOTS || bank >= BANKS) return fail("invalid sequence slot or bank");
    recvxAudioLock(); data = sequences[bank]; size = sequence_sizes[bank];
    if (!data || size < 48) { recvxAudioUnlock(); return fail("sequence bank is not loaded"); }
    base = le32(data + 36);
    if (!span(size, base, 16) || memcmp(data + base, "IECSidiM", 8)) { recvxAudioUnlock(); return fail("invalid sequence section"); }
    count = le32(data + base + 12);
    if ((unsigned int)(sound & 255) >= count || count > 512 || !span(size, base + 16, count * 4u)) { recvxAudioUnlock(); return fail("invalid sequence index"); }
    offset = base + le32(data + base + 16 + (sound & 255) * 4u);
    end = (unsigned int)size;
    for (unsigned int i = 0; i < count; ++i) { unsigned int next = base + le32(data + base + 16 + i * 4u); if (next > offset && next < end) end = next; }
    if (end <= offset || !span(size, offset, 7) || le32(data + offset) != 6 || !le16(data + offset + 4)) { recvxAudioUnlock(); return fail("invalid compact MIDI header"); }
    s.data = data + offset; s.size = end - offset; s.pc = 6; s.division = le16(data + offset + 4);
    s.bank = (int)bank; s.volume = volume; s.pan = pan; s.tempo = 500000; s.active = 1;
    s.master = frames > 0 ? 0 : 4096; s.target = 4096; s.fade = frames > 0 ? frames : 0;
    for (int i = 0; i < 16; ++i) { s.cv[i] = 127; s.cp[i] = 64; }
    variable(&s, &s.ticks);
    for (int i = 4; i < VOICES; ++i) if (voices[i].active && voices[i].owner == slot) stop(&voices[i]);
    midi[slot] = s;
    recvxAudioUnlock(); return fault[0] ? -1 : 0;
}
int recvxAudioPlayMidi(int slot, int sound, int volume, int pan)
{ return recvxAudioPlayMidiFade(slot, sound, volume, pan, 0); }
void recvxAudioStopSe(int slot) { recvxAudioLock(); for (int i = 4; i < VOICES; ++i) if (voices[i].owner == -1 && voices[i].slot == slot) stop(&voices[i]); recvxAudioUnlock(); }
void recvxAudioUpdateSe(int slot, int volume, int pan, int pitch)
{
    recvxAudioLock();
    for (int i = 4; i < VOICES; ++i) if (voices[i].active && voices[i].owner == -1 && voices[i].slot == slot) {
        voices[i].volume = voices[i].target = gain(volume); voices[i].pan = pan;
        recvxAudioNativeVolume((unsigned int)i, voices[i].volume, pan);
        if (pitch) recvxAudioNativePitch((unsigned int)i, (unsigned int)(voices[i].rate * pow(2, pitch / 4096.0)));
    }
    recvxAudioUnlock();
}
void recvxAudioStopMidi(int slot) { recvxAudioLock(); if (slot >= 0 && slot < MIDI_SLOTS) { midi[slot].active = 0; for (int i = 4; i < VOICES; ++i) if (voices[i].owner == slot) stop(&voices[i]); } recvxAudioUnlock(); }
void recvxAudioMidiVolume(int slot, int volume, int frames, int stop_when_done)
{
    recvxAudioLock();
    if (slot >= 0 && slot < MIDI_SLOTS) {
        Sequence *s = &midi[slot];
        s->target = gain(volume); s->fade = frames > 0 ? frames : 0;
        s->stop_when_done = !!stop_when_done;
        if (!s->fade) {
            s->master = s->target;
            for (int i = 4; i < VOICES; ++i) if (voices[i].active && voices[i].owner == slot)
                recvxAudioNativeVolume((unsigned int)i, native_volume(&voices[i]), voices[i].pan);
            if (s->stop_when_done) {
                s->active = 0;
                for (int i = 4; i < VOICES; ++i) if (voices[i].active && voices[i].owner == slot) stop(&voices[i]);
            }
        }
    }
    recvxAudioUnlock();
}
void recvxAudioRender(int16_t *out, size_t frames)
{
    unsigned int ended;
    for (int slot = 0; slot < MIDI_SLOTS; ++slot) {
        Sequence *s = &midi[slot]; int budget = 256;
        while (s->active && !s->ticks && !fault[0] && budget > 0) { --budget; if (sequence_event(slot)) break; }
        if (!budget && s->active && !s->ticks) { fail("sequence event watchdog"); s->active = 0; }
        if (s->active && s->ticks) {
            s->phase += (uint64_t)s->division * 1000000u * frames;
            while (s->ticks && s->phase >= (uint64_t)s->tempo * RECVX_AUDIO_RATE) { s->phase -= (uint64_t)s->tempo * RECVX_AUDIO_RATE; --s->ticks; }
        }
    }
    if (recvxAudioNativeRender(out, frames)) fail("native SAS synthesis failed");
    if (recvxMovieAudioRenderLocked(frames)) {
        recvxMovieAudioStopLocked();
        fail("native movie ADPCM stream refill failed");
    }
    ended = recvxAudioNativeEnded();
    for (int i = 0; i < VOICES; ++i) if (voices[i].active && !voices[i].paused) {
        if (ended & (1u << i)) voices[i].active = 0;
        voices[i].position += (unsigned int)frames;
    }
}
void recvxAudioAdvanceFades(void)
{
    for (int slot = 0; slot < MIDI_SLOTS; ++slot) {
        Sequence *s = &midi[slot];
        if (!s->fade) continue;
        s->master += (s->target - s->master) / s->fade;
        if (!--s->fade) s->master = s->target;
        for (int i = 4; i < VOICES; ++i) if (voices[i].active && voices[i].owner == slot)
            recvxAudioNativeVolume((unsigned int)i, native_volume(&voices[i]), voices[i].pan);
        if (!s->fade && s->stop_when_done) {
            s->active = 0;
            for (int i = 4; i < VOICES; ++i) if (voices[i].active && voices[i].owner == slot) stop(&voices[i]);
        }
    }
    for (int i = 0; i < VOICES; ++i) {
        Voice *v = &voices[i];
        if (!v->fade) continue;
        v->volume += (v->target - v->volume) / v->fade;
        if (!--v->fade) v->volume = v->target;
        recvxAudioNativeVolume((unsigned int)i, native_volume(v), v->pan);
        if (!v->fade && !v->target && v->active) stop(v);
    }
}
