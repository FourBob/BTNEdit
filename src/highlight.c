/*
 * Reiner String-Tokenizer, keine Abhaengigkeit von der Editor-Engine oder
 * von Core Text/Core Graphics - nimmt einfach Text + Sprachdefinition und
 * gibt Token-Bereiche zurueck. render.c ordnet den Tokens Farben zu und
 * mappt die Bereiche auf die Anzeige-Koordinaten (Tabs, Wortumbruch).
 */
#include "highlight.h"

#include <ctype.h>
#include <string.h>

struct BtnLangSpec {
    const char *const *keywords; /* NULL-terminiertes Array */
    int line_comment_slash;      /* // */
    int line_comment_hash;       /* # als Kommentar (Python/Shell/INI) */
    int block_comment;           /* Blockkommentare wie in C */
    /* 0 = aus, sonst das Zeichen, das (nach optionalem fuehrenden
     * Leerraum) die GANZE restliche Zeile zu einem einzigen
     * BTN_TOK_PREPROCESSOR-Token macht - C-Praeprozessor ('#'), Markdown-
     * Ueberschrift ('#', gleiche Form: ganze Zeile, ein Token, muss am
     * Zeilenanfang stehen) und INI-Abschnitt ('[') nutzen denselben
     * Mechanismus, nur mit unterschiedlichem Ausloeser-Zeichen. */
    char line_prefix_char;
    int quote_backtick;          /* ` zusaetzlich zu "/' als String-Anfuehrungs-
                                   * zeichen behandeln (Markdown Inline-Code
                                   * wie `code`) - fuer alle anderen Sprachen 0,
                                   * die per Aggregat-Initialisierung ohne
                                   * dieses letzte Feld unveraendert bleiben. */
    int line_comment_semicolon;  /* ; als Kommentar (INI) - eigenes Feld statt
                                   * line_comment_hash zu verallgemeinern, weil
                                   * manche INI-Dialekte BEIDE ';' und '#'
                                   * gleichzeitig als Kommentar akzeptieren. */
};

static const char *const C_KEYWORDS[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double",
    "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long",
    "register", "restrict", "return", "short", "signed", "sizeof", "static", "struct",
    "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
    "class", "namespace", "template", "public", "private", "protected", "virtual",
    "new", "delete", "try", "catch", "throw", "using", "this", "nullptr", "true", "false",
    "id", "nil", "BOOL", "YES", "NO", "self", "super",
    NULL
};
static const BtnLangSpec C_LANG = { C_KEYWORDS, 1, 0, 1, '#', 0, 0 };

static const char *const PY_KEYWORDS[] = {
    "and", "as", "assert", "async", "await", "break", "class", "continue", "def",
    "del", "elif", "else", "except", "finally", "for", "from", "global", "if",
    "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
    "return", "try", "while", "with", "yield", "None", "True", "False", "self",
    NULL
};
static const BtnLangSpec PY_LANG = { PY_KEYWORDS, 0, 1, 0, 0, 0, 0 };

static const char *const SHELL_KEYWORDS[] = {
    "if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case", "esac",
    "function", "return", "exit", "local", "export", "echo", "in",
    NULL
};
static const BtnLangSpec SHELL_LANG = { SHELL_KEYWORDS, 0, 1, 0, 0, 0, 0 };

static const char *const JS_KEYWORDS[] = {
    "break", "case", "catch", "class", "const", "continue", "debugger", "default",
    "delete", "do", "else", "export", "extends", "finally", "for", "function", "if",
    "import", "in", "instanceof", "let", "new", "return", "super", "switch", "this",
    "throw", "try", "typeof", "var", "void", "while", "with", "yield", "async", "await",
    "true", "false", "null", "undefined",
    NULL
};
static const BtnLangSpec JS_LANG = { JS_KEYWORDS, 1, 0, 1, 0, 0, 0 };

