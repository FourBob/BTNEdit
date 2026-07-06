#include "strings.h"
#include <string.h>

static BtnUiLang g_lang = BTN_LANG_EN;

/* Reihenfolge muss exakt zu BtnStringId in strings.h passen - ein Kommentar
 * pro Zeile waere hier mehr Rauschen als Nutzen, die Spalten sprechen fuer
 * sich (jede Zeile = eine Sprache, jede Spalte = ein BTN_STR_*). Nicht-ASCII
 * Zeichen stehen als rohes UTF-8 direkt im Quelltext (wie in shim.m) statt
 * als \x-Escapes: \x-Escapes verschlucken beliebig viele folgende Hex-Ziffern
 * (nicht nur zwei!), sodass z.B. "\xc3\x9cber" durch das folgende 'b' zu
 * "\xc3" + "\x9cb" (ausserhalb des char-Wertebereichs) zusammengezogen wird -
 * ein klassischer C-Stolperstein, der hier zu Compile-Fehlern fuehrte. */
static const char *const g_strings[BTN_LANG_COUNT][BTN_STR_COUNT] = {
    /* BTN_LANG_EN */
    {
        "About ", "Quit ", "File", "New", "Open...", "Open Recent", "(None)",
        "Save", "Save As...", "Close", "Print...", "Edit", "Undo", "Redo",
        "Cut", "Copy", "Paste", "Select All", "Find...", "Untitled",
        "Do you want to save the changes made to “%s”?",
        "Your changes will be lost if you don't save them.",
        "Save", "Don't Save", "Cancel"
    },
    /* BTN_LANG_DE */
    {
        "Über ", "Beende ", "Ablage", "Neu", "Öffnen...",
        "Zuletzt geöffnet", "(Keine)",
        "Sichern", "Sichern unter...", "Schließen", "Drucken...",
        "Bearbeiten", "Widerrufen", "Wiederholen",
        "Ausschneiden", "Kopieren", "Einfügen", "Alles auswählen",
        "Suchen...", "Unbenannt",
        "Möchtest du die Änderungen an „%s“ sichern?",
        "Deine Änderungen gehen verloren, wenn du sie nicht sicherst.",
        "Sichern", "Nicht sichern", "Abbrechen"
    },
    /* BTN_LANG_FR */
    {
        "À propos de ", "Quitter ", "Fichier", "Nouveau", "Ouvrir...",
        "Ouvrir un élément récent", "(Aucun)",
        "Enregistrer", "Enregistrer sous...", "Fermer", "Imprimer...",
        "Édition", "Annuler", "Rétablir",
        "Couper", "Copier", "Coller", "Tout sélectionner",
        "Rechercher...", "Sans titre",
        "Voulez-vous enregistrer les modifications apportées à « %s » ?",
        "Vos modifications seront perdues si vous ne les enregistrez pas.",
        "Enregistrer", "Ne pas enregistrer", "Annuler"
    },
    /* BTN_LANG_ES */
    {
        "Acerca de ", "Salir de ", "Archivo", "Nuevo", "Abrir...",
        "Abrir reciente", "(Ninguno)",
        "Guardar", "Guardar como...", "Cerrar", "Imprimir...",
        "Edición", "Deshacer", "Rehacer",
        "Cortar", "Copiar", "Pegar", "Seleccionar todo",
        "Buscar...", "Sin título",
        "¿Quieres guardar los cambios realizados en “%s”?",
        "Tus cambios se perderán si no los guardas.",
        "Guardar", "No guardar", "Cancelar"
    },
    /* BTN_LANG_ZH (Simplified) */
    {
        "关于", "退出", "文件",
        "新建", "打开…",
        "打开最近使用的文件",
        "（无）",
        "存储", "存储为…",
        "关闭", "打印…",
        "编辑", "撤销", "重做",
        "剪切", "拷贝", "粘贴",
        "全选", "查找…", "未命名",
        "要存储对“%s”的修改吗？",
        "如果不存储，你所做的修改将丢失。",
        "存储", "不存储", "取消"
    }
};

void btn_strings_set_language(BtnUiLang lang) {
    if (lang >= 0 && lang < BTN_LANG_COUNT) {
        g_lang = lang;
    }
}

const char *btn_tr(BtnStringId id) {
    return g_strings[g_lang][id];
}

BtnUiLang btn_strings_lang_from_code(const char *code) {
    if (!code) {
        return BTN_LANG_EN;
    }
    if (strncmp(code, "de", 2) == 0) {
        return BTN_LANG_DE;
    }
    if (strncmp(code, "fr", 2) == 0) {
        return BTN_LANG_FR;
    }
    if (strncmp(code, "es", 2) == 0) {
        return BTN_LANG_ES;
    }
    if (strncmp(code, "zh", 2) == 0) {
        return BTN_LANG_ZH;
    }
    return BTN_LANG_EN;
}
