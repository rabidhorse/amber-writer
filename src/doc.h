/* Reading and writing documents. No windows here, so it is easy to test.
 *
 * A document is UTF-16 text with paragraphs ended by '\r', one style byte per
 * character made of the ST_ flags below, and an AL_ alignment for each paragraph.
 */
#ifndef AMBER_DOC_H
#define AMBER_DOC_H

#include "port.h"

#define ST_BOLD 1
#define ST_ITALIC 2
#define ST_UNDERLINE 4
#define ST_STRIKE 8

#define AL_LEFT 0
#define AL_CENTER 1
#define AL_RIGHT 2
#define AL_JUSTIFY 3 /* kept from other files; Amber Writer has no key for it */

typedef struct {
    WCHAR *text;
    BYTE *style;
    int len, cap;
    BYTE *align; /* by paragraph; paragraphs past the end are left aligned */
    int paras;
} Doc;

void doc_init(Doc *d);
void doc_free(Doc *d);
BOOL doc_append(Doc *d, const WCHAR *text, int len, BYTE style);
void doc_set_align(Doc *d, int para, BYTE align);
BYTE doc_align(const Doc *d, int para);

/* Laying out fixed-pitch lines, for printing */
int char_columns(WCHAR c);
void wrap_line(const WCHAR *text, int pos, int end, int columns, int tab, int *line_end, int *width, int *next);

BOOL save_rtf(const WCHAR *path, const Doc *d);
BOOL save_docx(const WCHAR *path, const Doc *d);
BOOL save_txt(const WCHAR *path, const Doc *d);
BOOL load_docx(const WCHAR *path, Doc *d);
BOOL load_txt(const WCHAR *path, Doc *d);

/* Text and styles from RTF, as written by the RichEdit control. */
BOOL rtf_to_doc(const BYTE *rtf, DWORD size, Doc *d);

/* Whole file in memory. Free with HeapFree(GetProcessHeap(), 0, p). */
BYTE *read_file(const WCHAR *path, DWORD *size);

/* Exposed for tests. */
BOOL docx_from_zip(const BYTE *zip, DWORD size, Doc *d);
BYTE *make_docx(const Doc *d, DWORD *size);
BYTE *make_rtf(const Doc *d, DWORD *size);
int inflate_raw(const BYTE *src, DWORD srclen, BYTE *dst, DWORD dstlen);

#endif
