/* Amber Writer: a distraction-free word processor in the style of WordPerfect for DOS,
 * written against the plain Win32 API so one small .exe runs on Windows XP through 11.
 *
 * The page is a RichEdit control sized to 65 columns and centered on a black window.
 * The status line, its prompts and the help screen are drawn by the main window.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <richedit.h>
#include <commdlg.h>
#include <cderr.h>
#include <shellapi.h>
#include <shlobj.h>
#include "beep.h"
#include "doc.h"
#include "spell.h"
#include "version.h"

#define AMBER RGB(0xFF, 0xB0, 0x00)
#define DIM_AMBER RGB(0x9A, 0x6A, 0x00)
#define BLACK RGB(0, 0, 0)

/* WordPerfect 5.1 defaults: 1" margins on letter paper, 10 characters and 6 lines per inch. */
#define COLUMNS 65
#define LINES_PER_PAGE 54
#define LINES_PER_INCH 6
#define CHARS_PER_INCH 10
#define TAB_COLUMNS 5

#define ID_EDIT 100
#define ID_PROMPT 101
#define TIMER_COUNTS 1
#define TIMER_MESSAGE 2
#define TIMER_SQUIGGLES 3
#define TIMER_HOLD 4

static const WCHAR *FONT_CHOICES[] = {
    L"Perfect DOS VGA 437",
    L"Px437 IBM VGA 8x16",
    L"Consolas",
    L"Lucida Console",
    L"Courier New",
};

static const WCHAR HELP_TEXT[] =
    L"AMBER WRITER " AMBER_VERSION_W L"                                      Help\n"
    L"\n"
    L"Alt         Tap for the menu (or Alt+=): arrows, Enter, Esc, letters\n"
    L"Ctrl, Alt   Hold a moment to see the keys that go with them\n"
    L"F3          This help screen (Esc to leave)\n"
    L"F10         Save document as...            Ctrl+S   Save\n"
    L"Shift+F10   Retrieve (open) a document     Ctrl+O   Retrieve\n"
    L"Shift+F7    Print                          Ctrl+P   Print\n"
    L"F7          Exit                           Ctrl+Q   Exit\n"
    L"Ctrl+B, F6  Bold                           Ctrl+I   Italics\n"
    L"Ctrl+U, F8  Underline                      Ctrl+Shift+X  Strikethrough\n"
    L"Shift+F6    Center (Ctrl+E)                Alt+F6   Flush right (Ctrl+R)\n"
    L"Ctrl+L      Back to the left; Enter after a centered title does it too\n"
    L"\n"
    L"Alt+N       Next typo                      Alt+P    Previous typo\n"
    L"Ctrl+F2     Spell check from the cursor, with suggestions\n"
    L"Alt+A       Add the word at the cursor to your dictionary\n"
    L"Alt+B       Turn the misspelling beep on or off\n"
    L"\n"
    L"F2          Search forward (F2 again: next) Shift+F2  Search back\n"
    L"Alt+F2      Replace a word or name everywhere\n"
    L"Ctrl+F      Search                         Ctrl+H   Replace\n"
    L"Ctrl+=      Bigger text                    Ctrl+-   Smaller text\n"
    L"F11         Full screen on or off          Ins      Typeover\n"
    L"Ctrl+Z / Ctrl+Y   Undo / redo         Ctrl+X/C/V   Cut / copy / paste\n"
    L"Right-click the page or click the status line for the menu.\n"
    L"\n"
    L"The beep sounds when you finish a word that is not in the English\n"
    L"dictionary. Put the cursor on an underlined word to see suggestions.\n"
    L"Replace changes whole words only and keeps capitals: John -> Peter.\n"
    L"\n"
    L"Documents are saved as Rich Text (.rtf), which Word, WordPad and\n"
    L"LibreOffice open with bold, italics, underline and strikethrough.\n"
    L"Type a name ending in .docx for a Word file, or .txt for plain text.\n"
    L"A name without a folder is saved in your My Documents folder.\n"
    L"\n"
    L"Press Esc to return to your document.";

/* Prompts on the status line, WordPerfect style */

enum {
    P_NONE, P_SAVE_AS, P_REPLACE, P_SAVE_BEFORE_RETRIEVE, P_RETRIEVE, P_SAVE_BEFORE_EXIT, P_EXIT, P_SPELL,
    P_FIND, P_SWAP_CONFIRM, P_SWAP_FIND, P_SWAP_WITH, P_SWAP_EACH
};
enum { THEN_NONE, THEN_ASK_RETRIEVE, THEN_QUIT };

static HINSTANCE g_inst;
static HWND g_main, g_edit, g_prompt;
static WNDPROC g_edit_proc, g_prompt_proc;
static HFONT g_font;
static HBRUSH g_black;
static WCHAR g_face[LF_FACESIZE];
static int g_font_size = 16, g_shown_size, g_cw = 8, g_ch = 16, g_ascent = 12;
static int g_status_y, g_status_h;
static WCHAR g_path[MAX_PATH];
static WCHAR g_docs[MAX_PATH];
static BOOL g_beep = TRUE, g_help, g_typeover, g_busy, g_quitting, g_fullscreen;
static WINDOWPLACEMENT g_placement;
static WCHAR g_message[512];
static WCHAR g_counts[128];
static WCHAR g_hint[256];
static BOOL g_hints = TRUE; /* the faint key reminders on the status line */
static int g_typos, g_pages = 1;
/* True while a word is being typed, so it is not called a typo before it is finished. */
static BOOL g_typing;
static WCHAR g_find[256], g_swap_with[256];
static BOOL g_find_back, g_swap_confirm;
static LONG g_swap_pos, g_swap_start;
static int g_swap_done;

static int g_prompt_kind;
static BOOL g_prompt_default_yes;
static WCHAR g_prompt_label[512];
static char g_prompt_choices[16];
static int g_prompt_then;
static WCHAR g_prompt_path[MAX_PATH];
static LONG g_spell_start, g_spell_end;
static char g_spell_hints[5][MAX_WORD];
static int g_spell_hint_count;
static LONG g_last_caret_line = -1;

static void update_status(void);
static void layout(void);
static void update_hint(void);

/* Small helpers */

static void fmt_thousands(WCHAR *out, int n)
{
    WCHAR raw[16];
    int len, i, j = 0;
    wsprintfW(raw, L"%d", n);
    len = lstrlenW(raw);
    for (i = 0; i < len; i++) {
        if (i && (len - i) % 3 == 0)
            out[j++] = ',';
        out[j++] = raw[i];
    }
    out[j] = 0;
}

static const WCHAR *file_name(const WCHAR *path)
{
    const WCHAR *name = path, *p;
    for (p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            name = p + 1;
    return name;
}

static const WCHAR *extension(const WCHAR *path)
{
    const WCHAR *name = file_name(path), *dot = NULL, *p;
    for (p = name; *p; p++)
        if (*p == '.')
            dot = p;
    return dot ? dot : name + lstrlenW(name);
}

static BOOL has_ext(const WCHAR *path, const WCHAR *ext)
{
    return lstrcmpiW(extension(path), ext) == 0;
}

static BOOL file_exists(const WCHAR *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void trim(WCHAR *s)
{
    int n = lstrlenW(s), start = 0;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '"'))
        s[--n] = 0;
    while (s[start] == ' ' || s[start] == '\t' || s[start] == '"')
        start++;
    if (start)
        MoveMemory(s, s + start, (n - start + 1) * sizeof(WCHAR));
}

static void error_text(DWORD err, WCHAR *out, int size)
{
    int n;
    out[0] = 0;
    n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, out, size, NULL);
    while (n > 0 && (out[n - 1] == '\r' || out[n - 1] == '\n' || out[n - 1] == ' ' || out[n - 1] == '.'))
        out[--n] = 0;
    if (!n)
        wsprintfW(out, L"error %lu", err);
}

/* Settings live in HKEY_CURRENT_USER\Software\AmberWriter. */

static DWORD setting_get(const WCHAR *name, DWORD fallback)
{
    HKEY key;
    DWORD value = fallback, size = sizeof value, type;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\AmberWriter", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
            value = fallback;
        RegCloseKey(key);
    }
    return value;
}

static void setting_set(const WCHAR *name, DWORD value)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\AmberWriter", 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof value);
        RegCloseKey(key);
    }
}

/* The amber caret. RichEdit makes its own plain caret, which shows white on black,
   so its CreateCaret import is pointed here and given an amber bitmap instead. */

typedef BOOL(WINAPI *CreateCaretFn)(HWND, HBITMAP, int, int);
static CreateCaretFn real_create_caret = CreateCaret;
static HBITMAP caret_bitmaps[64];

static BOOL WINAPI amber_create_caret(HWND hwnd, HBITMAP bitmap, int width, int height)
{
    if (bitmap == NULL && height > 0 && height < 64 * 4) {
        int slot = height % 64;
        BITMAP info;
        if (caret_bitmaps[slot] && (!GetObjectW(caret_bitmaps[slot], sizeof info, &info) || info.bmHeight != height)) {
            DeleteObject(caret_bitmaps[slot]);
            caret_bitmaps[slot] = NULL;
        }
        if (!caret_bitmaps[slot]) {
            HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
            HBITMAP bmp = CreateCompatibleBitmap(screen, 2, height);
            if (bmp) {
                HGDIOBJ old = SelectObject(mem, bmp);
                RECT r = {0, 0, 2, height};
                HBRUSH amber = CreateSolidBrush(AMBER);
                FillRect(mem, &r, amber);
                DeleteObject(amber);
                SelectObject(mem, old);
            }
            DeleteDC(mem);
            ReleaseDC(NULL, screen);
            caret_bitmaps[slot] = bmp;
        }
        if (caret_bitmaps[slot])
            return real_create_caret(hwnd, caret_bitmaps[slot], 0, 0);
    }
    return real_create_caret(hwnd, bitmap, width, height);
}

static BOOL g_hook_works;

static BOOL WINAPI hooked_create_caret(HWND hwnd, HBITMAP bitmap, int width, int height)
{
    g_hook_works = TRUE;
    return amber_create_caret(hwnd, bitmap, width, height);
}

/* Swaps in the amber caret after a control has made its own plain one. */
static void recolor_caret(HWND hwnd, int height)
{
    POINT at;
    GetCaretPos(&at);
    amber_create_caret(hwnd, NULL, 2, height);
    SetCaretPos(at.x, at.y);
    ShowCaret(hwnd);
}

static void hook_caret(HMODULE module)
{
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY *dir;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    FARPROC target = GetProcAddress(GetModuleHandleW(L"user32.dll"), "CreateCaret");
    if (!module || !target || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return;
    dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress)
        return;
    real_create_caret = (CreateCaretFn)target;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; thunk->u1.Function; thunk++) {
            if ((FARPROC)thunk->u1.Function == target) {
                DWORD old;
                if (VirtualProtect(&thunk->u1.Function, sizeof thunk->u1.Function, PAGE_READWRITE, &old)) {
                    thunk->u1.Function = (ULONG_PTR)hooked_create_caret;
                    VirtualProtect(&thunk->u1.Function, sizeof thunk->u1.Function, old, &old);
                }
            }
        }
    }
}

/* Fonts */

static int CALLBACK font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM found)
{
    (void)lf;
    (void)tm;
    (void)type;
    *(BOOL *)found = TRUE;
    return 0;
}

static void pick_font_face(void)
{
    HDC dc = GetDC(NULL);
    int i;
    for (i = 0; i < (int)(sizeof FONT_CHOICES / sizeof FONT_CHOICES[0]); i++) {
        LOGFONTW lf = {0};
        BOOL found = FALSE;
        lf.lfCharSet = DEFAULT_CHARSET;
        lstrcpynW(lf.lfFaceName, FONT_CHOICES[i], LF_FACESIZE);
        EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)font_found, (LPARAM)&found, 0);
        if (found) {
            lstrcpynW(g_face, FONT_CHOICES[i], LF_FACESIZE);
            break;
        }
    }
    if (!g_face[0])
        lstrcpyW(g_face, L"Courier New");
    ReleaseDC(NULL, dc);
}

static HFONT make_font(int points, int *cw, int *ch, int *ascent)
{
    HDC dc = GetDC(NULL);
    HFONT font = CreateFontW(-MulDiv(points, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             FIXED_PITCH | FF_MODERN, g_face);
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW tm;
    SIZE size;
    GetTextMetricsW(dc, &tm);
    GetTextExtentPoint32W(dc, L"MMMMMMMMMM", 10, &size);
    *cw = size.cx / 10;
    *ch = tm.tmHeight;
    *ascent = tm.tmAscent;
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return font;
}

/* The page */

static void char_format(CHARFORMAT2W *cf, int points)
{
    ZeroMemory(cf, sizeof *cf);
    cf->cbSize = sizeof *cf;
    cf->dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET | CFM_BACKCOLOR | CFM_OFFSET | CFM_PROTECTED | CFM_LINK;
    cf->yHeight = points * 20;
    cf->crTextColor = AMBER;
    cf->dwEffects = CFE_AUTOBACKCOLOR;
    cf->bCharSet = DEFAULT_CHARSET;
    cf->bPitchAndFamily = FIXED_PITCH | FF_MODERN;
    lstrcpynW(cf->szFaceName, g_face, LF_FACESIZE);
}

/* Sets the font, color, tab stops and margins of everything on the page, keeping
   bold, italics, underline and strikethrough. */
static void apply_page_format(void)
{
    CHARFORMAT2W cf;
    PARAFORMAT2 pf;
    CHARRANGE sel, all = {0, -1};
    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX), i;
    BOOL modified = (BOOL)SendMessageW(g_edit, EM_GETMODIFY, 0, 0);
    ReleaseDC(NULL, dc);

    g_busy = TRUE;
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    char_format(&cf, g_shown_size);
    SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);

    ZeroMemory(&pf, sizeof pf);
    pf.cbSize = sizeof pf;
    /* Alignment is left as it is: each paragraph keeps its own. */
    pf.dwMask = PFM_TABSTOPS | PFM_STARTINDENT | PFM_RIGHTINDENT | PFM_OFFSET | PFM_SPACEBEFORE |
                PFM_SPACEAFTER | PFM_LINESPACING | PFM_NUMBERING;
    pf.cTabCount = MAX_TAB_STOPS;
    for (i = 0; i < MAX_TAB_STOPS; i++)
        pf.rgxTabs[i] = MulDiv((i + 1) * TAB_COLUMNS * g_cw, 1440, dpi);
    SendMessageW(g_edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM)&all);
    SendMessageW(g_edit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM)&sel);

    SendMessageW(g_edit, EM_SETBKGNDCOLOR, 0, BLACK);
    SendMessageW(g_edit, EM_SETMODIFY, modified, 0);
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_edit, NULL, TRUE);
    g_busy = FALSE;
}

/* The text width is exactly 65 columns; the control is a little wider so the caret
   still shows after the last column. */
static void set_page_rect(void)
{
    RECT r;
    GetClientRect(g_edit, &r);
    r.right = r.left + COLUMNS * g_cw + 1;
    SendMessageW(g_edit, EM_SETRECT, 0, (LPARAM)&r);
}

