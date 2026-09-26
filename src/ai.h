#ifndef BTN_AI_H
#define BTN_AI_H

/* KI-Vervollstaendigung ueber einen lokal laufenden Server (Ollama oder
 * llama.cpp's llama-server) - optional, standardmaessig aus. Reines C ohne
 * Netzwerk: Konfiguration, JSON der Anfrage, Auswerten der Antwort und
 * Aufraeumen des Vorschlags. Die HTTP-Anfrage selbst stellt shim.m. */

#include <stddef.h>

enum { BTN_AI_API_OLLAMA = 0, BTN_AI_API_LLAMA = 1 };

typedef struct {
    int enabled;
    int api;            /* BTN_AI_API_* */
    char url[256];      /* ohne Pfad, z.B. "http://127.0.0.1:11434" */
    char model[128];    /* nur Ollama */
    int delay_ms;       /* Tipp-Pause bis zur Anfrage */
    int max_tokens;
} BtnAiConfig;

#define BTN_AI_PREFIX_BYTES 4096 /* Kontext vor dem Cursor */
#define BTN_AI_SUFFIX_BYTES 1024 /* und danach */

void btn_ai_config_defaults(BtnAiConfig *c);

/* Liest "schluessel=wert"-Zeilen (enabled, api, url, model, delay_ms,
 * max_tokens; '#' leitet einen Kommentar ein) ueber die Standardwerte;
 * Unbekanntes wird ignoriert, Zahlen werden auf sinnvolle Bereiche geklemmt. */
void btn_ai_config_parse(BtnAiConfig *c, const char *text, size_t len);

/* Schreibt die Konfiguration im selben Format (malloc, NUL-terminiert). */
char *btn_ai_config_format(const BtnAiConfig *c);

/* Voller Endpunkt (malloc): url + "/api/generate" bzw. "/infill". */
char *btn_ai_endpoint(const BtnAiConfig *c);

/* JSON-Koerper der Anfrage (malloc, *out_len ohne NUL): Text vor und nach
 * dem Cursor als Fill-in-the-Middle, nur eine Zeile (Stopp bei '\n').
 * Ungueltiges UTF-8 wird zu U+FFFD. */
char *btn_ai_request_body(const BtnAiConfig *c, const char *prefix, size_t prefix_len, const char *suffix,
                          size_t suffix_len, size_t *out_len);

/* Hol den Vorschlag aus der Antwort ("response" bei Ollama, "content" bei
 * llama-server) als UTF-8 (malloc). 1 = gefunden. */
int btn_ai_parse_response(int api, const char *body, size_t len, char **out, size_t *out_len);

/* Wert des Schluessels key auf oberster Ebene eines JSON-Objekts, falls er
 * ein String ist (malloc, dekodiert). 1 = gefunden. */
int btn_ai_json_get_string(const char *json, size_t len, const char *key, char **out, size_t *out_len);

/* Macht aus der Rohantwort einen einzeiligen Vorschlag: bis zum ersten
 * Zeilenende; was der Rest der aktuellen Zeile (rest, bis zum '\n') schon
 * enthaelt, wird am Ende abgeschnitten (") " + Vorschlag "x)" -> "x"). Nur
 * Leerraum zaehlt als kein Vorschlag. Rueckgabe: neue Laenge (s wird
 * gekuerzt), 0 = nichts anzubieten. */
size_t btn_ai_clean_suggestion(char *s, size_t len, const char *rest, size_t rest_len);

/* Soll an dieser Stelle gefragt werden? Nur wenn rechts vom Cursor bis zum
 * Zeilenende hoechstens Leerraum und schliessende Klammern/Anfuehrungszeichen
 * stehen - mitten in einer Zeile passt ein Vorschlag selten. */
int btn_ai_rest_allows_request(const char *rest, size_t rest_len);

#endif /* BTN_AI_H */
