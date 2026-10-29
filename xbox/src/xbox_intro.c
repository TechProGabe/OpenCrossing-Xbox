/* xbox_intro.c — the boot screen's music, played while the first boot's disc
 * check runs (xbox_disc_check.c). It rides at the end of default.xbe, so a
 * copy of the game always has it; tools/xbox/xbe_payload.py appends it (entry
 * INTR) and documents the layout: IMA ADPCM in QuickTime packets, 32 kHz
 * stereo (the game's ring rate), volume baked in at 75%. Decoder from
 * Prime-X.
 *
 * As cheap as audio gets: a thread reads 8.5 KB at a time from the disk
 * (~4 reads a second: the disc check's reads barely notice), expands it (a
 * table lookup and a few adds a sample) into the 32 kHz ring xbox_audio.c's
 * AC97 pump plays from, and sleeps. Loops if the check outlasts the song;
 * fades over a second when it ends. The game's own audio then starts on the
 * same ring and pump.
 *
 *   xbox_intro_start()   finds the music (none: silence) and starts playing
 *   xbox_intro_stop()    fades out over a second and stops; returns once done
 * Kill switch: -DXBOX_NO_INTRO. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "xbox_io.h"
#include "xbox_intro.h"
#include "xbox_payload.h"

int xbox_audio_intro_begin(void);
unsigned xbox_audio_intro_space(void);
void xbox_audio_intro_write(const short* frames, unsigned n);
void xbox_audio_intro_end(void);
int xbox_crash_guard(int (*fn)(void*), void* arg);

#define INTRO_RATE 32000
#define PACKET_BYTES 68     /* left 34, right 34: a header and 64 nibbles each */
#define PACKET_FRAMES 64
#define CHUNK_PACKETS 128   /* 8704 bytes, 8192 frames a read */
#define CHUNK_FRAMES (CHUNK_PACKETS * PACKET_FRAMES)
#define FADE_FRAMES INTRO_RATE

static HANDLE s_file = INVALID_HANDLE_VALUE;
static unsigned s_off, s_size, s_pos;   /* the payload in default.xbe; s_pos within it */
static unsigned char* s_packets;
static short* s_buf;
static SDL_Thread* s_thread;
static volatile int s_run, s_stop, s_done;

/* ---- IMA ADPCM, QuickTime flavour (as ffmpeg's adpcm_ima_qt decodes it) ---- */
static const short k_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};
static const signed char k_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

typedef struct { int pred, index; } adpcm_ch;
static adpcm_ch s_ch[2];

/* one channel's 34 bytes into every other sample of out */
static void decode_channel(adpcm_ch* c, const unsigned char* p, short* out) {
    int head = p[0] << 8 | p[1], pred = (short)(head & 0xff80), index = head & 0x7f, m;
    if (index > 88) index = 88;
    /* the header holds 9 bits of the predictor: carry on from the last
     * packet unless it says something else (a loop, or the first packet) */
    if (c->index != index || pred - c->pred > 0x7f || c->pred - pred > 0x7f) {
        c->pred = pred;
        c->index = index;
    }
    pred = c->pred;
    index = c->index;
    for (m = 0; m < PACKET_FRAMES; m++) {
        int nib = m & 1 ? p[2 + (m >> 1)] >> 4 : p[2 + (m >> 1)] & 15;
        int step = k_step[index], diff = step >> 3;
        if (nib & 4) diff += step;
        if (nib & 2) diff += step >> 1;
        if (nib & 1) diff += step >> 2;
        pred = nib & 8 ? pred - diff : pred + diff;
        if (pred < -32768) pred = -32768;
        if (pred > 32767) pred = 32767;
        index += k_index[nib & 7];
        if (index < 0) index = 0;
        if (index > 88) index = 88;
        out[2 * m] = (short)pred;
    }
    c->pred = pred;
    c->index = index;
}

static int file_read(unsigned off, void* dst, unsigned n) { return xbox_payload_read(s_file, off, dst, n); }

