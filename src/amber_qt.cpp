/* Amber Writer for macOS and Linux: the same program as amber.c, with Qt in place of
 * the Win32 calls. The documents, spelling and line breaking are the same C code
 * (doc.c, spell.c), so files and typos come out the same on every system.
 *
 * The page is a QTextEdit sized to 65 columns and centered on a black window. The
 * status line, its prompts, the help screen, the menu bar and the key template are
 * drawn here, as amber.c draws them.
 */
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QFontDatabase>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>
#include <QTimer>
#include <QWindow>
#include <cerrno>
#include <cmath>
#include <cstring>

extern "C" {
#include "beep.h"
#include "doc.h"
#include "spell.h"
#include "version.h"
}

static const QColor AMBER(0xFF, 0xB0, 0x00);
static const QColor DIM_AMBER(0x9A, 0x6A, 0x00);
static const QColor BLACK(0, 0, 0);

/* WordPerfect 5.1 defaults: 1" margins on letter paper, 10 characters and 6 lines per inch. */
enum { COLUMNS = 65, LINES_PER_PAGE = 54, LINES_PER_INCH = 6, CHARS_PER_INCH = 10, TAB_COLUMNS = 5 };

#ifdef Q_OS_MACOS
static const bool MAC = true;
#else
static const bool MAC = false;
#endif

/* On a Mac the keys are Cmd and Option: Qt already treats Cmd as Ctrl. */
static QString keys(QString s)
{
    if (MAC) {
        s.replace(QRegularExpression("\\bCtrl\\b"), "Cmd");
        s.replace(QRegularExpression("\\bAlt\\b"), "Opt");
    }
    return s;
}

static const char *FONT_CHOICES[] = {
    "Perfect DOS VGA 437", "Px437 IBM VGA 8x16",
#ifdef Q_OS_MACOS
    "Menlo", "Monaco", "Courier New",
#else
    "DejaVu Sans Mono", "Liberation Mono", "Ubuntu Mono", "Noto Sans Mono", "Courier New",
#endif
};

static const char *PRINT_FONTS[] = {"Courier New", "Liberation Mono", "Nimbus Mono PS", "FreeMono",
                                    "Courier",     "DejaVu Sans Mono"};

/* The help screen, in two columns like the WordPerfect template. */
struct HelpRow {
    const char *k1, *t1, *k2, *t2;
};

static const HelpRow HELP_ROWS[] = {
    {"Alt", "Tap for the menu (or Alt+=): arrows, Enter, Esc, letters", nullptr, nullptr},
    {"Ctrl, Alt", "Hold a moment to see the keys that go with them", nullptr, nullptr},
    {"F3", "This help screen (Esc to leave)", nullptr, nullptr},
    {"F10", "Save document as...", "Ctrl+S", "Save"},
    {"Shift+F10", "Retrieve (open) a document", "Ctrl+O", "Retrieve"},
    {"Shift+F7", "Print", "Ctrl+P", "Print"},
    {"F7", "Exit", "Ctrl+Q", "Exit"},
    {"Ctrl+B, F6", "Bold", "Ctrl+I", "Italics"},
    {"Ctrl+U, F8", "Underline", "Ctrl+Shift+X", "Strikethrough"},
    {"Shift+F6", "Center (Ctrl+E)", "Alt+F6", "Flush right (Ctrl+R)"},
    {"Ctrl+L", "Back to the left; Enter after a centered title does it too", nullptr, nullptr},
    {nullptr, "", nullptr, nullptr},
    {"Alt+N", "Next typo", "Alt+P", "Previous typo"},
    {"Ctrl+F2", "Spell check from the cursor, with suggestions", nullptr, nullptr},
    {"Alt+A", "Add the word at the cursor to your dictionary", nullptr, nullptr},
    {"Alt+B", "Turn the misspelling beep on or off", nullptr, nullptr},
    {nullptr, "", nullptr, nullptr},
    {"F2", "Search forward (F2 again: next)", "Shift+F2", "Search back"},
    {"Alt+F2", "Replace a word or name everywhere", nullptr, nullptr},
    {"Ctrl+F", "Search", "Ctrl+H", "Replace"},
    {"Ctrl+=", "Bigger text", "Ctrl+-", "Smaller text"},
    {"F11", "Full screen on or off", "Ins", "Typeover"},
    {"Ctrl+Z", "Undo", "Ctrl+Y", "Redo"},
    {"Ctrl+X/C/V", "Cut, copy and paste", nullptr, nullptr},
    {nullptr, "Right-click the page or click the status line for the menu.", nullptr, nullptr},
    {nullptr, "", nullptr, nullptr},
    {nullptr, "The beep sounds when you finish a word that is not in the English", nullptr, nullptr},
    {nullptr, "dictionary. Put the cursor on an underlined word to see suggestions.", nullptr, nullptr},
    {nullptr, "Replace changes whole words only and keeps capitals: John -> Peter.", nullptr, nullptr},
    {nullptr, "", nullptr, nullptr},
    {nullptr, "Documents are saved as Rich Text (.rtf), which Word, TextEdit and", nullptr, nullptr},
    {nullptr, "LibreOffice open with bold, italics, underline and strikethrough.", nullptr, nullptr},
    {nullptr, "Type a name ending in .docx for a Word file, or .txt for plain text.", nullptr, nullptr},
    {nullptr, "A name without a folder is saved in your Documents folder.", nullptr, nullptr},
    {nullptr, "", nullptr, nullptr},
    {nullptr, "Press Esc to return to your document.", nullptr, nullptr},
};

static QString help_text()
{
    QString out = QString("AMBER WRITER %1").arg(AMBER_VERSION);
    out += QString(COLUMNS + 4 - out.size() - 4, ' ') + "Help\n\n";
    for (const HelpRow &r : HELP_ROWS) {
        QString line;
        if (r.k1)
            line = keys(r.k1).leftJustified(11) + ' ';
        line += r.t1;
        if (r.k2) {
            line = line.leftJustified(42) + ' ';
            line += keys(r.k2).leftJustified(8) + ' ' + r.t2;
        }
        out += line + '\n';
    }
    if (MAC)
        out += "\nOn a Mac keyboard, hold fn for F1 to F12. Control works like Option.\n";
    out.chop(1);
    return out;
}

/* Prompts on the status line, WordPerfect style */

enum {
    P_NONE, P_SAVE_AS, P_REPLACE, P_SAVE_BEFORE_RETRIEVE, P_RETRIEVE, P_SAVE_BEFORE_EXIT, P_EXIT, P_SPELL,
    P_FIND, P_SWAP_CONFIRM, P_SWAP_FIND, P_SWAP_WITH, P_SWAP_EACH
};
enum { THEN_NONE, THEN_ASK_RETRIEVE, THEN_QUIT };

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

struct MenuItem {
    const char *label;
    const char *keys;
    int cmd;
};

struct Menu {
    const char *title;
    char letter;
    const MenuItem *items;
    int count;
};

static const MenuItem FILE_ITEMS[] = {
    {"Save", "Ctrl+S", C_SAVE},
    {"Save as...", "F10", C_SAVE_AS},
    {"Retrieve...", "Shift+F10", C_RETRIEVE},
    {"Print...", "Shift+F7  Ctrl+P", C_PRINT},
    {"Exit", "F7", C_EXIT},
};
static const MenuItem EDIT_ITEMS[] = {
    {"Undo", "Ctrl+Z", C_UNDO},
    {"Redo", "Ctrl+Y", C_REDO},
    {"Cut", "Ctrl+X", C_CUT},
    {"Copy", "Ctrl+C", C_COPY},
    {"Paste", "Ctrl+V", C_PASTE},
    {"Select all", "Ctrl+A", C_SELECT_ALL},
    {"Typeover", "Ins", C_TYPEOVER},
};
static const MenuItem SEARCH_ITEMS[] = {
    {"Search forward...", "F2", C_FIND},
    {"Search back...", "Shift+F2", C_FIND_BACK},
    {"Replace...", "Alt+F2", C_REPLACE},
};
static const MenuItem LAYOUT_ITEMS[] = {
    {"Left", "Ctrl+L", C_ALIGN_LEFT},
    {"Center", "Shift+F6  Ctrl+E", C_ALIGN_CENTER},
    {"Flush right", "Alt+F6  Ctrl+R", C_ALIGN_RIGHT},
};
static const MenuItem FONT_ITEMS[] = {
    {"Bold", "Ctrl+B  F6", C_BOLD},
    {"Italics", "Ctrl+I", C_ITALIC},
    {"Underline", "Ctrl+U  F8", C_UNDERLINE},
    {"Strikethrough", "Ctrl+Shift+X", C_STRIKE},
};
static const MenuItem TOOLS_ITEMS[] = {
    {"Next typo", "Alt+N", C_NEXT_TYPO},
    {"Previous typo", "Alt+P", C_PREV_TYPO},
    {"Spell check...", "Ctrl+F2", C_SPELL},
    {"Add word to dictionary", "Alt+A", C_ADD_WORD},
    {"Beep", "Alt+B", C_BEEP},
};
static const MenuItem VIEW_ITEMS[] = {
    {"Bigger text", "Ctrl+=", C_BIGGER},
    {"Smaller text", "Ctrl+-", C_SMALLER},
    {"Full screen", "F11", C_FULL_SCREEN},
    {"Hints on the status line", "", C_HINTS},
};
static const MenuItem HELP_ITEMS[] = {
    {"Keys and help", "F3", C_HELP},
    {"About Amber Writer", "", C_ABOUT},
};

#define ITEMS(a) a, (int)(sizeof a / sizeof a[0])
static const Menu MENUS[] = {
    {"File", 'F', ITEMS(FILE_ITEMS)},     {"Edit", 'E', ITEMS(EDIT_ITEMS)},
    {"Search", 'S', ITEMS(SEARCH_ITEMS)}, {"Layout", 'L', ITEMS(LAYOUT_ITEMS)},
    {"Font", 'O', ITEMS(FONT_ITEMS)},     {"Tools", 'T', ITEMS(TOOLS_ITEMS)},
    {"View", 'V', ITEMS(VIEW_ITEMS)},     {"Help", 'H', ITEMS(HELP_ITEMS)},
};
static const int MENU_COUNT = (int)(sizeof MENUS / sizeof MENUS[0]);

struct Tip {
    const char *keys;
    const char *label;
};

static const Tip CTRL_TIPS[] = {
    /* In rows of three on most screens */
    {"S", "Save"},         {"O", "Retrieve"},     {"P", "Print"},
    {"B", "Bold"},         {"I", "Italics"},      {"U", "Underline"},
    {"Shift+X", "Strike"}, {"F2", "Spell check"}, {"Q", "Exit"},
    {"L", "Left"},         {"E", "Center"},       {"R", "Flush right"},
    {"F", "Search"},       {"H", "Replace"},      {"A", "Select all"},
    {"Z", "Undo"},         {"Y", "Redo"},         {"=/-", "Text size"},
    {"X", "Cut"},          {"C", "Copy"},         {"V", "Paste"},
};
static const Tip ALT_TIPS[] = {
    {"Alt", "alone: Menu"}, {"=", "Menu"},        {"N", "Next typo"}, {"P", "Previous typo"},
    {"A", "Add word"},      {"B", "Beep on/off"}, {"F2", "Replace"},  {"F6", "Flush right"},
};
static const Tip FKEY_TIPS[] = {
    /* Pairs with and without Shift, for rows of two */
    {"F2", "Search"},   {"Shift+F2", "Search back"},
    {"F6", "Bold"},     {"Shift+F6", "Center"},
    {"F7", "Exit"},     {"Shift+F7", "Print"},
    {"F10", "Save as"}, {"Shift+F10", "Retrieve"},
    {"F3", "Help"},     {"F8", "Underline"},
    {"F11", "Full screen"},
};
#define COUNT(a) (int)(sizeof a / sizeof a[0])

/* Text between C and Qt. WCHAR and Qt's QChar are both UTF-16. */

static const WCHAR *W(const QString &s)
{
    return reinterpret_cast<const WCHAR *>(s.utf16());
}

static QString Q(const WCHAR *s, int n)
{
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(s), n);
}

static QString file_name(const QString &path)
{
    int at = path.lastIndexOf('/');
    return at < 0 ? path : path.mid(at + 1);
}

static QString extension(const QString &path)
{
    QString name = file_name(path);
    int dot = name.lastIndexOf('.');
    return dot < 0 ? QString() : name.mid(dot);
}

static bool has_ext(const QString &path, const char *ext)
{
    return extension(path).compare(ext, Qt::CaseInsensitive) == 0;
}

static bool file_exists(const QString &path)
{
    QFileInfo fi(path);
    return fi.exists() && !fi.isDir();
}

