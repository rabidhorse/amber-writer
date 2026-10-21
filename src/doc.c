/* Reading and writing documents: RTF (written here, read by the RichEdit control),
 * Word .docx (a small zip reader and writer with its own inflate) and plain text.
 */
#include "doc.h"

#define HEAP GetProcessHeap()

void doc_init(Doc *d)
{
    d->text = NULL;
    d->style = NULL;
    d->len = d->cap = 0;
    d->align = NULL;
    d->paras = 0;
}

void doc_free(Doc *d)
{
    if (d->text)
        HeapFree(HEAP, 0, d->text);
    if (d->style)
        HeapFree(HEAP, 0, d->style);
    if (d->align)
        HeapFree(HEAP, 0, d->align);
    doc_init(d);
}

static BOOL doc_reserve(Doc *d, int need)
{
    WCHAR *t;
    BYTE *s;
    int cap;
    if (need <= d->cap)
        return TRUE;
    cap = d->cap ? d->cap : 256;
    while (cap < need)
        cap *= 2;
    t = d->text ? HeapReAlloc(HEAP, 0, d->text, cap * sizeof(WCHAR)) : HeapAlloc(HEAP, 0, cap * sizeof(WCHAR));
    if (!t)
        return FALSE;
    d->text = t;
    s = d->style ? HeapReAlloc(HEAP, 0, d->style, cap) : HeapAlloc(HEAP, 0, cap);
    if (!s)
        return FALSE;
    d->style = s;
    d->cap = cap;
    return TRUE;
}

BOOL doc_append(Doc *d, const WCHAR *text, int len, BYTE style)
{
    int i;
    if (!doc_reserve(d, d->len + len + 1))
        return FALSE;
    for (i = 0; i < len; i++) {
        d->text[d->len] = text[i];
        d->style[d->len] = style;
        d->len++;
    }
    d->text[d->len] = 0;
    return TRUE;
}

void doc_set_align(Doc *d, int para, BYTE align)
{
    if (para < 0)
        return;
    if (para >= d->paras) {
        int n = para + 16;
        BYTE *a;
        if (align == AL_LEFT)
            return;
        a = d->align ? HeapReAlloc(HEAP, HEAP_ZERO_MEMORY, d->align, n) : HeapAlloc(HEAP, HEAP_ZERO_MEMORY, n);
        if (!a)
            return;
        d->align = a;
        d->paras = n;
    }
    d->align[para] = align;
}

BYTE doc_align(const Doc *d, int para)
{
    return para >= 0 && para < d->paras ? d->align[para] : AL_LEFT;
}

/* Lines of fixed-pitch text */

/* Columns a character takes: none for the second half of a surrogate pair or a
   combining accent. */
int char_columns(WCHAR c)
{
    return (c >= 0xDC00 && c <= 0xDFFF) || (c >= 0x0300 && c <= 0x036F) ? 0 : 1;
}

static int line_width(const WCHAR *text, int pos, int end, int tab)
{
    int col = 0, width = 0, i;
    for (i = pos; i < end; i++) {
        if (text[i] == '\t')
            col = (col / tab + 1) * tab;
        else if (text[i] == ' ')
            col++;
        else
            width = col += char_columns(text[i]);
    }
    return width;
}

/* The line that starts at pos, wrapped like the page: it breaks after the last space or
   tab that fits in the columns, and spaces at the end may run past them. A word
   longer than a line is cut. The line's text ends at *line_end (before any paragraph
   mark or line break), *width is its width in columns without the spaces at the end,
   and the next line starts at *next. */
void wrap_line(const WCHAR *text, int pos, int end, int columns, int tab, int *line_end, int *width, int *next)
{
    int i, col = 0, brk = -1;
    for (i = pos; i < end; i++) {
        WCHAR c = text[i];
        if (c == '\r' || c == '\n' || c == 0x0B) {
            *line_end = i;
            *next = i + 1;
            *width = line_width(text, pos, i, tab);
            return;
        }
        if (c == ' ') {
            col++;
            brk = i + 1;
        } else if (c == '\t') {
            col = (col / tab + 1) * tab;
            brk = i + 1;
        } else if (col + char_columns(c) > columns && i > pos) {
            *line_end = *next = brk > pos ? brk : i;
            *width = line_width(text, pos, *line_end, tab);
            return;
        } else {
            col += char_columns(c);
        }
    }
    *line_end = *next = end;
    *width = line_width(text, pos, end, tab);
}

/* A growing byte buffer */

typedef struct {
    BYTE *p;
    DWORD len, cap;
    BOOL failed;
} Buf;