static const char *const SWIFT_KEYWORDS[] = {
    "associatedtype", "class", "deinit", "enum", "extension", "fileprivate", "func",
    "import", "init", "inout", "internal", "let", "open", "operator", "private",
    "protocol", "public", "rethrows", "static", "struct", "subscript", "typealias",
    "var", "break", "case", "continue", "default", "defer", "do", "else", "fallthrough",
    "for", "guard", "if", "in", "repeat", "return", "switch", "where", "while", "as",
    "Any", "catch", "false", "is", "nil", "self", "Self", "super", "throw", "throws",
    "true", "try",
    NULL
};
static const BtnLangSpec SWIFT_LANG = { SWIFT_KEYWORDS, 1, 0, 1, 0, 0, 0 };

/* Kein eigener Tokenizer-Zustand fuer Markdown - reine Zweckentfremdung
 * bestehender Mechanismen (siehe Kommentare bei line_prefix_char/
 * quote_backtick oben): "# Ueberschrift" faellt in dieselbe Form wie ein
 * C-Praeprozessor-Statement (ganze Zeile, muss am Anfang stehen), "`code`"
 * in dieselbe Form wie ein String. Keine Keywords, keine Kommentare. */
static const BtnLangSpec MD_LANG = { NULL, 0, 0, 0, '#', 1, 0 };

static const char *const STL_KEYWORDS[] = {
    "solid", "facet", "normal", "outer", "loop", "vertex",
    "endloop", "endfacet", "endsolid",
    NULL
};
/* Nur das ASCII-STL-Format ist Text (Binaer-STL ist ein binaeres Format und
 * wuerde wie jede andere Binaerdatei als Rohbytes angezeigt - das ist eine
 * allgemeine Einschraenkung eines reinen Text-Editors, keine STL-
 * Besonderheit). Keine Kommentare im Format, Zahlen (Koordinaten) werden
 * bereits vom generischen, sprachunabhaengigen Zahlen-Scan erfasst. */
static const BtnLangSpec STL_LANG = { STL_KEYWORDS, 0, 0, 0, 0, 0, 0 };

/* INI/generische Config-Dateien: '[Abschnitt]' nutzt denselben "ganze Zeile
 * ein Token"-Mechanismus wie C-Praeprozessor/Markdown-Ueberschrift (siehe
 * line_prefix_char oben), nur mit '[' statt '#' als Ausloeser. Kommentare
 * akzeptieren grosszuegig sowohl ';' (klassisches INI) als auch '#' (von
 * vielen Parsern, z.B. Pythons configparser, ebenfalls unterstuetzt).
 * '.config' wird mangels eines XML-Tokenizers hier mit angehaengt - passt
 * fuer einfache Key=Value-Configs, nicht fuer XML-basierte .config-Dateien
 * (die zeigen dann einfach unformatierten Text wie bisher, keine
 * Verschlechterung). */
static const BtnLangSpec INI_LANG = { NULL, 0, 1, 0, '[', 0, 1 };

/* Kein XML-Tokenizer vorhanden - "Keywords" sind hier haeufige Element-/
 * Attributnamen statt echter Sprach-Schluesselwoerter (aehnliche
 * Zweckentfremdung wie bei STL: gibt der Datei sichtbare Struktur, ohne
 * einen vollen XML-Parser zu brauchen). Attributwerte in Anfuehrungszeichen
 * werden bereits vom generischen, sprachunabhaengigen Anfuehrungszeichen-
 * Scan erfasst; XML-Kommentare werden bewusst nicht erkannt (block_comment
 * ist fest auf die C-Kommentarmarken verdrahtet, nicht konfigurierbar -
 * fuer diese einfache Wortliste nicht den Aufwand wert). */