static QString trim(QString s)
{
    while (!s.isEmpty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '"'))
        s.chop(1);
    int start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '"'))
        start++;
    return s.mid(start);
}

static QString thousands(int n)
{
    QString raw = QString::number(n), out;
    for (int i = 0; i < raw.size(); i++) {
        if (i && (raw.size() - i) % 3 == 0)
            out += ',';
        out += raw[i];
    }
    return out;
}

static QString error_text(int err)
{
    QString s = QString::fromLocal8Bit(strerror(err ? err : EIO));
    return s.isEmpty() ? QString("error %1").arg(err) : s;
}

/* Lower case, one character for one, so positions stay the same. */
static QString fold_case(const QString &s)
{
    QString out = s;
    for (QChar &c : out)
        c = c.toLower();
    return out;
}

static bool ends_word(QChar c)
{
    if (c == '\r' || c == '\t')
        return true;
    if (c.unicode() < 32 || c == '\'' || c.unicode() == 127)
        return false;
    return !c.isLetterOrNumber();
}

static Qt::Alignment qt_align(BYTE a)
{
    return a == AL_CENTER ? Qt::AlignHCenter : a == AL_RIGHT ? Qt::AlignRight : a == AL_JUSTIFY ? Qt::AlignJustify : Qt::AlignLeft;
}

static BYTE align_of(Qt::Alignment a)
{
    if (a & Qt::AlignHCenter)
        return AL_CENTER;
    if (a & Qt::AlignRight)
        return AL_RIGHT;
    if (a & Qt::AlignJustify)
        return AL_JUSTIFY;
    return AL_LEFT;
}

static BYTE style_of(const QTextCharFormat &f)
{
    BYTE s = 0;
    if (f.fontWeight() > QFont::Medium)
        s |= ST_BOLD;
    if (f.fontItalic())
        s |= ST_ITALIC;
    if (f.fontUnderline())
        s |= ST_UNDERLINE;
    if (f.fontStrikeOut())
        s |= ST_STRIKE;
    return s;
}

static QTextCharFormat format_of(BYTE s)
{
    QTextCharFormat f;
    f.setFontWeight(s & ST_BOLD ? QFont::Bold : QFont::Normal);
    f.setFontItalic(s & ST_ITALIC);
    f.setFontUnderline(s & ST_UNDERLINE);
    f.setFontStrikeOut(s & ST_STRIKE);
    return f;
}

class Screen;

/* The page */

/* Qt draws its text cursors by inverting the screen, which shows white on black, so the
   page and the prompt hide Qt's and draw their own amber one. */
class Caret : public QObject {
public:
    explicit Caret(QWidget *w);
    QWidget *widget;
    QTimer timer;
    bool shown = true;
    void restart();
    void draw(QPainter &p, QRect r);
};

class Page : public QTextEdit {
public:
    explicit Page(Screen *s);
    Screen *screen;
    Caret caret;

protected:
    void paintEvent(QPaintEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void insertFromMimeData(const QMimeData *m) override;
    bool canInsertFromMimeData(const QMimeData *m) const override { return m->hasText(); }
    void contextMenuEvent(QContextMenuEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
};

/* The wavy amber line under unknown words, except the word being typed. It is drawn
   over the text and never gets into the document. */
class Squiggles : public QSyntaxHighlighter {
public:
    Squiggles(Screen *s, QTextDocument *doc);
    Screen *screen;
    QTextCharFormat wave;

protected:
    void highlightBlock(const QString &text) override;
};

/* The status line prompt */
class Prompt : public QLineEdit {
public:
    explicit Prompt(Screen *s);
    Screen *screen;
    Caret caret;

protected:
    void paintEvent(QPaintEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
};

/* The menu bar, the pull-down and the key template */
enum { PANEL_BAR = 1, PANEL_DROP, PANEL_TIPS };

class Panel : public QWidget {
public:
    Panel(Screen *s, int kind);
    Screen *screen;
    int kind;

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
};

class Screen : public QWidget {
public:
    Screen();
    ~Screen() override;

    Page *page;
    Squiggles *squiggles;
    Prompt *prompt;
    Panel *bar, *drop, *tips;

    QString face;
    QFont font;
    int font_size = 16, shown_size = 0, ch = 16, ascent = 12, wrap_w = 520;
    qreal cw = 8;
    int status_y = 0, status_h = 20;
    QString path, docs;
    bool beep_on = true, help = false, typeover = false, busy = false, quitting = false, fullscreen = false;
    bool was_maximized = true;
    QString message, counts, hint;
    bool hints = true;
    int typos = 0, pages = 1;
    /* True while a word is being typed, so it is not called a typo before it is finished. */
    bool typing = false;
    QString find_text, swap_with;
    bool find_back = false, swap_confirm = false;
    int swap_pos = 0, swap_start_pos = 0, swap_done = 0;

    int prompt_kind = P_NONE;
    bool prompt_default_yes = false;
    QString prompt_label, prompt_choices, prompt_path, open_after;
    int prompt_then = THEN_NONE;
    int spell_start = 0, spell_end = 0;
    QStringList spell_hints;
    int last_block = -1;
    bool squiggle_refresh_queued = false;

    QTimer counts_timer, message_timer, hold_timer;
    QPrinter *printer = nullptr;

    bool menu_open = false;
    int menu = 0, item = -1;
    int hold_key = 0, tips_key = 0;
    bool hold_other = false;

    /* Reading the page */
    QTextDocument *doc() const { return page->document(); }
    int length() const { return doc()->characterCount() - 1; }
    QString text() const { return doc()->toPlainText(); }
    QString range(int start, int end) const;
    int caret() const { return page->textCursor().position(); }
    void set_sel(int start, int end);
    bool being_typed(int start, int end) const;
    bool page_to_doc(Doc *d) const;
    void doc_to_page(const Doc *d);
    void clear_page();
    int total_lines() const;
    void caret_line(int *line, int *column) const;

    /* Layout and painting */
    QFont make_font(int points, qreal *cw_out, int *ch_out, int *ascent_out) const;
    void pick_font_face();
    void choose_font();
    void layout();
    QRect status_rect() const { return QRect(0, status_y, width(), status_h); }
    void update_status() { update(status_rect()); }
    QString right_text(int level) const;
    void paint_help(QPainter &p);
    void paint_status(QPainter &p);
    void update_title();
    void update_counts();
    void update_hint();
    void flash(const QString &text, int ms);
    void schedule_squiggles();
    void refresh_squiggles();

    /* Prompts */
    void ask(int kind, const QString &label, const QString &initial, const char *choices);
    void ask_yes_no(int kind, const QString &question, bool default_yes);
    void end_prompt();
    void place_prompt();
    void prompt_choice(QChar key);
    void prompt_enter(const QString &typed);

    /* Files */
    QString resolve(const QString &typed, bool retrieving) const;
    void quit();
    void ask_retrieve_name();
    void run_then(int then);
    void write_document(const QString &path, int then);
    void save_as(int then);
    void save_then(int then);
    void retrieve_path(const QString &path);
    void retrieve();
    void open_file(const QString &path);
    void exit_app();
    void print_document(const QString &pdf = QString());

    /* Spelling */
    bool word_at(int pos, int *start, int *end, QString *word) const;
    QString word_ending_at(int pos) const;
    QString hint_list(const QString &word, int limit, bool numbered);
    void misspelled(const QString &word);
    void play_beep();
    void spell_check();
    void spell_answer(char key);
    void add_word();
    void toggle_beep();
    void stop_typing();
    void jump_typo(bool forward);

    /* Search and replace */
    void find_prompt(bool back);
    void do_find();
    int replace_at(int start, int n);
    void swap_start();
    void swap_finish();
    void swap_all_from(int from);
    void swap_next();
    void swap_answer(char key);
    void swap_go();

    /* Formatting and view */
    void toggle_style(BYTE style);
    BYTE sel_align() const;
    void set_sel_align(BYTE align);
    void toggle_align(BYTE align);
    void resize_text(int step);
    void toggle_full_screen();
    void toggle_help();
    void toggle_hints();
    void toggle_typeover();
    void paste();

    /* Menu and key template */
    void run_command(int cmd);
    QString item_label(const MenuItem &it) const;
    int item_letter(const Menu &m, int index) const;
    int bar_x(const QFontMetrics &fm, int index, int *w) const;
    void open_menu(int index);
    void close_menu();
    void choose_item(int index);
    bool menu_key(QKeyEvent *e);
    void place_drop();
    int draw_tip_block(QPainter *p, const QString &prefix, const Tip *t, int count, int x, int y, int width, bool dim) const;
    void paint_tips(QPainter &p, QRect r);
    void show_tips(int key);
    void hide_tips();
    void cancel_hold();
    bool track_modifiers(QEvent *e);
    void hold_fired();
    bool handle_key(int key, bool ctrl, bool shift, bool alt);

    void after_char(const QString &word, bool title_ends);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;
};

/* Settings: ~/.config/AmberWriter/AmberWriter.conf on Linux, a .plist on a Mac. */

static QSettings &settings()
{
    static QSettings s("AmberWriter", "AmberWriter");
    return s;
}

/* Caret */

Caret::Caret(QWidget *w) : widget(w)
{
    int flash = QApplication::cursorFlashTime();
    timer.setInterval(flash > 0 ? flash / 2 : 530);
    connect(&timer, &QTimer::timeout, this, [this] {
        shown = !shown;
        widget->update();
    });
}

/* Shows the caret at once, as it moves or the window gets the focus. */
void Caret::restart()
{
    shown = true;
    if (QApplication::cursorFlashTime() > 0)
        timer.start();
    widget->update();
}

void Caret::draw(QPainter &p, QRect r)
{
    if (shown && widget->hasFocus())
        p.fillRect(QRect(r.x(), r.y(), 2, r.height()), AMBER);
}

/* Page */

Page::Page(Screen *s) : QTextEdit(s), screen(s), caret(viewport())
{
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAcceptRichText(false);
    setAcceptDrops(false);
    setLineWrapMode(QTextEdit::FixedPixelWidth);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setCursorWidth(0);
    setUndoRedoEnabled(true);
    connect(this, &QTextEdit::cursorPositionChanged, &caret, &Caret::restart);
    document()->setDocumentMargin(0);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, BLACK);
    pal.setColor(QPalette::Window, BLACK);
    pal.setColor(QPalette::Text, AMBER);
    pal.setColor(QPalette::Highlight, AMBER);
    pal.setColor(QPalette::HighlightedText, BLACK);
    pal.setColor(QPalette::Inactive, QPalette::Highlight, DIM_AMBER);
    pal.setColor(QPalette::Inactive, QPalette::HighlightedText, BLACK);
    setPalette(pal);
    viewport()->setAutoFillBackground(true);
}

void Page::paintEvent(QPaintEvent *e)
{
    QTextEdit::paintEvent(e);
    QPainter p(viewport());
    caret.draw(p, cursorRect());
}

void Page::focusInEvent(QFocusEvent *e)
{
    QTextEdit::focusInEvent(e);
    caret.restart();
}

void Page::focusOutEvent(QFocusEvent *e)
{
    QTextEdit::focusOutEvent(e);
    caret.timer.stop();
    viewport()->update();
}

void Page::keyPressEvent(QKeyEvent *e)
{
    Qt::KeyboardModifiers mods = e->modifiers();
    /* On a Mac, Option types accented letters, so only Cmd makes a key a command. */
    bool command = (mods & Qt::ControlModifier) || (!MAC && (mods & Qt::AltModifier));
    QTextCursor cur = textCursor();
    QChar c;
    QString word;
    bool title_ends = false;
    int key = e->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter)
        c = '\r';
    else if (key == Qt::Key_Backspace)
        c = QChar(8);
    else if (key == Qt::Key_Tab)
        c = '\t';
    else if (!e->text().isEmpty())
        c = e->text()[0];
    if (!command && !c.isNull()) {
        if (!cur.hasSelection() && ends_word(c))
            word = screen->word_ending_at(cur.position());
        if (is_word_char(c.unicode()) || c == '\'')
            screen->typing = true;
        else if (c.unicode() != 8)
            screen->typing = false; /* the word is finished; Backspace keeps editing it */
    }
    if (!command && c == '\r' && !cur.hasSelection() && screen->sel_align() != AL_LEFT) {
        /* Like WordPerfect's Center, a centered or flush right line ends at Enter:
           the next paragraph starts back at the left. */
        title_ends = cur.atBlockEnd();
    }
    if (c == '\r' && (mods & Qt::ShiftModifier)) {
        /* Shift+Enter would start a new line inside the paragraph; Amber Writer only has
           paragraphs, as WordPerfect only had hard returns. */
        QKeyEvent plain(QEvent::KeyPress, Qt::Key_Return, mods & ~Qt::ShiftModifier, "\r");
        QTextEdit::keyPressEvent(&plain);
    } else {
        QTextEdit::keyPressEvent(e);
    }
    screen->after_char(word, title_ends);
}

void Page::insertFromMimeData(const QMimeData *m)
{
    /* Pasted text takes the style of the place it goes, as in amber.c. */
    QString t = m->text();
    t.replace("\r\n", "\n");
    t.replace('\r', '\n');
    t.replace(QChar::LineSeparator, '\n');
    t.replace(QChar::ParagraphSeparator, '\n');
    textCursor().insertText(t);
    ensureCursorVisible();
}

void Page::contextMenuEvent(QContextMenuEvent *e)
{
    e->accept();
    screen->open_menu(0);
}

void Page::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        screen->stop_typing();
    if (e->button() == Qt::RightButton) {
        e->accept();
        return;
    }
    QTextEdit::mousePressEvent(e);
}