static void buf_add(Buf *b, const void *data, DWORD n)
{
    if (b->failed)
        return;
    if (b->len + n > b->cap) {
        DWORD cap = b->cap ? b->cap : 4096;
        BYTE *p;
        while (cap < b->len + n)
            cap *= 2;
        p = b->p ? HeapReAlloc(HEAP, 0, b->p, cap) : HeapAlloc(HEAP, 0, cap);
        if (!p) {
            b->failed = TRUE;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    CopyMemory(b->p + b->len, data, n);
    b->len += n;
}

static void buf_str(Buf *b, const char *s)
{
    buf_add(b, s, lstrlenA(s));
}

static void buf_byte(Buf *b, BYTE c)
{
    buf_add(b, &c, 1);
}

static void buf_u16(Buf *b, WORD v)
{
    BYTE x[2] = {(BYTE)v, (BYTE)(v >> 8)};
    buf_add(b, x, 2);
}

static void buf_u32(Buf *b, DWORD v)
{
    BYTE x[4] = {(BYTE)v, (BYTE)(v >> 8), (BYTE)(v >> 16), (BYTE)(v >> 24)};
    buf_add(b, x, 4);
}

static BYTE *buf_done(Buf *b, DWORD *size)
{
    if (b->failed) {
        if (b->p)
            HeapFree(HEAP, 0, b->p);
        return NULL;
    }
    if (!b->p)
        b->p = HeapAlloc(HEAP, 0, 1);
    *size = b->len;
    return b->p;
}

/* UTF-16 to UTF-8, one code unit pair at a time. */
static void buf_utf8(Buf *b, const WCHAR *s, int n, int *i)
{
    DWORD c = s[*i];
    if (c >= 0xD800 && c < 0xDC00 && *i + 1 < n && s[*i + 1] >= 0xDC00 && s[*i + 1] < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (s[*i + 1] - 0xDC00);
        (*i)++;
    }
    if (c < 0x80) {
        buf_byte(b, (BYTE)c);
    } else if (c < 0x800) {
        buf_byte(b, (BYTE)(0xC0 | (c >> 6)));
        buf_byte(b, (BYTE)(0x80 | (c & 63)));
    } else if (c < 0x10000) {
        buf_byte(b, (BYTE)(0xE0 | (c >> 12)));
        buf_byte(b, (BYTE)(0x80 | ((c >> 6) & 63)));
        buf_byte(b, (BYTE)(0x80 | (c & 63)));
    } else {
        buf_byte(b, (BYTE)(0xF0 | (c >> 18)));
        buf_byte(b, (BYTE)(0x80 | ((c >> 12) & 63)));
        buf_byte(b, (BYTE)(0x80 | ((c >> 6) & 63)));
        buf_byte(b, (BYTE)(0x80 | (c & 63)));
    }
}

/* Files */

BYTE *read_file(const WCHAR *path, DWORD *size)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD got = 0;
    BYTE *p;
    if (f == INVALID_HANDLE_VALUE)
        return NULL;
    *size = GetFileSize(f, NULL);
    p = (*size == INVALID_FILE_SIZE) ? NULL : HeapAlloc(HEAP, 0, *size + 2);
    if (p && (!ReadFile(f, p, *size, &got, NULL) || got != *size)) {
        HeapFree(HEAP, 0, p);
        p = NULL;
    }
    CloseHandle(f);
    if (p)
        p[*size] = p[*size + 1] = 0;
    return p;
}

/* Writes to a temporary file first, then swaps it in, so a failed save never
   leaves a half-written document. */
static BOOL write_file(const WCHAR *path, const BYTE *data, DWORD size)
{
    WCHAR tmp[MAX_PATH + 8];
    HANDLE f;
    DWORD wrote = 0;
    BOOL ok;
    if (lstrlenW(path) >= MAX_PATH)
        return FALSE;
    lstrcpyW(tmp, path);
    lstrcatW(tmp, L".tmp~");
    f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    ok = WriteFile(f, data, size, &wrote, NULL) && wrote == size;
    ok = CloseHandle(f) && ok;
    if (ok)
        ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        DWORD err = GetLastError();
        DeleteFileW(tmp);
        SetLastError(err);
    }
    return ok;
}

static BOOL write_buf(const WCHAR *path, BYTE *data, DWORD size)
{
    BOOL ok;
    if (!data) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    ok = write_file(path, data, size);
    HeapFree(HEAP, 0, data);
    return ok;
}

/* RTF */

static void rtf_style(Buf *b, BYTE from, BYTE to)
{
    BYTE diff = from ^ to;
    if (!diff)
        return;
    if (diff & ST_BOLD)
        buf_str(b, to & ST_BOLD ? "\\b" : "\\b0");
    if (diff & ST_ITALIC)
        buf_str(b, to & ST_ITALIC ? "\\i" : "\\i0");
    if (diff & ST_UNDERLINE)
        buf_str(b, to & ST_UNDERLINE ? "\\ul" : "\\ulnone");
    if (diff & ST_STRIKE)
        buf_str(b, to & ST_STRIKE ? "\\strike" : "\\strike0");
    buf_byte(b, ' ');
}

static const char *RTF_ALIGN[] = {"\\ql ", "\\qc ", "\\qr ", "\\qj "};

BYTE *make_rtf(const Doc *d, DWORD *size)
{
    Buf b = {0};
    BYTE cur = 0, align = AL_LEFT;
    int i, para = 0;
    char num[16];
    buf_str(&b, "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fmodern\\fcharset0 Courier New;}}\r\n"
                "{\\*\\generator Amber Writer;}\\viewkind4\\uc1\\pard\\plain\\f0\\fs24 ");
    for (i = 0; i <= d->len; i++) {
        WCHAR c;
        if (i == 0 || d->text[i - 1] == '\r' || d->text[i - 1] == '\n') {
            /* Alignment lasts until changed, so it is written where it changes. */
            BYTE a = doc_align(d, para++);
            if (a != align && a <= AL_JUSTIFY)
                buf_str(&b, RTF_ALIGN[a]);
            align = a;
        }
        if (i == d->len)
            break;
        c = d->text[i];
        if (c == '\r' || c == '\n') {
            /* Paragraph marks carry no character style, which keeps Word tidy. */
            buf_str(&b, "\\par\r\n");
            continue;
        }
        rtf_style(&b, cur, d->style[i]);
        cur = d->style[i];
        if (c == '\\' || c == '{' || c == '}') {
            buf_byte(&b, '\\');
            buf_byte(&b, (BYTE)c);
        } else if (c == '\t') {
            buf_str(&b, "\\tab ");
        } else if (c >= 32 && c < 127) {
            buf_byte(&b, (BYTE)c);
        } else if (c >= 32) {
            wsprintfA(num, "\\u%d?", (int)(short)c);
            buf_str(&b, num);
        }
    }
    buf_str(&b, "}\r\n");
    return buf_done(&b, size);
}

BOOL save_rtf(const WCHAR *path, const Doc *d)
{
    DWORD size;
    BYTE *data = make_rtf(d, &size);
    return write_buf(path, data, size);
}

/* Plain text */

