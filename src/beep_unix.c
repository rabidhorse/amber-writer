/* The short typewriter beep on macOS and Linux, made the same way as beep.c on Windows.
 *
 * Linux plays it through PulseAudio (which PipeWire also provides) or else ALSA. Both
 * are opened at run time, so the program still starts on a system with neither; it
 * then returns 0 and the caller falls back to the desktop's own beep. macOS plays it
 * as a system sound, at the alert volume set in System Settings.
 */
#ifndef _WIN32
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "beep.h"

#define RATE 22050

static short *samples;
static int sample_count;

/* A sine plus a little third harmonic sounds like the small piezo buzzer in an
   electronic typewriter. */
static void make_samples(double freq, double seconds, double volume)
{
    int n = (int)(RATE * seconds), fade = (int)(RATE * 0.004), i;
    samples = malloc(n * sizeof(short));
    if (!samples)
        return;
    for (i = 0; i < n; i++) {
        double t = (double)i / RATE;
        double s = sin(2 * 3.14159265358979 * freq * t) + 0.3 * sin(2 * 3.14159265358979 * 3 * freq * t);
        double env = 1.0;
        if (i < fade)
            env = (double)i / fade;
        if (n - i < fade)
            env = (double)(n - i) / fade;
        samples[i] = (short)(s / 1.3 * env * volume * 32767);
    }
    sample_count = n;
}

static void ensure_samples(void)
{
    if (!samples)
        make_samples(1760.0, 0.09, 0.35);
}

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <unistd.h>

static SystemSoundID sound;
static int sound_ready;

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

/* System sounds play from a file, so the WAV is written once to the temp folder. */
static int make_sound(void)
{
    char path[1024];
    unsigned char head[44];
    unsigned data;
    const char *tmp = getenv("TMPDIR");
    FILE *f;
    CFURLRef url;
    ensure_samples();
    if (!samples)
        return 0;
    snprintf(path, sizeof path, "%s/amberwriter-beep-%d.wav", tmp && *tmp ? tmp : "/tmp", (int)getuid());
    f = fopen(path, "wb");
    if (!f)
        return 0;
    data = sample_count * 2;
    memcpy(head, "RIFF", 4);
    put32(head + 4, 36 + data);
    memcpy(head + 8, "WAVEfmt ", 8);
    put32(head + 16, 16);
    put16(head + 20, 1);
    put16(head + 22, 1);
    put32(head + 24, RATE);
    put32(head + 28, RATE * 2);
    put16(head + 32, 2);
    put16(head + 34, 16);
    memcpy(head + 36, "data", 4);
    put32(head + 40, data);
    fwrite(head, 1, 44, f);
    fwrite(samples, 2, sample_count, f); /* Macs are little-endian, like WAV */
    if (fclose(f) != 0)
        return 0;
    url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, strlen(path), false);
    if (!url)
        return 0;
    sound_ready = AudioServicesCreateSystemSoundID(url, &sound) == 0;
    CFRelease(url);
    return sound_ready;
}

int beep_play_sound(void)
{
    if (!sound_ready && !make_sound())
        return 0;
    AudioServicesPlaySystemSound(sound);
    return 1;
}

#else /* Linux and other Unix */
#include <dlfcn.h>

/* Just the parts of the PulseAudio and ALSA headers used here. */
typedef struct {
    int format;
    unsigned rate;
    unsigned char channels;
} PaSpec;
#define PA_SAMPLE_S16LE 3
#define PA_STREAM_PLAYBACK 1
typedef void *(*PaNew)(const char *, const char *, int, const char *, const char *, const PaSpec *, const void *,
                       const void *, int *);
typedef int (*PaWrite)(void *, const void *, size_t, int *);
typedef int (*PaDrain)(void *, int *);
typedef void (*PaFree)(void *);

#define SND_PCM_STREAM_PLAYBACK 0
#define SND_PCM_FORMAT_S16_LE 2
#define SND_PCM_ACCESS_RW_INTERLEAVED 3
typedef int (*AlsaOpen)(void **, const char *, int, int);
typedef int (*AlsaSetParams)(void *, int, int, unsigned, unsigned, int, unsigned);
typedef long (*AlsaWrite)(void *, const void *, unsigned long);
typedef int (*AlsaDrain)(void *);
typedef int (*AlsaClose)(void *);

static void *pulse, *alsa;
static PaNew pa_new;
static PaWrite pa_write;
static PaDrain pa_drain;
static PaFree pa_free;
static AlsaOpen alsa_open;
static AlsaSetParams alsa_set_params;
static AlsaWrite alsa_write;
static AlsaDrain alsa_drain;
static AlsaClose alsa_close;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int playing, loaded;

static void load_libraries(void)
{
    pulse = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
    if (pulse) {
        pa_new = (PaNew)dlsym(pulse, "pa_simple_new");
        pa_write = (PaWrite)dlsym(pulse, "pa_simple_write");
        pa_drain = (PaDrain)dlsym(pulse, "pa_simple_drain");
        pa_free = (PaFree)dlsym(pulse, "pa_simple_free");
        if (!pa_new || !pa_write || !pa_drain || !pa_free)
            pulse = NULL;
    }
    alsa = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (alsa) {
        alsa_open = (AlsaOpen)dlsym(alsa, "snd_pcm_open");
        alsa_set_params = (AlsaSetParams)dlsym(alsa, "snd_pcm_set_params");
        alsa_write = (AlsaWrite)dlsym(alsa, "snd_pcm_writei");
        alsa_drain = (AlsaDrain)dlsym(alsa, "snd_pcm_drain");
        alsa_close = (AlsaClose)dlsym(alsa, "snd_pcm_close");
        if (!alsa_open || !alsa_set_params || !alsa_write || !alsa_drain || !alsa_close)
            alsa = NULL;
    }
}

static int play_pulse(void)
{
    PaSpec spec = {PA_SAMPLE_S16LE, RATE, 1};
    int err = 0, ok;
    void *s = pa_new(NULL, "Amber Writer", PA_STREAM_PLAYBACK, NULL, "Typo beep", &spec, NULL, NULL, &err);
    if (!s)
        return 0;
    ok = pa_write(s, samples, sample_count * sizeof(short), &err) == 0;
    pa_drain(s, &err);
    pa_free(s);
    return ok;
}

static int play_alsa(void)
{
    void *pcm = NULL;
    int ok;
    if (alsa_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        return 0;
    ok = alsa_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1, 200000) >= 0 &&
         alsa_write(pcm, samples, sample_count) > 0;
    if (ok)
        alsa_drain(pcm);
    alsa_close(pcm);
    return ok;
}

static void *play_thread(void *arg)
{
    (void)arg;
    if (!(pulse && play_pulse()) && alsa)
        play_alsa();
    pthread_mutex_lock(&lock);
    playing = 0;
    pthread_mutex_unlock(&lock);
    return NULL;
}

int beep_play_sound(void)
{
    pthread_t t;
    pthread_attr_t attr;
    int started;
    pthread_mutex_lock(&lock);
    if (!loaded) {
        load_libraries();
        ensure_samples();
        loaded = 1;
    }
    if ((!pulse && !alsa) || !samples) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    if (playing) { /* one beep at a time, as with a real buzzer */
        pthread_mutex_unlock(&lock);
        return 1;
    }
    playing = 1;
    pthread_mutex_unlock(&lock);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    started = pthread_create(&t, &attr, play_thread, NULL) == 0;
    pthread_attr_destroy(&attr);
    if (!started) {
        pthread_mutex_lock(&lock);
        playing = 0;
        pthread_mutex_unlock(&lock);
    }
    return started;
}
#endif
#endif