/* Squiggles */

Squiggles::Squiggles(Screen *s, QTextDocument *doc) : QSyntaxHighlighter(doc), screen(s)
{
    wave.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    wave.setUnderlineColor(AMBER);
}

void Squiggles::highlightBlock(const QString &text)
{
    const WCHAR *w = W(text);
    int base = currentBlock().position(), pos = 0, s, e;
    while (next_word(w, text.size(), pos, &s, &e)) {
        pos = e;
        if (screen->being_typed(base + s, base + e))
            continue;
        if (!spell_is_correct(w + s, e - s))
            setFormat(s, e - s, wave);
    }
}

/* Prompt */

/* A style that gives the prompt no cursor of its own */
class NoCursorStyle : public QProxyStyle {
public:
    int pixelMetric(PixelMetric m, const QStyleOption *o = nullptr, const QWidget *w = nullptr) const override
    {
        return m == PM_TextCursorWidth ? 0 : QProxyStyle::pixelMetric(m, o, w);
    }
};

Prompt::Prompt(Screen *s) : QLineEdit(s), screen(s), caret(this)
{
    NoCursorStyle *style = new NoCursorStyle;
    style->setParent(this);
    setStyle(style);
    connect(this, &QLineEdit::cursorPositionChanged, &caret, &Caret::restart);
    connect(this, &QLineEdit::selectionChanged, &caret, &Caret::restart);
    setFrame(false);
    setTextMargins(0, 0, 0, 0);
    setContentsMargins(0, 0, 0, 0);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, BLACK);
    pal.setColor(QPalette::Window, BLACK);
    pal.setColor(QPalette::Text, AMBER);
    pal.setColor(QPalette::Highlight, AMBER);
    pal.setColor(QPalette::HighlightedText, BLACK);
    setPalette(pal);
    hide();
}

void Prompt::paintEvent(QPaintEvent *e)
{
    QLineEdit::paintEvent(e);
    QPainter p(this);
    caret.draw(p, cursorRect().adjusted(cursorRect().width() / 2 + 1, 0, 0, 0));
}

void Prompt::focusInEvent(QFocusEvent *e)
{
    QLineEdit::focusInEvent(e);
    caret.restart();
}

void Prompt::keyPressEvent(QKeyEvent *e)
{
    int key = e->key();
    if (key == Qt::Key_Escape) {
        int kind = screen->prompt_kind;
        screen->end_prompt();
        if (kind == P_SWAP_EACH)
            screen->swap_finish();
        return;
    }
    /* In WordPerfect, F2 starts the search, so F2 F2 finds the next one. */
    if (key == Qt::Key_F2 && (screen->prompt_kind == P_FIND || screen->prompt_kind == P_SWAP_FIND))
        key = Qt::Key_Return;
    if (!screen->prompt_choices.isEmpty()) {
        QChar c = key == Qt::Key_Return || key == Qt::Key_Enter ? QChar('\r')
                  : e->text().isEmpty()                         ? QChar()
                                                                : e->text()[0].toLower();
        if (!c.isNull() && !(e->modifiers() & Qt::ControlModifier) && screen->prompt_choices.contains(c))
            screen->prompt_choice(c);
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        screen->prompt_enter(QLineEdit::text());
        return;
    }
    QLineEdit::keyPressEvent(e);
}

/* Panels */

Panel::Panel(Screen *s, int k) : QWidget(s), screen(s), kind(k)
{
    setMouseTracking(true);
    hide();
}

void Panel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    Screen *s = screen;
    QFontMetrics fm(s->font);
    QRect r = rect();
    p.setFont(s->font);
    if (kind == PANEL_BAR) {
        /* An amber bar with black writing, as in WordPerfect 5.1. */
        p.fillRect(r, AMBER);
        for (int i = 0; i < MENU_COUNT; i++) {
            QString title = QString(" %1 ").arg(MENUS[i].title);
            int w, x = s->bar_x(fm, i, &w);
            bool sel = i == s->menu;
            p.fillRect(QRect(x, 2, w, s->ch), sel ? BLACK : AMBER);
            p.setPen(sel ? AMBER : BLACK);
            p.drawText(x, 2 + s->ascent, title);
            int at = title.indexOf(QChar(MENUS[i].letter), 0, Qt::CaseInsensitive);
            if (at >= 0) {
                int lx = x + fm.horizontalAdvance(title.left(at));
                int y = 2 + s->ascent + 2;
                p.drawLine(lx, y, lx + fm.horizontalAdvance(title.mid(at, 1)) - 1, y);
            }
        }
    } else if (kind == PANEL_DROP) {
        const Menu &m = MENUS[s->menu];
        p.fillRect(r, BLACK);
        p.setPen(AMBER);
        p.drawRect(r.adjusted(0, 0, -1, -1));
        for (int i = 0; i < m.count; i++) {
            QRect row(2, 4 + i * s->ch, r.width() - 4, s->ch);
            bool sel = i == s->item;
            QString label = s->item_label(m.items[i]);
            QString k = keys(m.items[i].keys);
            p.fillRect(row, sel ? AMBER : BLACK);
            p.setPen(sel ? BLACK : AMBER);
            int x = qRound(s->cw * 2), y = row.top() + s->ascent;
            p.drawText(x, y, label);
            int at = s->item_letter(m, i);
            if (at >= 0) {
                int lx = x + fm.horizontalAdvance(label.left(at));
                p.drawLine(lx, y + 2, lx + fm.horizontalAdvance(label.mid(at, 1)) - 1, y + 2);
            }
            p.setPen(sel ? BLACK : DIM_AMBER);
            p.drawText(r.width() - qRound(s->cw * 2) - fm.horizontalAdvance(k), y, k);
        }
    } else {
        p.fillRect(r, BLACK);
        s->paint_tips(p, r);
    }
}

void Panel::mouseMoveEvent(QMouseEvent *e)
{
    if (kind != PANEL_DROP)
        return;
    int y = e->position().toPoint().y(), row = (y - 4) / screen->ch;
    if (y >= 4 && row >= 0 && row < MENUS[screen->menu].count && row != screen->item) {
        screen->item = row;
        update();
    }
}

void Panel::mousePressEvent(QMouseEvent *e)
{
    QPoint at = e->position().toPoint();
    if (kind == PANEL_BAR) {
        QFontMetrics fm(screen->font);
        for (int i = 0; i < MENU_COUNT; i++) {
            int w, left = screen->bar_x(fm, i, &w);
            if (at.x() >= left && at.x() < left + w) {
                screen->open_menu(i);
                break;
            }
        }
    } else if (kind == PANEL_DROP) {
        int row = (at.y() - 4) / screen->ch;
        if (at.y() >= 4 && row >= 0 && row < MENUS[screen->menu].count)
            screen->choose_item(row);
    } else {
        screen->hide_tips();
    }
}

/* Screen */

Screen::Screen()
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 200);
    page = new Page(this);
    squiggles = new Squiggles(this, page->document());
    prompt = new Prompt(this);
    bar = new Panel(this, PANEL_BAR);
    drop = new Panel(this, PANEL_DROP);
    tips = new Panel(this, PANEL_TIPS);

    counts_timer.setSingleShot(true);
    message_timer.setSingleShot(true);
    hold_timer.setSingleShot(true);
    connect(&counts_timer, &QTimer::timeout, this, [this] { update_counts(); });
    connect(&message_timer, &QTimer::timeout, this, [this] {
        message.clear();
        update_status();
    });
    connect(&hold_timer, &QTimer::timeout, this, [this] { hold_fired(); });
    connect(page->document(), &QTextDocument::contentsChanged, this, [this] {
        if (busy)
            return;
        counts_timer.start(250);
        update_hint();
    });
    connect(page->document(), &QTextDocument::modificationChanged, this, [this] { update_title(); });
    connect(page, &QTextEdit::cursorPositionChanged, this, [this] {
        if (busy)
            return;
        update_status();
        schedule_squiggles();
        update_hint();
    });
    connect(page, &QTextEdit::selectionChanged, this, [this] {
        if (!busy)
            update_status();
    });

    QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty())
        docs = QDir::homePath();
    QDir().mkpath(docs);
    /* The same place the Python version keeps its word list, so both share it. */
    QString words;
    if (!data.isEmpty() && QDir().mkpath(data))
        words = data + "/words.txt";
    spell_init(words.isEmpty() ? nullptr : W(words));

    beep_on = settings().value("Beep", 1).toInt() != 0;
    hints = settings().value("Hints", 1).toInt() != 0;
    font_size = settings().value("FontSize", 16).toInt();
    if (font_size < 9 || font_size > 40)
        font_size = 16;
    pick_font_face();
    qApp->installEventFilter(this);
    resize(1100, 760);
    clear_page();
    update_counts();
    update_title();
}

Screen::~Screen()
{
    qApp->removeEventFilter(this);
    delete printer;
}

/* Reading the page */

QString Screen::range(int start, int end) const
{
    int len = length();
    start = qBound(0, start, len);
    end = qBound(start, end, len);
    QTextCursor c(doc());
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    QString t = c.selectedText();
    t.replace(QChar::ParagraphSeparator, '\r');
    t.replace(QChar::LineSeparator, '\r');
    return t;
}

void Screen::set_sel(int start, int end)
{
    int len = length();
    QTextCursor c(doc());
    c.setPosition(qBound(0, start, len));
    c.setPosition(qBound(0, end, len), QTextCursor::KeepAnchor);
    page->setTextCursor(c);
}

bool Screen::being_typed(int start, int end) const
{
    QTextCursor c = page->textCursor();
    return typing && !c.hasSelection() && c.position() >= start && c.position() <= end;
}

/* Reads the page into a Doc: the text, the style of every character and the alignment
   of every paragraph. */
bool Screen::page_to_doc(Doc *d) const
{
    static const WCHAR cr = '\r';
    int para = 0;
    doc_init(d);
    for (QTextBlock b = doc()->begin(); b.isValid(); b = b.next()) {
        BYTE align = align_of(b.blockFormat().alignment());
        if (b != doc()->begin() && !doc_append(d, &cr, 1, 0))
            return false;
        doc_set_align(d, para++, align);
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            QTextFragment f = it.fragment();
            if (!f.isValid())
                continue;
            QString t = f.text();
            BYTE style = style_of(f.charFormat());
            int start = 0;
            for (int i = 0; i <= t.size(); i++) {
                if (i < t.size() && t[i] != QChar::LineSeparator && t[i] != QChar::ParagraphSeparator)
                    continue;
                if (i > start && !doc_append(d, W(t) + start, i - start, style))
                    return false;
                if (i < t.size()) {
                    doc_append(d, &cr, 1, 0);
                    doc_set_align(d, para++, align);
                }
                start = i + 1;
            }
        }
    }
    if (!d->text) {
        static const WCHAR empty = 0;
        doc_append(d, &empty, 0, 0);
    }
    return d->text != nullptr;
}

static bool page_has_formatting(const Doc *d)
{
    int para = 0;
    for (int i = 0; i < d->len; i++) {
        if (d->style[i] && d->text[i] != '\r')
            return true;
        if (d->text[i] == '\r')
            para++;
    }
    for (int i = 0; i <= para; i++)
        if (doc_align(d, i) != AL_LEFT)
            return true;
    return false;
}