BOOL save_txt(const WCHAR *path, const Doc *d)
{
    Buf b = {0};
    DWORD size;
    BYTE *data;
    int i, ascii = 1;
    for (i = 0; i < d->len; i++)
        if (d->text[i] > 127)
            ascii = 0;
    /* A byte order mark helps Notepad on XP see UTF-8; plain ASCII needs none. */
    if (!ascii)
        buf_str(&b, "\xEF\xBB\xBF");
    for (i = 0; i < d->len; i++) {
        if (d->text[i] == '\r')
            buf_str(&b, "\r\n");
        else
            buf_utf8(&b, d->text, d->len, &i);
    }
    data = buf_done(&b, &size);
    return write_buf(path, data, size);
}

static BOOL valid_utf8(const BYTE *p, DWORD n)
{
    DWORD i = 0;
    while (i < n) {
        int extra;
        BYTE c = p[i];
        if (c < 0x80)
            extra = 0;
        else if ((c & 0xE0) == 0xC0)
            extra = 1;
        else if ((c & 0xF0) == 0xE0)
            extra = 2;
        else if ((c & 0xF8) == 0xF0)
            extra = 3;
        else
            return FALSE;
        if (i + extra >= n + (extra ? 0 : 1))
            return FALSE;
        i++;
        while (extra--) {
            if ((p[i] & 0xC0) != 0x80)
                return FALSE;
            i++;
        }
    }
    return TRUE;
}

/* Adds text, turning \r\n and lone \n into the '\r' paragraph mark. */
static void append_lines(Doc *d, const WCHAR *w, int n)
{
    int i, start = 0;
    WCHAR cr = '\r';
    for (i = 0; i < n; i++) {
        if (w[i] == '\r' || w[i] == '\n') {
            doc_append(d, w + start, i - start, 0);
            doc_append(d, &cr, 1, 0);
            if (w[i] == '\r' && i + 1 < n && w[i + 1] == '\n')
                i++;
            start = i + 1;
        }
    }
    doc_append(d, w + start, n - start, 0);
}

BOOL load_txt(const WCHAR *path, Doc *d)
{
    DWORD size;
    BYTE *p = read_file(path, &size);
    WCHAR *w;
    int n;
    if (!p)
        return FALSE;
    doc_init(d);
    if (size >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        append_lines(d, (WCHAR *)(p + 2), (size - 2) / 2);
    } else {
        UINT cp = CP_ACP;
        BYTE *s = p;
        if (size >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
            s += 3;
            size -= 3;
            cp = CP_UTF8;
        } else if (valid_utf8(p, size)) {
            cp = CP_UTF8;
        }
        n = size ? MultiByteToWideChar(cp, 0, (char *)s, size, NULL, 0) : 0;
        w = HeapAlloc(HEAP, 0, (n + 1) * sizeof(WCHAR));
        if (w) {
            if (n)
                MultiByteToWideChar(cp, 0, (char *)s, size, w, n);
            append_lines(d, w, n);
            HeapFree(HEAP, 0, w);
        }
    }
    HeapFree(HEAP, 0, p);
    if (!d->text)
        doc_append(d, L"", 0, 0);
    return TRUE;
}

/* Inflate (RFC 1951), after Mark Adler's puff.c */

typedef struct {
    const BYTE *in;
    DWORD inlen, inpos;
    BYTE *out;
    DWORD outlen, outpos;
    DWORD bitbuf;
    int bitcnt;
    int error;
} Inflate;

typedef struct {
    short count[16];
    short symbol[288];
} Huffman;

static int bits(Inflate *s, int need)
{
    DWORD val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) {
            s->error = 1;
            return 0;
        }
        val |= (DWORD)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1UL << need) - 1));
}

static int stored(Inflate *s)
{
    DWORD len;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inpos + 4 > s->inlen)
        return -1;
    len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8);
    if ((s->in[s->inpos + 2] | (s->in[s->inpos + 3] << 8)) != (int)(~len & 0xffff))
        return -1;
    s->inpos += 4;
    if (s->inpos + len > s->inlen || s->outpos + len > s->outlen)
        return -1;
    CopyMemory(s->out + s->outpos, s->in + s->inpos, len);
    s->inpos += len;
    s->outpos += len;
    return 0;
}

static int decode(Inflate *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len < 16; len++) {
        int count;
        code |= bits(s, 1);
        if (s->error)
            return -1;
        count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int construct(Huffman *h, const short *length, int n)
{
    int symbol, len, left;
    short offs[16];
    for (len = 0; len < 16; len++)
        h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++)
        h->count[length[symbol]]++;
    if (h->count[0] == n)
        return 0;
    left = 1;
    for (len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return left;
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++)
        offs[len + 1] = offs[len] + h->count[len];
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0)
            h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}

static const short LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                               3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                4097, 6145, 8193, 12289, 16385, 24577};
static const short DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                               7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int codes(Inflate *s, const Huffman *lencode, const Huffman *distcode)
{
    int symbol;
    do {
        symbol = decode(s, lencode);
        if (symbol < 0)
            return symbol;
        if (symbol < 256) {
            if (s->outpos >= s->outlen)
                return -2;
            s->out[s->outpos++] = (BYTE)symbol;
        } else if (symbol > 256) {
            int len;
            DWORD dist;
            symbol -= 257;
            if (symbol >= 29)
                return -10;
            len = LBASE[symbol] + bits(s, LEXT[symbol]);
            symbol = decode(s, distcode);
            if (symbol < 0 || symbol >= 30)
                return -11;
            dist = DBASE[symbol] + bits(s, DEXT[symbol]);
            if (s->error || dist > s->outpos)
                return -11;
            if (s->outpos + len > s->outlen)
                return -2;
            while (len--) {
                s->out[s->outpos] = s->out[s->outpos - dist];
                s->outpos++;
            }
        }
    } while (symbol != 256);
    return 0;
}

