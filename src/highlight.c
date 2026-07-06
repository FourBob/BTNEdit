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
    int line_comment_hash;       /* # als Kommentar (Python/Shell) */
    int block_comment;           /* Blockkommentare wie in C */
    int preprocessor_hash;       /* # am Zeilenanfang = Praeprozessor (C) */
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
static const BtnLangSpec C_LANG = { C_KEYWORDS, 1, 0, 1, 1 };

static const char *const PY_KEYWORDS[] = {
    "and", "as", "assert", "async", "await", "break", "class", "continue", "def",
    "del", "elif", "else", "except", "finally", "for", "from", "global", "if",
    "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
    "return", "try", "while", "with", "yield", "None", "True", "False", "self",
    NULL
};
static const BtnLangSpec PY_LANG = { PY_KEYWORDS, 0, 1, 0, 0 };

static const char *const SHELL_KEYWORDS[] = {
    "if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case", "esac",
    "function", "return", "exit", "local", "export", "echo", "in",
    NULL
};
static const BtnLangSpec SHELL_LANG = { SHELL_KEYWORDS, 0, 1, 0, 0 };

static const char *const JS_KEYWORDS[] = {
    "break", "case", "catch", "class", "const", "continue", "debugger", "default",
    "delete", "do", "else", "export", "extends", "finally", "for", "function", "if",
    "import", "in", "instanceof", "let", "new", "return", "super", "switch", "this",
    "throw", "try", "typeof", "var", "void", "while", "with", "yield", "async", "await",
    "true", "false", "null", "undefined",
    NULL
};
static const BtnLangSpec JS_LANG = { JS_KEYWORDS, 1, 0, 1, 0 };

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
static const BtnLangSpec SWIFT_LANG = { SWIFT_KEYWORDS, 1, 0, 1, 0 };

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

    if (lang->preprocessor_hash) {
        size_t j = i;
        while (j < len && (text[j] == ' ' || text[j] == '\t')) {
            j++;
        }
        if (j < len && text[j] == '#') {
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
        if (c == '"' || c == '\'') {
            char quote = c;
            size_t start = i;
            i++;
            while (i < len && text[i] != quote) {
                if (text[i] == '\\' && i + 1 < len) {
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