void Screen::doc_to_page(const Doc *d)
{
    bool was_busy = busy;
    busy = true;
    QTextDocument *document = doc();
    document->setUndoRedoEnabled(false);
    document->clear();
    QTextCursor c(document);
    QTextBlockFormat bf;
    int pos = 0, para = 0;
    c.beginEditBlock();
    bf.setAlignment(qt_align(doc_align(d, 0)));
    c.setBlockFormat(bf);
    c.setBlockCharFormat(format_of(0));
    while (pos < d->len) {
        if (d->text[pos] == '\r' || d->text[pos] == '\n') {
            QTextBlockFormat next;
            next.setAlignment(qt_align(doc_align(d, ++para)));
            c.insertBlock(next, format_of(0));
            pos++;
            continue;
        }
        int end = pos;
        BYTE style = d->style[pos];
        while (end < d->len && d->text[end] != '\r' && d->text[end] != '\n' && d->style[end] == style)
            end++;
        c.insertText(Q(d->text + pos, end - pos), format_of(style));
        pos = end;
    }
    c.endEditBlock();
    document->setUndoRedoEnabled(true);
    document->setModified(false);
    QTextCursor start(document);
    page->setTextCursor(start);
    page->setCurrentCharFormat(format_of(0));
    page->ensureCursorVisible();
    last_block = -1;
    busy = was_busy;
}

void Screen::clear_page()
{
    Doc empty;
    static const WCHAR nothing = 0;
    doc_init(&empty);
    doc_append(&empty, &nothing, 0, 0);
    doc_to_page(&empty);
    doc_free(&empty);
}

/* Lines on the page, as they are wrapped on the screen. */
int Screen::total_lines() const
{
    int lines = 0;
    /* Lays out the whole document, which Qt otherwise does a little at a time. */
    doc()->documentLayout()->blockBoundingRect(doc()->lastBlock());
    for (QTextBlock b = doc()->begin(); b.isValid(); b = b.next())
        lines += qMax(1, b.layout() ? b.layout()->lineCount() : 1);
    return lines;
}

/* The line the caret is on, counted from the top of the document, and its column. */
void Screen::caret_line(int *line, int *column) const
{
    int pos = caret(), before = 0;
    QTextBlock block = doc()->findBlock(pos);
    doc()->documentLayout()->blockBoundingRect(block);
    for (QTextBlock b = doc()->begin(); b.isValid() && b != block; b = b.next())
        before += qMax(1, b.layout() ? b.layout()->lineCount() : 1);
    int rel = pos - block.position();
    QTextLine ln = block.layout() ? block.layout()->lineForTextPosition(rel) : QTextLine();
    if (!ln.isValid()) {
        *line = before;
        *column = rel;
        return;
    }
    *line = before + ln.lineNumber();
    *column = rel - ln.textStart();
    /* Centered and flush right lines start part way across the page. */
    qreal x = ln.cursorToX(ln.textStart());
    if (x > 0)
        *column += int((x + cw / 2) / cw);
}

/* Fonts */

void Screen::pick_font_face()
{
    QStringList families = QFontDatabase::families();
    for (const char *f : FONT_CHOICES) {
        if (families.contains(f, Qt::CaseInsensitive)) {
            face = f;
            return;
        }
    }
    face = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

/* Sizes are in points at 96 dots an inch, as on Windows, so the text is the same size
   on every system. */
QFont Screen::make_font(int points, qreal *cw_out, int *ch_out, int *ascent_out) const
{
    QFont f(face);
    f.setPixelSize(qRound(points * 96.0 / 72.0));
    f.setStyleHint(QFont::TypeWriter);
    f.setFixedPitch(true);
    f.setKerning(false);
    QFontMetricsF fm(f);
    *cw_out = fm.horizontalAdvance("MMMMMMMMMM") / 10.0;
    *ch_out = qCeil(fm.height());
    *ascent_out = qRound(fm.ascent());
    return f;
}

void Screen::choose_font()
{
    int size = font_size, c_h, asc;
    qreal c_w;
    QFont f;
    /* Shrink the text when 65 columns would not fit, as on an 800x600 screen. */
    for (;;) {
        f = make_font(size, &c_w, &c_h, &asc);
        if (size <= 8 || width() == 0 || COLUMNS * c_w + 8 <= width() - 16)
            break;
        size--;
    }
    font = f;
    cw = c_w;
    ch = c_h;
    ascent = asc;
    wrap_w = qCeil(COLUMNS * cw - 0.01);
    if (size != shown_size) {
        shown_size = size;
        doc()->setDefaultFont(font);
        page->setFont(font);
        page->setTabStopDistance(TAB_COLUMNS * cw);
        prompt->setFont(font);
    }
}

void Screen::layout()
{
    int top = 16, bottom = 10;
    choose_font();
    status_h = ch + 4;
    status_y = height() - bottom - status_h;
    int text_w = wrap_w + 4, x = qMax(0, (width() - text_w) / 2);
    int edit_h = qMax(ch, status_y - 6 - top);
    page->setGeometry(x, top, text_w, edit_h);
    page->setLineWrapColumnOrWidth(wrap_w);
    update();
}

/* Status line */

void Screen::flash(const QString &text, int ms)
{
    message = text;
    message_timer.start(ms);
    update_status();
}

void Screen::update_title()
{
    setWindowTitle(QString("%1%2 - Amber Writer").arg(path.isEmpty() ? QString("Untitled") : file_name(path),
                                                      doc()->isModified() ? " *" : ""));
}

void Screen::update_counts()
{
    QString t = text();
    const WCHAR *w = W(t);
    int len = t.size(), words = 0, chars = 0, pos = 0, s, e;
    typos = 0;
    while (next_word(w, len, pos, &s, &e)) {
        words++;
        if (!spell_is_correct(w + s, e - s) && !being_typed(s, e))
            typos++;
        pos = e;
    }
    for (int i = 0; i < len; i++)
        if (w[i] != '\r' && w[i] != '\n')
            chars++;
    int lines = total_lines();
    pages = (lines - 1) / LINES_PER_PAGE + 1;
    counts = QString("Words %1  Chars %2  Pages %3").arg(thousands(words), thousands(chars)).arg(pages);
    update_status();
}

/* The right of the status line. Level 0 has everything; 1 leaves out the word,
   character and page counts; 2 is just the typo count. */
QString Screen::right_text(int level) const
{
    int line, column;
    caret_line(&line, &column);
    int page_no = line / LINES_PER_PAGE + 1, on_page = line % LINES_PER_PAGE;
    int ln = 100 + (on_page * 100 + LINES_PER_INCH / 2) / LINES_PER_INCH;
    int pos = CHARS_PER_INCH + column;
    if (level >= 2)
        return QString("Typos %1").arg(thousands(typos));
    return QString("%1%2Typos %3   Pg %4 Ln %5.%6\" Pos %7.%8\"")
        .arg(level == 0 ? counts : QString(), level == 0 ? "  " : "", thousands(typos))
        .arg(page_no)
        .arg(ln / 100)
        .arg(ln % 100, 2, 10, QChar('0'))
        .arg(pos / 10)
        .arg(pos % 10);
}

void Screen::paint_help(QPainter &p)
{
    QString help = help_text();
    QFont f = font;
    int size = shown_size;
    QRect box = QFontMetrics(f).boundingRect(QRect(0, 0, 10000, 10000), Qt::TextExpandTabs, help);
    /* Smaller text when the help would not fit, as on a 1024x768 screen. */
    while ((box.height() > status_y - 8 || box.width() > width() - 16) && size > 7) {
        qreal c_w;
        int c_h, asc;
        f = make_font(--size, &c_w, &c_h, &asc);
        box = QFontMetrics(f).boundingRect(QRect(0, 0, 10000, 10000), Qt::TextExpandTabs, help);
    }
    int left = qMax(8, (width() - box.width()) / 2), top = qMax(8, (status_y - box.height()) / 2);
    p.setFont(f);
    p.setPen(AMBER);
    p.drawText(QRect(left, top, box.width() + 4, box.height() + 4), Qt::TextExpandTabs, help);
}

void Screen::paint_status(QPainter &p)
{
    QFontMetrics fm(font);
    QString right;
    p.setFont(font);
    p.setPen(AMBER);
    /* A prompt gets the whole status line, as in WordPerfect. */
    if (prompt_kind == P_NONE) {
        QString note = !message.isEmpty() ? message : hint;
        int level = 0;
        int need = fm.horizontalAdvance(note);
        /* On a narrow screen a message pushes the counts aside until it goes. */
        for (;;) {
            right = right_text(level);
            if (note.isEmpty() || level == 2 || need <= width() - 48 - fm.horizontalAdvance(right) - cw * 2)
                break;
            level++;
        }
    }
    QRect r = status_rect();
    int right_w = fm.horizontalAdvance(right);
    p.drawText(QRect(r.left(), r.top(), r.width() - 24, r.height()), Qt::AlignRight | Qt::AlignVCenter, right);
    QRect lr(24, r.top(), qMax(0, int(width() - 24 - right_w - cw * 2) - 24), r.height());
    if (prompt_kind != P_NONE) {
        p.drawText(QRect(24, r.top(), width() - 48, r.height()), Qt::AlignLeft | Qt::AlignVCenter, prompt_label);
        return;
    }
    QString left = !message.isEmpty() ? message : !hint.isEmpty() ? hint : path;
    bool showing_path = message.isEmpty() && hint.isEmpty();
    if (showing_path && typeover)
        left = "Typeover  " + path;
    if (showing_path && fm.horizontalAdvance(left) > lr.width())
        left = (typeover ? "Typeover  " : "") + file_name(path);
    left = fm.elidedText(left, Qt::ElideRight, lr.width());
    p.drawText(lr, Qt::AlignLeft | Qt::AlignVCenter, left);
    if (hints && showing_path) {
        /* A faint reminder of where the commands are, as long as it fits. */
        const QString tips_text[] = {
            MAC ? "Tap Option for the menu, hold Cmd or Option for keys, F3 help"
                : "Tap Alt for the menu, hold Ctrl or Alt for keys, F3 help",
            MAC ? "Option: menu  Hold Cmd/Option: keys" : "Alt: menu  Hold Ctrl/Alt: keys",
            MAC ? "Option: menu" : "Alt: menu",
        };
        int used = fm.horizontalAdvance(left);
        if (used)
            used += qRound(cw * 3);
        for (const QString &h : tips_text) {
            if (used + fm.horizontalAdvance(h) <= lr.width()) {
                p.setPen(DIM_AMBER);
                p.drawText(lr.adjusted(used, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, h);
                p.setPen(AMBER);
                break;
            }
        }
    }
}

void Screen::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), BLACK);
    if (help)
        paint_help(p);
    paint_status(p);
}

void Screen::resizeEvent(QResizeEvent *)
{
    layout();
    place_prompt();
    if (menu_open)
        open_menu(menu);
    hide_tips();
}

void Screen::mousePressEvent(QMouseEvent *e)
{
    /* A click on the status line opens the menu too. */
    if (prompt_kind == P_NONE && e->position().y() >= status_y)
        open_menu(0);
}

void Screen::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::ActivationChange && !isActiveWindow()) {
        cancel_hold();
        close_menu();
    }
    QWidget::changeEvent(e);
}

void Screen::closeEvent(QCloseEvent *e)
{
    if (quitting || !doc()->isModified()) {
        e->accept();
        return;
    }
    e->ignore();
    if (help)
        toggle_help();
    if (prompt_kind != P_NONE)
        end_prompt();
    exit_app();
}

/* The squiggles follow the caret: the word being typed has none until it is finished. */
void Screen::schedule_squiggles()
{
    if (squiggle_refresh_queued)
        return;
    squiggle_refresh_queued = true;
    QTimer::singleShot(0, this, [this] { refresh_squiggles(); });
}

void Screen::refresh_squiggles()
{
    squiggle_refresh_queued = false;
    if (busy)
        return;
    int now = page->textCursor().block().blockNumber();
    QTextBlock b = doc()->findBlockByNumber(now);
    if (b.isValid())
        squiggles->rehighlightBlock(b);
    if (last_block >= 0 && last_block != now) {
        QTextBlock old = doc()->findBlockByNumber(last_block);
        if (old.isValid())
            squiggles->rehighlightBlock(old);
    }
    last_block = now;
}

/* Prompts */

void Screen::place_prompt()
{
    if (prompt_kind == P_NONE)
        return;
    QFontMetrics fm(font);
    int x = 24 + fm.horizontalAdvance(prompt_label);
    int w = width() - 24 - x;
    if (!prompt_choices.isEmpty())
        w = qRound(cw * 2);
    if (w < cw * 4)
        w = qRound(cw * 4);
    prompt->setGeometry(x, status_y + (status_h - ch) / 2, w, ch);
}

void Screen::ask(int kind, const QString &label, const QString &initial, const char *choices)
{
    message_timer.stop();
    message.clear();
    prompt_kind = kind;
    prompt_label = label;
    prompt_choices = choices ? QString::fromLatin1(choices) : QString();
    prompt->setReadOnly(choices != nullptr);
    prompt->setText(initial);
    place_prompt();
    prompt->show();
    prompt->setFocus();
    /* As in WordPerfect, typing replaces the suggested name; Enter keeps it. */
    prompt->selectAll();
    update_status();
}

