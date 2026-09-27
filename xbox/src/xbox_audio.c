/* xbox_audio.c — replaces pc/src/pc_audio.c on Xbox.
 *
 * Same design as the PC file (game -> producer thread -> 32 kHz stereo ring),
 * but the output side is our own AC97 pump instead of SDL audio: nxdk's SDL
 * driver waits on the AC97 completion interrupt, which never arrives under
 * xemu once pbkit is up (measured: 3 callbacks in 2 s without pbkit, 0 with).
 * The pump POLLS the PCM-out current-index register (ACI 0xFEC00114), keeps
 * XBOX_AUDIO_NBUF descriptors queued ahead of the DMA, and resamples the
 * 32 kHz ring to the AC97's fixed 48 kHz with linear interpolation. */
/* (from pc_audio.c) SDL2 audio backend with dedicated producer thread.
 *
 * Architecture (matches GC):
 *   Game thread:  Na_GameFrame() queues commands via message queues
 *   Audio thread: pc_audio_process_frame() produces samples into ring buffer
 *   SDL callback: reads ring buffer → speakers
 *
 * The audio thread decouples sample production from the game frame,
 * so OS preemption of the game thread doesn't cause audio dropouts.
 */
/* kernel headers first: types.h (via pc_platform.h) defines __declspec() away,
 * which turns xboxkrnl.h's dllimport data declarations into definitions */
#include <hal/audio.h>
#include <xboxkrnl/xboxkrnl.h>
#include "pc_platform.h"
#include "pc_settings.h"
#include "jaudio_NES/audiothread.h"

#define PC_AUDIO_SAMPLE_RATE 32000

/* Native atomics, NOT SDL_Atomic*: nxdk's SDL implements those with one global
 * spinlock whose contention path is SDL_Delay(0), which only yields to threads
 * of equal or higher priority. The game thread (normal) preempted inside that
 * lock + the AC97 pump (high) spinning on it = the whole game livelocked,
 * intermittently, anywhere from 5 s to minutes in (docs/traps.md). */
#define AGET(x)    __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define ASET(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

/* lock-free SPSC ring buffer (producer=audio thread, consumer=SDL callback) */
#define RING_BUF_SAMPLES (32768) /* ~512ms at 32kHz stereo */
#define RING_BUF_MASK    (RING_BUF_SAMPLES - 1)

/* Produce more samples when buffer drops below this level.
 * ~4 audio frames ahead = ~70ms of buffer at 32kHz stereo. */
#define AUDIO_PRODUCE_THRESHOLD 4480

static s16 ring_buffer[RING_BUF_SAMPLES];
static int ring_write_pos; /* written by audio producer thread */
static int ring_read_pos;   /* written by SDL audio callback */
static int audio_device = 0;   /* 1 once the AC97 pump runs */

typedef void (*AIDMACallback)(void);
static AIDMACallback ai_dma_callback = NULL;
static u32 ai_dsp_sample_rate = PC_AUDIO_SAMPLE_RATE;

/* --- Audio producer thread --- */
static SDL_Thread* audio_producer_thread = NULL;
static int audio_thread_running;

static int pc_audio_producer_func(void* data) {
    (void)data;
    while (AGET(audio_thread_running)) {
        int fill = pc_audio_get_buffer_fill();
        if (fill < AUDIO_PRODUCE_THRESHOLD) {
            pc_audio_process_frame();
        } else {
            SDL_Delay(1);
        }
    }
    return 0;
}

void pc_audio_start_producer_thread(void) {
    if (audio_producer_thread) return;
    ASET(audio_thread_running, 1);
    audio_producer_thread = SDL_CreateThread(pc_audio_producer_func, "AudioProducer", NULL);
    if (audio_producer_thread) {
        printf("[AUDIO] Producer thread started\n");
    } else {
        printf("[AUDIO] Failed to create producer thread: %s\n", SDL_GetError());
    }
}

/* --- AC97 output pump --- */
#define XBOX_AUDIO_NBUF    8          /* divides 32 (the descriptor ring) */
#define XBOX_AUDIO_FRAMES  1024       /* 48 kHz frames per buffer, ~21 ms */
#define ACI ((volatile unsigned char*)0xFEC00000)