static int fixed(Inflate *s)
{
    static Huffman lencode, distcode;
    static int built;
    if (!built) {
        short lengths[288];
        int symbol;
        for (symbol = 0; symbol < 144; symbol++)
            lengths[symbol] = 8;
        for (; symbol < 256; symbol++)
            lengths[symbol] = 9;
        for (; symbol < 280; symbol++)
            lengths[symbol] = 7;
        for (; symbol < 288; symbol++)
            lengths[symbol] = 8;
        construct(&lencode, lengths, 288);
        for (symbol = 0; symbol < 30; symbol++)
            lengths[symbol] = 5;
        construct(&distcode, lengths, 30);
        built = 1;
    }
    return codes(s, &lencode, &distcode);
}

static int dynamic(Inflate *s)
{
    int nlen, ndist, ncode, index, err;
    short lengths[320];
    Huffman lencode, distcode;
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    nlen = bits(s, 5) + 257;
    ndist = bits(s, 5) + 1;
    ncode = bits(s, 4) + 4;
    if (s->error || nlen > 286 || ndist > 30)
        return -3;
    for (index = 0; index < ncode; index++)
        lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++)
        lengths[order[index]] = 0;
    if (construct(&lencode, lengths, 19) != 0)
        return -4;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode), len;
        if (symbol < 0)
            return symbol;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            len = 0;
            if (symbol == 16) {
                if (index == 0)
                    return -5;
                len = lengths[index - 1];
                symbol = 3 + bits(s, 2);
            } else if (symbol == 17) {
                symbol = 3 + bits(s, 3);
            } else {
                symbol = 11 + bits(s, 7);
            }
            if (index + symbol > nlen + ndist)
                return -6;
            while (symbol--)
                lengths[index++] = (short)len;
        }
        if (s->error)
            return -1;
    }
    if (lengths[256] == 0)
        return -9;
    err = construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1]))
        return -7;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1]))
        return -8;
    return codes(s, &lencode, &distcode);
}

/* Returns the number of bytes written, or -1. */
int inflate_raw(const BYTE *src, DWORD srclen, BYTE *dst, DWORD dstlen)
{
    Inflate s = {0};
    int last, type, err;
    s.in = src;
    s.inlen = srclen;
    s.out = dst;
    s.outlen = dstlen;
    do {
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (s.error)
            return -1;
        if (type == 0)
            err = stored(&s);
        else if (type == 1)
            err = fixed(&s);
        else if (type == 2)
            err = dynamic(&s);
        else
            err = -1;
        if (err != 0 || s.error)
            return -1;
    } while (!last);
    return (int)s.outpos;
}

/* Zip */

static DWORD crc_table[256];

static DWORD crc32(const BYTE *p, DWORD n)
{
    DWORD c = 0xFFFFFFFF, i;
    if (!crc_table[1]) {
        DWORD k, j;
        for (k = 0; k < 256; k++) {
            c = k;
            for (j = 0; j < 8; j++)
                c = (c & 1) ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            crc_table[k] = c;
        }
        c = 0xFFFFFFFF;
    }
    for (i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

static DWORD rd32(const BYTE *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24);
}

static WORD rd16(const BYTE *p)
{
    return (WORD)(p[0] | (p[1] << 8));
}

/* Finds and unpacks one member of a zip. Free the result with HeapFree. */
static BYTE *zip_extract(const BYTE *zip, DWORD size, const char *name, DWORD *outsize)
{
    DWORD eocd, cd, entries, i, namelen = lstrlenA(name);
    if (size < 22)
        return NULL;
    for (eocd = size - 22;; eocd--) {
        if (rd32(zip + eocd) == 0x06054b50)
            break;
        if (eocd == 0 || size - eocd > 22 + 65535)
            return NULL;
    }
    entries = rd16(zip + eocd + 10);
    cd = rd32(zip + eocd + 16);
    for (i = 0; i < entries; i++) {
        DWORD method, csize, usize, nlen, xlen, clen, local, data;
        if (cd + 46 > size || rd32(zip + cd) != 0x02014b50)
            return NULL;
        method = rd16(zip + cd + 10);
        csize = rd32(zip + cd + 20);
        usize = rd32(zip + cd + 24);
        nlen = rd16(zip + cd + 28);
        xlen = rd16(zip + cd + 30);
        clen = rd16(zip + cd + 32);
        local = rd32(zip + cd + 42);
        if (cd + 46 + nlen > size)
            return NULL;
        if (nlen == namelen && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, (char *)zip + cd + 46, nlen, name, nlen) == CSTR_EQUAL) {
            BYTE *out;
            if (local + 30 > size || rd32(zip + local) != 0x04034b50)
                return NULL;
            data = local + 30 + rd16(zip + local + 26) + rd16(zip + local + 28);
            if (data + csize > size || usize > 0x10000000)
                return NULL;
            out = HeapAlloc(HEAP, 0, usize + 1);
            if (!out)
                return NULL;
            if (method == 0 && csize == usize) {
                CopyMemory(out, zip + data, usize);
            } else if (method != 8 || inflate_raw(zip + data, csize, out, usize) != (int)usize) {
                HeapFree(HEAP, 0, out);
                return NULL;
            }
            out[usize] = 0;
            *outsize = usize;
            return out;
        }
        cd += 46 + nlen + xlen + clen;
    }
    return NULL;
}

typedef struct {
    const char *name;
    DWORD offset, crc, size;
} ZipMember;

static void zip_add(Buf *b, ZipMember *m, const char *name, const BYTE *data, DWORD size)
{
    m->name = name;
    m->offset = b->len;
    m->crc = crc32(data, size);
    m->size = size;
    buf_u32(b, 0x04034b50);
    buf_u16(b, 20);
    buf_u16(b, 0x0800); /* names are UTF-8 */
    buf_u16(b, 0);      /* stored */
    buf_u16(b, 0);      /* time 00:00 */
    buf_u16(b, (WORD)((2000 - 1980) << 9 | 1 << 5 | 1));
    buf_u32(b, m->crc);
    buf_u32(b, size);
    buf_u32(b, size);
    buf_u16(b, (WORD)lstrlenA(name));
    buf_u16(b, 0);
    buf_str(b, name);
    buf_add(b, data, size);
}

