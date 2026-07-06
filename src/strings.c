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
        "Cut", "Copy", "Paste", "Select All", "Find & Replace...", "Untitled",
        "Do you want to save the changes made to “%s”?",
        "Your changes will be lost if you don't save them.",
        "Save", "Don't Save", "Cancel",
        "Find:", "Replace:", "Not found",
        "%d replaced",
        "Help", "Keyboard Shortcuts...", "Keyboard Shortcuts",
        "⌘N — New\n⌘O — Open\n⌘S — Save\n⇧⌘S — Save As\n⌘W — Close\n⌘Z — Undo\n⇧⌘Z — Redo\n⌘X — Cut\n⌘C — Copy\n⌘V — Paste\n⌘A — Select All\n⌘F — Find & Replace\n⌥←/→ — Jump by word\n⌘←/→ — Line start/end\nEsc — Close find bar\nReturn (search field) — Next match\n⇧Return (search field) — Previous match\nReturn (replace field) — Replace + Next\n⌘Return (replace field) — Replace All"
    },
    /* BTN_LANG_DE */
    {
        "Über ", "Beende ", "Ablage", "Neu", "Öffnen...",
        "Zuletzt geöffnet", "(Keine)",
        "Sichern", "Sichern unter...", "Schließen", "Drucken...",
        "Bearbeiten", "Widerrufen", "Wiederholen",
        "Ausschneiden", "Kopieren", "Einfügen", "Alles auswählen",
        "Suchen und Ersetzen...", "Unbenannt",
        "Möchtest du die Änderungen an „%s“ sichern?",
        "Deine Änderungen gehen verloren, wenn du sie nicht sicherst.",
        "Sichern", "Nicht sichern", "Abbrechen",
        "Suchen:", "Ersetzen:", "Nicht gefunden",
        "%d ersetzt",
        "Hilfe", "Tastenkürzel...", "Tastenkürzel",
        "⌘N — Neu\n⌘O — Öffnen\n⌘S — Sichern\n⇧⌘S — Sichern unter\n⌘W — Schließen\n⌘Z — Widerrufen\n⇧⌘Z — Wiederholen\n⌘X — Ausschneiden\n⌘C — Kopieren\n⌘V — Einfügen\n⌘A — Alles auswählen\n⌘F — Suchen und Ersetzen\n⌥←/→ — Wortsprung\n⌘←/→ — Zeilenanfang/-ende\nEsc — Suchleiste schließen\nReturn (Suchfeld) — Nächster Treffer\n⇧Return (Suchfeld) — Voriger Treffer\nReturn (Ersetzen-Feld) — Ersetzen + Weiter\n⌘Return (Ersetzen-Feld) — Alle ersetzen"
    },
    /* BTN_LANG_FR */
    {
        "À propos de ", "Quitter ", "Fichier", "Nouveau", "Ouvrir...",
        "Ouvrir un élément récent", "(Aucun)",
        "Enregistrer", "Enregistrer sous...", "Fermer", "Imprimer...",
        "Édition", "Annuler", "Rétablir",
        "Couper", "Copier", "Coller", "Tout sélectionner",
        "Rechercher et remplacer...", "Sans titre",
        "Voulez-vous enregistrer les modifications apportées à « %s » ?",
        "Vos modifications seront perdues si vous ne les enregistrez pas.",
        "Enregistrer", "Ne pas enregistrer", "Annuler",
        "Rechercher :", "Remplacer :", "Introuvable",
        "%d remplacement(s)",
        "Aide", "Raccourcis clavier...", "Raccourcis clavier",
        "⌘N — Nouveau\n⌘O — Ouvrir\n⌘S — Enregistrer\n⇧⌘S — Enregistrer sous\n⌘W — Fermer\n⌘Z — Annuler\n⇧⌘Z — Rétablir\n⌘X — Couper\n⌘C — Copier\n⌘V — Coller\n⌘A — Tout sélectionner\n⌘F — Rechercher et remplacer\n⌥←/→ — Saut de mot\n⌘←/→ — Début/fin de ligne\nÉchap — Fermer la barre de recherche\nRetour (champ de recherche) — Occurrence suivante\n⇧Retour (champ de recherche) — Occurrence précédente\nRetour (champ de remplacement) — Remplacer + suivant\n⌘Retour (champ de remplacement) — Tout remplacer"
    },
    /* BTN_LANG_ES */
    {
        "Acerca de ", "Salir de ", "Archivo", "Nuevo", "Abrir...",
        "Abrir reciente", "(Ninguno)",
        "Guardar", "Guardar como...", "Cerrar", "Imprimir...",
        "Edición", "Deshacer", "Rehacer",
        "Cortar", "Copiar", "Pegar", "Seleccionar todo",
        "Buscar y reemplazar...", "Sin título",
        "¿Quieres guardar los cambios realizados en “%s”?",
        "Tus cambios se perderán si no los guardas.",
        "Guardar", "No guardar", "Cancelar",
        "Buscar:", "Reemplazar:", "No encontrado",
        "%d reemplazos",
        "Ayuda", "Atajos de teclado...", "Atajos de teclado",
        "⌘N — Nuevo\n⌘O — Abrir\n⌘S — Guardar\n⇧⌘S — Guardar como\n⌘W — Cerrar\n⌘Z — Deshacer\n⇧⌘Z — Rehacer\n⌘X — Cortar\n⌘C — Copiar\n⌘V — Pegar\n⌘A — Seleccionar todo\n⌘F — Buscar y reemplazar\n⌥←/→ — Salto de palabra\n⌘←/→ — Inicio/fin de línea\nEsc — Cerrar barra de búsqueda\nIntro (campo de búsqueda) — Siguiente coincidencia\n⇧Intro (campo de búsqueda) — Coincidencia anterior\nIntro (campo de reemplazo) — Reemplazar + siguiente\n⌘Intro (campo de reemplazo) — Reemplazar todo"
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
        "全选", "查找和替换…", "未命名",
        "要存储对“%s”的修改吗？",
        "如果不存储，你所做的修改将丢失。",
        "存储", "不存储", "取消",
        "查找:", "替换:", "未找到",
        "已替换 %d 处",
        "帮助", "键盘快捷键…", "键盘快捷键",
        "⌘N — 新建\n⌘O — 打开\n⌘S — 存储\n⇧⌘S — 存储为\n⌘W — 关闭\n⌘Z — 撤销\n⇧⌘Z — 重做\n⌘X — 剪切\n⌘C — 拷贝\n⌘V — 粘贴\n⌘A — 全选\n⌘F — 查找和替换\n⌥←/→ — 按词跳转\n⌘←/→ — 行首/行尾\nEsc — 关闭查找栏\n回车（查找框）— 下一个匹配项\n⇧回车（查找框）— 上一个匹配项\n回车（替换框）— 替换并查找下一个\n⌘回车（替换框）— 全部替换"
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
