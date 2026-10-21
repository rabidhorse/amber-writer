/* Stand-ins for the Windows calls in port.h, for macOS and Linux. */
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "port.h"

/* Each block keeps its size in front, so HeapReAlloc can zero what it adds. */
#define HEADER 16

void *HeapAlloc(HANDLE heap, DWORD flags, SIZE_T n)
{
    char *p = (flags & HEAP_ZERO_MEMORY) ? calloc(1, n + HEADER) : malloc(n + HEADER);
    (void)heap;
    if (!p) {
        errno = ENOMEM;
        return NULL;
    }
    *(SIZE_T *)p = n;
    return p + HEADER;
}

void *HeapReAlloc(HANDLE heap, DWORD flags, void *mem, SIZE_T n)
{
    char *p = (char *)mem - HEADER;
    SIZE_T old = *(SIZE_T *)p;
    (void)heap;
    p = realloc(p, n + HEADER);
    if (!p) {
        errno = ENOMEM;
        return NULL;
    }
    if ((flags & HEAP_ZERO_MEMORY) && n > old)
        memset(p + HEADER + old, 0, n - old);
    *(SIZE_T *)p = n;
    return p + HEADER;
}

BOOL HeapFree(HANDLE heap, DWORD flags, void *mem)
{
    (void)heap;
    (void)flags;
    if (mem)
        free((char *)mem - HEADER);
    return TRUE;
}

DWORD GetLastError(void)
{
    return (DWORD)errno;
}

void SetLastError(DWORD err)
{
    errno = (int)err;
}

/* Strings */

