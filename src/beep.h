#ifndef AMBER_BEEP_H
#define AMBER_BEEP_H
#ifdef _WIN32
void beep_play(void);
#else
/* Starts the beep and returns 1, or returns 0 when there is no way to play it. */
int beep_play_sound(void);
#endif
#endif