static void zip_finish(Buf *b, ZipMember *m, int n)
{
    DWORD start = b->len, cd_size;
    int i;
    for (i = 0; i < n; i++) {
        buf_u32(b, 0x02014b50);
        buf_u16(b, 20);
        buf_u16(b, 20);
        buf_u16(b, 0x0800);
        buf_u16(b, 0);
        buf_u16(b, 0);
        buf_u16(b, (WORD)((2000 - 1980) << 9 | 1 << 5 | 1));
        buf_u32(b, m[i].crc);
        buf_u32(b, m[i].size);
        buf_u32(b, m[i].size);
        buf_u16(b, (WORD)lstrlenA(m[i].name));
        buf_u16(b, 0);
        buf_u16(b, 0);
        buf_u16(b, 0);
        buf_u16(b, 0);
        buf_u32(b, 0);
        buf_u32(b, m[i].offset);
        buf_str(b, m[i].name);
    }
    cd_size = b->len - start;
    buf_u32(b, 0x06054b50);
    buf_u16(b, 0);
    buf_u16(b, 0);
    buf_u16(b, (WORD)n);
    buf_u16(b, (WORD)n);
    buf_u32(b, cd_size);
    buf_u32(b, start);
    buf_u16(b, 0);
}

/* Word .docx */