void Screen::ask_yes_no(int kind, const QString &question, bool default_yes)
{
    prompt_default_yes = default_yes;
    ask(kind, QString("%1 %2: ").arg(question, default_yes ? "(Yes) No" : "Yes (No)"), QString(), "yn\r");
}

void Screen::end_prompt()
{
    prompt_kind = P_NONE;
    prompt_choices.clear();
    prompt->hide();
    if (help)
        setFocus();
    else
        page->setFocus();
    update_status();
}

void Screen::prompt_choice(QChar c)
{
    char key = (char)c.unicode();
    int kind = prompt_kind;
    bool yes = key == 'y' || (key == '\r' && prompt_default_yes);
    end_prompt();
    switch (kind) {
    case P_REPLACE:
        if (yes)
            write_document(prompt_path, prompt_then);
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
        swap_confirm = yes;
        ask(P_SWAP_FIND, "-> Srch: ", find_text, nullptr);
        break;
    case P_SWAP_EACH:
        swap_answer(key);
        break;
    }
}

void Screen::prompt_enter(const QString &typed)
{
    int kind = prompt_kind;
    end_prompt();
    if (kind == P_FIND || kind == P_SWAP_FIND || kind == P_SWAP_WITH) {
        /* Search text is kept as typed, spaces and all. */
        if (typed.isEmpty())
            return;
        if (kind == P_FIND) {
            find_text = typed;
            do_find();
        } else if (kind == P_SWAP_FIND) {
            find_text = typed;
            ask(P_SWAP_WITH, "Replace with: ", swap_with, nullptr);
        } else {
            swap_with = typed;
            swap_go();
        }
        return;
    }
    QString name = trim(typed);
    if (name.isEmpty())
        return;
    if (kind == P_SAVE_AS) {
        QString to = resolve(name, false);
        if (file_exists(to) && to != path) {
            prompt_path = to;
            ask_yes_no(P_REPLACE, QString("Replace %1?").arg(to), false);
        } else {
            write_document(to, prompt_then);
        }
    } else if (kind == P_RETRIEVE) {
        retrieve_path(resolve(name, true));
    }
}

/* Files */

/* A name without a folder goes in Documents; one without an extension gets .rtf. When
   retrieving, an existing .rtf, .docx or .txt of that name is found. */
QString Screen::resolve(const QString &typed, bool retrieving) const
{
    QString name = trim(typed);
    if (name == "~" || name.startsWith("~/"))
        name = QDir::homePath() + name.mid(1);
    QString out = QDir::cleanPath(QDir::isAbsolutePath(name) ? name : docs + "/" + name);
    if (extension(out).isEmpty()) {
        if (retrieving) {
            for (const char *ext : {".rtf", ".docx", ".txt"})
                if (file_exists(out + ext))
                    return out + ext;
        }
        out += ".rtf";
    }
    return out;
}

void Screen::quit()
{
    quitting = true;
    close();
}

void Screen::ask_retrieve_name()
{
    if (!open_after.isEmpty()) {
        QString p = open_after;
        open_after.clear();
        retrieve_path(p);
        return;
    }
    ask(P_RETRIEVE, "Document to be retrieved: ", QString(), nullptr);
}

void Screen::run_then(int then)
{
    if (then == THEN_ASK_RETRIEVE)
        ask_retrieve_name();
    else if (then == THEN_QUIT)
        quit();
}

void Screen::write_document(const QString &to, int then)
{
    Doc d;
    if (!page_to_doc(&d)) {
        doc_free(&d);
        flash("ERROR: not enough memory to save", 6000);
        return;
    }
    bool formatted = page_has_formatting(&d), ok;
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (to.size() >= MAX_PATH) {
        ok = false;
        SetLastError(ENAMETOOLONG);
    } else if (has_ext(to, ".txt")) {
        ok = save_txt(W(to), &d);
    } else if (has_ext(to, ".docx")) {
        ok = save_docx(W(to), &d);
    } else {
        ok = save_rtf(W(to), &d);
    }
    int err = (int)GetLastError();
    doc_free(&d);
    if (!ok) {
        flash(QString("ERROR: can't save %1: %2").arg(to, error_text(err)), 6000);
        return;
    }
    path = to;
    doc()->setModified(false);
    update_title();
    if (has_ext(to, ".txt") && formatted)
        flash(QString("Saved %1 as plain text, without bold, italics or centering. Use .rtf to keep them.").arg(file_name(to)),
              7000);
    else
        flash(QString("Saved %1").arg(to), 3500);
    run_then(then);
}

void Screen::save_as(int then)
{
    prompt_then = then;
    ask(P_SAVE_AS, "Document to be saved: ", path, nullptr);
}

void Screen::save_then(int then)
{
    if (!path.isEmpty())
        write_document(path, then);
    else
        save_as(then);
}

static bool looks_like_rtf(const BYTE *p, DWORD n)
{
    return n >= 5 && memcmp(p, "{\\rtf", 5) == 0;
}

void Screen::retrieve_path(const QString &from)
{
    bool exists = file_exists(from);
    if (exists) {
        DWORD size;
        BYTE *data = from.size() < MAX_PATH ? read_file(W(from), &size) : nullptr;
        Doc d;
        bool ok;
        if (!data) {
            flash(QString("ERROR: can't open %1: %2").arg(from, error_text((int)GetLastError())), 6000);
            return;
        }
        bool rtf = looks_like_rtf(data, size);
        if (rtf)
            ok = rtf_to_doc(data, size, &d);
        else if (has_ext(from, ".docx"))
            ok = docx_from_zip(data, size, &d);
        else
            ok = load_txt(W(from), &d);
        HeapFree(GetProcessHeap(), 0, data);
        if (!ok) {
            flash(QString("ERROR: can't open %1: not a Word document Amber Writer can read").arg(from), 6000);
            return;
        }
        /* Word ends its RTF with a paragraph mark, which the page does not show, as the
           Rich Edit control on Windows does not. */
        if (rtf && d.len > 0 && d.text[d.len - 1] == '\r')
            d.len--;
        doc_to_page(&d);
        doc_free(&d);
    } else {
        clear_page();
    }
    path = from;
    update_title();
    squiggles->rehighlight();
    update_counts();
    flash(QString(exists ? "Retrieved %1" : "New document %1").arg(from), 3500);
}

void Screen::retrieve()
{
    if (doc()->isModified())
        ask_yes_no(P_SAVE_BEFORE_RETRIEVE, "Save current document?", true);
    else
        ask_retrieve_name();
}

/* A document opened from the Finder or a file manager while Amber Writer is running. */
void Screen::open_file(const QString &file)
{
    if (help)
        toggle_help();
    if (prompt_kind != P_NONE)
        end_prompt();
    close_menu();
    open_after = QFileInfo(file).absoluteFilePath();
    retrieve();
}

void Screen::exit_app()
{
    if (doc()->isModified())
        ask_yes_no(P_SAVE_BEFORE_EXIT, "Save document?", true);
    else
        ask_yes_no(P_EXIT, "Exit Amber Writer?", false);
}

/* Printing */

/* Amber Writer lays out the printed page itself, the way WordPerfect printed: black
   Courier 12 point, ten characters an inch, so the 65 columns are 6.5 inches, at six
   lines an inch, breaking lines where the screen does. */

struct PrintPage {
    QPainter *p;
    QFont fonts[16]; /* by style */
    bool made[16] = {};
    QString family;
    qreal dpi_x, dpi_y, left, top, width;

    QFont &font(BYTE style)
    {
        style &= 15;
        if (!made[style]) {
            QFont f(family);
            f.setStyleHint(QFont::TypeWriter);
            f.setPointSizeF(12);
            f.setBold(style & ST_BOLD);
            f.setItalic(style & ST_ITALIC);
            f.setKerning(false);
            fonts[style] = QFont(f, p->device());
            made[style] = true;
        }
        return fonts[style];
    }

    /* Where a column starts, in printer dots from the left of the text. */
    qreal column_x(int col) const { return col * dpi_x / CHARS_PER_INCH; }
};

/* Prints text[from, to) as one line. Each character goes in its own column, so bold
   and italics line up with the rest. */
static void print_line(PrintPage &pp, const Doc *d, int from, int to, int width, BYTE align, int line)
{
    qreal x = pp.left, y = pp.top + (line * 4 + 3) * pp.dpi_y / (LINES_PER_INCH * 4); /* the baseline */
    int col = 0, i = from;
    if (align == AL_CENTER)
        x += (pp.width - pp.column_x(width)) / 2;
    else if (align == AL_RIGHT)
        x += pp.width - pp.column_x(width);
    while (i < to) {
        BYTE style = d->style[i];
        int start = col;
        if (d->text[i] == '\t') {
            col = (col / TAB_COLUMNS + 1) * TAB_COLUMNS;
            i++;
            continue;
        }
        QFont &f = pp.font(style);
        pp.p->setFont(f);
        while (i < to && d->text[i] != '\t' && d->style[i] == style) {
            /* A character and the accents and second halves that go with it */
            int n = 1;
            while (i + n < to && char_columns(d->text[i + n]) == 0)
                n++;
            pp.p->drawText(QPointF(x + pp.column_x(col), y), Q(d->text + i, n));
            col += char_columns(d->text[i]);
            i += n;
        }
        if (style & (ST_UNDERLINE | ST_STRIKE)) {
            QFontMetricsF fm(f, pp.p->device());
            QPen pen(Qt::black, qMax<qreal>(1, fm.lineWidth()));
            pp.p->setPen(pen);
            qreal x1 = x + pp.column_x(start), x2 = x + pp.column_x(col);
            /* Low enough to clear the letters, where a typewriter put it. */
            qreal under = qMax(fm.underlinePos(), fm.descent() * 0.5);
            if (style & ST_UNDERLINE)
                pp.p->drawLine(QPointF(x1, y + under), QPointF(x2, y + under));
            if (style & ST_STRIKE)
                pp.p->drawLine(QPointF(x1, y - fm.strikeOutPos()), QPointF(x2, y - fm.strikeOutPos()));
        }
    }
}

void Screen::print_document(const QString &pdf)
{
    QTextCursor sel = page->textCursor();
    int from = 1, to = INT_MAX, printed = 0, start = 0, end;
    Doc d;
    if (length() == 0) {
        flash("Nothing to print yet", 3500);
        return;
    }
    if (!printer)
        printer = new QPrinter(QPrinter::HighResolution);
    if (!pdf.isEmpty()) {
        printer->setOutputFormat(QPrinter::PdfFormat);
        printer->setOutputFileName(pdf);
        printer->setPageSize(QPageSize(QPageSize::Letter));
    } else {
        QPrintDialog dialog(printer, this);
        dialog.setWindowTitle("Print");
        dialog.setOption(QAbstractPrintDialog::PrintSelection, sel.hasSelection());
        dialog.setOption(QAbstractPrintDialog::PrintPageRange, true);
        dialog.setMinMax(1, qMax(1, pages));
        dialog.setFromTo(1, qMax(1, pages));
        bool ok = dialog.exec() == QDialog::Accepted;
        page->setFocus();
        if (!ok)
            return;
    }
    if (printer->printRange() == QPrinter::PageRange && printer->fromPage() > 0) {
        from = printer->fromPage();
        to = printer->toPage() > 0 ? printer->toPage() : INT_MAX;
    }
    if (!page_to_doc(&d)) {
        doc_free(&d);
        flash("ERROR: not enough memory to print", 6000);
        return;
    }
    end = d.len;
    if (printer->printRange() == QPrinter::Selection && sel.hasSelection()) {
        start = qMin(sel.selectionStart(), end);
        end = qMin(sel.selectionEnd(), end);
    }

    flash("Printing...", 60000);
    repaint();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    printer->setFullPage(true);
    printer->setDocName(path.isEmpty() ? QString("Untitled") : file_name(path));
    QPainter painter;
    bool ok = painter.begin(printer);
    PrintPage pp;
    pp.p = &painter;
    for (const char *f : PRINT_FONTS) {
        if (QFontDatabase::families().contains(f, Qt::CaseInsensitive)) {
            pp.family = f;
            break;
        }
    }
    if (pp.family.isEmpty())
        pp.family = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    if (ok) {
        /* 6.5 inches of text leave 1 inch margins on letter paper and a little less on A4. */
        pp.dpi_x = printer->logicalDpiX();
        pp.dpi_y = printer->logicalDpiY();
        QRectF paper = printer->paperRect(QPrinter::DevicePixel);
        qreal page_w = paper.width(), page_h = paper.height();
        if (page_w <= 0 || page_h <= 0) {
            page_w = pp.dpi_x * 8.5;
            page_h = pp.dpi_y * 11;
        }
        pp.width = pp.column_x(COLUMNS);
        pp.left = qMin((page_w - pp.width) / 2, pp.dpi_x);
        pp.top = pp.dpi_y;
        int lines = qMax(1, int((page_h - 2 * pp.dpi_y) * LINES_PER_INCH / pp.dpi_y));
        int pos = start, para = 0, page_no = 1, line = 0;
        bool started = false;
        for (int i = 0; i < start; i++)
            if (d.text[i] == '\r')
                para++;
        painter.setPen(Qt::black);
        while (ok && pos < end && page_no <= to) {
            bool render = page_no >= from;
            int line_end, width, next;
            if (line == 0 && render) {
                if (started)
                    ok = printer->newPage();
                started = true;
                painter.setPen(Qt::black);
            }
            wrap_line(d.text, pos, end, COLUMNS, TAB_COLUMNS, &line_end, &width, &next);
            if (render && ok) {
                painter.setPen(Qt::black);
                print_line(pp, &d, pos, line_end, width, doc_align(&d, para), line);
            }
            if (next > line_end && d.text[line_end] == '\r')
                para++;
            pos = next;
            if (++line == lines || pos >= end) {
                if (render && ok)
                    printed++;
                line = 0;
                page_no++;
            }
        }
        ok = painter.end() && ok;
    }
    QApplication::restoreOverrideCursor();
    doc_free(&d);
    if (ok && printed == 0)
        flash("Nothing printed: there are no pages in that range", 6000);
    else if (ok)
        flash(QString("Printed %1 page%2").arg(printed).arg(printed == 1 ? "" : "s"), 3500);
    else
        flash("Printing stopped: the printer did not take the pages", 6000);
}