static const char *const SVG_KEYWORDS[] = {
    "svg", "path", "circle", "rect", "ellipse", "line", "polyline", "polygon",
    "g", "defs", "use", "symbol", "clipPath", "mask", "pattern", "linearGradient",
    "radialGradient", "stop", "text", "tspan", "image", "filter",
    "d", "fill", "stroke", "stroke-width", "transform", "viewBox", "xmlns",
    "width", "height", "cx", "cy", "r", "rx", "ry", "x", "y", "x1", "y1", "x2", "y2",
    "points", "id", "class", "style", "opacity",
    NULL
};
static const BtnLangSpec SVG_LANG = { SVG_KEYWORDS, 0, 0, 0, 0, 0, 0 };

/* ASCII-DXF (die verbreitete Textvariante, kein Binaer-DXF) - "Keywords"
 * sind Abschnitts-/Entitaetsnamen. Der eigentliche Code/Wert-Aufbau (jede
 * zweite Zeile eine Gruppencode-Zahl) braucht keine eigene Logik: Zahlen
 * werden schon vom generischen Zahlen-Scan erfasst. Keine Kommentare
 * (Gruppencode 999 fuer Kommentarzeilen wird hier nicht extra behandelt -
 * selten genug fuer diese einfache Wortliste). */
static const char *const DXF_KEYWORDS[] = {
    "SECTION", "ENDSEC", "EOF", "HEADER", "CLASSES", "TABLES", "BLOCKS",
    "ENTITIES", "OBJECTS", "TABLE", "ENDTAB", "BLOCK", "ENDBLK",
    "LINE", "CIRCLE", "ARC", "TEXT", "MTEXT", "POINT", "POLYLINE", "LWPOLYLINE",
    "VERTEX", "SEQEND", "INSERT", "SOLID", "3DFACE", "SPLINE", "ELLIPSE",
    "DIMENSION", "HATCH", "LAYER", "LTYPE", "STYLE", "VIEW", "UCS", "APPID",
    "DIMSTYLE", "BLOCK_RECORD",
    NULL
};
static const BtnLangSpec DXF_LANG = { DXF_KEYWORDS, 0, 0, 0, 0, 0, 0 };

static int ends_with_ci(const char *s, const char *suffix) {
    size_t ls = strlen(s), lsuf = strlen(suffix);
    if (lsuf > ls) {
        return 0;
    }
    for (size_t i = 0; i < lsuf; i++) {
        if (tolower((unsigned char)s[ls - lsuf + i]) != tolower((unsigned char)suffix[i])) {
            return 0;
        }
    }
    return 1;
}

const BtnLangSpec *btn_highlight_lang_for_path(const char *path) {
    if (!path) {
        return NULL;
    }
    static const struct {
        const char *ext;
        const BtnLangSpec *lang;
    } table[] = {
        { ".c", &C_LANG }, { ".h", &C_LANG }, { ".cpp", &C_LANG }, { ".cc", &C_LANG },
        { ".cxx", &C_LANG }, { ".hpp", &C_LANG }, { ".m", &C_LANG }, { ".mm", &C_LANG },
        { ".java", &C_LANG },
        { ".py", &PY_LANG },
        { ".sh", &SHELL_LANG }, { ".bash", &SHELL_LANG }, { ".zsh", &SHELL_LANG },
        { ".js", &JS_LANG }, { ".ts", &JS_LANG }, { ".jsx", &JS_LANG }, { ".tsx", &JS_LANG },
        { ".swift", &SWIFT_LANG },
        { ".md", &MD_LANG }, { ".markdown", &MD_LANG },
        { ".stl", &STL_LANG },
        { ".ini", &INI_LANG }, { ".config", &INI_LANG },
        { ".svg", &SVG_LANG },
        { ".dxf", &DXF_LANG },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (ends_with_ci(path, table[i].ext)) {
            return table[i].lang;
        }
    }
    return NULL;
}

static int is_keyword(const BtnLangSpec *lang, const char *word, size_t len) {
    if (!lang || !lang->keywords) {
        return 0;
    }
    for (const char *const *kw = lang->keywords; *kw; kw++) {
        size_t kwlen = strlen(*kw);
        if (kwlen == len && memcmp(*kw, word, len) == 0) {
            return 1;
        }
    }
    return 0;
}

