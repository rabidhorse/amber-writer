/* Tests for the file formats and spelling. Build and run with: make test */
#include <stdio.h>
#include "doc.h"
#include "spell.h"

static int failures, checks;

/* Prints UTF-16 text, which printf's %ls can't do off Windows; other characters show as ?. */
static void show(const WCHAR *s, int n)
{
    int i;
    for (i = 0; s && i < n; i++)
        putchar(s[i] >= 32 && s[i] < 127 ? (char)s[i] : '?');
}

#define CHECK(cond)                                                       \
    do {                                                                  \
        checks++;                                                         \
        if (!(cond)) {                                                    \
            failures++;                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                 \
    } while (0)

static void add(Doc *d, const WCHAR *s, BYTE style)
{
    doc_append(d, s, lstrlenW(s), style);
}

static BOOL same_doc(const Doc *a, const Doc *b)
{
    int i, para = 0;
    if (a->len != b->len)
        return FALSE;
    for (i = 0; i < a->len; i++) {
        if (a->text[i] != b->text[i] || (a->text[i] != '\r' && a->style[i] != b->style[i]))
            return FALSE;
        if (a->text[i] == '\r')
            para++;
    }
    for (i = 0; i <= para; i++)
        if (doc_align(a, i) != doc_align(b, i))
            return FALSE;
    return TRUE;
}

static void sample(Doc *d)
{
    doc_init(d);
    doc_set_align(d, 0, AL_CENTER);
    doc_set_align(d, 2, AL_RIGHT);
    doc_set_align(d, 3, AL_JUSTIFY);
    add(d, L"Plain ", 0);
    add(d, L"bold", ST_BOLD);
    add(d, L" and ", 0);
    add(d, L"all four", ST_BOLD | ST_ITALIC | ST_UNDERLINE | ST_STRIKE);
    add(d, L"\r\r", 0);
    add(d, L"Tab\there", ST_ITALIC);
    add(d, L"\r", 0);
    add(d, L"Caf\x00e9 & <tags> \x201cquoted\x201d {braces} \\", 0);
}

static void test_docx_round_trip(void)
{
    Doc d, back;
    DWORD size;
    BYTE *zip;
    sample(&d);
    zip = make_docx(&d, &size);
    CHECK(zip != NULL);
    CHECK(docx_from_zip(zip, size, &back));
    CHECK(same_doc(&d, &back));
    CHECK(save_docx(L"build/test.docx", &d));
    CHECK(save_rtf(L"build/test.rtf", &d));
    HeapFree(GetProcessHeap(), 0, zip);
    doc_free(&back);
    doc_free(&d);
}

static void test_reads_python_docx(void)
{
    Doc expected, got;
    sample(&expected);
    /* The Python version wrote no braces or backslash; trim that tail. */
    expected.len -= lstrlenW(L" {braces} \\");
    doc_set_align(&expected, 0, AL_LEFT); /* the Python version had no alignment */
    doc_set_align(&expected, 2, AL_LEFT);
    doc_set_align(&expected, 3, AL_LEFT);
    CHECK(load_docx(L"tests/files/python-0.2.docx", &got));
    CHECK(same_doc(&expected, &got));
    if (!same_doc(&expected, &got))
    {
        printf("  got %d chars: ", got.len);
        show(got.text, got.len);
        printf("\n");
    }
    doc_free(&expected);
    doc_free(&got);
}

static void test_rtf(void)
{
    Doc d;
    DWORD size;
    char *rtf;
    sample(&d);
    rtf = (char *)make_rtf(&d, &size);
    CHECK(rtf != NULL);
    rtf[size - 1] = 0;
    CHECK(strstr(rtf, "{\\rtf1") == rtf);
    CHECK(strstr(rtf, "\\qc Plain \\b bold\\b0  and ") != NULL);
    CHECK(strstr(rtf, "\\par\r\n\\ql \\par\r\n\\qr \\b0") != NULL);
    CHECK(strstr(rtf, "\\par\r\n\\qj \\i0 Caf") != NULL);
    CHECK(strstr(rtf, "\\b\\i\\ul\\strike all four") != NULL);
    CHECK(strstr(rtf, "Tab\\tab here") != NULL);
    CHECK(strstr(rtf, "Caf\\u233?") != NULL);
    CHECK(strstr(rtf, "\\u8220?quoted\\u8221?") != NULL);
    CHECK(strstr(rtf, "\\{braces\\} \\\\") != NULL);
    HeapFree(GetProcessHeap(), 0, rtf);
    doc_free(&d);
}

static void test_rtf_reader(void)
{
    Doc d, back;
    DWORD size;
    BYTE *rtf;
    sample(&d);
    rtf = make_rtf(&d, &size);
    CHECK(rtf_to_doc(rtf, size, &back));
    CHECK(same_doc(&d, &back));
    HeapFree(GetProcessHeap(), 0, rtf);
    doc_free(&back);
    {
        /* The way RichEdit writes it, with tables, fallbacks and a final \\par. */
        static const char in[] = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fmodern Courier New;}}"
                                 "{\\colortbl ;\\red255\\green176\\blue0;}{\\*\\generator Riched20 10.0;}"
                                 "\\uc1\\pard\\cf1\\f0\\fs32 Caf\\'e9 \\b bo\\u8212?ld\\b0  \\ul u\\ulnone  "
                                 "\\strike s\\strike0\\par\r\nnext\\par\r\n}";
        CHECK(rtf_to_doc((const BYTE *)in, sizeof in - 1, &back));
        CHECK(back.len == 20 && back.text[3] == 0xE9 && back.text[7] == 0x2014 && back.style[7] == ST_BOLD);
        CHECK(back.style[11] == ST_UNDERLINE && back.style[13] == ST_STRIKE && back.style[14] == 0);
        CHECK(back.text[14] == '\r' && back.text[15] == 'n' && back.text[19] == '\r');
        doc_free(&back);
    }
    {
        static const char in[] = "{\\rtf1\\ansi\\pard\\qc Title\\par\r\n\\pard Body\\par\r\n\\pard\\qr Signed\\par}";
        CHECK(rtf_to_doc((const BYTE *)in, sizeof in - 1, &back));
        CHECK(doc_align(&back, 0) == AL_CENTER && doc_align(&back, 1) == AL_LEFT && doc_align(&back, 2) == AL_RIGHT);
        doc_free(&back);
    }
    doc_free(&d);
}

