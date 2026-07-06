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
        "⌘N — New
⌘O — Open
⌘S — Save
⇧⌘S — Save As
⌘W — Close
⌘Z — Undo
⇧⌘Z — Redo
⌘X — Cut
⌘C — Copy
⌘V — Paste
⌘A — Select All
⌘F — Find & Replace
⌥←/→ — Jump by word
⌘←/→ — Line start/end
Esc — Close find bar
Return (search field) — Next match
⇧Return (search field) — Previous match
Return (replace field) — Replace + Next
⌘Return (replace field) — Replace All"
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
        "⌘N — Neu
⌘O — Öffnen
⌘S — Sichern
⇧⌘S — Sichern unter
⌘W — Schließen
⌘Z — Widerrufen
⇧⌘Z — Wiederholen
⌘X — Ausschneiden
⌘C — Kopieren
⌘V — Einfügen
⌘A — Alles auswählen
⌘F — Suchen und Ersetzen
⌥←/→ — Wortsprung
⌘←/→ — Zeilenanfang/-ende
Esc — Suchleiste schließen
Return (Suchfeld) — Nächster Treffer
⇧Return (Suchfeld) — Voriger Treffer
Return (Ersetzen-Feld) — Ersetzen + Weiter
⌘Return (Ersetzen-Feld) — Alle ersetzen"
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
        "⌘N — Nouveau
⌘O — Ouvrir
⌘S — Enregistrer
⇧⌘S — Enregistrer sous
⌘W — Fermer
⌘Z — Annuler
⇧⌘Z — Rétablir
⌘X — Couper
⌘C — Copier
⌘V — Coller
⌘A — Tout sélectionner
⌘F — Rechercher et remplacer
⌥←/→ — Saut de mot
⌘←/→ — Début/fin de ligne
Échap — Fermer la barre de recherche
Retour (champ de recherche) — Occurrence suivante
⇧Retour (champ de recherche) — Occurrence précédente
Retour (champ de remplacement) — Remplacer + suivant
⌘Retour (champ de remplacement) — Tout remplacer"
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
        "⌘N — Nuevo
⌘O — Abrir
⌘S — Guardar
⇧⌘S — Guardar como
⌘W — Cerrar
⌘Z — Deshacer
⇧⌘Z — Rehacer
⌘X — Cortar
⌘C — Copiar
⌘V — Pegar
⌘A — Seleccionar todo
⌘F — Buscar y reemplazar
⌥←/→ — Salto de palabra
⌘←/→ — Inicio/fin de línea
Esc — Cerrar barra de búsqueda
Intro (campo de búsqueda) — Siguiente coincidencia
⇧Intro (campo de búsqueda) — Coincidencia anterior
Intro (campo de reemplazo) — Reemplazar + siguiente
⌘Intro (campo de reemplazo) — Reemplazar todo"
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
        "⌘N — 新建
⌘O — 打开
⌘S — 存储
⇧⌘S — 存储为
⌘W — 关闭
⌘Z — 撤销
⇧⌘Z — 重做
⌘X — 剪切
⌘C — 拷贝
⌘V — 粘贴
⌘A — 全选
⌘F — 查找和替换
⌥←/→ — 按词跳转
⌘←/→ — 行首/行尾
Esc — 关闭查找栏
回车（查找框）— 下一个匹配项
⇧回车（查找框）— 上一个匹配项
回车（替换框）— 替换并查找下一个
⌘回车（替换框）— 全部替换"
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