static int intro_thread(void* arg) {
    unsigned fade_left = FADE_FRAMES;
    (void)arg;
    while (s_run) {
        unsigned n, k;
        if (xbox_audio_intro_space() < CHUNK_FRAMES) {
            Sleep(30);
            continue;
        }
        n = CHUNK_PACKETS;
        if (s_pos + n * PACKET_BYTES > s_size) n = (s_size - s_pos) / PACKET_BYTES;
        if (n == 0 || !file_read(s_off + s_pos, s_packets, n * PACKET_BYTES)) {
            s_pos = 0;   /* the end, or a bad read: from the top, the headers starting over */
            s_ch[0].index = s_ch[1].index = -1;
            continue;
        }
        s_pos += n * PACKET_BYTES;
        for (k = 0; k < n; k++) {
            short* out = s_buf + k * PACKET_FRAMES * 2;
            decode_channel(&s_ch[0], s_packets + k * PACKET_BYTES, out);
            decode_channel(&s_ch[1], s_packets + k * PACKET_BYTES + 34, out + 1);
        }
        n *= PACKET_FRAMES;
        if (s_stop) {
            /* the fade: the rest of the second, linear */
            for (k = 0; k < n && fade_left; k++, fade_left--) {
                int g = (int)(fade_left * 256 / FADE_FRAMES);
                s_buf[2 * k] = (short)(s_buf[2 * k] * g >> 8);
                s_buf[2 * k + 1] = (short)(s_buf[2 * k + 1] * g >> 8);
            }
            if (k < n) memset(s_buf + 2 * k, 0, (n - k) * 4);
            xbox_audio_intro_write(s_buf, n);
            if (!fade_left) break;
        } else {
            xbox_audio_intro_write(s_buf, n);
        }
    }
    s_done = 1;
    return 0;
}

static int thread_entry(void* d) { return xbox_crash_guard(intro_thread, d); }

static int find_music(void) {
    unsigned off, size;
    if (!xbox_payload_find("INTR", &off, &size) || size < PACKET_BYTES * 500) return 0;
    s_file = xbox_payload_open();
    if (s_file == INVALID_HANDLE_VALUE) return 0;
    s_off = off;
    s_size = size - size % PACKET_BYTES;
    return 1;
}

static void close_file(void) {
    if (s_file != INVALID_HANDLE_VALUE) CloseHandle(s_file);
    s_file = INVALID_HANDLE_VALUE;
}

void xbox_intro_start(void) {
#ifndef XBOX_NO_INTRO
    if (s_run) return;
    if (!find_music()) {
        xbox_logf("[INTRO] no music in default.xbe: the check runs silent\n");
        close_file();
        return;
    }
    s_packets = (unsigned char*)malloc(CHUNK_PACKETS * PACKET_BYTES);
    s_buf = (short*)malloc(CHUNK_FRAMES * 4);
    if (!s_packets || !s_buf || !xbox_audio_intro_begin()) {
        xbox_logf("[INTRO] no audio: the check runs silent\n");
        free(s_packets);
        free(s_buf);
        s_packets = NULL;
        s_buf = NULL;
        close_file();
        return;
    }
    memset(s_ch, 0, sizeof s_ch);
    s_ch[0].index = s_ch[1].index = -1;   /* the first packet's header sets them */
    s_pos = 0;
    s_stop = 0;
    s_done = 0;
    s_run = 1;
    s_thread = SDL_CreateThread(thread_entry, "Intro", NULL);
    if (!s_thread) {
        s_run = 0;
        return;
    }
    xbox_logf("[INTRO] playing (%u KB at %08x, %u s)\n", s_size >> 10, s_off,
              s_size / PACKET_BYTES * PACKET_FRAMES / INTRO_RATE);
#endif
}

void xbox_intro_stop(void) {
    if (!s_run) return;
    s_stop = 1;
    SDL_WaitThread(s_thread, NULL);   /* the second's fade, then out */
    s_thread = NULL;
    s_run = 0;
    xbox_audio_intro_end();
    free(s_packets);
    free(s_buf);
    s_packets = NULL;
    s_buf = NULL;
    close_file();
    xbox_logf("[INTRO] stopped\n");
}