static void choose_font(void)
{
    RECT client;
    int size = g_font_size, cw, ch, ascent;
    HFONT font;
    GetClientRect(g_main, &client);
    /* Shrink the text when 65 columns would not fit, as on an 800x600 screen. */
    for (;;) {
        font = make_font(size, &cw, &ch, &ascent);
        if (size <= 8 || client.right == 0 || COLUMNS * cw + 8 <= client.right - 16)
            break;
        DeleteObject(font);
        size--;
    }
    if (g_font)
        DeleteObject(g_font);
    g_font = font;
    g_cw = cw;
    g_ch = ch;
    g_ascent = ascent;
    if (size != g_shown_size) {
        g_shown_size = size;
        if (g_edit) {
            apply_page_format();
            /* Otherwise Ctrl+Z would undo the change of text size. */
            SendMessageW(g_edit, EM_EMPTYUNDOBUFFER, 0, 0);
        }
    }
    if (g_prompt)
        SendMessageW(g_prompt, WM_SETFONT, (WPARAM)g_font, TRUE);
}

static void layout(void)
{
    RECT client;
    int margin_x, top = 16, bottom = 10, text_w, edit_h, x;
    if (!g_edit)
        return;
    choose_font();
    GetClientRect(g_main, &client);
    margin_x = client.right > COLUMNS * g_cw + 48 ? 24 : 8;
    (void)margin_x;
    g_status_h = g_ch + 4;
    g_status_y = client.bottom - bottom - g_status_h;
    text_w = COLUMNS * g_cw + 4;
    x = (client.right - text_w) / 2;
    if (x < 0)
        x = 0;
    edit_h = g_status_y - 6 - top;
    if (edit_h < g_ch)
        edit_h = g_ch;
    MoveWindow(g_edit, x, top, text_w, edit_h, TRUE);
    set_page_rect();
    InvalidateRect(g_main, NULL, TRUE);
}

/* Reading the page */

static int get_text(WCHAR **out)
{
    GETTEXTLENGTHEX gl = {GTL_NUMCHARS | GTL_PRECISE, 1200};
    GETTEXTEX gt = {0};
    int len = (int)SendMessageW(g_edit, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
    WCHAR *text = HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR));
    if (!text) {
        *out = NULL;
        return 0;
    }
    gt.cb = (len + 1) * sizeof(WCHAR);
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    len = (int)SendMessageW(g_edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)text);
    text[len] = 0;
    *out = text;
    return len;
}

static int get_range(LONG start, LONG end, WCHAR *buf)
{
    TEXTRANGEW tr;
    tr.chrg.cpMin = start;
    tr.chrg.cpMax = end;
    tr.lpstrText = buf;
    return (int)SendMessageW(g_edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
}

static CHARRANGE get_sel(void)
{
    CHARRANGE sel;
    SendMessageW(g_edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    return sel;
}

static void set_sel(LONG start, LONG end)
{
    CHARRANGE sel = {start, end};
    SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM)&sel);
}

static BYTE style_bits(const CHARFORMAT2W *cf)
{
    BYTE s = 0;
    if (cf->dwEffects & CFE_BOLD)
        s |= ST_BOLD;
    if (cf->dwEffects & CFE_ITALIC)
        s |= ST_ITALIC;
    if (cf->dwEffects & CFE_UNDERLINE)
        s |= ST_UNDERLINE;
    if (cf->dwEffects & CFE_STRIKEOUT)
        s |= ST_STRIKE;
    return s;
}

#define STYLE_MASK (CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT)

/* Style of [start, end), or -1 when it is mixed. */
static int range_style(LONG start, LONG end)
{
    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof cf);
    cf.cbSize = sizeof cf;
    set_sel(start, end);
    SendMessageW(g_edit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    if ((cf.dwMask & STYLE_MASK) != STYLE_MASK)
        return -1;
    return style_bits(&cf);
}

static BOOL freeze(CHARRANGE *sel, POINT *scroll)
{
    BOOL was_busy = g_busy;
    g_busy = TRUE;
    *sel = get_sel();
    SendMessageW(g_edit, EM_GETSCROLLPOS, 0, (LPARAM)scroll);
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    return was_busy;
}

static void thaw(CHARRANGE sel, POINT scroll, BOOL was_busy)
{
    SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM)&sel);
    SendMessageW(g_edit, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_edit, NULL, TRUE);
    g_busy = was_busy;
}

/* Paragraph alignment */

static BYTE align_from_pfa(WORD pfa)
{
    return pfa == PFA_CENTER ? AL_CENTER : pfa == PFA_RIGHT ? AL_RIGHT : pfa == PFA_JUSTIFY ? AL_JUSTIFY : AL_LEFT;
}

static WORD pfa_from_align(BYTE align)
{
    return align == AL_CENTER ? PFA_CENTER : align == AL_RIGHT ? PFA_RIGHT : align == AL_JUSTIFY ? PFA_JUSTIFY : PFA_LEFT;
}

