/* English spell checking with a personal word list.
 *
 * The built-in list is decoded once into one sorted array. The personal list is a
 * small sorted array beside it. Suggestions work like pyspellchecker: words one edit
 * away, else two edits away, most common first.
 */
#include "spell.h"

typedef struct {
    const char *word;
    unsigned char freq;
} Entry;

static Entry *entries;
static int entry_count;
static char *pool;

static char **personal;
static int personal_count, personal_cap;
static WCHAR personal_file[MAX_PATH];

BOOL is_word_char(WCHAR c)
{
    if (c < 128)
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    /* Accented letters belong to the word (café), which is then not checked. */
    return IsCharAlphaW(c);
}

/* Find the next word at or after from. Returns 1 and sets start/end, or 0. */
int next_word(const WCHAR *text, int len, int from, int *start, int *end)
{
    int i = from;
    while (i < len && !is_word_char(text[i]))
        i++;
    if (i >= len)
        return 0;
    *start = i;
    for (;;) {
        while (i < len && is_word_char(text[i]))
            i++;
        if (i + 1 < len && text[i] == '\'' && is_word_char(text[i + 1])) {
            i++;
            continue;
        }
        break;
    }
    *end = i;
    return 1;
}

static int byte_cmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int find_entry(const char *w)
{
    int lo = 0, hi = entry_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = byte_cmp(entries[mid].word, w);
        if (c == 0)
            return mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

static int find_personal(const char *w, int *insert_at)
{
    int lo = 0, hi = personal_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = byte_cmp(personal[mid], w);
        if (c == 0)
            return mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    if (insert_at)
        *insert_at = lo;
    return -1;
}

static void personal_insert(const char *w)
{
    int at, n = lstrlenA(w);
    char *copy;
    if (n == 0 || find_personal(w, &at) >= 0)
        return;
    if (personal_count == personal_cap) {
        int cap = personal_cap ? personal_cap * 2 : 64;
        char **grown = HeapAlloc(GetProcessHeap(), 0, cap * sizeof(char *));
        if (!grown)
            return;
        if (personal) {
            CopyMemory(grown, personal, personal_count * sizeof(char *));
            HeapFree(GetProcessHeap(), 0, personal);
        }
        personal = grown;
        personal_cap = cap;
    }
    copy = HeapAlloc(GetProcessHeap(), 0, n + 1);
    if (!copy)
        return;
    CopyMemory(copy, w, n + 1);
    MoveMemory(personal + at + 1, personal + at, (personal_count - at) * sizeof(char *));
    personal[at] = copy;
    personal_count++;
}

static void load_personal(void)
{
    HANDLE f = CreateFileW(personal_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, got;
    char *buf, word[MAX_WORD];
    int n = 0;
    DWORD i;
    if (f == INVALID_HANDLE_VALUE)
        return;
    size = GetFileSize(f, NULL);
    buf = HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (buf && ReadFile(f, buf, size, &got, NULL)) {
        for (i = 0; i <= got; i++) {
            char c = i < got ? buf[i] : '\n';
            if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
                if (n) {
                    word[n] = 0;
                    personal_insert(word);
                }
                n = 0;
            } else if (n < MAX_WORD - 1) {
                word[n++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
            }
        }
    }
    if (buf)
        HeapFree(GetProcessHeap(), 0, buf);
    CloseHandle(f);
}

void spell_init(const WCHAR *personal_path)
{
    const unsigned char *p = dict_data, *end = dict_data + dict_data_size;
    char *out;
    const char *prev = "";
    int i;
    if (entries)
        HeapFree(GetProcessHeap(), 0, entries);
    if (pool)
        HeapFree(GetProcessHeap(), 0, pool);
    for (i = 0; i < personal_count; i++)
        HeapFree(GetProcessHeap(), 0, personal[i]);
    entry_count = personal_count = 0;
    personal_file[0] = 0;
    entries = HeapAlloc(GetProcessHeap(), 0, dict_word_count * sizeof(Entry));
    /* Every word is at most its stored suffix plus 63 shared bytes. */
    pool = HeapAlloc(GetProcessHeap(), 0, dict_data_size + (SIZE_T)dict_word_count * 64);
    if (!entries || !pool)
        return;
    out = pool;
    while (p < end && entry_count < dict_word_count) {
        int shared = *p++;
        unsigned char freq = *p++;
        char *start = out;
        CopyMemory(out, prev, shared);
        out += shared;
        while (p < end && *p)
            *out++ = (char)*p++;
        *out++ = 0;
        p++;
        entries[entry_count].word = start;
        entries[entry_count].freq = freq;
        entry_count++;
        prev = start;
    }
    if (personal_path) {
        lstrcpynW(personal_file, personal_path, MAX_PATH);
        load_personal();
    }
}

/* Lower-case ASCII copy of a word. Returns 0 if it does not fit or is not ASCII. */
static int to_key(const WCHAR *word, int len, char *key)
{
    int i;
    if (len <= 0 || len >= MAX_WORD)
        return 0;
    for (i = 0; i < len; i++) {
        WCHAR c = word[i];
        if (c > 127)
            return 0;
        key[i] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    key[len] = 0;
    return 1;
}

static BOOL known(const char *key)
{
    return find_entry(key) >= 0 || find_personal(key, NULL) >= 0;
}

BOOL spell_is_correct(const WCHAR *word, int len)
{
    char key[MAX_WORD];
    int i, upper = 0, letters = 0;
    /* Skip one-letter words, acronyms (NASA) and mixed-case names (iPhone, WordPerfect). */
    if (len < 2)
        return TRUE;
    for (i = 0; i < len; i++) {
        if (word[i] >= 'A' && word[i] <= 'Z')
            upper++;
        if (is_word_char(word[i]))
            letters++;
    }
    if (upper == letters)
        return TRUE;
    for (i = 1; i < len; i++)
        if (word[i] >= 'A' && word[i] <= 'Z')
            return TRUE;
    if (!to_key(word, len, key))
        return TRUE;
    return known(key);
}

/* Suggestions */

typedef struct {
    char word[MAX_WORD];
    int freq;
} Candidate;

typedef struct {
    Candidate *items;
    int count, cap;
} CandidateSet;

static void add_candidate(CandidateSet *set, const char *w)
{
    int i, idx = find_entry(w), freq;
    if (idx < 0)
        return; /* only offer words from the real dictionary */
    freq = entries[idx].freq;
    for (i = 0; i < set->count; i++)
        if (byte_cmp(set->items[i].word, w) == 0)
            return;
    if (set->count == set->cap) {
        int cap = set->cap ? set->cap * 2 : 32;
        Candidate *grown = HeapAlloc(GetProcessHeap(), 0, cap * sizeof(Candidate));
        if (!grown)
            return;
        if (set->items) {
            CopyMemory(grown, set->items, set->count * sizeof(Candidate));
            HeapFree(GetProcessHeap(), 0, set->items);
        }
        set->items = grown;
        set->cap = cap;
    }
    lstrcpynA(set->items[set->count].word, w, MAX_WORD);
    set->items[set->count].freq = freq;
    set->count++;
}

static const char LETTERS[] = "abcdefghijklmnopqrstuvwxyz'";

/* Calls visit(word) for every string one edit away from w. */
typedef void (*Visit)(const char *w, void *ctx);

static void edits1(const char *w, Visit visit, void *ctx)
{
    char buf[MAX_WORD + 2];
    int n = lstrlenA(w), i, j;
    const char *c;
    for (i = 0; i < n; i++) { /* deletes */
        for (j = 0; j < i; j++)
            buf[j] = w[j];
        for (j = i + 1; j <= n; j++)
            buf[j - 1] = w[j];
        visit(buf, ctx);
    }
    for (i = 0; i + 1 < n; i++) { /* transposes */
        lstrcpyA(buf, w);
        buf[i] = w[i + 1];
        buf[i + 1] = w[i];
        visit(buf, ctx);
    }
    for (i = 0; i < n; i++) { /* replaces */
        for (c = LETTERS; *c; c++) {
            if (*c == w[i])
                continue;
            lstrcpyA(buf, w);
            buf[i] = *c;
            visit(buf, ctx);
        }
    }
    if (n + 1 < MAX_WORD) { /* inserts */
        for (i = 0; i <= n; i++) {
            for (c = LETTERS; *c; c++) {
                for (j = 0; j < i; j++)
                    buf[j] = w[j];
                buf[i] = *c;
                for (j = i; j <= n; j++)
                    buf[j + 1] = w[j];
                visit(buf, ctx);
            }
        }
    }
}

static void visit_known(const char *w, void *ctx)
{
    add_candidate((CandidateSet *)ctx, w);
}

static void visit_two(const char *w, void *ctx)
{
    edits1(w, visit_known, ctx);
}

int spell_suggest(const WCHAR *word, int len, char out[][MAX_WORD], int limit)
{
    char key[MAX_WORD];
    CandidateSet set = {0};
    int i, j, n;
    if (!to_key(word, len, key))
        return 0;
    edits1(key, visit_known, &set);
    if (set.count == 0 && len <= 20)
        edits1(key, visit_two, &set);
    /* Most common first; ties alphabetical. */
    for (i = 1; i < set.count; i++) {
        Candidate c = set.items[i];
        for (j = i; j > 0 && (set.items[j - 1].freq < c.freq ||
                              (set.items[j - 1].freq == c.freq && byte_cmp(set.items[j - 1].word, c.word) > 0)); j--)
            set.items[j] = set.items[j - 1];
        set.items[j] = c;
    }
    n = set.count < limit ? set.count : limit;
    for (i = 0; i < n; i++)
        lstrcpynA(out[i], set.items[i].word, MAX_WORD);
    if (set.items)
        HeapFree(GetProcessHeap(), 0, set.items);
    return n;
}

void spell_add(const WCHAR *word, int len)
{
    char key[MAX_WORD], line[MAX_WORD + 2];
    HANDLE f;
    DWORD wrote;
    WCHAR dir[MAX_PATH], *slash;
    int n;
    if (!to_key(word, len, key) || find_personal(key, NULL) >= 0)
        return;
    personal_insert(key);
    if (!personal_file[0])
        return;
    lstrcpynW(dir, personal_file, MAX_PATH);
    slash = dir + lstrlenW(dir);
    while (slash > dir && *slash != '\\' && *slash != '/')
        slash--;
    *slash = 0;
    CreateDirectoryW(dir, NULL);
    f = CreateFileW(personal_file, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    n = lstrlenA(key);
    CopyMemory(line, key, n);
    line[n++] = '\r';
    line[n++] = '\n';
    WriteFile(f, line, n, &wrote, NULL);
    CloseHandle(f);
}