static const char CONTENT_TYPES[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
    "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
    "</Types>";

static const char RELS[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
    "</Relationships>";

static const char DOC_RELS[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
    "</Relationships>";

/* Normal style: Courier New 12 pt, single spaced, like the Python version. */
static const char STYLES[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
    "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Courier New\" w:hAnsi=\"Courier New\" w:cs=\"Courier New\" w:eastAsia=\"Courier New\"/>"
    "<w:sz w:val=\"24\"/><w:szCs w:val=\"24\"/><w:lang w:val=\"en-US\"/></w:rPr></w:rPrDefault>"
    "<w:pPrDefault><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>"
    "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>"
    "</w:styles>";

static void xml_text(Buf *b, const WCHAR *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        WCHAR c = s[i];
        if (c == '<')
            buf_str(b, "&lt;");
        else if (c == '>')
            buf_str(b, "&gt;");
        else if (c == '&')
            buf_str(b, "&amp;");
        else if (c < 32)
            continue;
        else
            buf_utf8(b, s, n, &i);
    }
}

static void docx_run(Buf *b, const WCHAR *s, int n, BYTE style)
{
    int i, start = 0;
    if (n == 0)
        return;
    buf_str(b, "<w:r>");
    if (style) {
        buf_str(b, "<w:rPr>");
        if (style & ST_BOLD)
            buf_str(b, "<w:b/>");
        if (style & ST_ITALIC)
            buf_str(b, "<w:i/>");
        if (style & ST_STRIKE)
            buf_str(b, "<w:strike/>");
        if (style & ST_UNDERLINE)
            buf_str(b, "<w:u w:val=\"single\"/>");
        buf_str(b, "</w:rPr>");
    }
    for (i = 0; i <= n; i++) {
        if (i == n || s[i] == '\t') {
            if (i > start) {
                buf_str(b, "<w:t xml:space=\"preserve\">");
                xml_text(b, s + start, i - start);
                buf_str(b, "</w:t>");
            }
            if (i < n)
                buf_str(b, "<w:tab/>");
            start = i + 1;
        }
    }
    buf_str(b, "</w:r>");
}

static void docx_para(Buf *b, BYTE align)
{
    static const char *jc[] = {"left", "center", "right", "both"};
    buf_str(b, "<w:p>");
    if (align != AL_LEFT && align <= AL_JUSTIFY) {
        buf_str(b, "<w:pPr><w:jc w:val=\"");
        buf_str(b, jc[align]);
        buf_str(b, "\"/></w:pPr>");
    }
}

BYTE *make_docx(const Doc *d, DWORD *size)
{
    Buf body = {0}, zip = {0};
    ZipMember m[5];
    DWORD bodysize;
    BYTE *bodydata;
    int i, start = 0, para = 0;
    buf_str(&body, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
                   "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>");
    docx_para(&body, doc_align(d, para++));
    for (i = 0; i <= d->len; i++) {
        if (i == d->len || d->text[i] == '\r' || d->style[i] != d->style[start]) {
            docx_run(&body, d->text + start, i - start, i > start ? d->style[start] : 0);
            start = i;
            if (i < d->len && d->text[i] == '\r') {
                buf_str(&body, "</w:p>");
                docx_para(&body, doc_align(d, para++));
                start = i + 1;
            }
        }
    }
    buf_str(&body, "</w:p><w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/>"
                   "<w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/>"
                   "</w:sectPr></w:body></w:document>");
    bodydata = buf_done(&body, &bodysize);
    if (!bodydata)
        return NULL;
    zip_add(&zip, &m[0], "[Content_Types].xml", (const BYTE *)CONTENT_TYPES, sizeof CONTENT_TYPES - 1);
    zip_add(&zip, &m[1], "_rels/.rels", (const BYTE *)RELS, sizeof RELS - 1);
    zip_add(&zip, &m[2], "word/_rels/document.xml.rels", (const BYTE *)DOC_RELS, sizeof DOC_RELS - 1);
    zip_add(&zip, &m[3], "word/styles.xml", (const BYTE *)STYLES, sizeof STYLES - 1);
    zip_add(&zip, &m[4], "word/document.xml", bodydata, bodysize);
    zip_finish(&zip, m, 5);
    HeapFree(HEAP, 0, bodydata);
    return buf_done(&zip, size);
}

BOOL save_docx(const WCHAR *path, const Doc *d)
{
    DWORD size;
    BYTE *data = make_docx(d, &size);
    return write_buf(path, data, size);
}

/* Reading document.xml. Only what Amber Writer shows is kept: paragraphs, tabs, and
   bold, italics, underline and strikethrough set on each run. */

static BOOL tag_is(const char *tag, int taglen, const char *name)
{
    int n = lstrlenA(name);
    return taglen == n && CompareStringA(LOCALE_INVARIANT, 0, tag, n, name, n) == CSTR_EQUAL;
}

/* For <w:b/>, <w:b w:val="0"/> etc: is the property switched on? */
static BOOL prop_on(const char *attrs, int n, BOOL underline)
{
    int i;
    for (i = 0; i + 7 <= n; i++) {
        if (CompareStringA(LOCALE_INVARIANT, 0, attrs + i, 7, "w:val=\"", 7) == CSTR_EQUAL) {
            const char *v = attrs + i + 7;
            if (v[0] == '0' || (v[0] == 'f' && v[1] == 'a') || (v[0] == 'n' && v[1] == 'o' && v[2] == 'n') ||
                (v[0] == 'o' && v[1] == 'f'))
                return FALSE;
            return TRUE;
        }
    }
    (void)underline;
    return TRUE;
}

static void append_utf8(Doc *d, const char *s, int n, BYTE style)
{
    Buf tmp = {0};
    int i = 0;
    while (i < n) {
        if (s[i] == '&') {
            int j = i + 1;
            DWORD c = 0;
            while (j < n && j - i < 12 && s[j] != ';')
                j++;
            if (j < n && s[j] == ';') {
                const char *e = s + i + 1;
                int elen = j - i - 1;
                if (elen == 2 && e[0] == 'l' && e[1] == 't')
                    c = '<';
                else if (elen == 2 && e[0] == 'g' && e[1] == 't')
                    c = '>';
                else if (elen == 3 && e[0] == 'a' && e[1] == 'm' && e[2] == 'p')
                    c = '&';
                else if (elen == 4 && e[0] == 'q')
                    c = '"';
                else if (elen == 4 && e[0] == 'a')
                    c = '\'';
                else if (elen > 1 && e[0] == '#') {
                    int k = 1, hex = (e[1] == 'x' || e[1] == 'X');
                    if (hex)
                        k++;
                    for (; k < elen; k++) {
                        char h = e[k];
                        int v = (h >= '0' && h <= '9') ? h - '0' : (h | 32) >= 'a' && (h | 32) <= 'f' ? (h | 32) - 'a' + 10 : -1;
                        if (v < 0 || (!hex && v > 9))
                            break;
                        c = c * (hex ? 16 : 10) + v;
                    }
                }
                if (c) {
                    char u[4];
                    int un = 0;
                    if (c < 0x80)
                        u[un++] = (char)c;
                    else if (c < 0x800) {
                        u[un++] = (char)(0xC0 | (c >> 6));
                        u[un++] = (char)(0x80 | (c & 63));
                    } else if (c < 0x10000) {
                        u[un++] = (char)(0xE0 | (c >> 12));
                        u[un++] = (char)(0x80 | ((c >> 6) & 63));
                        u[un++] = (char)(0x80 | (c & 63));
                    } else {
                        u[un++] = (char)(0xF0 | (c >> 18));
                        u[un++] = (char)(0x80 | ((c >> 12) & 63));
                        u[un++] = (char)(0x80 | ((c >> 6) & 63));
                        u[un++] = (char)(0x80 | (c & 63));
                    }
                    buf_add(&tmp, u, un);
                    i = j + 1;
                    continue;
                }
            }
        }
        buf_byte(&tmp, (BYTE)s[i]);
        i++;
    }
    if (tmp.len && !tmp.failed) {
        int wn = MultiByteToWideChar(CP_UTF8, 0, (char *)tmp.p, tmp.len, NULL, 0);
        WCHAR *w = HeapAlloc(HEAP, 0, (wn + 1) * sizeof(WCHAR));
        if (w) {
            MultiByteToWideChar(CP_UTF8, 0, (char *)tmp.p, tmp.len, w, wn);
            doc_append(d, w, wn, style);
            HeapFree(HEAP, 0, w);
        }
    }
    if (tmp.p)
        HeapFree(HEAP, 0, tmp.p);
}

/* The value of w:val="..." in a tag's attributes, lower case, or "". */
static void attr_val(const char *attrs, int n, char *out, int size)
{
    int i, j = 0;
    out[0] = 0;
    for (i = 0; i + 7 <= n; i++) {
        if (CompareStringA(LOCALE_INVARIANT, 0, attrs + i, 7, "w:val=\"", 7) == CSTR_EQUAL) {
            for (i += 7; i < n && attrs[i] != '"' && j < size - 1; i++)
                out[j++] = (char)((attrs[i] >= 'A' && attrs[i] <= 'Z') ? attrs[i] + 32 : attrs[i]);
            out[j] = 0;
            return;
        }
    }
}

static BYTE docx_align(const char *v)
{
    if (lstrcmpA(v, "center") == 0)
        return AL_CENTER;
    if (lstrcmpA(v, "right") == 0 || lstrcmpA(v, "end") == 0)
        return AL_RIGHT;
    if (lstrcmpA(v, "both") == 0 || lstrcmpA(v, "distribute") == 0)
        return AL_JUSTIFY;
    return AL_LEFT;
}

static void parse_document_xml(const char *x, DWORD n, Doc *d)
{
    DWORD i = 0;
    BYTE style = 0, align = AL_LEFT;
    int in_rpr = 0, in_ppr = 0, in_t = 0, paragraphs = 0, para = 0;
    DWORD text_start = 0;
    WCHAR cr = '\r', tab = '\t';
    while (i < n) {
        DWORD lt = i, end, name_end;
        int closing, selfclose, taglen;
        const char *tag;
        while (lt < n && x[lt] != '<')
            lt++;
        if (lt >= n)
            break;
        end = lt + 1;
        while (end < n && x[end] != '>')
            end++;
        if (end >= n)
            break;
        closing = x[lt + 1] == '/';
        selfclose = x[end - 1] == '/';
        tag = x + lt + 1 + closing;
        name_end = (DWORD)(tag - x);
        while (name_end < end && x[name_end] != ' ' && x[name_end] != '/' && x[name_end] != '\t' &&
               x[name_end] != '\r' && x[name_end] != '\n')
            name_end++;
        taglen = (int)(name_end - (DWORD)(tag - x));

        if (in_t && closing && tag_is(tag, taglen, "w:t")) {
            append_utf8(d, x + text_start, lt - text_start, style);
            in_t = 0;
        } else if (!closing && tag_is(tag, taglen, "w:t") && !selfclose) {
            in_t = 1;
            text_start = end + 1;
        } else if (tag_is(tag, taglen, "w:p")) {
            if (!closing) {
                if (paragraphs++) {
                    doc_append(d, &cr, 1, 0);
                    para++;
                }
                align = AL_LEFT;
            }
        } else if (in_ppr && !closing && tag_is(tag, taglen, "w:jc")) {
            char v[16];
            attr_val(x + name_end, (int)(end - name_end), v, sizeof v);
            align = docx_align(v);
            doc_set_align(d, para, align);
        } else if (tag_is(tag, taglen, "w:pPr")) {
            in_ppr = !closing && !selfclose;
        } else if (tag_is(tag, taglen, "w:r") && !closing) {
            style = 0;
        } else if (tag_is(tag, taglen, "w:rPr")) {
            in_rpr = !closing && !selfclose;
        } else if (!closing && tag_is(tag, taglen, "w:tab") && !in_ppr) {
            doc_append(d, &tab, 1, style);
        } else if (!closing && (tag_is(tag, taglen, "w:br") || tag_is(tag, taglen, "w:cr"))) {
            /* A line break becomes a paragraph here, lined up like the one it is in. */
            doc_append(d, &cr, 1, 0);
            doc_set_align(d, ++para, align);
        } else if (in_rpr && !in_ppr && !closing) {
            const char *attrs = x + name_end;
            int alen = (int)(end - name_end);
            BYTE flag = 0;
            if (tag_is(tag, taglen, "w:b"))
                flag = ST_BOLD;
            else if (tag_is(tag, taglen, "w:i"))
                flag = ST_ITALIC;
            else if (tag_is(tag, taglen, "w:u"))
                flag = ST_UNDERLINE;
            else if (tag_is(tag, taglen, "w:strike") || tag_is(tag, taglen, "w:dstrike"))
                flag = ST_STRIKE;
            if (flag) {
                if (prop_on(attrs, alen, flag == ST_UNDERLINE))
                    style |= flag;
                else
                    style &= ~flag;
            }
        }
        i = end + 1;
    }
}

BOOL docx_from_zip(const BYTE *zip, DWORD size, Doc *d)
{
    DWORD xsize;
    BYTE *xml = zip_extract(zip, size, "word/document.xml", &xsize);
    if (!xml)
        return FALSE;
    doc_init(d);
    parse_document_xml((char *)xml, xsize, d);
    HeapFree(HEAP, 0, xml);
    if (!d->text)
        doc_append(d, L"", 0, 0);
    return TRUE;
}

BOOL load_docx(const WCHAR *path, Doc *d)
{
    DWORD size;
    BYTE *zip = read_file(path, &size);
    BOOL ok;
    if (!zip)
        return FALSE;
    ok = docx_from_zip(zip, size, d);
    HeapFree(HEAP, 0, zip);
    if (!ok)
        SetLastError(ERROR_INVALID_DATA);
    return ok;
}

/* Reading RTF. Only used on what the RichEdit control writes, to learn the style of
   every character quickly; RTF files themselves are opened by the control. */

typedef struct {
    BYTE style, align;
    BOOL skip;
} RtfGroup;

static BOOL word_is(const char *w, int n, const char *name)
{
    int len = lstrlenA(name);
    return n == len && CompareStringA(LOCALE_INVARIANT, 0, w, n, name, len) == CSTR_EQUAL;
}

static BOOL skipped_destination(const char *w, int n)
{
    static const char *names[] = {"fonttbl", "colortbl", "stylesheet", "info", "pict", "object", "fldinst",
                                  "header", "headerl", "headerr", "headerf", "footer", "footerl", "footerr",
                                  "footerf", "footnote", "listtable", "listoverridetable", "rsidtbl",
                                  "generator", "themedata", "colorschememapping", "latentstyles", "datastore",
                                  "xmlnstbl", "pntext", "pntxtb", "pntxta", "nonshppict", "txfieldtext"};
    int i;
    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if (word_is(w, n, names[i]))
            return TRUE;
    return FALSE;
}

static void flush_bytes(Doc *d, Buf *bytes, UINT cp, BYTE style)
{
    if (bytes->len && !bytes->failed) {
        int wn = MultiByteToWideChar(cp, 0, (char *)bytes->p, bytes->len, NULL, 0);
        WCHAR *w = HeapAlloc(HEAP, 0, (wn + 1) * sizeof(WCHAR));
        if (w) {
            MultiByteToWideChar(cp, 0, (char *)bytes->p, bytes->len, w, wn);
            doc_append(d, w, wn, style);
            HeapFree(HEAP, 0, w);
        }
    }
    bytes->len = 0;
}

BOOL rtf_to_doc(const BYTE *rtf, DWORD n, Doc *d)
{
    RtfGroup stack[256];
    int depth = 0, uc = 1, skip_chars = 0;
    UINT cp = 1252;
    DWORD i = 0;
    Buf bytes = {0};
    BYTE style = 0, align = AL_LEFT;
    BOOL skip = FALSE, group_start = FALSE;
    int para = 0;
    doc_init(d);
    while (i < n) {
        BYTE c = rtf[i];
        if (c == '{' || c == '}') {
            flush_bytes(d, &bytes, cp, style);
            if (c == '{') {
                if (depth < 256) {
                    stack[depth].style = style;
                    stack[depth].align = align;
                    stack[depth].skip = skip;
                }
                depth++;
                group_start = TRUE;
            } else if (depth > 0) {
                if (depth == 1)
                    doc_set_align(d, para, align); /* the last paragraph has no \par */
                depth--;
                if (depth < 256) {
                    style = stack[depth].style;
                    align = stack[depth].align;
                    skip = stack[depth].skip;
                }
            }
            skip_chars = 0;
            i++;
            continue;
        }
        if (c < 32 && c != '\t') {
            i++;
            continue;
        }
        if (c != '\\') {
            if (skip_chars > 0)
                skip_chars--;
            else if (!skip)
                buf_byte(&bytes, c);
            group_start = FALSE;
            i++;
            continue;
        }
        /* A control word or symbol */
        i++;
        if (i >= n)
            break;
        c = rtf[i];
        if (c == '\'') {
            int v = 0, k;
            for (k = 1; k <= 2 && i + k < n; k++) {
                char h = (char)(rtf[i + k] | 32);
                v = v * 16 + ((h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : 0);
            }
            i += 3;
            if (skip_chars > 0)
                skip_chars--;
            else if (!skip)
                buf_byte(&bytes, (BYTE)v);
            group_start = FALSE;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            WCHAR w = 0;
            i++;
            if (c == '*') {
                skip = TRUE;
                continue;
            }
            group_start = FALSE;
            if (c == '\\' || c == '{' || c == '}')
                w = c;
            else if (c == '~')
                w = 0xA0;
            else if (c == '_')
                w = 0x2011;
            else if (c == '\r' || c == '\n')
                w = '\r';
            if (w && skip_chars > 0) {
                skip_chars--;
            } else if (w && !skip) {
                flush_bytes(d, &bytes, cp, style);
                if (w == '\r')
                    doc_set_align(d, para++, align);
                doc_append(d, &w, 1, w == '\r' ? 0 : style);
            }
            continue;
        } else {
            const char *word = (const char *)rtf + i;
            int wn = 0, neg = 0, has_param = 0;
            long param = 0;
            WCHAR out = 0;
            while (i < n && ((rtf[i] >= 'a' && rtf[i] <= 'z') || (rtf[i] >= 'A' && rtf[i] <= 'Z'))) {
                i++;
                wn++;
            }
            if (i < n && rtf[i] == '-') {
                neg = 1;
                i++;
            }
            while (i < n && rtf[i] >= '0' && rtf[i] <= '9') {
                param = param * 10 + (rtf[i] - '0');
                has_param = 1;
                i++;
            }
            if (neg)
                param = -param;
            if (i < n && rtf[i] == ' ')
                i++;
            if (group_start && skipped_destination(word, wn))
                skip = TRUE;
            group_start = FALSE;
            if (skip)
                continue;
            if (word_is(word, wn, "u")) {
                out = (WCHAR)(param < 0 ? param + 65536 : param);
                flush_bytes(d, &bytes, cp, style);
                doc_append(d, &out, 1, style);
                skip_chars = uc;
                continue;
            }
            /* A fallback after \u is skipped by characters, but control words end it. */
            skip_chars = 0;
            flush_bytes(d, &bytes, cp, style);
            if (word_is(word, wn, "ansicpg"))
                cp = (UINT)param;
            else if (word_is(word, wn, "uc"))
                uc = (int)param;
            else if (word_is(word, wn, "b"))
                style = (has_param && param == 0) ? style & ~ST_BOLD : style | ST_BOLD;
            else if (word_is(word, wn, "i"))
                style = (has_param && param == 0) ? style & ~ST_ITALIC : style | ST_ITALIC;
            else if (word_is(word, wn, "strike") || word_is(word, wn, "striked"))
                style = (has_param && param == 0) ? style & ~ST_STRIKE : style | ST_STRIKE;
            else if (word_is(word, wn, "ulnone"))
                style &= ~ST_UNDERLINE;
            else if (wn >= 2 && word[0] == 'u' && word[1] == 'l' && !word_is(word, wn, "ulc"))
                style = (has_param && param == 0) ? style & ~ST_UNDERLINE : style | ST_UNDERLINE;
            else if (word_is(word, wn, "plain"))
                style = 0;
            else if (word_is(word, wn, "pard") || word_is(word, wn, "ql"))
                align = AL_LEFT;
            else if (word_is(word, wn, "qc"))
                align = AL_CENTER;
            else if (word_is(word, wn, "qr"))
                align = AL_RIGHT;
            else if (word_is(word, wn, "qj") || word_is(word, wn, "qd"))
                align = AL_JUSTIFY;
            else if (word_is(word, wn, "par") || word_is(word, wn, "line") || word_is(word, wn, "row"))
                out = '\r';
            else if (word_is(word, wn, "tab") || word_is(word, wn, "cell"))
                out = '\t';
            else if (word_is(word, wn, "emdash"))
                out = 0x2014;
            else if (word_is(word, wn, "endash"))
                out = 0x2013;
            else if (word_is(word, wn, "lquote"))
                out = 0x2018;
            else if (word_is(word, wn, "rquote"))
                out = 0x2019;
            else if (word_is(word, wn, "ldblquote"))
                out = 0x201C;
            else if (word_is(word, wn, "rdblquote"))
                out = 0x201D;
            else if (word_is(word, wn, "bullet"))
                out = 0x2022;
            if (out == '\r')
                doc_set_align(d, para++, align);
            if (out)
                doc_append(d, &out, 1, (out == '\r') ? 0 : style);
        }
    }
    flush_bytes(d, &bytes, cp, style);
    if (depth > 0)
        doc_set_align(d, para, align);
    if (bytes.p)
        HeapFree(HEAP, 0, bytes.p);
    if (!d->text)
        doc_append(d, L"", 0, 0);
    return TRUE;
}