static s16* s_outbuf[XBOX_AUDIO_NBUF];
static SDL_Thread* s_pump_thread;
static int s_pump_run, s_playing;
static unsigned s_queued;             /* descriptors queued since start */
static u32 s_frac;                    /* resampler phase, 16.16 in input frames */

static void fill_48k(s16* out) {
    /* step = 32000/48000 = 2/3 of an input frame, in 16.16 */
    const u32 step = (u32)((32000ull << 16) / 48000);
    u32 wp = (u32)AGET(ring_write_pos);
    u32 rp = (u32)AGET(ring_read_pos);
    int i;
    for (i = 0; i < XBOX_AUDIO_FRAMES; i++) {
        u32 need = rp + 4;   /* current and next stereo frame */
        if (!AGET(s_playing) || (s32)(wp - need) < 0) {
            out[2 * i] = out[2 * i + 1] = 0;
            continue;
        }
        {
            s32 t = (s32)(s_frac & 0xFFFF);
            s32 l0 = ring_buffer[rp & RING_BUF_MASK], r0 = ring_buffer[(rp + 1) & RING_BUF_MASK];
            s32 l1 = ring_buffer[(rp + 2) & RING_BUF_MASK], r1 = ring_buffer[(rp + 3) & RING_BUF_MASK];
            out[2 * i] = (s16)(l0 + (((l1 - l0) * t) >> 16));
            out[2 * i + 1] = (s16)(r0 + (((r1 - r0) * t) >> 16));
        }
        s_frac += step;
        rp += (s_frac >> 16) * 2;
        s_frac &= 0xFFFF;
    }
    ASET(ring_read_pos, (int)rp);
}

#ifdef XBOX_DBG_AUDIO
/* log DMA progress + output peak every ~2 s (splits device vs game path) */
static void dbg_audio(const s16* last) {
    static u32 t0, n;
    u32 now = SDL_GetTicks();
    int i, peak = 0;
    if (now - t0 < 2000) return;
    t0 = now;
    for (i = 0; i < XBOX_AUDIO_FRAMES * 2; i++) {
        int v = last[i] < 0 ? -last[i] : last[i];
        if (v > peak) peak = v;
    }
    printf("[AUDIO] #%u civ %u lvi %u sr %04x picb %u cr %02x queued %u fill %d playing %d peak %d\n", (unsigned)n++,
           ACI[0x114] & 31, ACI[0x115] & 31, *(volatile u16*)(ACI + 0x116), *(volatile u16*)(ACI + 0x118),
           ACI[0x11B], s_queued, pc_audio_get_buffer_fill(), AGET(s_playing), peak);
}
#endif

static int pump_func(void* data) {
    (void)data;
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    while (AGET(s_pump_run)) {
#ifdef XBOX_DBG_AUDIO
        dbg_audio(s_outbuf[(s_queued + XBOX_AUDIO_NBUF - 1) % XBOX_AUDIO_NBUF]);
#endif
        unsigned civ = ACI[0x114] & 31;
        unsigned played = s_queued & 31;
        unsigned ahead = (played - civ) & 31;
        while (ahead < XBOX_AUDIO_NBUF - 1) {
            s16* b = s_outbuf[s_queued % XBOX_AUDIO_NBUF];
            fill_48k(b);
            XAudioProvideSamples((unsigned char*)b, XBOX_AUDIO_FRAMES * 4, FALSE);
            s_queued++;
            ahead++;
        }
        SDL_Delay(2);
    }
    return 0;
}

/* --- AI (Audio Interface) --- */