static BOOL wraps(const WCHAR *text, int columns, int want_end, int want_width, int want_next)
{
    int line_end, width, next;
    wrap_line(text, 0, lstrlenW(text), columns, 5, &line_end, &width, &next);
    if (line_end != want_end || width != want_width || next != want_next)
    {
        printf("  wrap \"");
        show(text, lstrlenW(text));
        printf("\": end %d width %d next %d\n", line_end, width, next);
    }
    return line_end == want_end && width == want_width && next == want_next;
}

static void test_wrap(void)
{
    CHECK(wraps(L"short line", 20, 10, 10, 10));
    CHECK(wraps(L"one two three", 9, 8, 7, 8));        /* "one two " then "three" */
    CHECK(wraps(L"one two    three", 7, 11, 7, 11));   /* spaces after the last word hang */
    CHECK(wraps(L"title\rnext", 20, 5, 5, 6));
    CHECK(wraps(L"\rnext", 20, 0, 0, 1));              /* an empty paragraph */
    CHECK(wraps(L"abcdefghij", 4, 4, 4, 4));           /* a word wider than the line is cut */
    CHECK(wraps(L"a\tb", 20, 3, 6, 3));                /* tab stops every 5 columns */
    CHECK(wraps(L"abcd efgh", 4, 5, 4, 5));            /* the space at column 5 hangs */
    CHECK(wraps(L"caf\x0065\x0301 au lait", 8, 9, 7, 9)); /* the combining accent takes no column */
}

static void test_txt(void)
{
    Doc d, back;
    sample(&d);
    CHECK(save_txt(L"build/test.txt", &d));
    CHECK(load_txt(L"build/test.txt", &back));
    CHECK(back.len == d.len && CompareStringW(LOCALE_INVARIANT, 0, back.text, back.len, d.text, d.len) == CSTR_EQUAL);
    doc_free(&back);
    doc_free(&d);
}

static BOOL ok(const WCHAR *w)
{
    return spell_is_correct(w, lstrlenW(w));
}

static BOOL suggests(const WCHAR *w, const char *want)
{
    char hints[5][MAX_WORD];
    int n = spell_suggest(w, lstrlenW(w), hints, 5), i;
    for (i = 0; i < n; i++)
        if (lstrcmpA(hints[i], want) == 0)
            return TRUE;
    return FALSE;
}

static void test_spelling(void)
{
    int s, e;
    DeleteFileW(L"build/words.txt");
    spell_init(L"build/words.txt");
    CHECK(ok(L"hello"));
    CHECK(ok(L"Don't"));
    CHECK(!ok(L"recieve"));
    CHECK(suggests(L"recieve", "receive"));
    CHECK(suggests(L"teh", "the"));
    CHECK(ok(L"NASA") && ok(L"x") && ok(L"WordPerfect") && ok(L"DON'T"));
    CHECK(!ok(L"zorblax"));
    spell_add(L"Zorblax", 7);
    CHECK(ok(L"zorblax"));
    CHECK(next_word(L"the quikc", 9, 0, &s, &e) && s == 0 && e == 3);
    CHECK(next_word(L"the quikc", 9, 3, &s, &e) && s == 4 && e == 9);
    CHECK(next_word(L"don't", 5, 0, &s, &e) && e == 5);
    CHECK(next_word(L"rock 'n' roll", 13, 4, &s, &e) && s == 6 && e == 7);
    CHECK(!next_word(L"  ", 2, 0, &s, &e));
    CHECK(next_word(L"caf\x00e9 au", 7, 0, &s, &e) && s == 0 && e == 4);
    CHECK(ok(L"caf\x00e9") && ok(L"Caf\x00e9"));
}

static void test_personal_list_persists(void)
{
    /* A fresh start reads build\words.txt written above. */
    spell_init(L"build/words.txt");
    CHECK(ok(L"zorblax"));
}

int main(void)
{
    test_docx_round_trip();
    test_reads_python_docx();
    test_rtf();
    test_txt();
    test_rtf_reader();
    test_wrap();
    test_spelling();
    test_personal_list_persists();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