int lstrlenW(const WCHAR *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

WCHAR *lstrcpyW(WCHAR *d, const WCHAR *s)
{
    int i = 0;
    do
        d[i] = s[i];
    while (s[i++]);
    return d;
}

WCHAR *lstrcpynW(WCHAR *d, const WCHAR *s, int n)
{
    int i;
    if (n <= 0)
        return d;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
    return d;
}

WCHAR *lstrcatW(WCHAR *d, const WCHAR *s)
{
    lstrcpyW(d + lstrlenW(d), s);
    return d;
}

char *lstrcpynA(char *d, const char *s, int n)
{
    int i;
    if (n <= 0)
        return d;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
    return d;
}

/* The Windows-1252 characters in 0x80-0x9F; the rest of the code page is Latin-1. */
static const WCHAR CP1252[32] = {
    0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178,
};

static int put_w(WCHAR *out, int size, int at, WCHAR c)
{
    if (out && at < size)
        out[at] = c;
    return at + 1;
}

/* UTF-8 or Windows-1252 (which also stands in for any other code page) to UTF-16.
   With out NULL, returns the length needed. */
int MultiByteToWideChar(UINT cp, DWORD flags, const char *str, int n, WCHAR *out, int size)
{
    const unsigned char *s = (const unsigned char *)str;
    int i = 0, at = 0;
    (void)flags;
    if (n < 0)
        n = (int)strlen(str) + 1;
    if (cp != CP_UTF8) {
        for (i = 0; i < n; i++)
            at = put_w(out, size, at, s[i] >= 0x80 && s[i] < 0xA0 ? CP1252[s[i] - 0x80] : s[i]);
        return at;
    }
    while (i < n) {
        unsigned long c = s[i];
        int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        int k;
        if (extra < 0 || i + extra >= n + (extra ? 0 : 1)) {
            at = put_w(out, size, at, 0xFFFD);
            i++;
            continue;
        }
        c &= extra == 0 ? 0x7F : extra == 1 ? 0x1F : extra == 2 ? 0x0F : 0x07;
        for (k = 1; k <= extra; k++) {
            if ((s[i + k] & 0xC0) != 0x80)
                break;
            c = (c << 6) | (s[i + k] & 0x3F);
        }
        if (k <= extra) {
            at = put_w(out, size, at, 0xFFFD);
            i += k;
            continue;
        }
        i += extra + 1;
        if (c >= 0x10000) {
            c -= 0x10000;
            at = put_w(out, size, at, (WCHAR)(0xD800 + (c >> 10)));
            at = put_w(out, size, at, (WCHAR)(0xDC00 + (c & 0x3FF)));
        } else {
            at = put_w(out, size, at, (WCHAR)c);
        }
    }
    return at;
}

static int fold(int c, DWORD flags)
{
    return (flags & NORM_IGNORECASE) && c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int CompareStringA(DWORD locale, DWORD flags, const char *a, int an, const char *b, int bn)
{
    int i;
    (void)locale;
    if (an < 0)
        an = (int)strlen(a);
    if (bn < 0)
        bn = (int)strlen(b);
    for (i = 0; i < an && i < bn; i++) {
        int x = fold((unsigned char)a[i], flags), y = fold((unsigned char)b[i], flags);
        if (x != y)
            return x < y ? 1 : 3;
    }
    return an == bn ? CSTR_EQUAL : an < bn ? 1 : 3;
}

int CompareStringW(DWORD locale, DWORD flags, const WCHAR *a, int an, const WCHAR *b, int bn)
{
    int i;
    (void)locale;
    if (an < 0)
        an = lstrlenW(a);
    if (bn < 0)
        bn = lstrlenW(b);
    for (i = 0; i < an && i < bn; i++) {
        int x = fold(a[i], flags), y = fold(b[i], flags);
        if (x != y)
            return x < y ? 1 : 3;
    }
    return an == bn ? CSTR_EQUAL : an < bn ? 1 : 3;
}

/* Letters in the Latin, Greek and Cyrillic blocks, which is all a word's accented
   letters need. */
BOOL IsCharAlphaW(WCHAR c)
{
    if (c < 128)
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (c == 0xAA || c == 0xB5 || c == 0xBA)
        return TRUE;
    if (c >= 0xC0 && c <= 0x24F)
        return c != 0xD7 && c != 0xF7;
    if (c >= 0x370 && c <= 0x3FF)
        return c >= 0x386 && c != 0x387 && c != 0x3F6;
    if (c >= 0x400 && c <= 0x52F)
        return c < 0x482 || c > 0x489;
    if (c >= 0x1E00 && c <= 0x1FFF)
        return TRUE;
    return FALSE;
}

/* Files */

void path_to_utf8(const WCHAR *w, char *out, int size)
{
    int at = 0, i;
    for (i = 0; w[i] && at < size - 4; i++) {
        unsigned long c = w[i];
        if (c >= 0xD800 && c < 0xDC00 && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00);
            i++;
        }
        if (c < 0x80) {
            out[at++] = (char)c;
        } else if (c < 0x800) {
            out[at++] = (char)(0xC0 | (c >> 6));
            out[at++] = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out[at++] = (char)(0xE0 | (c >> 12));
            out[at++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[at++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[at++] = (char)(0xF0 | (c >> 18));
            out[at++] = (char)(0x80 | ((c >> 12) & 0x3F));
            out[at++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[at++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[at] = 0;
}

#define PATH_BYTES (4 * MAX_PATH)

/* A handle is the file descriptor plus one, so that 0 can mean failure. */
HANDLE CreateFileW(const WCHAR *path, DWORD access, DWORD share, void *security, DWORD how, DWORD attrs, HANDLE tmpl)
{
    char p[PATH_BYTES];
    int flags, fd;
    (void)share;
    (void)security;
    (void)attrs;
    (void)tmpl;
    path_to_utf8(path, p, sizeof p);
    if (access & FILE_APPEND_DATA)
        flags = O_WRONLY | O_APPEND;
    else if (access & GENERIC_WRITE)
        flags = O_WRONLY;
    else
        flags = O_RDONLY;
    if (how == CREATE_ALWAYS)
        flags |= O_CREAT | O_TRUNC;
    else if (how == OPEN_ALWAYS)
        flags |= O_CREAT;
    fd = open(p, flags | O_CLOEXEC, 0644);
    if (fd >= 0 && !(access & (GENERIC_WRITE | FILE_APPEND_DATA))) {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
            close(fd);
            errno = EISDIR;
            fd = -1;
        }
    }
    return fd < 0 ? INVALID_HANDLE_VALUE : (HANDLE)(intptr_t)(fd + 1);
}

static int fd_of(HANDLE f)
{
    return (int)(intptr_t)f - 1;
}

BOOL ReadFile(HANDLE f, void *buf, DWORD n, DWORD *got, void *overlapped)
{
    DWORD done = 0;
    (void)overlapped;
    while (done < n) {
        ssize_t r = read(fd_of(f), (char *)buf + done, n - done);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0) {
            *got = done;
            return FALSE;
        }
        if (r == 0)
            break;
        done += (DWORD)r;
    }
    *got = done;
    return TRUE;
}

BOOL WriteFile(HANDLE f, const void *buf, DWORD n, DWORD *wrote, void *overlapped)
{
    DWORD done = 0;
    (void)overlapped;
    while (done < n) {
        ssize_t r = write(fd_of(f), (const char *)buf + done, n - done);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            *wrote = done;
            return FALSE;
        }
        done += (DWORD)r;
    }
    *wrote = done;
    return TRUE;
}

DWORD GetFileSize(HANDLE f, DWORD *high)
{
    struct stat st;
    if (high)
        *high = 0;
    if (fstat(fd_of(f), &st) != 0 || st.st_size >= 0xFFFFFFFFLL)
        return INVALID_FILE_SIZE;
    return (DWORD)st.st_size;
}

BOOL CloseHandle(HANDLE f)
{
    int fd = fd_of(f);
    BOOL ok = fsync(fd) == 0 || errno == EINVAL || errno == EBADF || errno == EROFS;
    return close(fd) == 0 && ok;
}

BOOL MoveFileExW(const WCHAR *from, const WCHAR *to, DWORD flags)
{
    char a[PATH_BYTES], b[PATH_BYTES];
    (void)flags;
    path_to_utf8(from, a, sizeof a);
    path_to_utf8(to, b, sizeof b);
    return rename(a, b) == 0;
}

BOOL DeleteFileW(const WCHAR *path)
{
    char p[PATH_BYTES];
    path_to_utf8(path, p, sizeof p);
    return unlink(p) == 0;
}

BOOL CreateDirectoryW(const WCHAR *path, void *security)
{
    char p[PATH_BYTES];
    (void)security;
    path_to_utf8(path, p, sizeof p);
    return mkdir(p, 0755) == 0;
}
#endif