size_t btn_highlight_tokenize(const char *text, size_t len, const BtnLangSpec *lang,
                               int starts_in_comment, int *ends_in_comment,
                               BtnToken *out_tokens, size_t max_tokens) {
    size_t count = 0;
    size_t i = 0;
    *ends_in_comment = 0;

    if (!lang) {
        return 0;
    }

#define BTN_EMIT(k, s, e)                             \
    do {                                              \
        if (count < max_tokens) {                     \
            out_tokens[count].start = (s);             \
            out_tokens[count].len = (e) - (s);          \
            out_tokens[count].kind = (k);               \
        }                                               \
        count++;                                       \
    } while (0)

    if (starts_in_comment && lang->block_comment) {
        size_t start = i;
        int closed = 0;
        while (i < len) {
            if (text[i] == '*' && i + 1 < len && text[i + 1] == '/') {
                i += 2;
                closed = 1;
                break;
            }
            i++;
        }
        BTN_EMIT(BTN_TOK_COMMENT, start, i);
        if (!closed) {
            *ends_in_comment = 1;
            return count;
        }
    }

    if (lang->line_prefix_char) {
        size_t j = i;
        while (j < len && (text[j] == ' ' || text[j] == '\t')) {
            j++;
        }
        if (j < len && text[j] == lang->line_prefix_char) {
            BTN_EMIT(BTN_TOK_PREPROCESSOR, i, len);
            return count;
        }
    }

    while (i < len) {
        char c = text[i];

        if (lang->line_comment_hash && c == '#') {
            BTN_EMIT(BTN_TOK_COMMENT, i, len);
            break;
        }
        if (lang->line_comment_semicolon && c == ';') {
            BTN_EMIT(BTN_TOK_COMMENT, i, len);
            break;
        }
        if (lang->line_comment_slash && c == '/' && i + 1 < len && text[i + 1] == '/') {
            BTN_EMIT(BTN_TOK_COMMENT, i, len);
            break;
        }
        if (lang->block_comment && c == '/' && i + 1 < len && text[i + 1] == '*') {
            size_t start = i;
            i += 2;
            int closed = 0;
            while (i < len) {
                if (text[i] == '*' && i + 1 < len && text[i + 1] == '/') {
                    i += 2;
                    closed = 1;
                    break;
                }
                i++;
            }
            BTN_EMIT(BTN_TOK_COMMENT, start, i);
            if (!closed) {
                *ends_in_comment = 1;
                return count;
            }
            continue;
        }
        if (c == '"' || c == '\'' || (lang->quote_backtick && c == '`')) {
            char quote = c;
            size_t start = i;
            i++;
            while (i < len && text[i] != quote) {
                /* Backtick-Spans (Markdown Inline-Code) kennen anders als
                 * "/'-Strings kein Backslash-Escaping - ein Backslash direkt
                 * vor dem schliessenden Backtick ist dort ein ganz normales
                 * Zeichen, keine Escape-Sequenz. Ohne diese Ausnahme wuerde
                 * z.B. `C:\` (ein Pfad in einem Code-Span) den schliessenden
                 * Backtick ueberspringen und den Rest der Zeile mit
                 * einfaerben. */
                if (quote != '`' && text[i] == '\\' && i + 1 < len) {
                    i++;
                }
                i++;
            }
            if (i < len) {
                i++;
            }
            BTN_EMIT(BTN_TOK_STRING, start, i);
            continue;
        }
        if (isdigit((unsigned char)c)) {
            size_t start = i;
            while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '.' || text[i] == '_')) {
                i++;
            }
            BTN_EMIT(BTN_TOK_NUMBER, start, i);
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t start = i;
            while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '_')) {
                i++;
            }
            if (is_keyword(lang, text + start, i - start)) {
                BTN_EMIT(BTN_TOK_KEYWORD, start, i);
            }
            continue;
        }
        i++;
    }

#undef BTN_EMIT
    return count;
}