void AIInit(u8* stack) {
    int i;
    (void)stack;
    if (audio_device != 0) return;
    for (i = 0; i < XBOX_AUDIO_NBUF; i++) {
        s_outbuf[i] = (s16*)MmAllocateContiguousMemoryEx(XBOX_AUDIO_FRAMES * 4, 0, 0xFFFFFFFF, 0,
                                                         PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (!s_outbuf[i]) { printf("[AUDIO] buffer alloc failed\n"); return; }
        memset(s_outbuf[i], 0, XBOX_AUDIO_FRAMES * 4);
    }
    XAudioInit(16, 2, NULL, NULL);
    /* Unmute the codec mixer. The retail WM9709 has no mixer registers (these
     * writes are no-ops there), but xemu models a generic AC97 codec whose
     * reset state is Master 0x8000 / PCM-out 0x8808 = muted, and nxdk never
     * touches them: DMA ran at 48 kHz with real samples and nothing was heard.
     * 0x0000 = unmuted, full scale (QEMU hw/audio/ac97.c set_volume). */
    *(volatile u16*)(ACI + 0x02) = 0x0000;   /* AC97_Master_Volume_Mute */
    *(volatile u16*)(ACI + 0x18) = 0x0000;   /* AC97_PCM_Out_Volume_Mute */
    s_queued = 0;
    ASET(s_pump_run, 1);
    s_pump_thread = SDL_CreateThread(pump_func, "AC97Pump", NULL);
    XAudioPlay();
    audio_device = 1;
    printf("[AUDIO] AC97 pump: 48 kHz, %d x %d frames, polled\n", XBOX_AUDIO_NBUF, XBOX_AUDIO_FRAMES);
}

void AIInitDMA(u32 addr, u32 size) {
    s16* src = (s16*)(uintptr_t)addr;
    u32 n_samples = size / sizeof(s16);
    n_samples &= ~1u; /* whole stereo frames */

    u32 wp = (u32)AGET(ring_write_pos);
    u32 rp = (u32)AGET(ring_read_pos);
    u32 used = wp - rp;
    u32 free = RING_BUF_SAMPLES - used;

    if (n_samples > free) {
        n_samples = free & ~1u;
    }

    int vol = g_pc_settings.master_volume;
    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;

    for (u32 i = 0; i < n_samples; i++) {
        int s = ((int)src[i] * vol) / 100;
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        ring_buffer[(wp + i) & RING_BUF_MASK] = (s16)s;
    }
    ASET(ring_write_pos, (int)(wp + n_samples));
}

void AIStartDMA(void) { ASET(s_playing, 1); }

void AIStopDMA(void) { ASET(s_playing, 0); }

void pc_audio_set_paused(int paused) { ASET(s_playing, paused ? 0 : 1); }

u32  AIGetDMAStartAddr(void) { return 0; }
u16  AIGetDMALength(void) { return 0; }
u32  AIGetStreamTrigger(void) { return 0; }
u32  AIGetStreamSampleCount(void) { return 0; }
void AISetStreamPlayState(u32 state) { (void)state; }
u32  AIGetStreamPlayState(void) { return 0; }
void AISetStreamSampleRate(u32 rate) { (void)rate; }
u32  AIGetStreamSampleRate(void) { return PC_AUDIO_SAMPLE_RATE; }
void AISetStreamVolLeft(u8 vol) { (void)vol; }
void AISetStreamVolRight(u8 vol) { (void)vol; }
u8   AIGetStreamVolLeft(void) { return 0; }
u8   AIGetStreamVolRight(void) { return 0; }
void AIResetStreamSampleCount(void) {}
void AISetDSPSampleRate(u32 rate) { ai_dsp_sample_rate = rate; }
u32  AIGetDSPSampleRate(void) { return ai_dsp_sample_rate; }

void* AIRegisterDMACallback(void* callback) {
    void* old = (void*)ai_dma_callback;
    ai_dma_callback = (AIDMACallback)callback;
    return old;
}

/* --- DSP stubs (rspsim does everything in software) --- */

void DSPInit(void) {}
BOOL DSPCheckMailToDSP(void) { return FALSE; }
BOOL DSPCheckMailFromDSP(void) { return FALSE; }
u32  DSPReadMailFromDSP(void) { return 0; }
void DSPSendMailToDSP(u32 mail) { (void)mail; }
void DSPAssertInt(void) {}
void* DSPAddTask(void* task) { return task; }

/* --- ring buffer queries for frame pacing (pc_vi.c) --- */

int pc_audio_get_buffer_fill(void) {
    return AGET(ring_write_pos) - AGET(ring_read_pos);
}

int pc_audio_is_active(void) {
    return audio_device != 0;
}

void pc_audio_shutdown(void) {
    /* Stop producer thread first */
    ASET(audio_thread_running, 0);
    if (audio_producer_thread) {
        SDL_WaitThread(audio_producer_thread, NULL);
        audio_producer_thread = NULL;
    }
    ASET(s_pump_run, 0);
    if (s_pump_thread) {
        SDL_WaitThread(s_pump_thread, NULL);
        s_pump_thread = NULL;
    }
    XAudioPause();
    audio_device = 0;
}
