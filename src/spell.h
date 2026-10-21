/* English spell checking with a personal word list. */
#ifndef AMBER_SPELL_H
#define AMBER_SPELL_H

#include "port.h"

extern const int dict_word_count;
extern const unsigned int dict_data_size;
extern const unsigned char dict_data[];

#define MAX_WORD 64

void spell_init(const WCHAR *personal_path);
BOOL spell_is_correct(const WCHAR *word, int len);
int spell_suggest(const WCHAR *word, int len, char out[][MAX_WORD], int limit);
void spell_add(const WCHAR *word, int len);

/* Words are letters with inner apostrophes: don't, o'clock. */
BOOL is_word_char(WCHAR c);
int next_word(const WCHAR *text, int len, int from, int *start, int *end);

#endif