/* Spelling */

/* The word that touches pos, as Alt+A sees it. */
bool Screen::word_at(int pos, int *start, int *end, QString *word) const
{
    int from = pos > MAX_WORD ? pos - MAX_WORD : 0;
    QString buf = range(from, pos + MAX_WORD);
    int at = 0, s, e;
    while (next_word(W(buf), buf.size(), at, &s, &e)) {
        if (from + s <= pos && pos <= from + e) {
            if (e - s >= MAX_WORD)
                return false;
            *start = from + s;
            *end = from + e;
            *word = buf.mid(s, e - s);
            return true;
        }
        at = e;
    }
    return false;
}

/* The word that ends exactly at pos, or an empty string. */
QString Screen::word_ending_at(int pos) const
{
    int from = pos > MAX_WORD ? pos - MAX_WORD : 0;
    QString buf = range(from, pos);
    int at = 0, s, e;
    while (next_word(W(buf), buf.size(), at, &s, &e)) {
        if (e == buf.size() && !(s == 0 && from > 0))
            return buf.mid(s, e - s);
        at = e;
    }
    return QString();
}

QString Screen::hint_list(const QString &word, int limit, bool numbered)
{
    char found[5][MAX_WORD];
    int n = spell_suggest(W(word), word.size(), found, limit);
    QString out;
    if (numbered)
        spell_hints.clear();
    for (int i = 0; i < n; i++) {
        QString w = QString::fromLatin1(found[i]);
        if (numbered) {
            out += QString("%1.%2  ").arg(i + 1).arg(w);
            spell_hints << w;
        } else {
            out += (i ? ", " : "") + w;
        }
    }
    return out;
}

void Screen::play_beep()
{
    if (!beep_play_sound())
        QApplication::beep();
}

void Screen::misspelled(const QString &word)
{
    if (beep_on)
        play_beep();
    QString hints_text = hint_list(word, 3, false);
    flash(hints_text.isEmpty() ? QString("Not found: %1").arg(word) : QString("Not found: %1  (%2)").arg(word, hints_text),
          3500);
}

void Screen::spell_check()
{
    QString t = text();
    const WCHAR *w = W(t);
    int len = t.size(), s, e;
    QTextCursor sel = page->textCursor();
    int sel_end = sel.selectionEnd(), pos = sel_end;
    /* Back up to the start of a word the cursor is inside. */
    while (pos > 0 && pos <= len && (is_word_char(w[pos - 1]) || w[pos - 1] == '\''))
        pos--;
    while (next_word(w, len, pos, &s, &e)) {
        pos = e;
        if (e <= sel_end)
            continue;
        if (!spell_is_correct(w + s, e - s)) {
            QString word = t.mid(s, qMin(e - s, MAX_WORD - 1));
            spell_start = s;
            spell_end = e;
            set_sel(s, e);
            page->ensureCursorVisible();
            QString numbered = hint_list(word, 5, true);
            QByteArray choices = "as";
            for (int i = 0; i < spell_hints.size(); i++)
                choices += char('1' + i);
            ask(P_SPELL, QString("Not found: %1   %2A.Add  S.Skip  Esc.Stop ").arg(word, numbered), QString(),
                choices.constData());
            return;
        }
    }
    flash("Spell check done", 3500);
}

void Screen::spell_answer(char key)
{
    QString word = range(spell_start, spell_end);
    if (key == 'a') {
        spell_add(W(word), word.size());
        squiggles->rehighlight();
        counts_timer.start(250);
    } else if (key >= '1' && key <= '5' && key - '1' < spell_hints.size()) {
        QString replacement = spell_hints[key - '1'];
        if (!word.isEmpty() && word[0] >= 'A' && word[0] <= 'Z' && !replacement.isEmpty() && replacement[0] >= 'a' &&
            replacement[0] <= 'z')
            replacement[0] = replacement[0].toUpper();
        set_sel(spell_start, spell_end);
        page->textCursor().insertText(replacement);
    } else {
        set_sel(spell_end, spell_end);
    }
    spell_check();
}

void Screen::add_word()
{
    int start, end;
    QString word;
    if (!word_at(caret(), &start, &end, &word)) {
        flash("No word at the cursor", 3500);
        return;
    }
    spell_add(W(word), word.size());
    squiggles->rehighlight();
    update_counts();
    update_hint();
    QString shown = word;
    for (QChar &c : shown)
        if (c >= 'A' && c <= 'Z')
            c = c.toLower();
    flash(QString("Added %1 to your dictionary").arg(shown), 3500);
}

void Screen::toggle_beep()
{
    beep_on = !beep_on;
    settings().setValue("Beep", beep_on ? 1 : 0);
    flash(beep_on ? "Beep on" : "Beep off", 3500);
    if (beep_on)
        play_beep();
}

/* Shows the suggestions on the status line whenever the cursor is on a typo. */
void Screen::update_hint()
{
    static QString last_word, last_hint;
    QString before = hint;
    int start, end;
    QString word;
    hint.clear();
    if (!page->textCursor().hasSelection() && word_at(caret(), &start, &end, &word) && !being_typed(start, end) &&
        !spell_is_correct(W(word), word.size())) {
        if (word != last_word) {
            QString h = hint_list(word, 3, false);
            /* Short, so it fits beside the counts on a 1024-wide screen. */
            last_hint = h.isEmpty() ? QString("%1: not in the dictionary").arg(word) : QString("%1 -> %2").arg(word, h);
            last_word = word;
        }
        hint = last_hint;
    }
    if (before != hint)
        update_status();
}

/* The cursor moved without typing: the word it was in is finished. */
void Screen::stop_typing()
{
    if (!typing)
        return;
    typing = false;
    counts_timer.start(250);
    schedule_squiggles();
}

/* After a key typed on the page: the beep for a finished word that is not in the
   dictionary, and the left alignment after a centered title. */
void Screen::after_char(const QString &word, bool title_ends)
{
    if (title_ends) {
        QTextCursor c = page->textCursor();
        QTextBlockFormat bf;
        bf.setAlignment(Qt::AlignLeft);
        c.joinPreviousEditBlock();
        c.mergeBlockFormat(bf);
        c.endEditBlock();
    }
    if (!word.isEmpty() && !spell_is_correct(W(word), word.size()))
        misspelled(word);
    schedule_squiggles();
}

/* Alt+N and Alt+P: the cursor goes to the start of the next or previous typo. */
void Screen::jump_typo(bool forward)
{
    stop_typing();
    QTextCursor sel = page->textCursor();
    int at = forward ? sel.selectionEnd() : sel.selectionStart();
    QString t = text();
    const WCHAR *w = W(t);
    int len = t.size(), pos = 0, s, e, total = 0, first = -1, last = -1, target = -1, target_index = 0;
    bool wrapped = false;
    while (next_word(w, len, pos, &s, &e)) {
        pos = e;
        if (spell_is_correct(w + s, e - s))
            continue;
        total++;
        if (first < 0)
            first = s;
        last = s;
        if ((forward && target < 0 && s > at) || (!forward && e < at)) {
            target = s;
            target_index = total;
        }
    }
    if (total == 0) {
        flash("No typos", 3000);
        return;
    }
    if (target < 0) {
        wrapped = true;
        target = forward ? first : last;
        target_index = forward ? 1 : total;
    }
    next_word(w, len, target, &s, &e);
    QString word = t.mid(s, qMin(e - s, MAX_WORD - 1));
    set_sel(s, s);
    page->ensureCursorVisible();
    QString h = hint_list(word, 3, false);
    flash(QString("Typo %1 of %2%3: %4%5%6")
              .arg(target_index)
              .arg(total)
              .arg(wrapped && total > 1 ? (forward ? " (from the top)" : " (from the end)") : "")
              .arg(word, h.isEmpty() ? "" : " -> ", h),
          4000);
}

/* Searching. Case does not matter. Replacing matches whole words only, so changing
   the name Ann leaves Anne and planning alone. */

static bool boundary_ok(const QString &t, int at, int n, const QString &needle)
{
    if (needle[0].isLetterOrNumber() && at > 0 && t[at - 1].isLetterOrNumber())
        return false;
    if (needle[n - 1].isLetterOrNumber() && at + n < t.size() && t[at + n].isLetterOrNumber())
        return false;
    return true;
}

/* Finds needle in t (both already lower case), going forward from or back from from.
   Returns the position, or -1. */
static int find_in(const QString &t, const QString &needle, int from, bool back, bool whole)
{
    int n = needle.size(), len = t.size();
    if (n == 0 || n > len)
        return -1;
    if (back) {
        for (int i = from > len - n ? len - n : from; i >= 0; i--)
            if (t[i] == needle[0] && QStringView(t).mid(i, n) == needle && (!whole || boundary_ok(t, i, n, needle)))
                return i;
    } else {
        for (int i = from < 0 ? 0 : from; i <= len - n; i++)
            if (t[i] == needle[0] && QStringView(t).mid(i, n) == needle && (!whole || boundary_ok(t, i, n, needle)))
                return i;
    }
    return -1;
}

void Screen::find_prompt(bool back)
{
    stop_typing();
    find_back = back;
    ask(P_FIND, back ? "<- Srch: " : "-> Srch: ", find_text, nullptr);
}

void Screen::do_find()
{
    QString t = fold_case(text()), needle = fold_case(find_text);
    QTextCursor sel = page->textCursor();
    int at, total = 0, index = 0;
    bool wrapped = false;
    if (find_back) {
        at = find_in(t, needle, sel.selectionStart() - 1, true, false);
        if (at < 0 && (at = find_in(t, needle, t.size(), true, false)) >= 0)
            wrapped = true;
    } else {
        at = find_in(t, needle, sel.hasSelection() ? sel.selectionStart() + 1 : sel.position(), false, false);
        if (at < 0 && (at = find_in(t, needle, 0, false, false)) >= 0)
            wrapped = true;
    }
    if (at < 0) {
        flash("* Not found *", 3000);
        return;
    }
    for (int i = find_in(t, needle, 0, false, false); i >= 0; i = find_in(t, needle, i + 1, false, false)) {
        total++;
        if (i == at)
            index = total;
    }
    set_sel(at, at + needle.size());
    page->ensureCursorVisible();
    flash(QString("Found %1 of %2%3")
              .arg(index)
              .arg(total)
              .arg(wrapped ? (find_back ? "  - went round from the end" : "  - went round from the top") : ""),
          3500);
}

/* The replacement, capitalized like the text it replaces: john -> peter, John -> Peter,
   JOHN -> PETER. */
static QString match_case(const QString &found, const QString &with)
{
    int letters = 0, upper = 0;
    QString out = with;
    for (QChar c : found) {
        if (c.isLetter()) {
            letters++;
            if (c.isUpper())
                upper++;
        }
    }
    if (letters > 1 && upper == letters)
        out = out.toUpper();
    else if (!found.isEmpty() && found[0].isUpper() && !out.isEmpty() && out[0].isLower())
        out[0] = out[0].toUpper();
    return out;
}

int Screen::replace_at(int start, int n)
{
    QString with = match_case(range(start, start + n), swap_with);
    QTextCursor c(doc());
    c.setPosition(start);
    c.setPosition(start + n, QTextCursor::KeepAnchor);
    c.insertText(with);
    return with.size();
}

