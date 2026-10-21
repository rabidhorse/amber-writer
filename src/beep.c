/* The short typewriter beep, made as a WAV in memory and played with PlaySound. */
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include "beep.h"

#define RATE 22050

static BYTE *wav;

static void put32(BYTE *p, DWORD v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
    p[2] = (BYTE)(v >> 16);
    p[3] = (BYTE)(v >> 24);
}

static void put16(BYTE *p, WORD v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
}

/* A sine plus a little third harmonic sounds like the small piezo buzzer in an
   electronic typewriter. */
static void make_wav(double freq, double seconds, double volume)
{
    int n = (int)(RATE * seconds), fade = (int)(RATE * 0.004), i;
    DWORD data = n * 2;
    BYTE *p;
    wav = HeapAlloc(GetProcessHeap(), 0, 44 + data);
    if (!wav)
        return;
    CopyMemory(wav, "RIFF", 4);
    put32(wav + 4, 36 + data);
    CopyMemory(wav + 8, "WAVEfmt ", 8);
    put32(wav + 16, 16);
    put16(wav + 20, 1);
    put16(wav + 22, 1);
    put32(wav + 24, RATE);
    put32(wav + 28, RATE * 2);
    put16(wav + 32, 2);
    put16(wav + 34, 16);
    CopyMemory(wav + 36, "data", 4);
    put32(wav + 40, data);
    p = wav + 44;
    for (i = 0; i < n; i++) {
        double t = (double)i / RATE;
        double s = sin(2 * 3.14159265358979 * freq * t) + 0.3 * sin(2 * 3.14159265358979 * 3 * freq * t);
        double env = 1.0;
        if (i < fade)
            env = (double)i / fade;
        if (n - i < fade)
            env = (double)(n - i) / fade;
        put16(p + 2 * i, (WORD)(short)(s / 1.3 * env * volume * 32767));
    }
}

void beep_play(void)
{
    if (!wav)
        make_wav(1760.0, 0.09, 0.35);
    if (!wav || !PlaySoundA((LPCSTR)wav, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT))
        MessageBeep(MB_OK);
}
