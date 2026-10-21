/* The few Windows types and calls that doc.c and spell.c use. On Windows this is just
 * windows.h; on macOS and Linux they are small stand-ins written in port.c.
 *
 * Text is UTF-16 everywhere. Off Windows the C files are built with -fshort-wchar so
 * that L"..." strings are UTF-16 too.
 */
#ifndef AMBER_PORT_H
#define AMBER_PORT_H

#ifdef _WIN32
#include <windows.h>
#else

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef unsigned short WCHAR;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef uint32_t DWORD;
typedef unsigned int UINT;
typedef int BOOL;
typedef size_t SIZE_T;
typedef void *HANDLE;

#define TRUE 1
#define FALSE 0
#define MAX_PATH 1024

#define CP_ACP 0
#define CP_UTF8 65001
#define LOCALE_INVARIANT 0
#define NORM_IGNORECASE 1
#define CSTR_EQUAL 2

#define GENERIC_READ 1
#define GENERIC_WRITE 2
#define FILE_APPEND_DATA 4
#define FILE_SHARE_READ 0
#define FILE_ATTRIBUTE_NORMAL 0
#define OPEN_EXISTING 1
#define CREATE_ALWAYS 2
#define OPEN_ALWAYS 3
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define INVALID_HANDLE_VALUE ((HANDLE)0)
#define INVALID_FILE_SIZE 0xFFFFFFFFu

/* Error codes are errno values. */
#define ERROR_NOT_ENOUGH_MEMORY 12 /* ENOMEM */
#define ERROR_INVALID_DATA 22      /* EINVAL */

#define HEAP_ZERO_MEMORY 8
#define GetProcessHeap() ((HANDLE)0)
void *HeapAlloc(HANDLE heap, DWORD flags, SIZE_T n);
void *HeapReAlloc(HANDLE heap, DWORD flags, void *p, SIZE_T n);
BOOL HeapFree(HANDLE heap, DWORD flags, void *p);
#define CopyMemory(d, s, n) memcpy((d), (s), (n))
#define MoveMemory(d, s, n) memmove((d), (s), (n))
#define ZeroMemory(d, n) memset((d), 0, (n))

DWORD GetLastError(void);
void SetLastError(DWORD err);

HANDLE CreateFileW(const WCHAR *path, DWORD access, DWORD share, void *security, DWORD how, DWORD attrs, HANDLE tmpl);
BOOL ReadFile(HANDLE f, void *buf, DWORD n, DWORD *got, void *overlapped);
BOOL WriteFile(HANDLE f, const void *buf, DWORD n, DWORD *wrote, void *overlapped);
DWORD GetFileSize(HANDLE f, DWORD *high);
BOOL CloseHandle(HANDLE f);
BOOL MoveFileExW(const WCHAR *from, const WCHAR *to, DWORD flags);
BOOL DeleteFileW(const WCHAR *path);
BOOL CreateDirectoryW(const WCHAR *path, void *security);

int lstrlenW(const WCHAR *s);
WCHAR *lstrcpyW(WCHAR *d, const WCHAR *s);
WCHAR *lstrcpynW(WCHAR *d, const WCHAR *s, int n);
WCHAR *lstrcatW(WCHAR *d, const WCHAR *s);
#define lstrlenA(s) ((int)strlen(s))
#define lstrcpyA(d, s) strcpy((d), (s))
#define lstrcmpA(a, b) strcmp((a), (b))
char *lstrcpynA(char *d, const char *s, int n);
#define wsprintfA sprintf

int MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int n, WCHAR *out, int size);
int CompareStringA(DWORD locale, DWORD flags, const char *a, int an, const char *b, int bn);
int CompareStringW(DWORD locale, DWORD flags, const WCHAR *a, int an, const WCHAR *b, int bn);
BOOL IsCharAlphaW(WCHAR c);

/* UTF-16 file names to and from UTF-8. utf8 needs room for 4 * MAX_PATH bytes. */
void path_to_utf8(const WCHAR *path, char *utf8, int size);

#include <stdio.h>
#endif

#endif