void Screen::swap_start()
{
    stop_typing();
    ask_yes_no(P_SWAP_CONFIRM, "Replace: confirm each one?", false);
}

void Screen::swap_finish()
{
    update_counts();
    update_hint();
    flash(swap_done ? QString("Replaced %1").arg(swap_done) : QString("* Not found *"), 4000);
}

/* Replaces every whole-word match from from to the end, last first so the earlier
   positions stay put. */
void Screen::swap_all_from(int from)
{
    QString t = fold_case(text()), needle = fold_case(find_text);
    int n = needle.size();
    QList<int> at;
    for (int i = find_in(t, needle, from, false, true); i >= 0; i = find_in(t, needle, i + n, false, true))
        at << i;
    int caret_at = page->textCursor().selectionStart();
    int scroll = page->verticalScrollBar()->value();
    busy = true;
    for (int i = at.size() - 1; i >= 0; i--) {
        int added = replace_at(at[i], n);
        if (at[i] + n <= caret_at)
            caret_at += added - n;
    }
    set_sel(caret_at, caret_at);
    page->verticalScrollBar()->setValue(scroll);
    busy = false;
    swap_done += at.size();
    update_title();
    swap_finish();
}

/* With confirming: shows the next match and asks. */
void Screen::swap_next()
{
    QString t = fold_case(text()), needle = fold_case(find_text);
    int at = find_in(t, needle, swap_pos, false, true);
    if (at < 0) {
        swap_finish();
        return;
    }
    swap_start_pos = at;
    set_sel(at, at + needle.size());
    page->ensureCursorVisible();
    prompt_default_yes = true;
    ask(P_SWAP_EACH, "Replace? (Yes) No  A.All the rest  Esc.Stop: ", QString(), "yna\r");
}

void Screen::swap_answer(char key)
{
    int n = find_text.size();
    if (key == 'y' || key == '\r') {
        int added = replace_at(swap_start_pos, n);
        swap_done++;
        swap_pos = swap_start_pos + added;
        swap_next();
    } else if (key == 'n') {
        swap_pos = swap_start_pos + n;
        swap_next();
    } else if (key == 'a') {
        swap_all_from(swap_start_pos);
    }
}

void Screen::swap_go()
{
    swap_done = 0;
    if (swap_confirm) {
        swap_pos = 0;
        swap_next();
    } else {
        swap_all_from(0);
    }
}

/* Bold, italics, underline and strikethrough on the selection, or on what is typed next. */
void Screen::toggle_style(BYTE style)
{
    QTextCursor c = page->textCursor();
    bool on = true;
    if (c.hasSelection()) {
        /* On only if the whole selection has it already. */
        int start = c.selectionStart(), end = c.selectionEnd();
        for (QTextBlock b = doc()->findBlock(start); on && b.isValid() && b.position() < end; b = b.next()) {
            for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                QTextFragment f = it.fragment();
                if (f.position() + f.length() <= start || f.position() >= end)
                    continue;
                if (!(style_of(f.charFormat()) & style)) {
                    on = false;
                    break;
                }
            }
        }
    } else {
        on = (style_of(page->currentCharFormat()) & style) != 0;
    }
    QTextCharFormat f;
    if (style == ST_BOLD)
        f.setFontWeight(on ? QFont::Normal : QFont::Bold);
    else if (style == ST_ITALIC)
        f.setFontItalic(!on);
    else if (style == ST_UNDERLINE)
        f.setFontUnderline(!on);
    else
        f.setFontStrikeOut(!on);
    page->mergeCurrentCharFormat(f);
}

/* Alignment of the paragraph the caret is in. */
BYTE Screen::sel_align() const
{
    return align_of(page->textCursor().blockFormat().alignment());
}

/* Lines up every paragraph the selection touches. */
void Screen::set_sel_align(BYTE align)
{
    QTextCursor c = page->textCursor();
    QTextBlockFormat bf;
    bf.setAlignment(qt_align(align));
    c.mergeBlockFormat(bf);
    update_status();
}

/* Center or flush right, or back to the left if it is already so. */
void Screen::toggle_align(BYTE align)
{
    set_sel_align(sel_align() == align ? AL_LEFT : align);
}

void Screen::resize_text(int step)
{
    int size = qBound(9, font_size + step, 40);
    if (size == font_size)
        return;
    font_size = size;
    settings().setValue("FontSize", size);
    layout();
    page->ensureCursorVisible();
    if (shown_size < font_size)
        flash("Text is as big as fits 65 columns on this screen", 3500);
}

void Screen::toggle_full_screen()
{
    if (!fullscreen) {
        was_maximized = isMaximized();
        showFullScreen();
        fullscreen = true;
    } else {
        if (was_maximized)
            showMaximized();
        else
            showNormal();
        fullscreen = false;
    }
}

void Screen::toggle_help()
{
    help = !help;
    page->setVisible(!help);
    if (prompt_kind == P_NONE) {
        if (help)
            setFocus();
        else
            page->setFocus();
    }
    update();
}

void Screen::toggle_hints()
{
    hints = !hints;
    settings().setValue("Hints", hints ? 1 : 0);
    update_status();
}

void Screen::toggle_typeover()
{
    typeover = !typeover;
    page->setOverwriteMode(typeover);
    update_status();
}

void Screen::paste()
{
    page->paste(); /* as plain text, through Page::insertFromMimeData */
}

/* Commands, the pull-down menu and the key template.
 *
 * Every command has a key. Tapping Alt (or Alt+=) opens a WordPerfect 5.1 style menu
 * bar that works with the arrow keys, Enter and the underlined letters, or the mouse.
 * Holding Ctrl or Alt for a moment shows the keys that go with it across the top, like
 * the paper template that sat above the function keys. */

QString Screen::item_label(const MenuItem &it) const
{
    switch (it.cmd) {
    case C_BEEP:
        return QString("%1: %2").arg(it.label, beep_on ? "on" : "off");
    case C_TYPEOVER:
        return QString("%1: %2").arg(it.label, typeover ? "on" : "off");
    case C_FULL_SCREEN:
        return QString("%1: %2").arg(it.label, fullscreen ? "on" : "off");
    case C_HINTS:
        return QString("%1: %2").arg(it.label, hints ? "on" : "off");
    }
    return it.label;
}

/* The underlined letter of each item: the first letter no item above it has taken. */
int Screen::item_letter(const Menu &m, int index) const
{
    QString taken;
    for (int i = 0; i <= index; i++) {
        QString label = m.items[i].label;
        for (int at = 0; at < label.size(); at++) {
            QChar c = label[at].toUpper();
            if (!c.isLetter())
                continue;
            if (!taken.contains(c)) {
                if (i == index)
                    return at;
                taken += c;
                break;
            }
        }
    }
    return -1;
}

void Screen::run_command(int cmd)
{
    switch (cmd) {
    case C_SAVE: save_then(THEN_NONE); break;
    case C_SAVE_AS: save_as(THEN_NONE); break;
    case C_RETRIEVE: retrieve(); break;
    case C_PRINT: print_document(); break;
    case C_EXIT: exit_app(); break;
    case C_UNDO: page->undo(); break;
    case C_REDO: page->redo(); break;
    case C_CUT: page->cut(); break;
    case C_COPY: page->copy(); break;
    case C_PASTE: paste(); break;
    case C_SELECT_ALL: page->selectAll(); break;
    case C_TYPEOVER: toggle_typeover(); break;
    case C_FIND: find_prompt(false); break;
    case C_FIND_BACK: find_prompt(true); break;
    case C_REPLACE: swap_start(); break;
    case C_BOLD: toggle_style(ST_BOLD); break;
    case C_ITALIC: toggle_style(ST_ITALIC); break;
    case C_UNDERLINE: toggle_style(ST_UNDERLINE); break;
    case C_STRIKE: toggle_style(ST_STRIKE); break;
    case C_ALIGN_LEFT: set_sel_align(AL_LEFT); break;
    case C_ALIGN_CENTER: toggle_align(AL_CENTER); break;
    case C_ALIGN_RIGHT: toggle_align(AL_RIGHT); break;
    case C_NEXT_TYPO: jump_typo(true); break;
    case C_PREV_TYPO: jump_typo(false); break;
    case C_SPELL: spell_check(); break;
    case C_ADD_WORD: add_word(); break;
    case C_BEEP: toggle_beep(); break;
    case C_BIGGER: resize_text(1); break;
    case C_SMALLER: resize_text(-1); break;
    case C_FULL_SCREEN: toggle_full_screen(); break;
    case C_HINTS: toggle_hints(); break;
    case C_HELP: if (!help) toggle_help(); break;
    case C_ABOUT: flash(QString("Amber Writer %1 - F3 for help").arg(AMBER_VERSION), 5000); break;
    }
}

/* Where each menu title sits on the bar. */
int Screen::bar_x(const QFontMetrics &fm, int index, int *w) const
{
    int x = 24 - qRound(cw);
    for (int i = 0; i <= index && i < MENU_COUNT; i++) {
        int size = fm.horizontalAdvance(QString(" %1 ").arg(MENUS[i].title));
        if (i == index) {
            if (w)
                *w = size;
            return x;
        }
        x += size + qRound(cw);
    }
    return x;
}

void Screen::place_drop()
{
    QFontMetrics fm(font);
    const Menu &m = MENUS[menu];
    int label_w = 0, keys_w = 0;
    for (int i = 0; i < m.count; i++) {
        label_w = qMax(label_w, fm.horizontalAdvance(item_label(m.items[i])));
        keys_w = qMax(keys_w, fm.horizontalAdvance(keys(m.items[i].keys)));
    }
    int w = label_w + keys_w + qRound(cw * 7), h = m.count * ch + 8;
    drop->setGeometry(bar_x(fm, menu, nullptr), ch + 4, w, h);
    drop->show();
    drop->raise();
    drop->update();
}

void Screen::open_menu(int index)
{
    menu_open = true;
    menu = (index + MENU_COUNT) % MENU_COUNT;
    item = 0;
    hide_tips();
    bar->setGeometry(0, 0, width(), ch + 4);
    bar->show();
    bar->raise();
    bar->update();
    place_drop();
}

void Screen::close_menu()
{
    if (!menu_open)
        return;
    menu_open = false;
    drop->hide();
    bar->hide();
    if (prompt_kind != P_NONE)
        prompt->setFocus();
    else if (help)
        setFocus();
    else
        page->setFocus();
}

void Screen::choose_item(int index)
{
    if (index < 0 || index >= MENUS[menu].count)
        return;
    int cmd = MENUS[menu].items[index].cmd;
    close_menu();
    run_command(cmd);
}

/* Keys while the menu is open. Returns false for a key the menu does not use: the menu
   closes and the key does what it always does. */
bool Screen::menu_key(QKeyEvent *e)
{
    const Menu &m = MENUS[menu];
    int key = e->key();
    Qt::KeyboardModifiers mods = e->modifiers();
    if ((mods & Qt::ControlModifier) || ((mods & Qt::AltModifier) && key != Qt::Key_Alt))
        return false;
    switch (key) {
    case Qt::Key_Escape:
        close_menu();
        return true;
    case Qt::Key_Left:
        open_menu(menu - 1);
        return true;
    case Qt::Key_Right:
        open_menu(menu + 1);
        return true;
    case Qt::Key_Up:
        item = (item + m.count - 1) % m.count;
        drop->update();
        return true;
    case Qt::Key_Down:
        item = (item + 1) % m.count;
        drop->update();
        return true;
    case Qt::Key_Home:
    case Qt::Key_PageUp:
        item = 0;
        drop->update();
        return true;
    case Qt::Key_End:
    case Qt::Key_PageDown:
        item = m.count - 1;
        drop->update();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        choose_item(item);
        return true;
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
        return true;
    }
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) {
        for (int i = 0; i < m.count; i++) {
            int at = item_letter(m, i);
            if (at >= 0 && QString(m.items[i].label)[at].toUpper().unicode() == key) {
                choose_item(i);
                return true;
            }
        }
        for (int i = 0; i < MENU_COUNT; i++) {
            if (MENUS[i].letter == key) {
                open_menu(i);
                return true;
            }
        }
        return true;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return false;
    return true;
}

/* The key template */

static const Tip *tips_for(int key, int *count)
{
    if (key == Qt::Key_Control) {
        *count = COUNT(CTRL_TIPS);
        return CTRL_TIPS;
    }
    *count = COUNT(ALT_TIPS);
    return ALT_TIPS;
}

/* Lays out (and with a painter, draws) a block of tips in even columns. Returns the
   height used. */