/* Alignment of the paragraph(s) in the selection. */
static BYTE sel_align(void)
{
    PARAFORMAT2 pf;
    ZeroMemory(&pf, sizeof pf);
    pf.cbSize = sizeof pf;
    SendMessageW(g_edit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    return align_from_pfa(pf.wAlignment);
}

/* Lines up every paragraph the selection touches. */
static void set_sel_align(BYTE align)
{
    PARAFORMAT2 pf;
    ZeroMemory(&pf, sizeof pf);
    pf.cbSize = sizeof pf;
    pf.dwMask = PFM_ALIGNMENT;
    pf.wAlignment = pfa_from_align(align);
    SendMessageW(g_edit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    if (!g_busy) {
        CHARRANGE sel = get_sel();
        set_sel(sel.cpMin, sel.cpMax); /* moves the caret along with the line */
    }
}

/* Reads the page into a Doc: the text plus the style of every character. Runs of one
   style are found by doubling and then halving, so this asks RichEdit about each
   change of style a few times rather than about every character. */
static BOOL page_to_doc_slow(Doc *d)
{
    WCHAR *text;
    int len = get_text(&text), pos = 0, i;
    CHARRANGE sel;
    POINT scroll;
    BOOL was_busy;
    if (!text)
        return FALSE;
    doc_init(d);
    for (i = 0; i < len; i++)
        if (text[i] == 0x0B)
            text[i] = '\r';
    if (!doc_append(d, text, len, 0)) {
        HeapFree(GetProcessHeap(), 0, text);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, text);
    was_busy = freeze(&sel, &scroll);
    while (pos < len) {
        int style = range_style(pos, pos + 1), good = 1, bad, step = 1;
        if (style < 0)
            style = 0;
        /* Grow until the run ends or the text does. */
        for (;;) {
            int next = good + step;
            if (pos + next > len) {
                next = len - pos;
                if (next == good || range_style(pos, pos + next) == style) {
                    good = next;
                    bad = -1;
                } else {
                    bad = next;
                }
                break;
            }
            if (range_style(pos, pos + next) != style) {
                bad = next;
                break;
            }
            good = next;
            step *= 2;
        }
        if (bad > 0) {
            while (bad - good > 1) {
                int mid = (good + bad) / 2;
                if (range_style(pos, pos + mid) == style)
                    good = mid;
                else
                    bad = mid;
            }
        }
        for (i = pos; i < pos + good; i++)
            d->style[i] = (BYTE)style;
        pos += good;
    }
    for (pos = 0, i = 0; pos <= len; pos++) {
        if (pos == 0 || d->text[pos - 1] == '\r') {
            set_sel(pos, pos);
            doc_set_align(d, i++, sel_align());
        }
    }
    thaw(sel, scroll, was_busy);
    return TRUE;
}

typedef struct {
    BYTE *p;
    DWORD len, cap;
    BOOL failed;
} StreamOut;

static DWORD CALLBACK stream_out(DWORD_PTR cookie, LPBYTE buf, LONG n, LONG *done)
{
    StreamOut *s = (StreamOut *)cookie;
    if (s->len + n > s->cap) {
        DWORD cap = s->cap ? s->cap * 2 : 65536;
        BYTE *p;
        while (cap < s->len + n)
            cap *= 2;
        p = s->p ? HeapReAlloc(GetProcessHeap(), 0, s->p, cap) : HeapAlloc(GetProcessHeap(), 0, cap);
        if (!p) {
            s->failed = TRUE;
            return 1;
        }
        s->p = p;
        s->cap = cap;
    }
    CopyMemory(s->p + s->len, buf, n);
    s->len += n;
    *done = n;
    return 0;
}

/* Reads the page into a Doc. The quick way reads the RTF that RichEdit writes; if
   that ever disagrees with the plain text on the page, the slow way is used. */
static BOOL page_to_doc(Doc *d)
{
    StreamOut out = {0};
    EDITSTREAM es = {(DWORD_PTR)&out, 0, stream_out};
    WCHAR *text;
    int len = get_text(&text), i;
    BOOL same = FALSE;
    if (!text)
        return FALSE;
    for (i = 0; i < len; i++)
        if (text[i] == 0x0B)
            text[i] = '\r';
    SendMessageW(g_edit, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
    if (!out.failed && out.p && rtf_to_doc(out.p, out.len, d)) {
        /* RichEdit ends its RTF with the final paragraph mark, which the text leaves out. */
        if (d->len == len + 1 && d->text[len] == '\r')
            d->len = len;
        same = d->len == len;
        for (i = 0; same && i < len; i++)
            if (d->text[i] != text[i])
                same = FALSE;
        if (!same)
            doc_free(d);
    }
    if (out.p)
        HeapFree(GetProcessHeap(), 0, out.p);
    HeapFree(GetProcessHeap(), 0, text);
    return same || page_to_doc_slow(d);
}

static BOOL page_has_formatting(const Doc *d)
{
    int i, para = 0;
    for (i = 0; i < d->len; i++) {
        if (d->style[i] && d->text[i] != '\r')
            return TRUE;
        if (d->text[i] == '\r')
            para++;
    }
    for (i = 0; i <= para; i++)
        if (doc_align(d, i) != AL_LEFT)
            return TRUE;
    return FALSE;
}

/* Putting text on the page */

typedef struct {
    const BYTE *data;
    DWORD size, pos;
} StreamIn;

static DWORD CALLBACK stream_in(DWORD_PTR cookie, LPBYTE buf, LONG want, LONG *got)
{
    StreamIn *s = (StreamIn *)cookie;
    DWORD n = s->size - s->pos;
    if (n > (DWORD)want)
        n = want;
    CopyMemory(buf, s->data + s->pos, n);
    s->pos += n;
    *got = n;
    return 0;
}

static void stream_into_page(const BYTE *data, DWORD size, UINT format)
{
    StreamIn s = {data, size, 0};
    EDITSTREAM es = {(DWORD_PTR)&s, 0, stream_in};
    SendMessageW(g_edit, EM_STREAMIN, format, (LPARAM)&es);
}

static void reset_page(void)
{
    SendMessageW(g_edit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    set_sel(0, 0);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    g_last_caret_line = -1;
}

/* Wine's RichEdit drops the alignment of a last paragraph with no \par after it, which
   is how Amber Writer and Word end their files; this takes it from the file instead. */
static void fix_last_align(const BYTE *rtf, DWORD size)
{
    Doc d;
    WCHAR *text;
    int len = get_text(&text), i, para = 0;
    BOOL same;
    if (!text)
        return;
    if (rtf_to_doc(rtf, size, &d)) {
        if (d.len == len + 1 && d.text[len] == '\r')
            d.len = len;
        same = d.len == len;
        for (i = 0; same && i < len; i++) {
            if (text[i] == 0x0B)
                text[i] = '\r';
            if (d.text[i] != text[i])
                same = FALSE;
            else if (text[i] == '\r')
                para++;
        }
        if (same) {
            for (i = len; i > 0 && text[i - 1] != '\r'; i--)
                ;
            set_sel(i, i);
            if (sel_align() != doc_align(&d, para))
                set_sel_align(doc_align(&d, para));
        }
        doc_free(&d);
    }
    HeapFree(GetProcessHeap(), 0, text);
}

static void doc_to_page(const Doc *d)
{
    int pos = 0;
    BOOL was_busy = g_busy;
    g_busy = TRUE;
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    stream_into_page((const BYTE *)d->text, d->len * sizeof(WCHAR), SF_TEXT | SF_UNICODE);
    apply_page_format();
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    {
        /* Start from plain text: RichEdit's own default can be the bold System font. */
        CHARFORMAT2W plain;
        ZeroMemory(&plain, sizeof plain);
        plain.cbSize = sizeof plain;
        plain.dwMask = STYLE_MASK | CFM_UNDERLINETYPE;
        plain.bUnderlineType = CFU_UNDERLINENONE;
        SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&plain);
        SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&plain);
    }
    while (pos < d->len) {
        int end = pos;
        BYTE style = d->style[pos];
        while (end < d->len && d->style[end] == style)
            end++;
        if (style) {
            CHARFORMAT2W cf;
            ZeroMemory(&cf, sizeof cf);
            cf.cbSize = sizeof cf;
            cf.dwMask = STYLE_MASK;
            if (style & ST_BOLD)
                cf.dwEffects |= CFE_BOLD;
            if (style & ST_ITALIC)
                cf.dwEffects |= CFE_ITALIC;
            if (style & ST_UNDERLINE)
                cf.dwEffects |= CFE_UNDERLINE;
            if (style & ST_STRIKE)
                cf.dwEffects |= CFE_STRIKEOUT;
            set_sel(pos, end);
            SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        }
        pos = end;
    }
    {
        int para = 0;
        for (pos = 0; pos <= d->len; pos++) {
            if (pos == 0 || d->text[pos - 1] == '\r') {
                BYTE align = doc_align(d, para++);
                if (align != AL_LEFT) {
                    set_sel(pos, pos);
                    set_sel_align(align);
                }
            }
        }
    }
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_edit, NULL, TRUE);
    g_busy = was_busy;
    reset_page();
}

static void clear_page(void)
{
    Doc empty;
    doc_init(&empty);
    doc_append(&empty, L"", 0, 0);
    doc_to_page(&empty);
    doc_free(&empty);
}

/* Status line */

static void flash(const WCHAR *text, UINT ms)
{
    lstrcpynW(g_message, text, sizeof g_message / sizeof g_message[0]);
    SetTimer(g_main, TIMER_MESSAGE, ms, NULL);
    update_status();
}

static void update_title(void)
{
    WCHAR title[MAX_PATH + 64];
    BOOL modified = (BOOL)SendMessageW(g_edit, EM_GETMODIFY, 0, 0);
    wsprintfW(title, L"%s%s - Amber Writer", g_path[0] ? file_name(g_path) : L"Untitled", modified ? L" *" : L"");
    SetWindowTextW(g_main, title);
}

static BOOL being_typed(LONG start, LONG end, CHARRANGE sel)
{
    return g_typing && sel.cpMin == sel.cpMax && sel.cpMax >= start && sel.cpMax <= end;
}

static void update_counts(void)
{
    WCHAR *text, words_s[16], chars_s[16];
    int len = get_text(&text), words = 0, chars = 0, pos = 0, start, end, lines, pages, i;
    CHARRANGE sel = get_sel();
    g_typos = 0;
    if (text) {
        while (next_word(text, len, pos, &start, &end)) {
            words++;
            if (!spell_is_correct(text + start, end - start) && !being_typed(start, end, sel))
                g_typos++;
            pos = end;
        }
        for (i = 0; i < len; i++)
            if (text[i] != '\r' && text[i] != '\n')
                chars++;
        HeapFree(GetProcessHeap(), 0, text);
    }
    lines = (int)SendMessageW(g_edit, EM_GETLINECOUNT, 0, 0);
    if (lines < 1)
        lines = 1;
    pages = g_pages = (lines - 1) / LINES_PER_PAGE + 1;
    fmt_thousands(words_s, words);
    fmt_thousands(chars_s, chars);
    wsprintfW(g_counts, L"Words %s  Chars %s  Pages %d", words_s, chars_s, pages);
    update_status();
}

static RECT status_rect(void)
{
    RECT r;
    GetClientRect(g_main, &r);
    r.top = g_status_y;
    r.bottom = g_status_y + g_status_h;
    return r;
}

/* The right of the status line. Level 0 has everything; 1 leaves out the word,
   character and page counts; 2 is just the typo count. */
static void right_text(WCHAR *out, int level)
{
    CHARRANGE sel = get_sel();
    LONG caret = sel.cpMax, line = (LONG)SendMessageW(g_edit, EM_EXLINEFROMCHAR, 0, caret);
    LONG first = (LONG)SendMessageW(g_edit, EM_LINEINDEX, line, 0), column = caret - first;
    int page = line / LINES_PER_PAGE + 1, on_page = line % LINES_PER_PAGE;
    int ln = 100 + (on_page * 100 + LINES_PER_INCH / 2) / LINES_PER_INCH;
    int pos;
    POINTL start = {0, 0};
    WCHAR typos[24];
    /* Centered and flush right lines start part way across the page. */
    SendMessageW(g_edit, EM_POSFROMCHAR, (WPARAM)&start, first);
    if (start.x > 0)
        column += (start.x + g_cw / 2) / g_cw;
    pos = CHARS_PER_INCH + column;
    fmt_thousands(typos, g_typos);
    if (level >= 2)
        wsprintfW(out, L"Typos %s", typos);
    else
        wsprintfW(out, L"%s%sTypos %s   Pg %d Ln %d.%02d\" Pos %d.%d\"", level == 0 ? g_counts : L"",
                  level == 0 ? L"  " : L"", typos, page, ln / 100, ln % 100, pos / 10, pos % 10);
}

static void update_status(void)
{
    RECT r = status_rect();
    InvalidateRect(g_main, &r, TRUE);
}

static void paint_main(HDC dc)
{
    RECT client, r;
    WCHAR right[256];
    SIZE size;
    HGDIOBJ old = SelectObject(dc, g_font);
    GetClientRect(g_main, &client);
    SetBkColor(dc, BLACK);
    SetTextColor(dc, AMBER);

    if (g_help) {
        RECT box = {0, 0, 0, 0};
        HFONT small = NULL;
        int size = g_shown_size;
        DrawTextW(dc, HELP_TEXT, -1, &box, DT_CALCRECT | DT_NOPREFIX | DT_EXPANDTABS);
        /* Smaller text when the help would not fit, as on a 1024x768 screen. */
        while ((box.bottom > g_status_y - 8 || box.right > client.right - 16) && size > 7) {
            int cw, ch, ascent;
            if (small)
                DeleteObject(small);
            small = make_font(--size, &cw, &ch, &ascent);
            SelectObject(dc, small);
            box.right = box.bottom = 0;
            DrawTextW(dc, HELP_TEXT, -1, &box, DT_CALCRECT | DT_NOPREFIX | DT_EXPANDTABS);
        }
        r.left = (client.right - box.right) / 2;
        r.top = (g_status_y - box.bottom) / 2;
        if (r.left < 8)
            r.left = 8;
        if (r.top < 8)
            r.top = 8;
        r.right = r.left + box.right;
        r.bottom = r.top + box.bottom;
        DrawTextW(dc, HELP_TEXT, -1, &r, DT_NOPREFIX | DT_EXPANDTABS);
        SelectObject(dc, g_font);
        if (small)
            DeleteObject(small);
    }

    /* A prompt gets the whole status line, as in WordPerfect. */
    if (g_prompt_kind != P_NONE) {
        right[0] = 0;
    } else {
        const WCHAR *note = g_message[0] ? g_message : g_hint;
        int level = 0;
        SIZE need;
        GetTextExtentPoint32W(dc, note, lstrlenW(note), &need);
        /* On a narrow screen a message pushes the counts aside until it goes. */
        for (;;) {
            right_text(right, level);
            GetTextExtentPoint32W(dc, right, lstrlenW(right), &size);
            if (!note[0] || level == 2 || need.cx <= client.right - 48 - size.cx - g_cw * 2)
                break;
            level++;
        }
    }
    GetTextExtentPoint32W(dc, right, lstrlenW(right), &size);
    r = status_rect();
    r.right -= 24;
    DrawTextW(dc, right, -1, &r, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    r.left = 24;
    r.right = client.right - 24 - size.cx - g_cw * 2;
    if (g_prompt_kind != P_NONE) {
        DrawTextW(dc, g_prompt_label, -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    } else {
        const WCHAR *left = g_message[0] ? g_message : g_hint[0] ? g_hint : g_path;
        WCHAR both[MAX_PATH + 32];
        if (left == g_path && g_typeover) {
            wsprintfW(both, L"Typeover  %s", g_path);
            left = both;
        }
        if (left == g_path) {
            /* Show just the file name when the whole path does not fit. */
            SIZE need;
            GetTextExtentPoint32W(dc, left, lstrlenW(left), &need);
            if (need.cx > r.right - r.left)
                left = file_name(g_path);
        }
        DrawTextW(dc, left, -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (g_hints && (left == g_path || left == file_name(g_path))) {
            /* A faint reminder of where the commands are, as long as it fits. */
            static const WCHAR *hints[] = {L"Tap Alt for the menu, hold Ctrl or Alt for keys, F3 help",
                                           L"Alt: menu  Hold Ctrl/Alt: keys", L"Alt: menu"};
            SIZE used, need;
            int i;
            GetTextExtentPoint32W(dc, left, lstrlenW(left), &used);
            if (used.cx)
                used.cx += g_cw * 3;
            for (i = 0; i < 3; i++) {
                GetTextExtentPoint32W(dc, hints[i], lstrlenW(hints[i]), &need);
                if (r.left + used.cx + need.cx <= r.right) {
                    RECT h = r;
                    h.left += used.cx;
                    SetTextColor(dc, DIM_AMBER);
                    DrawTextW(dc, hints[i], -1, &h, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                    SetTextColor(dc, AMBER);
                    break;
                }
            }
        }
    }
    SelectObject(dc, old);
}

/* Prompts */

static void place_prompt(void)
{
    HDC dc;
    SIZE size;
    RECT client;
    HGDIOBJ old;
    int x, w;
    if (g_prompt_kind == P_NONE)
        return;
    dc = GetDC(g_main);
    old = SelectObject(dc, g_font);
    GetTextExtentPoint32W(dc, g_prompt_label, lstrlenW(g_prompt_label), &size);
    x = 24 + size.cx;
    SelectObject(dc, old);
    ReleaseDC(g_main, dc);
    GetClientRect(g_main, &client);
    w = client.right - 24 - x;
    if (g_prompt_choices[0])
        w = g_cw * 2;
    if (w < g_cw * 4)
        w = g_cw * 4;
    MoveWindow(g_prompt, x, g_status_y + 2, w, g_ch, TRUE);
}

static void ask(int kind, const WCHAR *label, const WCHAR *initial, const char *choices)
{
    KillTimer(g_main, TIMER_MESSAGE);
    g_message[0] = 0;
    g_prompt_kind = kind;
    lstrcpynW(g_prompt_label, label, sizeof g_prompt_label / sizeof g_prompt_label[0]);
    lstrcpynA(g_prompt_choices, choices ? choices : "", sizeof g_prompt_choices);
    SendMessageW(g_prompt, EM_SETREADONLY, choices != NULL, 0);
    SetWindowTextW(g_prompt, initial ? initial : L"");
    place_prompt();
    ShowWindow(g_prompt, SW_SHOW);
    SetFocus(g_prompt);
    /* As in WordPerfect, typing replaces the suggested name; Enter keeps it. */
    SendMessageW(g_prompt, EM_SETSEL, 0, -1);
    update_status();
}

static void ask_yes_no(int kind, const WCHAR *question, BOOL default_yes)
{
    WCHAR label[MAX_PATH + 64];
    wsprintfW(label, L"%s %s: ", question, default_yes ? L"(Yes) No" : L"Yes (No)");
    g_prompt_default_yes = default_yes;
    ask(kind, label, NULL, "yn\r");
}

static void end_prompt(void)
{
    g_prompt_kind = P_NONE;
    g_prompt_choices[0] = 0;
    ShowWindow(g_prompt, SW_HIDE);
    SetFocus(g_help ? g_main : g_edit);
    update_status();
}

/* Files */

/* A name without a folder goes in My Documents; one without an extension gets .rtf.
   When retrieving, an existing .rtf, .docx or .txt of that name is found. */
static void resolve(const WCHAR *typed, WCHAR *out, BOOL retrieving)
{
    WCHAR name[MAX_PATH], expanded[MAX_PATH];
    lstrcpynW(name, typed, MAX_PATH);
    trim(name);
    if (!ExpandEnvironmentStringsW(name, expanded, MAX_PATH))
        lstrcpyW(expanded, name);
    if ((expanded[0] && expanded[1] == ':') || expanded[0] == '\\' || expanded[0] == '/') {
        lstrcpynW(out, expanded, MAX_PATH);
    } else {
        lstrcpynW(out, g_docs, MAX_PATH);
        if (lstrlenW(out) + 1 + lstrlenW(expanded) < MAX_PATH) {
            lstrcatW(out, L"\\");
            lstrcatW(out, expanded);
        }
    }
    if (!*extension(out) && lstrlenW(out) + 5 < MAX_PATH) {
        static const WCHAR *exts[] = {L".rtf", L".docx", L".txt"};
        int base = lstrlenW(out), i;
        if (retrieving) {
            for (i = 0; i < 3; i++) {
                lstrcpyW(out + base, exts[i]);
                if (file_exists(out))
                    return;
            }
        }
        lstrcpyW(out + base, L".rtf");
    }
}

static void quit(void)
{
    g_quitting = TRUE;
    DestroyWindow(g_main);
}

static void ask_retrieve_name(void)
{
    ask(P_RETRIEVE, L"Document to be retrieved: ", NULL, NULL);
}

static void run_then(int then)
{
    if (then == THEN_ASK_RETRIEVE)
        ask_retrieve_name();
    else if (then == THEN_QUIT)
        quit();
}

static void create_parent_dirs(const WCHAR *path)
{
    WCHAR dir[MAX_PATH];
    int i;
    lstrcpynW(dir, path, MAX_PATH);
    for (i = 3; dir[i]; i++) {
        if (dir[i] == '\\' || dir[i] == '/') {
            WCHAR c = dir[i];
            dir[i] = 0;
            CreateDirectoryW(dir, NULL);
            dir[i] = c;
        }
    }
}

static void write_document(const WCHAR *path, int then)
{
    Doc d;
    BOOL ok, formatted;
    WCHAR msg[MAX_PATH + 160], err[200];
    if (!page_to_doc(&d)) {
        flash(L"ERROR: not enough memory to save", 6000);
        return;
    }
    formatted = page_has_formatting(&d);
    create_parent_dirs(path);
    if (has_ext(path, L".txt"))
        ok = save_txt(path, &d);
    else if (has_ext(path, L".docx"))
        ok = save_docx(path, &d);
    else
        ok = save_rtf(path, &d);
    doc_free(&d);
    if (!ok) {
        error_text(GetLastError(), err, 200);
        wsprintfW(msg, L"ERROR: can't save %s: %s", path, err);
        flash(msg, 6000);
        return;
    }
    lstrcpynW(g_path, path, MAX_PATH);
    SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    update_title();
    if (has_ext(path, L".txt") && formatted)
        wsprintfW(msg, L"Saved %s as plain text, without bold, italics or centering. Use .rtf to keep them.", file_name(path));
    else
        wsprintfW(msg, L"Saved %s", path);
    flash(msg, has_ext(path, L".txt") && formatted ? 7000 : 3500);
    run_then(then);
}

static void save_as(int then)
{
    g_prompt_then = then;
    ask(P_SAVE_AS, L"Document to be saved: ", g_path, NULL);
}

static void save_then(int then)
{
    if (g_path[0])
        write_document(g_path, then);
    else
        save_as(then);
}

static BOOL looks_like_rtf(const BYTE *p, DWORD n)
{
    return n >= 5 && p[0] == '{' && p[1] == '\\' && p[2] == 'r' && p[3] == 't' && p[4] == 'f';
}

static void retrieve_path(const WCHAR *path)
{
    WCHAR msg[MAX_PATH + 160], err[200];
    BOOL exists = file_exists(path);
    if (exists) {
        DWORD size;
        BYTE *data = read_file(path, &size);
        Doc d;
        if (!data) {
            error_text(GetLastError(), err, 200);
            wsprintfW(msg, L"ERROR: can't open %s: %s", path, err);
            flash(msg, 6000);
            return;
        }
        if (looks_like_rtf(data, size)) {
            g_busy = TRUE;
            SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
            stream_into_page(data, size, SF_RTF);
            apply_page_format();
            fix_last_align(data, size);
            g_busy = FALSE;
            reset_page();
        } else if (has_ext(path, L".docx")) {
            if (!docx_from_zip(data, size, &d)) {
                HeapFree(GetProcessHeap(), 0, data);
                wsprintfW(msg, L"ERROR: can't open %s: not a Word document Amber Writer can read", path);
                flash(msg, 6000);
                return;
            }
            doc_to_page(&d);
            doc_free(&d);
        } else {
            HeapFree(GetProcessHeap(), 0, data);
            data = NULL;
            if (!load_txt(path, &d)) {
                error_text(GetLastError(), err, 200);
                wsprintfW(msg, L"ERROR: can't open %s: %s", path, err);
                flash(msg, 6000);
                return;
            }
            doc_to_page(&d);
            doc_free(&d);
        }
        if (data)
            HeapFree(GetProcessHeap(), 0, data);
    } else {
        clear_page();
    }
    lstrcpynW(g_path, path, MAX_PATH);
    update_title();
    update_counts();
    wsprintfW(msg, exists ? L"Retrieved %s" : L"New document %s", path);
    flash(msg, 3500);
}

static void retrieve(void)
{
    if (SendMessageW(g_edit, EM_GETMODIFY, 0, 0))
        ask_yes_no(P_SAVE_BEFORE_RETRIEVE, L"Save current document?", TRUE);
    else
        ask_retrieve_name();
}

static void exit_app(void)
{
    if (SendMessageW(g_edit, EM_GETMODIFY, 0, 0))
        ask_yes_no(P_SAVE_BEFORE_EXIT, L"Save document?", TRUE);
    else
        ask_yes_no(P_EXIT, L"Exit Amber Writer?", FALSE);
}

/* Printing */

/* Amber Writer lays out the printed page itself, the way WordPerfect printed: black
   Courier New 12 point, ten characters an inch, so the 65 columns are 6.5 inches, at six
   lines an inch, breaking lines where the screen does. */

static HGLOBAL g_devmode, g_devnames; /* the printer and settings chosen last time */

typedef struct {
    HDC dc;
    HFONT fonts[16]; /* by style */
    int dpi_x, dpi_y, left, top, width;
} Printer;

static HFONT printer_font(Printer *p, BYTE style)
{
    style &= 15;
    if (!p->fonts[style])
        p->fonts[style] = CreateFontW(-MulDiv(12, p->dpi_y, 72), 0, 0, 0, style & ST_BOLD ? FW_BOLD : FW_NORMAL,
                                      (style & ST_ITALIC) != 0, (style & ST_UNDERLINE) != 0, (style & ST_STRIKE) != 0,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                      FIXED_PITCH | FF_MODERN, L"Courier New");
    return p->fonts[style];
}

/* Where a column starts, in printer dots from the left of the text. */
static int column_x(const Printer *p, int col)
{
    return MulDiv(col, p->dpi_x, CHARS_PER_INCH);
}

/* Prints text[from, to) as one line. Each character goes in its own column, so bold
   and italics line up with the rest. */
static void print_line(Printer *p, const Doc *d, int from, int to, int width, BYTE align, int line)
{
    int x = p->left, y = p->top + MulDiv(line * 4 + 3, p->dpi_y, LINES_PER_INCH * 4); /* the baseline */
    int col = 0, i = from;
    if (align == AL_CENTER)
        x += (p->width - column_x(p, width)) / 2;
    else if (align == AL_RIGHT)
        x += p->width - column_x(p, width);
    while (i < to) {
        int dx[256], n = 0, start = col;
        BYTE style = d->style[i];
        if (d->text[i] == '\t') {
            col = (col / TAB_COLUMNS + 1) * TAB_COLUMNS;
            i++;
            continue;
        }
        while (i + n < to && n < 256 && d->text[i + n] != '\t' && d->style[i + n] == style) {
            int before = col;
            col += char_columns(d->text[i + n]);
            dx[n++] = column_x(p, col) - column_x(p, before);
        }
        SelectObject(p->dc, printer_font(p, style));
        ExtTextOutW(p->dc, x + column_x(p, start), y, 0, NULL, d->text + i, n, dx);
        i += n;
    }
}

static void print_document(void)
{
    PRINTDLGW pd;
    DOCINFOW di;
    Printer p;
    CHARRANGE sel = get_sel();
    GETTEXTLENGTHEX gl = {GTL_NUMCHARS | GTL_PRECISE, 1200};
    Doc d;
    HCURSOR cursor;
    HGDIOBJ old_font;
    WCHAR msg[MAX_PATH + 100], err[200];
    int page_w, page_h, off_x, off_y, lines, from = 1, to = 0xFFFF, printed = 0, copy, copies, i;
    int start = 0, end;
    BOOL ok;

    if (SendMessageW(g_edit, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0) == 0) {
        flash(L"Nothing to print yet", 3500);
        return;
    }
    ZeroMemory(&pd, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = g_main;
    pd.hDevMode = g_devmode;
    pd.hDevNames = g_devnames;
    pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | (sel.cpMin == sel.cpMax ? PD_NOSELECTION : 0);
    pd.nCopies = 1;
    pd.nFromPage = 1;
    pd.nToPage = (WORD)(g_pages < 0xFFFF ? g_pages : 0xFFFF);
    pd.nMinPage = 1;
    pd.nMaxPage = 0xFFFF;
    ok = PrintDlgW(&pd);
    g_devmode = pd.hDevMode;
    g_devnames = pd.hDevNames;
    SetFocus(g_edit);
    if (!ok) {
        DWORD code = CommDlgExtendedError();
        if (code == PDERR_NODEFAULTPRN) {
            flash(L"No printer is set up. Add one in the Printers part of Control Panel.", 7000);
        } else if (code) {
            wsprintfW(msg, L"ERROR: can't print (print dialog error %lu)", code);
            flash(msg, 6000);
        }
        return;
    }
    if (pd.Flags & PD_PAGENUMS) {
        from = pd.nFromPage;
        to = pd.nToPage;
    }
    copies = pd.nCopies > 0 ? pd.nCopies : 1; /* more than 1 only if the printer can't make copies itself */
    if (!page_to_doc(&d)) {
        DeleteDC(pd.hDC);
        flash(L"ERROR: not enough memory to print", 6000);
        return;
    }
    end = d.len;
    if (pd.Flags & PD_SELECTION) {
        start = sel.cpMin < end ? sel.cpMin : end;
        end = sel.cpMax < end ? sel.cpMax : end;
    }

    /* The printer's dots start where it can print, a little in from the edge of the
       paper. 6.5 inches of text leave 1 inch margins on letter paper and a little less
       on A4. */
    ZeroMemory(&p, sizeof p);
    p.dc = pd.hDC;
    p.dpi_x = GetDeviceCaps(p.dc, LOGPIXELSX);
    p.dpi_y = GetDeviceCaps(p.dc, LOGPIXELSY);
    page_w = GetDeviceCaps(p.dc, PHYSICALWIDTH);
    page_h = GetDeviceCaps(p.dc, PHYSICALHEIGHT);
    off_x = GetDeviceCaps(p.dc, PHYSICALOFFSETX);
    off_y = GetDeviceCaps(p.dc, PHYSICALOFFSETY);
    if (page_w <= 0 || page_h <= 0) { /* not a real printer: use letter paper */
        page_w = p.dpi_x * 17 / 2;
        page_h = p.dpi_y * 11;
        off_x = off_y = 0;
    }
    p.width = column_x(&p, COLUMNS);
    p.left = (page_w - p.width) / 2;
    if (p.left > p.dpi_x)
        p.left = p.dpi_x;
    p.left = p.left > off_x ? p.left - off_x : 0;
    p.top = p.dpi_y > off_y ? p.dpi_y - off_y : 0;
    lines = MulDiv(page_h - 2 * p.dpi_y, LINES_PER_INCH, p.dpi_y);
    if (lines < 1)
        lines = 1;

    flash(L"Printing...", 60000);
    UpdateWindow(g_main);
    cursor = SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    ZeroMemory(&di, sizeof di);
    di.cbSize = sizeof di;
    di.lpszDocName = g_path[0] ? file_name(g_path) : L"Untitled";
    old_font = SelectObject(p.dc, GetStockObject(SYSTEM_FONT));
    ok = StartDocW(p.dc, &di) > 0;
    for (copy = 0; ok && copy < copies; copy++) {
        int pos = start, para = 0, page = 1, line = 0;
        for (i = 0; i < start; i++)
            if (d.text[i] == '\r')
                para++;
        while (ok && pos < end && page <= to) {
            BOOL render = page >= from;
            int line_end, width, next;
            if (line == 0 && render) {
                ok = StartPage(p.dc) > 0;
                SetTextColor(p.dc, RGB(0, 0, 0));
                SetBkMode(p.dc, TRANSPARENT);
                SetTextAlign(p.dc, TA_BASELINE | TA_LEFT | TA_NOUPDATECP);
            }
            wrap_line(d.text, pos, end, COLUMNS, TAB_COLUMNS, &line_end, &width, &next);
            if (render && ok)
                print_line(&p, &d, pos, line_end, width, doc_align(&d, para), line);
            if (next > line_end && d.text[line_end] == '\r')
                para++;
            pos = next;
            if (++line == lines || pos >= end) {
                if (render && ok) {
                    ok = EndPage(p.dc) > 0;
                    printed++;
                }
                line = 0;
                page++;
            }
        }
    }
    if (ok)
        ok = EndDoc(p.dc) > 0;
    if (!ok) {
        error_text(GetLastError(), err, 200);
        AbortDoc(p.dc);
    }
    SelectObject(p.dc, old_font);
    for (i = 0; i < 16; i++)
        if (p.fonts[i])
            DeleteObject(p.fonts[i]);
    DeleteDC(p.dc);
    doc_free(&d);
    SetCursor(cursor);

    if (ok && printed == 0)
        lstrcpyW(msg, L"Nothing printed: there are no pages in that range");
    else if (ok)
        wsprintfW(msg, L"Printed %d page%s", printed, printed == 1 ? L"" : L"s");
    else
        wsprintfW(msg, L"Printing stopped: %s", err);
    flash(msg, ok && printed ? 3500 : 6000);
}

/* Spelling */

/* The word that touches pos, as Alt+A sees it. */
static BOOL word_at(LONG pos, LONG *start, LONG *end, WCHAR *word)
{
    WCHAR buf[2 * MAX_WORD + 2];
    LONG from = pos > MAX_WORD ? pos - MAX_WORD : 0;
    int n = get_range(from, pos + MAX_WORD, buf), at = 0, s, e;
    while (next_word(buf, n, at, &s, &e)) {
        if (from + s <= pos && pos <= from + e) {
            if (e - s >= MAX_WORD)
                return FALSE;
            *start = from + s;
            *end = from + e;
            CopyMemory(word, buf + s, (e - s) * sizeof(WCHAR));
            word[e - s] = 0;
            return TRUE;
        }
        at = e;
    }
    return FALSE;
}

/* The word that ends exactly at pos, or an empty string. */
static int word_ending_at(LONG pos, WCHAR *word)
{
    WCHAR buf[MAX_WORD + 2];
    LONG from = pos > MAX_WORD ? pos - MAX_WORD : 0;
    int n = get_range(from, pos, buf), at = 0, s, e;
    while (next_word(buf, n, at, &s, &e)) {
        if (e == n && !(s == 0 && from > 0)) {
            CopyMemory(word, buf + s, (e - s) * sizeof(WCHAR));
            word[e - s] = 0;
            return e - s;
        }
        at = e;
    }
    word[0] = 0;
    return 0;
}

static void hint_list(const WCHAR *word, int len, int limit, WCHAR *out, BOOL numbered)
{
    char hints[5][MAX_WORD];
    int n = spell_suggest(word, len, hints, limit), i;
    out[0] = 0;
    for (i = 0; i < n; i++) {
        WCHAR w[MAX_WORD + 8];
        if (numbered)
            wsprintfW(w, L"%d.%S  ", i + 1, hints[i]);
        else
            wsprintfW(w, i ? L", %S" : L"%S", hints[i]);
        lstrcatW(out, w);
    }
    if (numbered) {
        g_spell_hint_count = n;
        CopyMemory(g_spell_hints, hints, sizeof hints);
    }
}

static void misspelled(const WCHAR *word, int len)
{
    WCHAR hints[5 * (MAX_WORD + 4)], msg[64 + 6 * (MAX_WORD + 4)];
    if (g_beep)
        beep_play();
    hint_list(word, len, 3, hints, FALSE);
    if (hints[0])
        wsprintfW(msg, L"Not found: %s  (%s)", word, hints);
    else
        wsprintfW(msg, L"Not found: %s", word);
    flash(msg, 3500);
}

static void spell_check(void)
{
    WCHAR *text;
    int len = get_text(&text), pos, s, e;
    CHARRANGE sel = get_sel();
    if (!text)
        return;
    pos = sel.cpMax;
    /* Back up to the start of a word the cursor is inside. */
    while (pos > 0 && pos <= len && (is_word_char(text[pos - 1]) || text[pos - 1] == '\''))
        pos--;
    while (next_word(text, len, pos, &s, &e)) {
        pos = e;
        if (e <= sel.cpMax)
            continue;
        if (!spell_is_correct(text + s, e - s)) {
            WCHAR word[MAX_WORD], hints[5 * (MAX_WORD + 8)], label[64 + 6 * (MAX_WORD + 8)];
            int n = e - s < MAX_WORD ? e - s : MAX_WORD - 1;
            char choices[16] = "as";
            int i;
            CopyMemory(word, text + s, n * sizeof(WCHAR));
            word[n] = 0;
            HeapFree(GetProcessHeap(), 0, text);
            g_spell_start = s;
            g_spell_end = e;
            set_sel(s, e);
            SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
            hint_list(word, n, 5, hints, TRUE);
            for (i = 0; i < g_spell_hint_count; i++)
                choices[2 + i] = (char)('1' + i);
            choices[2 + g_spell_hint_count] = 0;
            wsprintfW(label, L"Not found: %s   %sA.Add  S.Skip  Esc.Stop ", word, hints);
            ask(P_SPELL, label, NULL, choices);
            return;
        }
    }
    HeapFree(GetProcessHeap(), 0, text);
    flash(L"Spell check done", 3500);
}

static void spell_answer(char key)
{
    WCHAR word[MAX_WORD];
    int n = get_range(g_spell_start, g_spell_end, word);
    if (key == 'a') {
        spell_add(word, n);
        InvalidateRect(g_edit, NULL, TRUE);
        SetTimer(g_main, TIMER_COUNTS, 250, NULL);
    } else if (key >= '1' && key <= '5') {
        WCHAR replacement[MAX_WORD];
        int i = key - '1';
        MultiByteToWideChar(CP_ACP, 0, g_spell_hints[i], -1, replacement, MAX_WORD);
        if (word[0] >= 'A' && word[0] <= 'Z' && replacement[0] >= 'a' && replacement[0] <= 'z')
            replacement[0] -= 32;
        set_sel(g_spell_start, g_spell_end);
        SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)replacement);
    } else {
        set_sel(g_spell_end, g_spell_end);
    }
    spell_check();
}

static void add_word(void)
{
    LONG start, end;
    WCHAR word[MAX_WORD], msg[MAX_WORD + 64];
    CHARRANGE sel = get_sel();
    int i;
    if (!word_at(sel.cpMax, &start, &end, word)) {
        flash(L"No word at the cursor", 3500);
        return;
    }
    spell_add(word, lstrlenW(word));
    InvalidateRect(g_edit, NULL, TRUE);
    update_counts();
    update_hint();
    for (i = 0; word[i]; i++)
        if (word[i] >= 'A' && word[i] <= 'Z')
            word[i] += 32;
    wsprintfW(msg, L"Added %s to your dictionary", word);
    flash(msg, 3500);
}

static void toggle_beep(void)
{
    g_beep = !g_beep;
    setting_set(L"Beep", g_beep);
    flash(g_beep ? L"Beep on" : L"Beep off", 3500);
    if (g_beep)
        beep_play();
}

/* Shows the suggestions on the status line whenever the cursor is on a typo. */
static void update_hint(void)
{
    static WCHAR last_word[MAX_WORD], last_hint[256];
    CHARRANGE sel = get_sel();
    LONG start, end;
    WCHAR word[MAX_WORD];
    WCHAR before[256];
    lstrcpyW(before, g_hint);
    g_hint[0] = 0;
    if (sel.cpMin == sel.cpMax && word_at(sel.cpMax, &start, &end, word) && !being_typed(start, end, sel) &&
        !spell_is_correct(word, lstrlenW(word))) {
        if (lstrcmpW(word, last_word) != 0) {
            WCHAR hints[5 * (MAX_WORD + 4)];
            hint_list(word, lstrlenW(word), 3, hints, FALSE);
            /* Short, so it fits beside the counts on a 1024-wide screen. */
            if (hints[0])
                wsprintfW(last_hint, L"%s -> %s", word, hints);
            else
                wsprintfW(last_hint, L"%s: not in the dictionary", word);
            lstrcpyW(last_word, word);
        }
        lstrcpyW(g_hint, last_hint);
    }
    if (lstrcmpW(before, g_hint) != 0)
        update_status();
}

/* The cursor moved without typing: the word it was in is finished. */
static void stop_typing(void)
{
    if (!g_typing)
        return;
    g_typing = FALSE;
    SetTimer(g_main, TIMER_COUNTS, 250, NULL);
}

/* Alt+N and Alt+P: the cursor goes to the start of the next or previous typo. */
static void jump_typo(BOOL forward)
{
    WCHAR *text, msg[400], word[MAX_WORD], hints[5 * (MAX_WORD + 4)];
    int len, pos = 0, s, e, total = 0, first = -1, last = -1, target = -1, target_index = 0, wrapped = 0, n;
    CHARRANGE sel;
    LONG caret;
    stop_typing();
    sel = get_sel();
    caret = forward ? sel.cpMax : sel.cpMin;
    len = get_text(&text);
    if (!text)
        return;
    while (next_word(text, len, pos, &s, &e)) {
        pos = e;
        if (spell_is_correct(text + s, e - s))
            continue;
        total++;
        if (first < 0)
            first = s;
        last = s;
        if ((forward && target < 0 && s > caret) || (!forward && e < caret)) {
            target = s;
            target_index = total;
        }
    }
    if (total == 0) {
        HeapFree(GetProcessHeap(), 0, text);
        flash(L"No typos", 3000);
        return;
    }
    if (target < 0) {
        wrapped = 1;
        target = forward ? first : last;
        target_index = forward ? 1 : total;
    }
    next_word(text, len, target, &s, &e);
    n = e - s < MAX_WORD ? e - s : MAX_WORD - 1;
    CopyMemory(word, text + s, n * sizeof(WCHAR));
    word[n] = 0;
    HeapFree(GetProcessHeap(), 0, text);
    set_sel(s, s);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    hint_list(word, n, 3, hints, FALSE);
    wsprintfW(msg, L"Typo %d of %d%s: %s%s%s", target_index, total,
              wrapped && total > 1 ? (forward ? L" (from the top)" : L" (from the end)") : L"", word,
              hints[0] ? L" -> " : L"", hints);
    flash(msg, 4000);
}

/* Searching. Case does not matter. Replacing matches whole words only, so changing
   the name Ann leaves Anne and planning alone. */

static BOOL boundary_ok(const WCHAR *text, int len, int at, int n, const WCHAR *needle)
{
    if (IsCharAlphaNumericW(needle[0]) && at > 0 && IsCharAlphaNumericW(text[at - 1]))
        return FALSE;
    if (IsCharAlphaNumericW(needle[n - 1]) && at + n < len && IsCharAlphaNumericW(text[at + n]))
        return FALSE;
    return TRUE;
}

/* Finds needle in text (both already lower case), going forward from or back from
   from. Returns the position, or -1. */
static int find_in(const WCHAR *text, int len, const WCHAR *needle, int from, BOOL back, BOOL whole)
{
    int n = lstrlenW(needle), i;
    if (n == 0 || n > len)
        return -1;
    if (back) {
        for (i = from > len - n ? len - n : from; i >= 0; i--)
            if (text[i] == needle[0] && CompareStringW(LOCALE_INVARIANT, 0, text + i, n, needle, n) == CSTR_EQUAL &&
                (!whole || boundary_ok(text, len, i, n, needle)))
                return i;
    } else {
        for (i = from < 0 ? 0 : from; i <= len - n; i++)
            if (text[i] == needle[0] && CompareStringW(LOCALE_INVARIANT, 0, text + i, n, needle, n) == CSTR_EQUAL &&
                (!whole || boundary_ok(text, len, i, n, needle)))
                return i;
    }
    return -1;
}

/* The page text in lower case, for searching. */
static int get_lower_text(WCHAR **out)
{
    int len = get_text(out);
    if (*out && len)
        CharLowerBuffW(*out, len);
    return len;
}

static void lower_copy(const WCHAR *in, WCHAR *out, int size)
{
    lstrcpynW(out, in, size);
    CharLowerBuffW(out, lstrlenW(out));
}

static void find_prompt(BOOL back)
{
    stop_typing();
    g_find_back = back;
    ask(P_FIND, back ? L"<- Srch: " : L"-> Srch: ", g_find, NULL);
}

static void do_find(void)
{
    WCHAR *text, needle[256], msg[128];
    int len = get_lower_text(&text), n, at, total = 0, index = 0, i, wrapped = 0;
    CHARRANGE sel = get_sel();
    if (!text)
        return;
    lower_copy(g_find, needle, 256);
    n = lstrlenW(needle);
    if (g_find_back) {
        at = find_in(text, len, needle, sel.cpMin - 1, TRUE, FALSE);
        if (at < 0 && (at = find_in(text, len, needle, len, TRUE, FALSE)) >= 0)
            wrapped = 1;
    } else {
        at = find_in(text, len, needle, sel.cpMin == sel.cpMax ? sel.cpMax : sel.cpMin + 1, FALSE, FALSE);
        if (at < 0 && (at = find_in(text, len, needle, 0, FALSE, FALSE)) >= 0)
            wrapped = 1;
    }
    if (at < 0) {
        HeapFree(GetProcessHeap(), 0, text);
        flash(L"* Not found *", 3000);
        return;
    }
    for (i = find_in(text, len, needle, 0, FALSE, FALSE); i >= 0; i = find_in(text, len, needle, i + 1, FALSE, FALSE)) {
        total++;
        if (i == at)
            index = total;
    }
    HeapFree(GetProcessHeap(), 0, text);
    set_sel(at, at + n);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    wsprintfW(msg, L"Found %d of %d%s", index, total,
              wrapped ? (g_find_back ? L"  - went round from the end" : L"  - went round from the top") : L"");
    flash(msg, 3500);
}

/* The replacement, capitalized like the text it replaces: john -> peter, John -> Peter,
   JOHN -> PETER. */
static void match_case(const WCHAR *found, int n, const WCHAR *with, WCHAR *out, int size)
{
    int i, letters = 0, upper = 0;
    lstrcpynW(out, with, size);
    for (i = 0; i < n; i++) {
        if (IsCharAlphaW(found[i])) {
            letters++;
            if (IsCharUpperW(found[i]))
                upper++;
        }
    }
    if (letters > 1 && upper == letters)
        CharUpperBuffW(out, lstrlenW(out));
    else if (n && IsCharUpperW(found[0]) && out[0] && IsCharLowerW(out[0]))
        CharUpperBuffW(out, 1);
}

static int replace_at(LONG start, int n)
{
    WCHAR found[256], with[256];
    get_range(start, start + n, found);
    match_case(found, n, g_swap_with, with, 256);
    set_sel(start, start + n);
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)with);
    return lstrlenW(with);
}

static void swap_start(void)
{
    stop_typing();
    ask_yes_no(P_SWAP_CONFIRM, L"Replace: confirm each one?", FALSE);
}

static void swap_finish(void)
{
    WCHAR msg[64];
    if (g_swap_done)
        wsprintfW(msg, L"Replaced %d", g_swap_done);
    else
        lstrcpyW(msg, L"* Not found *");
    update_counts();
    update_hint();
    flash(msg, 4000);
}

/* Replaces every whole-word match from from to the end, last first so the earlier
   positions stay put. */
static void swap_all_from(LONG from)
{
    WCHAR *text, needle[256];
    int len = get_lower_text(&text), n, i, count = 0, cap = 64;
    LONG *at, caret;
    CHARRANGE sel = get_sel();
    POINT scroll;
    if (!text)
        return;
    lower_copy(g_find, needle, 256);
    n = lstrlenW(needle);
    at = HeapAlloc(GetProcessHeap(), 0, cap * sizeof(LONG));
    for (i = find_in(text, len, needle, from, FALSE, TRUE); at && i >= 0; i = find_in(text, len, needle, i + n, FALSE, TRUE)) {
        if (count == cap) {
            LONG *grown = HeapReAlloc(GetProcessHeap(), 0, at, cap * 2 * sizeof(LONG));
            if (!grown)
                break;
            at = grown;
            cap *= 2;
        }
        at[count++] = i;
    }
    HeapFree(GetProcessHeap(), 0, text);
    if (!at)
        return;
    caret = sel.cpMin;
    g_busy = TRUE;
    SendMessageW(g_edit, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    for (i = count - 1; i >= 0; i--) {
        int added = replace_at(at[i], n);
        if (at[i] + n <= caret)
            caret += added - n;
    }
    set_sel(caret, caret);
    SendMessageW(g_edit, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_edit, NULL, TRUE);
    g_busy = FALSE;
    HeapFree(GetProcessHeap(), 0, at);
    g_swap_done += count;
    update_title();
    swap_finish();
}

/* With confirming: shows the next match and asks. */
static void swap_next(void)
{
    WCHAR *text, needle[256];
    int len = get_lower_text(&text), at;
    if (!text)
        return;
    lower_copy(g_find, needle, 256);
    at = find_in(text, len, needle, g_swap_pos, FALSE, TRUE);
    HeapFree(GetProcessHeap(), 0, text);
    if (at < 0) {
        swap_finish();
        return;
    }
    g_swap_start = at;
    set_sel(at, at + lstrlenW(needle));
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    g_prompt_default_yes = TRUE;
    ask(P_SWAP_EACH, L"Replace? (Yes) No  A.All the rest  Esc.Stop: ", NULL, "yna\r");
}

static void swap_answer(char key)
{
    int n = lstrlenW(g_find);
    if (key == 'y' || key == '\r') {
        int added = replace_at(g_swap_start, n);
        g_swap_done++;
        g_swap_pos = g_swap_start + added;
        swap_next();
    } else if (key == 'n') {
        g_swap_pos = g_swap_start + n;
        swap_next();
    } else if (key == 'a') {
        swap_all_from(g_swap_start);
    }
}

static void swap_go(void)
{
    g_swap_done = 0;
    if (g_swap_confirm) {
        g_swap_pos = 0;
        swap_next();
    } else {
        swap_all_from(0);
    }
}

/* Draws an amber wavy line under unknown words on screen, except the word being typed.
   This is painted over the RichEdit's own drawing, so it never gets into the document. */
static void draw_misspellings(HWND hwnd)
{
    RECT client;
    POINTL top_left, bottom_right;
    LONG first, last, from;
    WCHAR *buf;
    int n, at = 0, s, e;
    HDC dc;
    HPEN pen;
    HGDIOBJ old;
    CHARRANGE sel;
    if (g_busy)
        return;
    GetClientRect(hwnd, &client);
    top_left.x = 0;
    top_left.y = 0;
    bottom_right.x = client.right - 1;
    bottom_right.y = client.bottom - 1;
    first = (LONG)SendMessageW(hwnd, EM_CHARFROMPOS, 0, (LPARAM)&top_left);
    last = (LONG)SendMessageW(hwnd, EM_CHARFROMPOS, 0, (LPARAM)&bottom_right);
    from = first > MAX_WORD ? first - MAX_WORD : 0;
    last += MAX_WORD;
    if (last <= from)
        return;
    buf = HeapAlloc(GetProcessHeap(), 0, (last - from + 2) * sizeof(WCHAR));
    if (!buf)
        return;
    n = get_range(from, last, buf);
    sel = get_sel();
    HideCaret(hwnd);
    dc = GetDC(hwnd);
    pen = CreatePen(PS_SOLID, 1, AMBER);
    old = SelectObject(dc, pen);
    while (next_word(buf, n, at, &s, &e)) {
        at = e;
        if (being_typed(from + s, from + e, sel))
            continue;
        if (!spell_is_correct(buf + s, e - s)) {
            POINTL p1, p2;
            int x, y, x2, up;
            SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&p1, from + s);
            SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&p2, from + e - 1);
            if (p1.y != p2.y || p1.y < -g_ch || p1.y > client.bottom)
                continue;
            x2 = p2.x + g_cw;
            /* Just under the baseline, in the room the descenders use, whatever the line
               spacing of the Rich Edit version on this Windows. */
            y = p1.y + g_ascent + 1;
            if (y + 2 > p1.y + g_ch - 1)
                y = p1.y + g_ch - 3;
            MoveToEx(dc, p1.x, y + 2, NULL);
            for (x = p1.x, up = 1; x < x2; up = !up) {
                x += 2;
                if (x > x2)
                    x = x2;
                LineTo(dc, x, up ? y : y + 2);
            }
        }
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    ReleaseDC(hwnd, dc);
    ShowCaret(hwnd);
    HeapFree(GetProcessHeap(), 0, buf);
}

/* Repaints the line the caret is on, and the one it left, so the wavy lines follow it. */
static void refresh_caret_lines(void)
{
    CHARRANGE sel = get_sel();
    LONG line = (LONG)SendMessageW(g_edit, EM_EXLINEFROMCHAR, 0, sel.cpMax);
    LONG lines[2];
    RECT client;
    int i;
    lines[0] = line;
    lines[1] = g_last_caret_line;
    GetClientRect(g_edit, &client);
    for (i = 0; i < 2; i++) {
        POINTL p;
        LONG index;
        RECT r;
        if (lines[i] < 0)
            continue;
        index = (LONG)SendMessageW(g_edit, EM_LINEINDEX, lines[i], 0);
        if (index < 0)
            continue;
        SendMessageW(g_edit, EM_POSFROMCHAR, (WPARAM)&p, index);
        r.left = 0;
        r.right = client.right;
        r.top = p.y;
        r.bottom = p.y + g_ch;
        InvalidateRect(g_edit, &r, FALSE);
    }
    g_last_caret_line = line;
}

/* Bold, italics, underline and strikethrough on the selection, or on what is typed next. */
static void toggle_style(DWORD effect)
{
    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof cf);
    cf.cbSize = sizeof cf;
    SendMessageW(g_edit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    {
        DWORD mask = effect == CFE_BOLD ? CFM_BOLD : effect == CFE_ITALIC ? CFM_ITALIC
                   : effect == CFE_UNDERLINE ? CFM_UNDERLINE : CFM_STRIKEOUT;
        BOOL on = (cf.dwMask & mask) && (cf.dwEffects & effect);
        ZeroMemory(&cf, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = mask;
        cf.dwEffects = on ? 0 : effect;
        if (effect == CFE_UNDERLINE) {
            cf.dwMask |= CFM_UNDERLINETYPE;
            cf.bUnderlineType = on ? CFU_UNDERLINENONE : CFU_UNDERLINE;
        }
        SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
}

static void resize_text(int step)
{
    int size = g_font_size + step;
    if (size < 9)
        size = 9;
    if (size > 40)
        size = 40;
    if (size == g_font_size)
        return;
    g_font_size = size;
    setting_set(L"FontSize", size);
    layout();
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    if (g_shown_size < g_font_size)
        flash(L"Text is as big as fits 65 columns on this screen", 3500);
}

static void toggle_full_screen(void)
{
    if (!g_fullscreen) {
        MONITORINFO mi = {sizeof mi};
        g_placement.length = sizeof g_placement;
        GetWindowPlacement(g_main, &g_placement);
        GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(g_main, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPos(g_main, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_fullscreen = TRUE;
    } else {
        SetWindowLongW(g_main, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPlacement(g_main, &g_placement);
        SetWindowPos(g_main, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        g_fullscreen = FALSE;
    }
    layout();
}

static void toggle_help(void)
{
    g_help = !g_help;
    ShowWindow(g_edit, g_help ? SW_HIDE : SW_SHOW);
    if (g_prompt_kind == P_NONE)
        SetFocus(g_help ? g_main : g_edit);
    InvalidateRect(g_main, NULL, TRUE);
}

/* Commands, the pull-down menu and the key template.
 *
 * Every command has a key. Tapping Alt (or Alt+=) opens a WordPerfect 5.1 style menu
 * bar that works with the arrow keys, Enter and the underlined letters, or the mouse.
 * Holding Ctrl or Alt for a moment shows the keys that go with it across the top, like
 * the paper template that sat above the function keys. */

enum {
    C_NONE, C_SAVE, C_SAVE_AS, C_RETRIEVE, C_PRINT, C_EXIT,
    C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE, C_SELECT_ALL, C_TYPEOVER,
    C_FIND, C_FIND_BACK, C_REPLACE,
    C_BOLD, C_ITALIC, C_UNDERLINE, C_STRIKE,
    C_ALIGN_LEFT, C_ALIGN_CENTER, C_ALIGN_RIGHT,
    C_NEXT_TYPO, C_PREV_TYPO, C_SPELL, C_ADD_WORD, C_BEEP,
    C_BIGGER, C_SMALLER, C_FULL_SCREEN, C_HINTS,
    C_HELP, C_ABOUT
};

typedef struct {
    const WCHAR *label;
    const WCHAR *keys;
    int cmd;
} MenuItem;

typedef struct {
    const WCHAR *title;
    WCHAR letter;
    const MenuItem *items;
    int count;
} Menu;

static const MenuItem FILE_ITEMS[] = {
    {L"Save", L"Ctrl+S", C_SAVE},
    {L"Save as...", L"F10", C_SAVE_AS},
    {L"Retrieve...", L"Shift+F10", C_RETRIEVE},
    {L"Print...", L"Shift+F7  Ctrl+P", C_PRINT},
    {L"Exit", L"F7", C_EXIT},
};
static const MenuItem EDIT_ITEMS[] = {
    {L"Undo", L"Ctrl+Z", C_UNDO},
    {L"Redo", L"Ctrl+Y", C_REDO},
    {L"Cut", L"Ctrl+X", C_CUT},
    {L"Copy", L"Ctrl+C", C_COPY},
    {L"Paste", L"Ctrl+V", C_PASTE},
    {L"Select all", L"Ctrl+A", C_SELECT_ALL},
    {L"Typeover", L"Ins", C_TYPEOVER},
};
static const MenuItem SEARCH_ITEMS[] = {
    {L"Search forward...", L"F2", C_FIND},
    {L"Search back...", L"Shift+F2", C_FIND_BACK},
    {L"Replace...", L"Alt+F2", C_REPLACE},
};
static const MenuItem LAYOUT_ITEMS[] = {
    {L"Left", L"Ctrl+L", C_ALIGN_LEFT},
    {L"Center", L"Shift+F6  Ctrl+E", C_ALIGN_CENTER},
    {L"Flush right", L"Alt+F6  Ctrl+R", C_ALIGN_RIGHT},
};
static const MenuItem FONT_ITEMS[] = {
    {L"Bold", L"Ctrl+B  F6", C_BOLD},
    {L"Italics", L"Ctrl+I", C_ITALIC},
    {L"Underline", L"Ctrl+U  F8", C_UNDERLINE},
    {L"Strikethrough", L"Ctrl+Shift+X", C_STRIKE},
};
static const MenuItem TOOLS_ITEMS[] = {
    {L"Next typo", L"Alt+N", C_NEXT_TYPO},
    {L"Previous typo", L"Alt+P", C_PREV_TYPO},
    {L"Spell check...", L"Ctrl+F2", C_SPELL},
    {L"Add word to dictionary", L"Alt+A", C_ADD_WORD},
    {L"Beep", L"Alt+B", C_BEEP},
};
static const MenuItem VIEW_ITEMS[] = {
    {L"Bigger text", L"Ctrl+=", C_BIGGER},
    {L"Smaller text", L"Ctrl+-", C_SMALLER},
    {L"Full screen", L"F11", C_FULL_SCREEN},
    {L"Hints on the status line", L"", C_HINTS},
};
static const MenuItem HELP_ITEMS[] = {
    {L"Keys and help", L"F3", C_HELP},
    {L"About Amber Writer", L"", C_ABOUT},
};

#define ITEMS(a) a, (int)(sizeof a / sizeof a[0])
static const Menu MENUS[] = {
    {L"File", 'F', ITEMS(FILE_ITEMS)},     {L"Edit", 'E', ITEMS(EDIT_ITEMS)},
    {L"Search", 'S', ITEMS(SEARCH_ITEMS)}, {L"Layout", 'L', ITEMS(LAYOUT_ITEMS)},
    {L"Font", 'O', ITEMS(FONT_ITEMS)},
    {L"Tools", 'T', ITEMS(TOOLS_ITEMS)},   {L"View", 'V', ITEMS(VIEW_ITEMS)},
    {L"Help", 'H', ITEMS(HELP_ITEMS)},
};
#define MENU_COUNT (int)(sizeof MENUS / sizeof MENUS[0])

typedef struct {
    const WCHAR *keys;
    const WCHAR *label;
} Tip;

static const Tip CTRL_TIPS[] = {
    /* In rows of three on most screens */
    {L"S", L"Save"},         {L"O", L"Retrieve"},       {L"P", L"Print"},
    {L"B", L"Bold"},         {L"I", L"Italics"},        {L"U", L"Underline"},
    {L"Shift+X", L"Strike"}, {L"F2", L"Spell check"},   {L"Q", L"Exit"},
    {L"L", L"Left"},         {L"E", L"Center"},         {L"R", L"Flush right"},
    {L"F", L"Search"},       {L"H", L"Replace"},        {L"A", L"Select all"},
    {L"Z", L"Undo"},         {L"Y", L"Redo"},           {L"=/-", L"Text size"},
    {L"X", L"Cut"},          {L"C", L"Copy"},           {L"V", L"Paste"},
};
static const Tip ALT_TIPS[] = {
    {L"Alt", L"alone: Menu"}, {L"=", L"Menu"},        {L"N", L"Next typo"}, {L"P", L"Previous typo"},
    {L"A", L"Add word"},    {L"B", L"Beep on/off"}, {L"F2", L"Replace"},  {L"F6", L"Flush right"},
    {L"F4", L"Exit"},
};
static const Tip FKEY_TIPS[] = {
    /* Pairs with and without Shift, for rows of two */
    {L"F2", L"Search"},    {L"Shift+F2", L"Search back"},
    {L"F6", L"Bold"},      {L"Shift+F6", L"Center"},
    {L"F7", L"Exit"},      {L"Shift+F7", L"Print"},
    {L"F10", L"Save as"},  {L"Shift+F10", L"Retrieve"},
    {L"F3", L"Help"},      {L"F8", L"Underline"},
    {L"F11", L"Full screen"},
};

enum { PANEL_BAR = 1, PANEL_DROP, PANEL_TIPS };

static HWND g_bar, g_drop, g_tips;
static BOOL g_menu_open;
static int g_menu, g_item = -1;
static UINT g_hold_vk, g_tips_vk;
static BOOL g_hold_other;
static int g_tips_height;

static void item_label(const MenuItem *it, WCHAR *out)
{
    switch (it->cmd) {
    case C_BEEP:
        wsprintfW(out, L"%s: %s", it->label, g_beep ? L"on" : L"off");
        break;
    case C_TYPEOVER:
        wsprintfW(out, L"%s: %s", it->label, g_typeover ? L"on" : L"off");
        break;
    case C_FULL_SCREEN:
        wsprintfW(out, L"%s: %s", it->label, g_fullscreen ? L"on" : L"off");
        break;
    case C_HINTS:
        wsprintfW(out, L"%s: %s", it->label, g_hints ? L"on" : L"off");
        break;
    default:
        lstrcpyW(out, it->label);
    }
}

/* The underlined letter of each item: the first letter no item above it has taken. */
static int item_letter(const Menu *m, int index)
{
    WCHAR taken[32];
    int n = 0, i, j;
    for (i = 0; i <= index; i++) {
        const WCHAR *p;
        for (p = m->items[i].label; *p; p++) {
            WCHAR c = (WCHAR)(DWORD_PTR)CharUpperW((LPWSTR)(DWORD_PTR)*p);
            BOOL used = FALSE;
            if (!IsCharAlphaW(c))
                continue;
            for (j = 0; j < n; j++)
                if (taken[j] == c)
                    used = TRUE;
            if (!used) {
                if (i == index)
                    return (int)(p - m->items[i].label);
                taken[n++] = c;
                break;
            }
        }
    }
    return -1;
}

/* Center or flush right, or back to the left if it is already so. */
static void toggle_align(BYTE align)
{
    set_sel_align(sel_align() == align ? AL_LEFT : align);
}

static void toggle_hints(void)
{
    g_hints = !g_hints;
    setting_set(L"Hints", g_hints);
    update_status();
}

static void run_command(int cmd)
{
    WCHAR msg[128];
    switch (cmd) {
    case C_SAVE: save_then(THEN_NONE); break;
    case C_SAVE_AS: save_as(THEN_NONE); break;
    case C_RETRIEVE: retrieve(); break;
    case C_PRINT: print_document(); break;
    case C_EXIT: exit_app(); break;
    case C_UNDO: SendMessageW(g_edit, EM_UNDO, 0, 0); break;
    case C_REDO: SendMessageW(g_edit, EM_REDO, 0, 0); break;
    case C_CUT: SendMessageW(g_edit, WM_CUT, 0, 0); break;
    case C_COPY: SendMessageW(g_edit, WM_COPY, 0, 0); break;
    case C_PASTE: SendMessageW(g_edit, EM_PASTESPECIAL, CF_UNICODETEXT, 0); break;
    case C_SELECT_ALL: set_sel(0, -1); break;
    case C_TYPEOVER:
        SendMessageW(g_edit, WM_KEYDOWN, VK_INSERT, 0);
        SendMessageW(g_edit, WM_KEYUP, VK_INSERT, 0xC0000001);
        g_typeover = !g_typeover;
        update_status();
        break;
    case C_FIND: find_prompt(FALSE); break;
    case C_FIND_BACK: find_prompt(TRUE); break;
    case C_REPLACE: swap_start(); break;
    case C_BOLD: toggle_style(CFE_BOLD); break;
    case C_ITALIC: toggle_style(CFE_ITALIC); break;
    case C_UNDERLINE: toggle_style(CFE_UNDERLINE); break;
    case C_STRIKE: toggle_style(CFE_STRIKEOUT); break;
    case C_ALIGN_LEFT: set_sel_align(AL_LEFT); break;
    case C_ALIGN_CENTER: toggle_align(AL_CENTER); break;
    case C_ALIGN_RIGHT: toggle_align(AL_RIGHT); break;
    case C_NEXT_TYPO: jump_typo(TRUE); break;
    case C_PREV_TYPO: jump_typo(FALSE); break;
    case C_SPELL: spell_check(); break;
    case C_ADD_WORD: add_word(); break;
    case C_BEEP: toggle_beep(); break;
    case C_BIGGER: resize_text(1); break;
    case C_SMALLER: resize_text(-1); break;
    case C_FULL_SCREEN: toggle_full_screen(); break;
    case C_HINTS: toggle_hints(); break;
    case C_HELP: if (!g_help) toggle_help(); break;
    case C_ABOUT:
        wsprintfW(msg, L"Amber Writer %s - F3 for help", AMBER_VERSION_W);
        flash(msg, 5000);
        break;
    }
}

/* Where each menu title sits on the bar. */
static int bar_x(HDC dc, int index, int *width)
{
    int x = 24 - g_cw, i;
    for (i = 0; i <= index && i < MENU_COUNT; i++) {
        SIZE size;
        WCHAR title[32];
        wsprintfW(title, L" %s ", MENUS[i].title);
        GetTextExtentPoint32W(dc, title, lstrlenW(title), &size);
        if (i == index) {
            if (width)
                *width = size.cx;
            return x;
        }
        x += size.cx + g_cw;
    }
    return x;
}

static void underline_letter(HDC dc, const WCHAR *text, int at, int x, int y, COLORREF color)
{
    SIZE before, letter;
    HPEN pen;
    HGDIOBJ old;
    if (at < 0)
        return;
    GetTextExtentPoint32W(dc, text, at, &before);
    GetTextExtentPoint32W(dc, text + at, 1, &letter);
    pen = CreatePen(PS_SOLID, 1, color);
    old = SelectObject(dc, pen);
    MoveToEx(dc, x + before.cx, y + g_ascent + 2, NULL);
    LineTo(dc, x + before.cx + letter.cx, y + g_ascent + 2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void place_drop(void)
{
    HDC dc = GetDC(g_main);
    HGDIOBJ old = SelectObject(dc, g_font);
    const Menu *m = &MENUS[g_menu];
    int i, label_w = 0, keys_w = 0, x, w, h;
    for (i = 0; i < m->count; i++) {
        WCHAR label[64];
        SIZE size;
        item_label(&m->items[i], label);
        GetTextExtentPoint32W(dc, label, lstrlenW(label), &size);
        if (size.cx > label_w)
            label_w = size.cx;
        GetTextExtentPoint32W(dc, m->items[i].keys, lstrlenW(m->items[i].keys), &size);
        if (size.cx > keys_w)
            keys_w = size.cx;
    }
    x = bar_x(dc, g_menu, NULL);
    SelectObject(dc, old);
    ReleaseDC(g_main, dc);
    w = label_w + keys_w + g_cw * 7;
    h = m->count * g_ch + 8;
    SetWindowPos(g_drop, HWND_TOP, x, g_ch + 4, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(g_drop, NULL, TRUE);
}

static void open_menu(int index)
{
    RECT client;
    g_menu_open = TRUE;
    g_menu = (index + MENU_COUNT) % MENU_COUNT;
    g_item = 0;
    GetClientRect(g_main, &client);
    SetWindowPos(g_bar, HWND_TOP, 0, 0, client.right, g_ch + 4, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(g_bar, NULL, TRUE);
    place_drop();
}

static void close_menu(void)
{
    if (!g_menu_open)
        return;
    g_menu_open = FALSE;
    ShowWindow(g_drop, SW_HIDE);
    ShowWindow(g_bar, SW_HIDE);
    SetFocus(g_prompt_kind != P_NONE ? g_prompt : g_help ? g_main : g_edit);
}

static void choose_item(int index)
{
    int cmd;
    if (index < 0 || index >= MENUS[g_menu].count)
        return;
    cmd = MENUS[g_menu].items[index].cmd;
    close_menu();
    run_command(cmd);
}

/* Keys while the menu is open. Returns FALSE for a key the menu does not use: the
   menu closes and the key does what it always does. */
static BOOL menu_key(UINT vk, BOOL ctrl, BOOL alt)
{
    const Menu *m = &MENUS[g_menu];
    int i;
    if (ctrl || (alt && vk != VK_MENU))
        return FALSE;
    switch (vk) {
    case VK_ESCAPE:
        close_menu();
        return TRUE;
    case VK_LEFT:
        open_menu(g_menu - 1);
        return TRUE;
    case VK_RIGHT:
        open_menu(g_menu + 1);
        return TRUE;
    case VK_UP:
        g_item = (g_item + m->count - 1) % m->count;
        InvalidateRect(g_drop, NULL, FALSE);
        return TRUE;
    case VK_DOWN:
        g_item = (g_item + 1) % m->count;
        InvalidateRect(g_drop, NULL, FALSE);
        return TRUE;
    case VK_HOME:
    case VK_PRIOR:
        g_item = 0;
        InvalidateRect(g_drop, NULL, FALSE);
        return TRUE;
    case VK_END:
    case VK_NEXT:
        g_item = m->count - 1;
        InvalidateRect(g_drop, NULL, FALSE);
        return TRUE;
    case VK_RETURN:
    case VK_SPACE:
        choose_item(g_item);
        return TRUE;
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:
        return TRUE;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        for (i = 0; i < m->count; i++) {
            int at = item_letter(m, i);
            if (at >= 0 && (WCHAR)(DWORD_PTR)CharUpperW((LPWSTR)(DWORD_PTR)m->items[i].label[at]) == vk) {
                choose_item(i);
                return TRUE;
            }
        }
        for (i = 0; i < MENU_COUNT; i++) {
            if (MENUS[i].letter == vk) {
                open_menu(i);
                return TRUE;
            }
        }
        return TRUE;
    }
    if (vk >= VK_F1 && vk <= VK_F24)
        return FALSE;
    return TRUE;
}

/* The key template */

static const Tip *tips_for(UINT vk, int *count)
{
    if (vk == VK_CONTROL) {
        *count = sizeof CTRL_TIPS / sizeof CTRL_TIPS[0];
        return CTRL_TIPS;
    }
    *count = sizeof ALT_TIPS / sizeof ALT_TIPS[0];
    return ALT_TIPS;
}

/* "Alt" on its own needs no "Alt+" in front. */
static const WCHAR *tip_prefix(const WCHAR *prefix, const Tip *tip)
{
    return lstrcmpW(tip->keys, L"Alt") == 0 ? L"" : prefix;
}

/* Lays out (and with a dc, draws) a block of tips in even columns. Returns the
   height used. */
static int draw_tip_block(HDC dc, const WCHAR *prefix, const Tip *tips, int count, int x, int y, int width, BOOL dim)
{
    int i, cell = 0, cols, col = 0;
    HDC measure = dc ? dc : GetDC(g_main);
    HGDIOBJ old = SelectObject(measure, g_font);
    for (i = 0; i < count; i++) {
        WCHAR text[64];
        SIZE size;
        wsprintfW(text, L"%s%s %s", tip_prefix(prefix, &tips[i]), tips[i].keys, tips[i].label);
        GetTextExtentPoint32W(measure, text, lstrlenW(text), &size);
        if (size.cx + g_cw * 3 > cell)
            cell = size.cx + g_cw * 3;
    }
    cols = cell ? width / cell : 1;
    if (cols < 1)
        cols = 1;
    for (i = 0; i < count; i++) {
        if (dc) {
            WCHAR keys[48];
            SIZE size;
            int cx = x + col * cell;
            wsprintfW(keys, L"%s%s", tip_prefix(prefix, &tips[i]), tips[i].keys);
            /* The keys look like key caps: amber, with the writing in black. */
            SetBkColor(dc, dim ? DIM_AMBER : AMBER);
            SetTextColor(dc, BLACK);
            GetTextExtentPoint32W(dc, keys, lstrlenW(keys), &size);
            TextOutW(dc, cx, y, keys, lstrlenW(keys));
            SetBkColor(dc, BLACK);
            SetTextColor(dc, dim ? DIM_AMBER : AMBER);
            TextOutW(dc, cx + size.cx + g_cw, y, tips[i].label, lstrlenW(tips[i].label));
        }
        if (++col == cols && i + 1 < count) {
            col = 0;
            y += g_ch + 2;
        }
    }
    SelectObject(measure, old);
    if (!dc)
        ReleaseDC(g_main, measure);
    return (count ? (count + cols - 1) / cols : 0) * (g_ch + 2);
}

static void paint_tips(HDC dc, RECT r)
{
    int count, y = 6, width = r.right - 32;
    const Tip *tips = tips_for(g_tips_vk, &count);
    HPEN pen = CreatePen(PS_SOLID, 1, AMBER);
    HGDIOBJ old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, g_black);
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    y += draw_tip_block(dc, g_tips_vk == VK_CONTROL ? L"Ctrl+" : L"Alt+", tips, count, 16, y, width, FALSE);
    y += 6;
    draw_tip_block(dc, L"", FKEY_TIPS, sizeof FKEY_TIPS / sizeof FKEY_TIPS[0], 16, y, width, TRUE);
}

static void show_tips(UINT vk)
{
    RECT client;
    int count, width, h;
    const Tip *tips = tips_for(vk, &count);
    GetClientRect(g_main, &client);
    width = client.right - 32 - 32;
    g_tips_vk = vk;
    h = 6 + draw_tip_block(NULL, vk == VK_CONTROL ? L"Ctrl+" : L"Alt+", tips, count, 0, 0, width, FALSE) + 6 +
        draw_tip_block(NULL, L"", FKEY_TIPS, sizeof FKEY_TIPS / sizeof FKEY_TIPS[0], 0, 0, width, TRUE) + 6;
    g_tips_height = h;
    SetWindowPos(g_tips, HWND_TOP, 16, 4, client.right - 32, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(g_tips, NULL, TRUE);
}

static void hide_tips(void)
{
    if (IsWindowVisible(g_tips))
        ShowWindow(g_tips, SW_HIDE);
    g_tips_vk = 0;
}

static void cancel_hold(void)
{
    KillTimer(g_main, TIMER_HOLD);
    g_hold_vk = 0;
    hide_tips();
}

/* Watches Ctrl and Alt on their own: held, they show the key template; Alt tapped
   opens the menu. Returns TRUE when the message must not go on, which keeps Windows
   from starting its own (invisible) menu mode on Alt. */
static BOOL track_modifiers(const MSG *msg)
{
    UINT vk = (UINT)msg->wParam;
    BOOL down = msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN;
    BOOL up = msg->message == WM_KEYUP || msg->message == WM_SYSKEYUP;
    if (msg->message == WM_LBUTTONDOWN || msg->message == WM_RBUTTONDOWN || msg->message == WM_MOUSEWHEEL) {
        if (g_hold_vk) {
            g_hold_other = TRUE;
            KillTimer(g_main, TIMER_HOLD);
            hide_tips();
        }
        return FALSE;
    }
    if (!down && !up)
        return FALSE;
    if (down) {
        if (vk == VK_CONTROL || vk == VK_MENU) {
            if (msg->lParam & (1 << 30))
                return vk == VK_MENU; /* auto-repeat while held */
            if (g_hold_vk && g_hold_vk != vk) {
                /* Ctrl and Alt together, which is also AltGr: neither template nor menu. */
                g_hold_other = TRUE;
                KillTimer(g_main, TIMER_HOLD);
                hide_tips();
            } else {
                g_hold_vk = vk;
                g_hold_other = FALSE;
                SetTimer(g_main, TIMER_HOLD, 600, NULL);
            }
            return vk == VK_MENU;
        }
        if (vk != VK_SHIFT && g_hold_vk) {
            g_hold_other = TRUE;
            KillTimer(g_main, TIMER_HOLD);
            hide_tips();
        }
        return FALSE;
    }
    if (vk == g_hold_vk) {
        BOOL tap = !g_hold_other && !g_tips_vk;
        KillTimer(g_main, TIMER_HOLD);
        hide_tips();
        g_hold_vk = 0;
        if (vk == VK_MENU && tap && g_prompt_kind == P_NONE) {
            if (g_menu_open)
                close_menu();
            else
                open_menu(0);
        }
    }
    return vk == VK_MENU || vk == VK_F10;
}

static void hold_timer(void)
{
    KillTimer(g_main, TIMER_HOLD);
    if (g_hold_vk && !g_hold_other && GetKeyState(g_hold_vk) < 0 && GetForegroundWindow() == g_main &&
        g_prompt_kind == P_NONE && !g_menu_open)
        show_tips(g_hold_vk);
}

static int drop_row_at(int y)
{
    int row = (y - 4) / g_ch;
    if (y < 4 || row < 0 || row >= MENUS[g_menu].count)
        return -1;
    return row;
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int kind = (int)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r;
        HGDIOBJ old = SelectObject(dc, g_font);
        int i;
        GetClientRect(hwnd, &r);
        SetBkMode(dc, OPAQUE);
        if (kind == PANEL_BAR) {
            /* An amber bar with black writing, as in WordPerfect 5.1. */
            HBRUSH amber = CreateSolidBrush(AMBER);
            FillRect(dc, &r, amber);
            DeleteObject(amber);
            for (i = 0; i < MENU_COUNT; i++) {
                WCHAR title[32];
                int w, x = bar_x(dc, i, &w), at;
                BOOL sel = i == g_menu;
                wsprintfW(title, L" %s ", MENUS[i].title);
                SetBkColor(dc, sel ? BLACK : AMBER);
                SetTextColor(dc, sel ? AMBER : BLACK);
                TextOutW(dc, x, 2, title, lstrlenW(title));
                for (at = 0; title[at] && title[at] != MENUS[i].letter && title[at] != MENUS[i].letter + 32; at++)
                    ;
                underline_letter(dc, title, title[at] ? at : -1, x, 2, sel ? AMBER : BLACK);
            }
        } else if (kind == PANEL_DROP) {
            const Menu *m = &MENUS[g_menu];
            HPEN pen = CreatePen(PS_SOLID, 1, AMBER);
            HGDIOBJ old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, g_black);
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, old_pen);
            SelectObject(dc, old_brush);
            DeleteObject(pen);
            for (i = 0; i < m->count; i++) {
                WCHAR label[64];
                RECT row = {2, 4 + i * g_ch, r.right - 2, 4 + (i + 1) * g_ch};
                BOOL sel = i == g_item;
                SIZE keys;
                HBRUSH fill = CreateSolidBrush(sel ? AMBER : BLACK);
                FillRect(dc, &row, fill);
                DeleteObject(fill);
                item_label(&m->items[i], label);
                SetBkColor(dc, sel ? AMBER : BLACK);
                SetTextColor(dc, sel ? BLACK : AMBER);
                TextOutW(dc, g_cw * 2, row.top, label, lstrlenW(label));
                underline_letter(dc, label, item_letter(m, i), g_cw * 2, row.top, sel ? BLACK : AMBER);
                SetTextColor(dc, sel ? BLACK : DIM_AMBER);
                GetTextExtentPoint32W(dc, m->items[i].keys, lstrlenW(m->items[i].keys), &keys);
                TextOutW(dc, r.right - g_cw * 2 - keys.cx, row.top, m->items[i].keys, lstrlenW(m->items[i].keys));
            }
        } else {
            FillRect(dc, &r, g_black);
            paint_tips(dc, r);
        }
        SelectObject(dc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (kind == PANEL_DROP) {
            int row = drop_row_at((short)HIWORD(lp));
            if (row >= 0 && row != g_item) {
                g_item = row;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (kind == PANEL_BAR) {
            HDC dc = GetDC(hwnd);
            HGDIOBJ old = SelectObject(dc, g_font);
            int i, x = (short)LOWORD(lp);
            for (i = 0; i < MENU_COUNT; i++) {
                int w, left = bar_x(dc, i, &w);
                if (x >= left && x < left + w) {
                    open_menu(i);
                    break;
                }
            }
            SelectObject(dc, old);
            ReleaseDC(hwnd, dc);
        } else if (kind == PANEL_DROP) {
            choose_item(drop_row_at((short)HIWORD(lp)));
        } else {
            hide_tips();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND make_panel(int kind)
{
    HWND w = CreateWindowExW(0, L"AmberPanel", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, g_main, NULL, g_inst, NULL);
    SetWindowLongPtrW(w, GWLP_USERDATA, kind);
    return w;
}

/* Keys. Returns TRUE when the key was a command and must not reach the page. */
static BOOL handle_key(UINT vk, BOOL ctrl, BOOL shift, BOOL alt)
{
    if (g_prompt_kind != P_NONE)
        return FALSE;
    if (g_help) {
        if ((vk == VK_ESCAPE || vk == VK_F3) && !ctrl && !alt)
            toggle_help();
        else if (vk == VK_F7 && !ctrl && !alt)
            exit_app();
        return vk != VK_F4 || !alt;
    }
    if (!ctrl && !alt) {
        switch (vk) {
        case VK_F3:
            toggle_help();
            return TRUE;
        case VK_F2:
            find_prompt(shift);
            return TRUE;
        case VK_F10:
            if (shift)
                retrieve();
            else
                save_as(THEN_NONE);
            return TRUE;
        case VK_F7:
            if (shift)
                print_document();
            else
                exit_app();
            return TRUE;
        case VK_F6:
            if (shift)
                toggle_align(AL_CENTER);
            else
                toggle_style(CFE_BOLD);
            return TRUE;
        case VK_F8:
            toggle_style(CFE_UNDERLINE);
            return TRUE;
        case VK_F11:
            toggle_full_screen();
            return TRUE;
        case VK_ESCAPE:
            return TRUE;
        case VK_INSERT:
            if (shift) {
                SendMessageW(g_edit, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
                return TRUE;
            }
            g_typeover = !g_typeover;
            update_status();
            return FALSE;
        }
        return FALSE;
    }
    if (alt && !ctrl) {
        if (vk == VK_F2) {
            swap_start();
            return TRUE;
        }
        if (vk == VK_OEM_PLUS) {
            open_menu(0);
            return TRUE;
        }
        if (vk == VK_F6) {
            toggle_align(AL_RIGHT);
            return TRUE;
        }
        if (vk == 'A')
            add_word();
        else if (vk == 'B')
            toggle_beep();
        else if (vk == 'N' || vk == 'P')
            jump_typo(vk == 'N');
        /* Other Alt+letters would only ding, as there is no menu. */
        return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9');
    }
    if (ctrl && !alt) {
        switch (vk) {
        case 'S':
            if (!shift)
                save_then(THEN_NONE);
            return TRUE;
        case 'O':
            retrieve();
            return TRUE;
        case 'F':
            find_prompt(shift);
            return TRUE;
        case 'L':
            set_sel_align(AL_LEFT);
            return TRUE;
        case 'P':
            print_document();
            return TRUE;
        case 'E':
            toggle_align(AL_CENTER);
            return TRUE;
        case 'R':
            toggle_align(AL_RIGHT);
            return TRUE;
        case 'H':
            swap_start();
            return TRUE;
        case 'Q':
            exit_app();
            return TRUE;
        case VK_F2:
            spell_check();
            return TRUE;
        case 'B':
            toggle_style(CFE_BOLD);
            return TRUE;
        case 'I':
            toggle_style(CFE_ITALIC);
            return TRUE;
        case 'U':
            toggle_style(CFE_UNDERLINE);
            return TRUE;
        case 'X':
            if (shift) {
                toggle_style(CFE_STRIKEOUT);
                return TRUE;
            }
            return FALSE;
        case 'V':
            if (!shift)
                SendMessageW(g_edit, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
            return TRUE;
        case VK_OEM_PLUS:
        case VK_ADD:
            resize_text(1);
            return TRUE;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            resize_text(-1);
            return TRUE;
        case 'A':
        case 'C':
        case 'Z':
        case 'Y':
            return shift;
        }
        /* Keep RichEdit's own Ctrl shortcuts (centering, line spacing, fonts) away. */
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || (vk >= VK_OEM_1 && vk <= VK_OEM_8) ||
            (vk >= VK_F1 && vk <= VK_F24) || vk == VK_OEM_102)
            return TRUE;
        return FALSE;
    }
    return FALSE;
}

static BOOL ends_word(WCHAR c)
{
    if (c == '\r' || c == '\t')
        return TRUE;
    if (c < 32 || c == '\'' || c == 127)
        return FALSE;
    return !IsCharAlphaNumericW(c);
}

/* Window procedures */

static LRESULT CALLBACK edit_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        LRESULT r = CallWindowProcW(g_edit_proc, hwnd, msg, wp, lp);
        draw_misspellings(hwnd);
        return r;
    }
    if (msg == WM_CHAR) {
        WCHAR word[MAX_WORD + 2];
        int len = 0;
        CHARRANGE sel = get_sel();
        BOOL command = (GetKeyState(VK_CONTROL) < 0) || (GetKeyState(VK_MENU) < 0 && GetKeyState(VK_RMENU) >= 0);
        BOOL title_ends = FALSE;
        LRESULT r;
        if (!command && sel.cpMin == sel.cpMax && ends_word((WCHAR)wp))
            len = word_ending_at(sel.cpMax, word);
        if (!command) {
            if (is_word_char((WCHAR)wp) || wp == '\'')
                g_typing = TRUE;
            else if (wp != 8)
                g_typing = FALSE; /* the word is finished; Backspace keeps editing it */
        }
        if (!command && wp == '\r' && sel.cpMin == sel.cpMax && sel_align() != AL_LEFT) {
            /* Like WordPerfect's Center, a centered or flush right line ends at Enter:
               the next paragraph starts back at the left. */
            WCHAR next[2] = {0};
            get_range(sel.cpMax, sel.cpMax + 1, next);
            title_ends = next[0] == 0 || next[0] == '\r';
        }
        r = CallWindowProcW(g_edit_proc, hwnd, msg, wp, lp);
        if (title_ends)
            set_sel_align(AL_LEFT);
        if (len && !spell_is_correct(word, len))
            misspelled(word, len);
        return r;
    }
    if (msg == WM_SETFOCUS && !g_hook_works) {
        LRESULT r = CallWindowProcW(g_edit_proc, hwnd, msg, wp, lp);
        if (!g_hook_works)
            recolor_caret(hwnd, g_ch);
        return r;
    }
    if (msg == WM_PASTE) {
        SendMessageW(hwnd, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN)
        stop_typing();
    if (msg == WM_RBUTTONUP || msg == WM_CONTEXTMENU) {
        open_menu(0);
        return 0;
    }
    return CallWindowProcW(g_edit_proc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK prompt_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SETFOCUS) {
        /* Give the status line prompt the same amber caret as the page. */
        LRESULT r = CallWindowProcW(g_prompt_proc, hwnd, msg, wp, lp);
        recolor_caret(hwnd, g_ch);
        return r;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        int kind = g_prompt_kind;
        end_prompt();
        if (kind == P_SWAP_EACH)
            swap_finish();
        return 0;
    }
    /* In WordPerfect, F2 starts the search, so F2 F2 finds the next one. */
    if (msg == WM_KEYDOWN && wp == VK_F2 && (g_prompt_kind == P_FIND || g_prompt_kind == P_SWAP_FIND))
        return CallWindowProcW(prompt_proc, hwnd, WM_CHAR, '\r', 0);
    if (msg == WM_CHAR) {
        WCHAR c = (WCHAR)wp;
        if (c == 27)
            return 0;
        if (c == 1) { /* Ctrl+A */
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        if (g_prompt_choices[0]) {
            char key = (char)(c < 128 ? c : 0);
            int kind = g_prompt_kind;
            if (key >= 'A' && key <= 'Z')
                key += 32;
            if (key && lstrlenA(g_prompt_choices) > 0) {
                const char *p;
                for (p = g_prompt_choices; *p; p++) {
                    if (*p == key) {
                        BOOL yes = key == 'y' || (key == '\r' && g_prompt_default_yes);
                        end_prompt();
                        switch (kind) {
                        case P_REPLACE:
                            if (yes)
                                write_document(g_prompt_path, g_prompt_then);
                            break;
                        case P_SAVE_BEFORE_RETRIEVE:
                            if (yes)
                                save_then(THEN_ASK_RETRIEVE);
                            else
                                ask_retrieve_name();
                            break;
                        case P_SAVE_BEFORE_EXIT:
                            if (yes)
                                save_then(THEN_QUIT);
                            else
                                quit();
                            break;
                        case P_EXIT:
                            if (yes)
                                quit();
                            break;
                        case P_SPELL:
                            spell_answer(key);
                            break;
                        case P_SWAP_CONFIRM:
                            g_swap_confirm = yes;
                            ask(P_SWAP_FIND, L"-> Srch: ", g_find, NULL);
                            break;
                        case P_SWAP_EACH:
                            swap_answer(key);
                            break;
                        }
                        return 0;
                    }
                }
            }
            return 0;
        }
        if (c == '\r') {
            WCHAR text[MAX_PATH], path[MAX_PATH];
            int kind = g_prompt_kind;
            GetWindowTextW(hwnd, text, MAX_PATH);
            end_prompt();
            if (kind == P_FIND || kind == P_SWAP_FIND || kind == P_SWAP_WITH) {
                /* Search text is kept as typed, spaces and all. */
                if (!text[0])
                    return 0;
                if (kind == P_FIND) {
                    lstrcpynW(g_find, text, 256);
                    do_find();
                } else if (kind == P_SWAP_FIND) {
                    lstrcpynW(g_find, text, 256);
                    ask(P_SWAP_WITH, L"Replace with: ", g_swap_with, NULL);
                } else {
                    lstrcpynW(g_swap_with, text, 256);
                    swap_go();
                }
                return 0;
            }
            trim(text);
            if (!text[0])
                return 0;
            if (kind == P_SAVE_AS) {
                resolve(text, path, FALSE);
                if (file_exists(path) && lstrcmpiW(path, g_path) != 0) {
                    WCHAR q[MAX_PATH + 16];
                    lstrcpyW(g_prompt_path, path);
                    wsprintfW(q, L"Replace %s?", path);
                    ask_yes_no(P_REPLACE, q, FALSE);
                } else {
                    write_document(path, g_prompt_then);
                }
            } else if (kind == P_RETRIEVE) {
                resolve(text, path, TRUE);
                retrieve_path(path);
            }
            return 0;
        }
    }
    if (msg == WM_KEYDOWN && g_prompt_choices[0] && wp != VK_ESCAPE)
        return 0;
    return CallWindowProcW(g_prompt_proc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_main = hwnd;
        return 0;
    case WM_SIZE:
        layout();
        place_prompt();
        if (g_menu_open)
            open_menu(g_menu);
        hide_tips();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            cancel_hold();
            close_menu();
        }
        break;
    case WM_CONTEXTMENU:
        open_menu(0);
        return 0;
    case WM_LBUTTONDOWN:
        /* A click on the status line opens the menu too. */
        if (g_prompt_kind == P_NONE && (short)HIWORD(lp) >= g_status_y)
            open_menu(0);
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_prompt_kind != P_NONE ? g_prompt : g_help ? hwnd : g_edit);
        return 0;
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wp, &r, g_black);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_main(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, AMBER);
        SetBkColor((HDC)wp, BLACK);
        return (LRESULT)g_black;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_EDIT && HIWORD(wp) == EN_CHANGE && !g_busy) {
            SetTimer(hwnd, TIMER_COUNTS, 250, NULL);
            update_title();
            refresh_caret_lines();
            update_hint();
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->idFrom == ID_EDIT && nm->code == EN_SELCHANGE && !g_busy) {
            update_status();
            refresh_caret_lines();
            update_hint();
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_HOLD) {
            hold_timer();
            return 0;
        }
        if (wp == TIMER_SQUIGGLES) {
            /* Redrawn now and then too, in case the page repainted itself without them. */
            if (!g_help && !g_busy && IsWindowVisible(g_edit))
                draw_misspellings(g_edit);
            return 0;
        }
        KillTimer(hwnd, wp);
        if (wp == TIMER_COUNTS)
            update_counts();
        else if (wp == TIMER_MESSAGE) {
            g_message[0] = 0;
            update_status();
        }
        return 0;
    case WM_CLOSE:
        if (g_quitting || !SendMessageW(g_edit, EM_GETMODIFY, 0, 0)) {
            DestroyWindow(hwnd);
        } else {
            if (g_help)
                toggle_help();
            if (g_prompt_kind != P_NONE)
                end_prompt();
            exit_app();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Start up */

static void find_folders(WCHAR *personal_words)
{
    WCHAR appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, NULL, 0, g_docs)))
        GetCurrentDirectoryW(MAX_PATH, g_docs);
    /* The same place the Python version keeps its word list, so both share it. */
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, appdata)) &&
        lstrlenW(appdata) + 40 < MAX_PATH) {
        wsprintfW(personal_words, L"%s\\AmberWriter\\AmberWriter\\words.txt", appdata);
        create_parent_dirs(personal_words);
    } else {
        personal_words[0] = 0;
    }
}

static HMODULE load_richedit(const WCHAR **cls)
{
    HMODULE m = LoadLibraryW(L"msftedit.dll");
    if (m) {
        *cls = MSFTEDIT_CLASS;
        return m;
    }
    *cls = RICHEDIT_CLASSW;
    return LoadLibraryW(L"riched20.dll");
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    WNDCLASSEXW wc = {sizeof wc};
    WCHAR personal_words[MAX_PATH];
    const WCHAR *edit_class;
    HMODULE richedit;
    MSG msg;
    int argc;
    (void)prev;
    (void)cmdline;
    (void)show;
    g_inst = inst;
    g_black = CreateSolidBrush(BLACK);

    richedit = load_richedit(&edit_class);
    if (!richedit) {
        MessageBoxW(NULL, L"Amber Writer needs the Rich Edit control (riched20.dll), which is part of Windows.",
                    L"Amber Writer", MB_ICONERROR);
        return 1;
    }
    hook_caret(richedit);
    /* Some systems forward msftedit.dll to riched20.dll. */
    if (GetModuleHandleW(L"riched20.dll") && GetModuleHandleW(L"riched20.dll") != richedit)
        hook_caret(GetModuleHandleW(L"riched20.dll"));

    find_folders(personal_words);
    spell_init(personal_words[0] ? personal_words : NULL);
    g_beep = setting_get(L"Beep", 1) != 0;
    g_hints = setting_get(L"Hints", 1) != 0;
    g_font_size = (int)setting_get(L"FontSize", 16);
    if (g_font_size < 9 || g_font_size > 40)
        g_font_size = 16;
    pick_font_face();

    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = g_black;
    wc.lpszClassName = L"AmberWriter";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = panel_proc;
    wc.hIcon = NULL;
    wc.hIconSm = NULL;
    wc.lpszClassName = L"AmberPanel";
    RegisterClassExW(&wc);

    CreateWindowExW(0, L"AmberWriter", L"Amber Writer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                    CW_USEDEFAULT, 1100, 760, NULL, NULL, inst, NULL);
    if (!g_main)
        return 1;

    g_edit = CreateWindowExW(0, edit_class, L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN |
                                 ES_NOHIDESEL | ES_NOOLEDRAGDROP,
                             0, 0, 100, 100, g_main, (HMENU)ID_EDIT, inst, NULL);
    g_prompt = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 100, 20, g_main, (HMENU)ID_PROMPT,
                               inst, NULL);
    g_bar = make_panel(PANEL_BAR);
    g_drop = make_panel(PANEL_DROP);
    g_tips = make_panel(PANEL_TIPS);
    g_edit_proc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    g_prompt_proc = (WNDPROC)SetWindowLongPtrW(g_prompt, GWLP_WNDPROC, (LONG_PTR)prompt_proc);
    SendMessageW(g_edit, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    SendMessageW(g_edit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    SendMessageW(g_edit, EM_SETTEXTMODE, TM_RICHTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);
    SendMessageW(g_edit, EM_SETUNDOLIMIT, 200, 0);
    SendMessageW(g_edit, EM_SETBKGNDCOLOR, 0, BLACK);
    SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);

    ShowWindow(g_main, SW_SHOWMAXIMIZED);
    layout();
    clear_page();
    update_counts();

    {
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc > 1) {
            WCHAR path[MAX_PATH];
            if (GetFullPathNameW(argv[1], MAX_PATH, path, NULL))
                retrieve_path(path);
        }
        if (argv)
            LocalFree(argv);
    }
    update_title();
    SetFocus(g_edit);
    SetTimer(g_main, TIMER_SQUIGGLES, 500, NULL);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (track_modifiers(&msg))
            continue;
        if (g_menu_open) {
            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
                BOOL ctrl = GetKeyState(VK_CONTROL) < 0, alt = (msg.lParam & (1 << 29)) != 0;
                if (menu_key((UINT)msg.wParam, ctrl, alt))
                    continue;
                close_menu();
            } else if (msg.message == WM_CHAR || msg.message == WM_SYSCHAR) {
                continue;
            } else if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN) && msg.hwnd != g_bar &&
                       msg.hwnd != g_drop) {
                close_menu();
                if (msg.message == WM_RBUTTONDOWN)
                    continue;
            }
        }
        if ((msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) &&
            (msg.hwnd == g_edit || msg.hwnd == g_main)) {
            BOOL ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            BOOL alt = (msg.lParam & (1 << 29)) != 0;
            UINT vk = (UINT)msg.wParam;
            if (msg.hwnd == g_edit && ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_DELETE || vk == VK_RETURN))
                stop_typing();
            /* AltGr arrives as Ctrl+Alt; leave it for typing accented letters. */
            if (!(ctrl && alt) && handle_key((UINT)msg.wParam, ctrl, shift, alt))
                continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
