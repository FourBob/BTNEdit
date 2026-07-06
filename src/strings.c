#include "strings.h"
#include <string.h>

static BtnUiLang g_lang = BTN_LANG_EN;

/* Reihenfolge muss exakt zu BtnStringId in strings.h passen - ein Kommentar
 * pro Zeile waere hier mehr Rauschen als Nutzen, die Spalten sprechen fuer
 * sich (jede Zeile = eine Sprache, jede Spalte = ein BTN_STR_*). */
static const char *const g_strings[BTN_LANG_COUNT][BTN_STR_COUNT] = {
    /* BTN_LANG_EN */
    {
        "About ", "Quit ", "File", "New", "Open...", "Open Recent", "(None)",
        "Save", "Save As...", "Close", "Print...", "Edit", "Undo", "Redo",
        "Cut", "Copy", "Paste", "Select All", "Find...", "Untitled",
        "Do you want to save the changes made to \xe2\x80\x9c%s\xe2\x80\x9d?",
        "Your changes will be lost if you don't save them.",
        "Save", "Don't Save", "Cancel"
    },
    /* BTN_LANG_DE */
    {
        "\xc3\x9cber ", "Beende ", "Ablage", "Neu", "\xc3\x96ffnen...",
        "Zuletzt ge\xc3\xb6ffnet", "(Keine)",
        "Sichern", "Sichern unter...", "Schlie\xc3\x9fen", "Drucken...",
        "Bearbeiten", "Widerrufen", "Wiederholen",
        "Ausschneiden", "Kopieren", "Einf\xc3\xbcgen", "Alles ausw\xc3\xa4hlen",
        "Suchen...", "Unbenannt",
        "M\xc3\xb6chtest du die \xc3\x84nderungen an \xe2\x80\x9e%s\xe2\x80\x9c sichern?",
        "Deine \xc3\x84nderungen gehen verloren, wenn du sie nicht sicherst.",
        "Sichern", "Nicht sichern", "Abbrechen"
    },
    /* BTN_LANG_FR */
    {
        "\xc3\x80 propos de ", "Quitter ", "Fichier", "Nouveau", "Ouvrir...",
        "Ouvrir un \xc3\xa9l\xc3\xa9ment r\xc3\xa9cent", "(Aucun)",
        "Enregistrer", "Enregistrer sous...", "Fermer", "Imprimer...",
        "\xc3\x89dition", "Annuler", "R\xc3\xa9tablir",
        "Couper", "Copier", "Coller", "Tout s\xc3\xa9lectionner",
        "Rechercher...", "Sans titre",
        "Voulez-vous enregistrer les modifications apport\xc3\xa9es \xc3\xa0 \xc2\xab\xc2\xa0%s\xc2\xa0\xc2\xbb\xc2\xa0?",
        "Vos modifications seront perdues si vous ne les enregistrez pas.",
        "Enregistrer", "Ne pas enregistrer", "Annuler"
    },
    /* BTN_LANG_ES */
    {
        "Acerca de ", "Salir de ", "Archivo", "Nuevo", "Abrir...",
        "Abrir reciente", "(Ninguno)",
        "Guardar", "Guardar como...", "Cerrar", "Imprimir...",
        "Edici\xc3\xb3n", "Deshacer", "Rehacer",
        "Cortar", "Copiar", "Pegar", "Seleccionar todo",
        "Buscar...", "Sin t\xc3\xadtulo",
        "\xc2\xbfQuieres guardar los cambios realizados en \xe2\x80\x9c%s\xe2\x80\x9d?",
        "Tus cambios se perder\xc3\xa1n si no los guardas.",
        "Guardar", "No guardar", "Cancelar"
    },
    /* BTN_LANG_ZH (Simplified) */
    {
        "\xe5\x85\xb3\xe4\xba\x8e", "\xe9\x80\x80\xe5\x87\xba", "\xe6\x96\x87\xe4\xbb\xb6",
        "\xe6\x96\xb0\xe5\xbb\xba", "\xe6\x89\x93\xe5\xbc\x80\xe2\x80\xa6",
        "\xe6\x89\x93\xe5\xbc\x80\xe6\x9c\x80\xe8\xbf\x91\xe4\xbd\xbf\xe7\x94\xa8\xe7\x9a\x84\xe6\x96\x87\xe4\xbb\xb6",
        "\xef\xbc\x88\xe6\x97\xa0\xef\xbc\x89",
        "\xe5\xad\x98\xe5\x82\xa8", "\xe5\xad\x98\xe5\x82\xa8\xe4\xb8\xba\xe2\x80\xa6",
        "\xe5\x85\xb3\xe9\x97\xad", "\xe6\x89\x93\xe5\x8d\xb0\xe2\x80\xa6",
        "\xe7\xbc\x96\xe8\xbe\x91", "\xe6\x92\xa4\xe9\x94\x80", "\xe9\x87\x8d\xe5\x81\x9a",
        "\xe5\x89\xaa\xe5\x88\x87", "\xe6\x8b\xb7\xe8\xb4\x9d", "\xe7\xb2\x98\xe8\xb4\xb4",
        "\xe5\x85\xa8\xe9\x80\x89", "\xe6\x9f\xa5\xe6\x89\xbe\xe2\x80\xa6", "\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d",
        "\xe8\xa6\x81\xe5\xad\x98\xe5\x82\xa8\xe5\xaf\xb9\xe2\x80\x9c%s\xe2\x80\x9d\xe7\x9a\x84\xe4\xbf\xae\xe6\x94\xb9\xe5\x90\x97\xef\xbc\x9f",
        "\xe5\xa6\x82\xe6\x9e\x9c\xe4\xb8\x8d\xe5\xad\x98\xe5\x82\xa8\xef\xbc\x8c\xe4\xbd\xa0\xe6\x89\x80\xe5\x81\x9a\xe7\x9a\x84\xe4\xbf\xae\xe6\x94\xb9\xe5\xb0\x86\xe4\xb8\xa2\xe5\xa4\xb1\xe3\x80\x82",
        "\xe5\xad\x98\xe5\x82\xa8", "\xe4\xb8\x8d\xe5\xad\x98\xe5\x82\xa8", "\xe5\x8f\x96\xe6\xb6\x88"
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