int Screen::draw_tip_block(QPainter *p, const QString &prefix, const Tip *t, int count, int x, int y, int width,
                           bool dim) const
{
    QFontMetrics fm(font);
    int cell = 0, cols, col = 0;
    /* "Alt" on its own needs no "Alt+" in front. */
    auto key_text = [&](const Tip &tip) {
        return strcmp(tip.keys, "Alt") == 0 ? keys("Alt") : prefix + tip.keys;
    };
    for (int i = 0; i < count; i++)
        cell = qMax(cell, fm.horizontalAdvance(key_text(t[i]) + " " + t[i].label) + qRound(cw * 3));
    cols = cell ? qMax(1, width / cell) : 1;
    for (int i = 0; i < count; i++) {
        if (p) {
            QString k = key_text(t[i]);
            int cx = x + col * cell, kw = fm.horizontalAdvance(k);
            /* The keys look like key caps: amber, with the writing in black. */
            p->fillRect(QRect(cx, y, kw, ch), dim ? DIM_AMBER : AMBER);
            p->setPen(BLACK);
            p->drawText(cx, y + ascent, k);
            p->setPen(dim ? DIM_AMBER : AMBER);
            p->drawText(cx + kw + qRound(cw), y + ascent, t[i].label);
        }
        if (++col == cols && i + 1 < count) {
            col = 0;
            y += ch + 2;
        }
    }
    return (count ? (count + cols - 1) / cols : 0) * (ch + 2);
}

void Screen::paint_tips(QPainter &p, QRect r)
{
    int count, y = 6, width = r.width() - 32;
    const Tip *t = tips_for(tips_key, &count);
    p.setFont(font);
    p.setPen(AMBER);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    y += draw_tip_block(&p, keys(tips_key == Qt::Key_Control ? "Ctrl+" : "Alt+"), t, count, 16, y, width, false);
    y += 6;
    draw_tip_block(&p, "", FKEY_TIPS, COUNT(FKEY_TIPS), 16, y, width, true);
}

void Screen::show_tips(int key)
{
    int count, width = this->width() - 32 - 32;
    const Tip *t = tips_for(key, &count);
    tips_key = key;
    int h = 6 + draw_tip_block(nullptr, keys(key == Qt::Key_Control ? "Ctrl+" : "Alt+"), t, count, 0, 0, width, false) +
            6 + draw_tip_block(nullptr, "", FKEY_TIPS, COUNT(FKEY_TIPS), 0, 0, width, true) + 6;
    tips->setGeometry(16, 4, this->width() - 32, h);
    tips->show();
    tips->raise();
    tips->update();
}

void Screen::hide_tips()
{
    if (tips->isVisible())
        tips->hide();
    tips_key = 0;
}

void Screen::cancel_hold()
{
    hold_timer.stop();
    hold_key = 0;
    hide_tips();
}

/* Watches Ctrl and Alt on their own: held, they show the key template; Alt tapped
   opens the menu. Returns true when the event must not go on. */
bool Screen::track_modifiers(QEvent *e)
{
    if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::Wheel) {
        if (hold_key) {
            hold_other = true;
            hold_timer.stop();
            hide_tips();
        }
        return false;
    }
    if (e->type() != QEvent::KeyPress && e->type() != QEvent::KeyRelease)
        return false;
    QKeyEvent *k = static_cast<QKeyEvent *>(e);
    int key = k->key();
    bool modifier = key == Qt::Key_Control || key == Qt::Key_Alt;
    if (e->type() == QEvent::KeyPress) {
        if (modifier) {
            if (k->isAutoRepeat())
                return key == Qt::Key_Alt;
            if (hold_key && hold_key != key) {
                /* Ctrl and Alt together: neither template nor menu. */
                hold_other = true;
                hold_timer.stop();
                hide_tips();
            } else {
                hold_key = key;
                hold_other = false;
                hold_timer.start(600);
            }
            return key == Qt::Key_Alt;
        }
        if (key != Qt::Key_Shift && hold_key) {
            hold_other = true;
            hold_timer.stop();
            hide_tips();
        }
        return false;
    }
    if (k->isAutoRepeat())
        return modifier;
    if (key == hold_key) {
        bool tap = !hold_other && !tips_key;
        hold_timer.stop();
        hide_tips();
        hold_key = 0;
        if (key == Qt::Key_Alt && tap && prompt_kind == P_NONE) {
            if (menu_open)
                close_menu();
            else
                open_menu(0);
        }
    }
    return key == Qt::Key_Alt;
}

void Screen::hold_fired()
{
    Qt::KeyboardModifier held = hold_key == Qt::Key_Control ? Qt::ControlModifier : Qt::AltModifier;
    if (hold_key && !hold_other && (QGuiApplication::queryKeyboardModifiers() & held) && isActiveWindow() &&
        prompt_kind == P_NONE && !menu_open)
        show_tips(hold_key);
}

/* Keys. Returns true when the key was a command and must not reach the page. */
bool Screen::handle_key(int key, bool ctrl, bool shift, bool alt)
{
    if (prompt_kind != P_NONE)
        return false;
    if (help) {
        if ((key == Qt::Key_Escape || key == Qt::Key_F3) && !ctrl && !alt)
            toggle_help();
        else if (key == Qt::Key_F7 && !ctrl && !alt)
            exit_app();
        else if (key == Qt::Key_Q && ctrl)
            exit_app();
        return true;
    }
    if (!ctrl && !alt) {
        switch (key) {
        case Qt::Key_F3:
            toggle_help();
            return true;
        case Qt::Key_F2:
            find_prompt(shift);
            return true;
        case Qt::Key_F10:
            if (shift)
                retrieve();
            else
                save_as(THEN_NONE);
            return true;
        case Qt::Key_F7:
            if (shift)
                print_document();
            else
                exit_app();
            return true;
        case Qt::Key_F6:
            if (shift)
                toggle_align(AL_CENTER);
            else
                toggle_style(ST_BOLD);
            return true;
        case Qt::Key_F8:
            toggle_style(ST_UNDERLINE);
            return true;
        case Qt::Key_F11:
            toggle_full_screen();
            return true;
        case Qt::Key_Escape:
            return true;
        case Qt::Key_Insert:
            if (shift)
                paste();
            else
                toggle_typeover();
            return true;
        }
        return false;
    }
    if (alt && !ctrl) {
        switch (key) {
        case Qt::Key_F2:
            swap_start();
            return true;
        case Qt::Key_Equal:
        case Qt::Key_Plus:
            open_menu(0);
            return true;
        case Qt::Key_F6:
            toggle_align(AL_RIGHT);
            return true;
        case Qt::Key_A:
            add_word();
            return true;
        case Qt::Key_B:
            toggle_beep();
            return true;
        case Qt::Key_N:
        case Qt::Key_P:
            jump_typo(key == Qt::Key_N);
            return true;
        }
        /* Other Alt+letters would only type odd characters. */
        return (key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9);
    }
    if (ctrl && !alt) {
        switch (key) {
        case Qt::Key_S:
            if (!shift)
                save_then(THEN_NONE);
            return true;
        case Qt::Key_O:
            retrieve();
            return true;
        case Qt::Key_F:
            find_prompt(shift);
            return true;
        case Qt::Key_L:
            set_sel_align(AL_LEFT);
            return true;
        case Qt::Key_P:
            print_document();
            return true;
        case Qt::Key_E:
            toggle_align(AL_CENTER);
            return true;
        case Qt::Key_R:
            toggle_align(AL_RIGHT);
            return true;
        case Qt::Key_H:
            swap_start();
            return true;
        case Qt::Key_Q:
            exit_app();
            return true;
        case Qt::Key_F2:
            spell_check();
            return true;
        case Qt::Key_B:
            toggle_style(ST_BOLD);
            return true;
        case Qt::Key_I:
            toggle_style(ST_ITALIC);
            return true;
        case Qt::Key_U:
            toggle_style(ST_UNDERLINE);
            return true;
        case Qt::Key_X:
            if (shift)
                toggle_style(ST_STRIKE);
            else
                page->cut();
            return true;
        case Qt::Key_C:
            if (!shift)
                page->copy();
            return true;
        case Qt::Key_V:
            if (!shift)
                paste();
            return true;
        case Qt::Key_A:
            if (!shift)
                page->selectAll();
            return true;
        case Qt::Key_Z:
            if (shift)
                page->redo(); /* Ctrl+Shift+Z is redo on a Mac and in most Linux programs */
            else
                page->undo();
            return true;
        case Qt::Key_Y:
            if (!shift)
                page->redo();
            return true;
        case Qt::Key_Equal:
        case Qt::Key_Plus:
            resize_text(1);
            return true;
        case Qt::Key_Minus:
        case Qt::Key_Underscore:
            resize_text(-1);
            return true;
        }
        /* Keep QTextEdit's other Ctrl shortcuts away. */
        if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9) ||
            (key >= Qt::Key_F1 && key <= Qt::Key_F35) || (key >= Qt::Key_Exclam && key <= Qt::Key_At) ||
            (key >= Qt::Key_BracketLeft && key <= Qt::Key_QuoteLeft))
            return true;
        return false;
    }
    return false;
}

bool Screen::eventFilter(QObject *obj, QEvent *e)
{
    QEvent::Type type = e->type();
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease && type != QEvent::MouseButtonPress &&
        type != QEvent::Wheel)
        return false;
    if (!obj->isWidgetType())
        return false;
    QWidget *w = static_cast<QWidget *>(obj);
    if (w->window() != this)
        return false;
    if (type == QEvent::MouseButtonPress || type == QEvent::Wheel) {
        track_modifiers(e);
        if (menu_open && type == QEvent::MouseButtonPress && w != bar && w != drop) {
            close_menu();
            if (static_cast<QMouseEvent *>(e)->button() == Qt::RightButton)
                return true;
        }
        return false;
    }
    /* Key events go first to the widget with the focus, then up to its parents; each
       is looked at once. */
    QWidget *focus = focusWidget();
    if (w != (focus ? focus : this))
        return false;
    if (track_modifiers(e))
        return true;
    if (type != QEvent::KeyPress)
        return false;
    QKeyEvent *k = static_cast<QKeyEvent *>(e);
    if (menu_open) {
        if (menu_key(k))
            return true;
        close_menu();
    }
    if (w != page && w != this)
        return false;
    Qt::KeyboardModifiers mods = k->modifiers();
    bool ctrl = mods & Qt::ControlModifier, shift = mods & Qt::ShiftModifier;
    /* On a Mac, the Control key (Meta to Qt) works like Option. */
    bool alt = (mods & Qt::AltModifier) || (MAC && (mods & Qt::MetaModifier));
    int key = k->key();
    if (w == page && ((key >= Qt::Key_Home && key <= Qt::Key_PageDown) || key == Qt::Key_Delete ||
                      key == Qt::Key_Return || key == Qt::Key_Enter))
        stop_typing();
    /* AltGr can arrive as Ctrl+Alt; leave it for typing accented letters. */
    if (!(ctrl && alt) && handle_key(key, ctrl, shift, alt))
        return true;
    return false;
}

/* Start up */

class App : public QApplication {
public:
    App(int &argc, char **argv) : QApplication(argc, argv) {}
    Screen *screen = nullptr;
    QString pending;

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::FileOpen) {
            QString file = static_cast<QFileOpenEvent *>(e)->file();
            if (screen && screen->isVisible())
                screen->open_file(file);
            else
                pending = file;
            return true;
        }
        return QApplication::event(e);
    }
};

int main(int argc, char **argv)
{
    App app(argc, argv);
    app.setApplicationName("AmberWriter");
    app.setOrganizationName("AmberWriter");
    app.setApplicationDisplayName("Amber Writer");
    app.setApplicationVersion(AMBER_VERSION);
    app.setWindowIcon(QIcon(":/amber.png"));
    QApplication::setDesktopFileName("amberwriter");

    QStringList args = app.arguments();
    /* amberwriter --print-pdf out.pdf story.rtf prints a document to a PDF file and quits. */
    QString pdf;
    if (args.size() >= 4 && args[1] == "--print-pdf") {
        pdf = args[2];
        args.removeAt(1);
        args.removeAt(1);
    }
    Screen screen;
    app.screen = &screen;
    if (!pdf.isEmpty()) {
        screen.resize(1100, 760);
        screen.layout();
        screen.retrieve_path(QFileInfo(args[1]).absoluteFilePath());
        screen.update_counts();
        screen.print_document(pdf);
        printf("%s\n", screen.message.toLocal8Bit().constData());
        return screen.message.startsWith("Printed") ? 0 : 1;
    }
    screen.showMaximized();
    screen.layout();
    if (args.size() > 1)
        screen.retrieve_path(QFileInfo(args[1]).absoluteFilePath());
    else if (!app.pending.isEmpty())
        screen.retrieve_path(QFileInfo(app.pending).absoluteFilePath());
    screen.update_title();
    screen.page->setFocus();
    return app.exec();
}
