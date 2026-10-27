/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: covers from Steam, GOG, PCGamingWiki, Wikipedia and Wikidata (pc5_cover.h), and
 * the games' backgrounds from Steam and GOG.
 *
 *   Steam      store.steampowered.com/api/storesearch: the store's own search,
 *              then the app's 600x900 library picture on Steam's image server
 *              (its file name comes from IStoreBrowseService/GetItems when the
 *              plain one is not there, as for newer games); its background is
 *              the library's hero picture, found the same way
 *   GOG        catalog.gog.com/v1/catalog: each product's coverVertical, and its
 *              galaxyBackgroundImage for the background
 *   PCGamingWiki  www.pcgamingwiki.com/w/api.php: the pages found, with their
 *              text, whose infobox names the cover (fetched through
 *              Special:FilePath) and the game's Steam app (its library hero,
 *              the background)
 *   Wikipedia  en.wikipedia.org/w/api.php: the article's lead picture
 *              (pageimages, which for a game is its box art)
 *   Wikidata   www.wikidata.org/w/api.php: the items found by name, then their
 *              Steam application ID (P1733): that app's pictures on Steam's
 *              image server, for games Steam's own search doesn't find by that name
 *
 * None of them needs an account or a key; each sees PC5's own User-Agent. */
#include "pc5_cover.h"
#include "pc5_image.h"
#include "pc5_json.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEAM_IMAGES "https://shared.akamai.steamstatic.com/store_item_assets/"

enum { ANSWER_MAX = 2 * 1024 * 1024, PICTURE_MAX = 16 * 1024 * 1024, MIN_SIMILARITY = 50, WIKITEXT = 16 * 1024 };

static const char *const source_names[PC5_PICTURE_SOURCES] = { "Steam", "GOG", "PCGamingWiki", "Wikipedia", "Wikidata" };
static const char *const source_keys[PC5_PICTURE_SOURCES] = { "steam", "gog", "pcgamingwiki", "wikipedia", "wikidata" };

const char *pc5_picture_source_name(int index)
{
    return index >= 0 && index < PC5_PICTURE_SOURCES ? source_names[index] : "";
}

const char *pc5_picture_source_key(int index)
{
    return index >= 0 && index < PC5_PICTURE_SOURCES ? source_keys[index] : "";
}

int pc5_url_encode(const char *text, char *out, size_t size)
{
    static const char hexdigits[] = "0123456789ABCDEF";
    size_t n = 0;

    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("-._~", *p)) {
            if (n + 1 >= size) return -1;
            out[n++] = (char)*p;
        } else {
            if (n + 3 >= size) return -1;
            out[n++] = '%';
            out[n++] = hexdigits[*p >> 4];
            out[n++] = hexdigits[*p & 15];
        }
    }
    if (n >= size) return -1;
    out[n] = 0;
    return 0;
}

/* ---- names ------------------------------------------------------------------------- */

/* Words that say nothing about which game it is. */
static int filler(const char *word)
{
    static const char *const words[] = { "the", "a", "an", "of", "and", "edition", "complete", "goty", "game",
                                         "year", "definitive", "enhanced", "remastered", "deluxe", "ultimate",
                                         "collection", "gold", "premium", "standard", "directors", "cut",
                                         "anniversary", "special", "classic", "hd", "pc", "windows", "steam",
                                         "gog", "repack", "x64", "x86", "win64", "win32", "multi", "final" };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (!strcmp(word, words[i])) return 1;
    return 0;
}

enum { MAX_WORDS = 24, WORD = 32 };

/* Lower-case words of letters and digits; roman numerals stay words. */
static int words_of(const char *text, char words[MAX_WORDS][WORD], int keep_filler)
{
    int count = 0;
    size_t n = 0;
    char word[WORD];

    for (const unsigned char *p = (const unsigned char *)text;; p++) {
        int c = *p;
        int letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80;
        if (letter && c != '\'' && n + 1 < WORD) {
            word[n++] = (char)pc5_lower(c);
            continue;
        }
        if (c == '\'') continue;   /* "Assassin's" is one word */
        if (n) {
            word[n] = 0;
            if ((keep_filler || !filler(word)) && count < MAX_WORDS) memcpy(words[count++], word, n + 1);
            n = 0;
        }
        if (!c) break;
    }
    return count;
}

/* Does short (e.g. "gta") spell the first letters of long's words from i on? */
static int initials(const char *short_word, char words[MAX_WORDS][WORD], int count, int i)
{
    size_t length = strlen(short_word);

    if (length < 2 || i + (int)length > count) return 0;
    for (size_t k = 0; k < length; k++)
        if (words[i + (int)k][0] != short_word[k]) return 0;
    return (int)length;
}

/* Words of a matched against b: a's word counts when b has it, or when it is
 * the initials of a run of b's words (each of those then counts as matched). */
static int matched(char a[MAX_WORDS][WORD], int na, char b[MAX_WORDS][WORD], int nb, int *b_matched)
{
    int hits = 0, used[MAX_WORDS] = { 0 };

    *b_matched = 0;
    for (int i = 0; i < na; i++) {
        int found = 0;
        for (int j = 0; j < nb && !found; j++)
            if (!used[j] && !strcmp(a[i], b[j])) { used[j] = 1; found = 1; (*b_matched)++; }
        for (int j = 0; j < nb && !found; j++) {
            int run = initials(a[i], b, nb, j);
            if (run) {
                for (int k = 0; k < run; k++) if (!used[j + k]) { used[j + k] = 1; (*b_matched)++; }
                found = 1;
            }
        }
        hits += found;
    }
    return hits;
}

int pc5_cover_similarity(const char *a, const char *b)
{
    char wa[MAX_WORDS][WORD], wb[MAX_WORDS][WORD];
    int na = words_of(a, wa, 0), nb = words_of(b, wb, 0), ab, ba, b_from_a, a_from_b;

    if (!na) na = words_of(a, wa, 1);
    if (!nb) nb = words_of(b, wb, 1);
    if (!na || !nb) return 0;
    ab = matched(wa, na, wb, nb, &b_from_a);
    ba = matched(wb, nb, wa, na, &a_from_b);
    /* The share of both names explained by the other, the weaker of the two
     * ways round counting most. */
    double one = (double)(ab > a_from_b ? ab : a_from_b) / na, two = (double)(ba > b_from_a ? ba : b_from_a) / nb;
    if (one > 1) one = 1;
    if (two > 1) two = 1;
    return (int)(100.0 * (one < two ? one * 0.7 + two * 0.3 : two * 0.7 + one * 0.3) + 0.5);
}

/* A folder's name made a search: release tags, versions and brackets out,
 * dots and underscores to spaces, words split where the case changes. */
static void clean_name(const char *name, char *out, size_t size)
{
    char raw[PC5_NAME * 2], text[PC5_NAME * 2];
    size_t n = 0, r = 0;
    int depth = 0;

    /* Versions first ("v1.2.0", "1.0.3", "v2"), while their dots still tie them together. */
    for (const char *p = name; *p && r + 1 < sizeof(raw);) {
        int boundary = p == name || strchr(" ._-+[(", p[-1]);
        const char *q = p;
        if (boundary && (*q == 'v' || *q == 'V') && q[1] >= '0' && q[1] <= '9') q++;
        if (boundary && *q >= '0' && *q <= '9') {
            const char *e = q;
            int dots = 0;
            while ((*e >= '0' && *e <= '9') || (*e == '.' && e[1] >= '0' && e[1] <= '9')) dots += *e++ == '.';
            if ((dots || q != p) && (!*e || strchr(" ._-+])", *e))) { p = e; continue; }
        }
        raw[r++] = *p++;
    }
    raw[r] = 0;
    for (const char *p = raw; *p && n + 2 < sizeof(text); p++) {
        char c = *p;
        if (c == '[' || c == '(' || c == '{') { depth++; continue; }
        if (c == ']' || c == ')' || c == '}') { if (depth) depth--; continue; }
        if (depth) continue;
        if (c == '.' || c == '_' || c == '-' || c == '+') c = ' ';
        /* "HalfLife2" -> "Half Life 2", but "GTAIV" stays one word */
        if (n && c != ' ' && text[n - 1] != ' ' &&
            ((c >= 'A' && c <= 'Z' && text[n - 1] >= 'a' && text[n - 1] <= 'z') ||
             (c >= '0' && c <= '9' && ((text[n - 1] >= 'a' && text[n - 1] <= 'z') ||
                                        (text[n - 1] >= 'A' && text[n - 1] <= 'Z')))))
            text[n++] = ' ';
        text[n++] = c;
    }
    text[n] = 0;
    /* Release words out, the rest kept in order. */
    out[0] = 0;
    n = 0;
    for (char *word = text, *next; word && *word; word = next) {
        char lower[64];
        size_t length, k;
        while (*word == ' ') word++;
        if (!*word) break;
        next = strchr(word, ' ');
        length = next ? (size_t)(next - word) : strlen(word);
        if (next) *next++ = 0;
        for (k = 0; k < length && k + 1 < sizeof(lower); k++) lower[k] = (char)pc5_lower((unsigned char)word[k]);
        lower[k] = 0;
        if (!strcmp(lower, "gog") || !strcmp(lower, "steam") || !strcmp(lower, "repack") || !strcmp(lower, "x64") ||
            !strcmp(lower, "win64") || !strcmp(lower, "multi") || !strcmp(lower, "pc"))
            continue;
        if (n + length + 2 >= size) break;
        if (n) out[n++] = ' ';
        memcpy(out + n, word, length);
        n += length;
        out[n] = 0;
    }
}

/* Product names that only say what made the program. */
static int generic_product(const char *product)
{
    static const char *const names[] = { "unity", "unity player", "unreal engine", "ue4", "ue5", "game",
                                         "launcher", "bootstrapper", "microsoft windows operating system",
                                         "microsoft® windows® operating system", "godot engine", "gamemaker",
                                         "rpg maker", "nw.js", "electron", "love", "application" };
    char lower[PC5_NAME];
    size_t i = 0;

    for (; product[i] && i + 1 < sizeof(lower); i++) lower[i] = (char)pc5_lower((unsigned char)product[i]);
    lower[i] = 0;
    for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); k++)
        if (!strcmp(lower, names[k])) return 1;
    return strlen(lower) < 2;
}

int pc5_cover_queries(const Pc5CoverHint *hint, char queries[3][PC5_NAME])
{
    char candidates[3][PC5_NAME];
    int count = 0, n = 0;

    candidates[0][0] = candidates[1][0] = candidates[2][0] = 0;
    if (hint->product[0] && !generic_product(hint->product))
        pc5_copy(candidates[n++], PC5_NAME, hint->product);
    clean_name(hint->folder, candidates[n], PC5_NAME);
    if (candidates[n][0]) n++;
    clean_name(hint->program, candidates[n], PC5_NAME);
    if (candidates[n][0] && !generic_product(candidates[n])) n++;
    for (int i = 0; i < n; i++) {
        int seen = 0;
        for (int k = 0; k < count; k++) seen |= !pc5_casecmp(queries[k], candidates[i]);
        if (!seen) memcpy(queries[count++], candidates[i], PC5_NAME);
    }
    return count;
}

/* ---- the search ------------------------------------------------------------------- */

typedef enum Plan {
    PLAN_STEAM_APP, PLAN_STEAM, PLAN_GOG, PLAN_PCGAMINGWIKI, PLAN_WIKIPEDIA, PLAN_WIKIDATA, PLAN_WIKIDATA_APPS, PLAN_DONE
} Plan;

/* What step n does: the given app first, Steam for the first two queries, GOG,
 * PCGamingWiki, Wikipedia and Wikidata (two steps) for the first; the sources turned
 * off left out. */
static Plan plan(const Pc5CoverSearch *search, int step, int *query)
{
    Plan order[10];
    int queries[10], n = 0;
    uint32_t off = search->hint.off;

    if (search->hint.steam_appid && !(off & PC5_PICTURES_STEAM)) { order[n] = PLAN_STEAM_APP; queries[n++] = 0; }
    if (search->query_count > 0 && !(off & PC5_PICTURES_STEAM)) { order[n] = PLAN_STEAM; queries[n++] = 0; }
    if (search->query_count > 1 && !(off & PC5_PICTURES_STEAM)) { order[n] = PLAN_STEAM; queries[n++] = 1; }
    if (search->query_count > 0 && !(off & PC5_PICTURES_GOG)) { order[n] = PLAN_GOG; queries[n++] = 0; }
    if (search->query_count > 0 && !(off & PC5_PICTURES_PCGAMINGWIKI)) { order[n] = PLAN_PCGAMINGWIKI; queries[n++] = 0; }
    if (search->query_count > 0 && !(off & PC5_PICTURES_WIKIPEDIA)) { order[n] = PLAN_WIKIPEDIA; queries[n++] = 0; }
    if (search->query_count > 0 && !(off & PC5_PICTURES_WIKIDATA)) {
        order[n] = PLAN_WIKIDATA; queries[n++] = 0;
        order[n] = PLAN_WIKIDATA_APPS; queries[n++] = 0;
    }
    if (step >= n) return PLAN_DONE;
    *query = queries[step];
    return order[step];
}

void pc5_cover_search_start(Pc5CoverSearch *search, const Pc5CoverHint *hint)
{
    memset(search, 0, sizeof(*search));
    search->hint = *hint;
    search->query_count = pc5_cover_queries(hint, search->queries);
    /* The player's own picture comes before anything found online. */
    if (hint->picture[0]) {
        Pc5CoverChoice *own = &search->choices[search->count++];
        *own = (Pc5CoverChoice){ .score = 1000000 };
        pc5_copy(own->source, sizeof(own->source), "your picture");
        pc5_copy(own->title, sizeof(own->title), hint->folder);
        pc5_copy(own->url, sizeof(own->url), hint->picture);
        search->answered = 1;
    }
}

static void add_choice(Pc5CoverSearch *search, const char *source, const char *title, const char *url,
                       const char *background, uint32_t appid, int score)
{
    Pc5CoverChoice *slot = NULL;

    if (!url[0] || strncmp(url, "https://", 8)) return;
    for (int i = 0; i < search->count; i++)
        if (!strcmp(search->choices[i].url, url)) {
            if (score > search->choices[i].score) search->choices[i].score = score;
            return;
        }
    if (search->count < PC5_COVER_CHOICES) {
        slot = &search->choices[search->count++];
    } else {
        int worst = 0;
        for (int i = 1; i < search->count; i++) if (search->choices[i].score < search->choices[worst].score) worst = i;
        if (search->choices[worst].score >= score) return;
        slot = &search->choices[worst];
    }
    *slot = (Pc5CoverChoice){ .steam_appid = appid, .score = score };
    pc5_copy(slot->source, sizeof(slot->source), source);
    pc5_copy(slot->title, sizeof(slot->title), title);
    pc5_copy(slot->url, sizeof(slot->url), url);
    if (background && !strncmp(background, "https://", 8)) pc5_copy(slot->background, sizeof(slot->background), background);
}

/* Steam's library hero for an app, by its plain name (newer apps: the asset list's). */
static void steam_hero(uint32_t appid, char *out, size_t size)
{
    snprintf(out, size, STEAM_IMAGES "steam/apps/%u/library_hero.jpg", (unsigned)appid);
}

/* Store entries that are not the game itself. */
static int extra_item(const char *title)
{
    static const char *const words[] = { " soundtrack ", " ost ", " artbook ", " art book ", " season pass ", " dlc ",
                                         " demo ", " dedicated server ", " sdk ", " editor ", " bonus content ",
                                         " upgrade ", " costume ", " pack " };
    char padded[132];
    size_t n = 1;
    padded[0] = ' ';
    for (const unsigned char *p = (const unsigned char *)title; *p && n + 2 < sizeof(padded); p++) {
        int c = pc5_lower(*p);
        padded[n++] = (char)((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80 ? c : ' ');
    }
    padded[n++] = ' ';
    padded[n] = 0;
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (strstr(padded, words[i])) return 1;
    return 0;
}

/* The best similarity of a found title to any of the queries (and to the
 * folder's own name). */
static int best_similarity(const Pc5CoverSearch *search, const char *title)
{
    char plain[128];
    int best = 0;
    size_t n = 0;

    /* "Half-Life (video game)": the part in brackets is Wikipedia's, not the game's. */
    for (const char *p = title; *p && n + 1 < sizeof(plain) && *p != '('; p++) plain[n++] = *p;
    plain[n] = 0;
    for (int i = 0; i < search->query_count; i++) {
        int s = pc5_cover_similarity(search->queries[i], plain);
        if (s > best) best = s;
    }
    return extra_item(plain) ? best - 40 : best;
}

static int ask(const Pc5Http *http, const char *url, Pc5Json *json, Pc5Buf *answer)
{
    int status = 0;

    answer->len = 0;
    if (http->get(http->context, url, answer, ANSWER_MAX, &status) || status != 200 || pc5_buf_terminate(answer))
        return -1;
    return pc5_json_parse(json, (const char *)answer->data, answer->len, 200000);
}

static void steam_search(Pc5CoverSearch *search, const Pc5Http *http, const char *query)
{
    char term[3 * PC5_NAME + 1], url[512], name[128], picture[256], hero[256];
    Pc5Buf answer = { 0 };
    Pc5Json json;

    if (pc5_url_encode(query, term, sizeof(term)) ||
        pc5_format(url, sizeof(url), "https://store.steampowered.com/api/storesearch/?term=%s&cc=US&l=english", term))
        return;
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        search->answered_stores++;
        uint32_t items = pc5_json_get(&json, 0, "items");
        for (uint32_t i = 0; i < 8; i++) {
            uint32_t item = pc5_json_item(&json, items, i);
            if (!item) break;
            uint32_t id = (uint32_t)pc5_json_number(&json, pc5_json_get(&json, item, "id"), 0);
            char type[16];
            pc5_json_text(&json, pc5_json_get(&json, item, "type"), type, sizeof(type));
            if (!id || strcmp(type, "app") || pc5_json_text(&json, pc5_json_get(&json, item, "name"), name, sizeof(name)))
                continue;
            int similarity = best_similarity(search, name);
            if (similarity < MIN_SIMILARITY) continue;
            snprintf(picture, sizeof(picture), STEAM_IMAGES "steam/apps/%u/library_600x900_2x.jpg", (unsigned)id);
            steam_hero(id, hero, sizeof(hero));
            add_choice(search, source_names[0], name, picture, hero, id, similarity + 30 - (int)i * 2);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

static void gog_search(Pc5CoverSearch *search, const Pc5Http *http, const char *query)
{
    char term[3 * PC5_NAME + 1], url[512], name[128], picture[512], background[512];
    Pc5Buf answer = { 0 };
    Pc5Json json;

    if (pc5_url_encode(query, term, sizeof(term)) ||
        pc5_format(url, sizeof(url), "https://catalog.gog.com/v1/catalog?query=like:%s&limit=5&order=desc:score", term))
        return;
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        search->answered_stores++;
        uint32_t products = pc5_json_get(&json, 0, "products");
        for (uint32_t i = 0; i < 5; i++) {
            uint32_t product = pc5_json_item(&json, products, i);
            if (!product) break;
            if (pc5_json_text(&json, pc5_json_get(&json, product, "title"), name, sizeof(name)) ||
                pc5_json_text(&json, pc5_json_get(&json, product, "coverVertical"), picture, sizeof(picture)))
                continue;
            int similarity = best_similarity(search, name);
            if (similarity < MIN_SIMILARITY) continue;
            if (pc5_json_text(&json, pc5_json_get(&json, product, "galaxyBackgroundImage"), background, sizeof(background)))
                background[0] = 0;
            add_choice(search, source_names[1], name, picture, background, 0, similarity + 20 - (int)i * 2);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

static void wikipedia_search(Pc5CoverSearch *search, const Pc5Http *http, const char *query)
{
    char words[PC5_NAME + 16], term[3 * (PC5_NAME + 16) + 1], url[768], name[128], picture[512];
    Pc5Buf answer = { 0 };
    Pc5Json json;

    snprintf(words, sizeof(words), "%s video game", query);
    if (pc5_url_encode(words, term, sizeof(term)) ||
        pc5_format(url, sizeof(url),
                   "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2&generator=search"
                   "&gsrsearch=%s&gsrlimit=3&prop=pageimages&piprop=thumbnail&pithumbsize=900&pilicense=any",
                   term))
        return;
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        uint32_t pages = pc5_json_get(&json, pc5_json_get(&json, 0, "query"), "pages");
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t page = pc5_json_item(&json, pages, i);
            if (!page) break;
            uint32_t thumb = pc5_json_get(&json, page, "thumbnail");
            if (!thumb || pc5_json_text(&json, pc5_json_get(&json, page, "title"), name, sizeof(name)) ||
                pc5_json_text(&json, pc5_json_get(&json, thumb, "source"), picture, sizeof(picture)))
                continue;
            int similarity = best_similarity(search, name);
            if (similarity < MIN_SIMILARITY) continue;
            int rank = (int)pc5_json_number(&json, pc5_json_get(&json, page, "index"), 3);
            add_choice(search, source_names[3], name, picture, NULL, 0, similarity - rank * 2);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

/* A parameter of PCGamingWiki's {{Infobox game}} in a page's text: "|name = value" (any
 * spaces), its value to the end of its line, trimmed. 0, or -1 when it isn't there or is
 * empty. */
static int infobox_value(const char *text, const char *name, char *out, size_t size)
{
    const char *box = strstr(text, "{{Infobox game");
    size_t n = strlen(name);

    out[0] = 0;
    if (!box) return -1;
    for (const char *p = box; (p = strchr(p, '|')); p++) {
        const char *q = p + 1, *end;
        while (*q == ' ' || *q == '\t') q++;
        if (strncmp(q, name, n)) continue;
        q += n;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '=') continue;
        for (q++; *q == ' ' || *q == '\t'; q++) {
        }
        end = q;
        while (*end && *end != '\n' && *end != '\r' && *end != '|' && strncmp(end, "}}", 2)) end++;
        while (end > q && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if (end == q || (size_t)(end - q) >= size) return -1;
        memcpy(out, q, (size_t)(end - q));
        out[end - q] = 0;
        return 0;
    }
    return -1;
}

static void pcgamingwiki_search(Pc5CoverSearch *search, const Pc5Http *http, const char *query)
{
    char term[3 * PC5_NAME + 1], url[768], name[128], file[256], encoded[3 * 256 + 1], picture[1024], hero[256],
         appid_text[64];
    char *text = pc5_alloc(WIKITEXT);
    Pc5Buf answer = { 0 };
    Pc5Json json;

    if (!text) return;
    if (pc5_url_encode(query, term, sizeof(term)) ||
        pc5_format(url, sizeof(url),
                   "https://www.pcgamingwiki.com/w/api.php?action=query&format=json&formatversion=2&generator=search"
                   "&gsrnamespace=0&gsrlimit=3&gsrsearch=%s&prop=revisions&rvprop=content&rvslots=main",
                   term)) {
        pc5_free(text);
        return;
    }
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        uint32_t pages = pc5_json_get(&json, pc5_json_get(&json, 0, "query"), "pages");
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t page = pc5_json_item(&json, pages, i);
            if (!page) break;
            uint32_t revision = pc5_json_item(&json, pc5_json_get(&json, page, "revisions"), 0);
            uint32_t content = pc5_json_get(&json, pc5_json_get(&json, pc5_json_get(&json, revision, "slots"), "main"),
                                            "content");
            if (pc5_json_text(&json, pc5_json_get(&json, page, "title"), name, sizeof(name)) ||
                pc5_json_text(&json, content, text, WIKITEXT) || infobox_value(text, "cover", file, sizeof(file)))
                continue;
            int similarity = best_similarity(search, name);
            if (similarity < MIN_SIMILARITY) continue;
            const char *bare = !strncmp(file, "File:", 5) ? file + 5 : file;
            if (pc5_url_encode(bare, encoded, sizeof(encoded)) ||
                pc5_format(picture, sizeof(picture), "https://www.pcgamingwiki.com/wiki/Special:FilePath/%s", encoded))
                continue;
            uint32_t appid = 0;
            if (!infobox_value(text, "steam appid", appid_text, sizeof(appid_text)))
                appid = (uint32_t)strtoul(appid_text, NULL, 10);
            if (appid) steam_hero(appid, hero, sizeof(hero));
            int rank = (int)pc5_json_number(&json, pc5_json_get(&json, page, "index"), 3);
            add_choice(search, source_names[2], name, picture, appid ? hero : NULL, appid, similarity + 10 - rank * 2);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
    pc5_free(text);
}

/* Wikidata, first step: the items its search finds for the name, kept for the next. */
static void wikidata_find(Pc5CoverSearch *search, const Pc5Http *http, const char *query)
{
    char term[3 * PC5_NAME + 1], url[768], id[16];
    Pc5Buf answer = { 0 };
    Pc5Json json;

    search->wikidata_count = 0;
    if (pc5_url_encode(query, term, sizeof(term)) ||
        pc5_format(url, sizeof(url),
                   "https://www.wikidata.org/w/api.php?action=wbsearchentities&format=json&language=en&uselang=en"
                   "&type=item&limit=5&search=%s",
                   term))
        return;
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        uint32_t found = pc5_json_get(&json, 0, "search");
        for (uint32_t i = 0; i < 5 && search->wikidata_count < 5; i++) {
            uint32_t item = pc5_json_item(&json, found, i);
            if (!item) break;
            if (pc5_json_text(&json, pc5_json_get(&json, item, "id"), id, sizeof(id)) || id[0] != 'Q' || !id[1]) continue;
            int digits = 1;
            for (const char *p = id + 1; *p; p++) digits &= *p >= '0' && *p <= '9';
            if (digits) pc5_copy(search->wikidata[search->wikidata_count++], sizeof(search->wikidata[0]), id);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

/* Wikidata, second step: those items' names and Steam application IDs (P1733). */
static void wikidata_apps(Pc5CoverSearch *search, const Pc5Http *http)
{
    char ids[5 * 20], url[768], name[128], appid_text[32], picture[256], hero[256];
    Pc5Buf answer = { 0 };
    Pc5Json json;
    size_t n = 0;

    if (!search->wikidata_count) {
        search->requests--;   /* nothing to ask */
        return;
    }
    ids[0] = 0;
    for (int i = 0; i < search->wikidata_count; i++)
        n += (size_t)snprintf(ids + n, sizeof(ids) - n, "%s%s", i ? "%7C" : "", search->wikidata[i]);
    if (pc5_format(url, sizeof(url),
                   "https://www.wikidata.org/w/api.php?action=wbgetentities&format=json&props=claims%%7Clabels"
                   "&languages=en&ids=%s",
                   ids))
        return;
    if (!ask(http, url, &json, &answer)) {
        search->answered++;
        uint32_t entities = pc5_json_get(&json, 0, "entities");
        for (int i = 0; i < search->wikidata_count; i++) {
            uint32_t entity = pc5_json_get(&json, entities, search->wikidata[i]);
            uint32_t label = pc5_json_get(&json, pc5_json_get(&json, pc5_json_get(&json, entity, "labels"), "en"), "value");
            uint32_t claim = pc5_json_item(&json, pc5_json_get(&json, pc5_json_get(&json, entity, "claims"), "P1733"), 0);
            uint32_t value = pc5_json_get(&json, pc5_json_get(&json, pc5_json_get(&json, claim, "mainsnak"), "datavalue"),
                                          "value");
            if (!entity || pc5_json_text(&json, label, name, sizeof(name)) ||
                pc5_json_text(&json, value, appid_text, sizeof(appid_text)))
                continue;
            uint32_t appid = (uint32_t)strtoul(appid_text, NULL, 10);
            int similarity = best_similarity(search, name);
            if (!appid || similarity < MIN_SIMILARITY) continue;
            snprintf(picture, sizeof(picture), STEAM_IMAGES "steam/apps/%u/library_600x900_2x.jpg", (unsigned)appid);
            steam_hero(appid, hero, sizeof(hero));
            add_choice(search, source_names[4], name, picture, hero, appid, similarity + 25 - i * 2);
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
}

static int by_score(const void *a, const void *b)
{
    const Pc5CoverChoice *x = a, *y = b;
    return y->score - x->score;
}

int pc5_cover_search_step(Pc5CoverSearch *search, const Pc5Http *http)
{
    int query = 0;
    Plan what = search->hint.picture_only ? PLAN_DONE : plan(search, search->step, &query);

    if (what == PLAN_DONE) {
        qsort(search->choices, (size_t)search->count, sizeof(search->choices[0]), by_score);
        return 0;
    }
    search->requests++;
    switch (what) {
    case PLAN_STEAM_APP: {
        char picture[256];
        char hero[256];
        snprintf(picture, sizeof(picture), STEAM_IMAGES "steam/apps/%u/library_600x900_2x.jpg",
                 (unsigned)search->hint.steam_appid);
        steam_hero(search->hint.steam_appid, hero, sizeof(hero));
        add_choice(search, source_names[0], search->hint.folder, picture, hero, search->hint.steam_appid, 1000);
        search->requests--;   /* nothing was asked */
        break;
    }
    case PLAN_STEAM: steam_search(search, http, search->queries[query]); break;
    case PLAN_GOG: gog_search(search, http, search->queries[query]); break;
    case PLAN_PCGAMINGWIKI: pcgamingwiki_search(search, http, search->queries[query]); break;
    case PLAN_WIKIPEDIA: wikipedia_search(search, http, search->queries[query]); break;
    case PLAN_WIKIDATA: wikidata_find(search, http, search->queries[query]); break;
    case PLAN_WIKIDATA_APPS: wikidata_apps(search, http); break;
    default: break;
    }
    search->step++;
    if (plan(search, search->step, &query) == PLAN_DONE) {
        qsort(search->choices, (size_t)search->count, sizeof(search->choices[0]), by_score);
        return 0;
    }
    return 1;
}

/* Newer Steam apps keep their library picture under a hashed name, which the
 * store's item list gives. */
/* An asset of the app (its first named field present in its asset list) on Steam's image server. */
static int steam_asset_url(const Pc5Http *http, uint32_t appid, const char *const *fields, char *out, size_t size)
{
    char input[256], encoded[768], url[1024], format[256], file[160];
    Pc5Buf answer = { 0 };
    Pc5Json json;
    int status = -1;

    snprintf(input, sizeof(input),
             "{\"ids\":[{\"appid\":%u}],\"context\":{\"language\":\"english\",\"country_code\":\"US\"},"
             "\"data_request\":{\"include_assets\":true}}", (unsigned)appid);
    if (pc5_url_encode(input, encoded, sizeof(encoded)) ||
        pc5_format(url, sizeof(url), "https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json=%s",
                   encoded))
        return -1;
    if (!ask(http, url, &json, &answer)) {
        uint32_t item = pc5_json_item(&json, pc5_json_get(&json, pc5_json_get(&json, 0, "response"), "store_items"), 0);
        uint32_t assets = pc5_json_get(&json, item, "assets");
        int found = 0;
        for (int f = 0; fields[f] && !found; f++)
            found = !pc5_json_text(&json, pc5_json_get(&json, assets, fields[f]), file, sizeof(file));
        if (!pc5_json_text(&json, pc5_json_get(&json, assets, "asset_url_format"), format, sizeof(format)) && found) {
            char *slot = strstr(format, "${FILENAME}");
            if (slot) {
                *slot = 0;
                status = pc5_format(out, size, STEAM_IMAGES "%s%s%s", format, file, slot + 11);
            }
        }
        pc5_json_free(&json);
    }
    pc5_buf_free(&answer);
    return status;
}

static int download_picture(const Pc5Http *http, const char *url, Pc5Image *picture, Pc5Buf *original)
{
    Pc5Buf data = { 0 };
    int status = 0, result = -1;

    if (!http->get(http->context, url, &data, PICTURE_MAX, &status) && status == 200)
        result = pc5_image_decode(data.data, data.len, picture);
    if (!result && original) {
        pc5_buf_free(original);
        *original = data;
    } else {
        pc5_buf_free(&data);
    }
    return result;
}

/* The player's own picture, from its file. */
static int read_picture(const char *path, Pc5Image *picture, Pc5Buf *original)
{
    Pc5Buf data = { 0 };
    int result = -1;

    if (!pc5_read_file(path, &data, PICTURE_MAX)) result = pc5_image_decode(data.data, data.len, picture);
    if (!result && original) {
        pc5_buf_free(original);
        *original = data;
    } else {
        pc5_buf_free(&data);
    }
    return result;
}

int pc5_cover_fetch(const Pc5CoverChoice *choice, const Pc5Http *http, Pc5Image *cover, Pc5Buf *original)
{
    Pc5Image picture = { 0 };
    int status;

    *cover = (Pc5Image){ 0 };
    if (choice->url[0] == '/') {
        if (read_picture(choice->url, &picture, original)) return -1;
    } else if (download_picture(http, choice->url, &picture, original)) {
        static const char *const capsule[] = { "library_capsule_2x", "library_capsule", NULL };
        char other[512];
        if (!choice->steam_appid || steam_asset_url(http, choice->steam_appid, capsule, other, sizeof(other)) ||
            download_picture(http, other, &picture, original))
            return -1;
    }
    status = pc5_cover_compose(&picture, cover);
    pc5_image_free(&picture);
    return status;
}

int pc5_background_fetch(const char *url, uint32_t steam_appid, const Pc5Http *http, Pc5Image *background)
{
    static const char *const hero[] = { "library_hero_2x", "library_hero", NULL };
    Pc5Image picture = { 0 };
    char other[512];
    int status;

    *background = (Pc5Image){ 0 };
    if ((!url || !url[0] || download_picture(http, url, &picture, NULL)) &&
        (!steam_appid || steam_asset_url(http, steam_appid, hero, other, sizeof(other)) ||
         download_picture(http, other, &picture, NULL)))
        return -1;
    status = pc5_background_compose(&picture, background);
    pc5_image_free(&picture);
    return status;
}

int pc5_background_read(const char *path, Pc5Image *background)
{
    Pc5Image picture = { 0 };
    int status;

    *background = (Pc5Image){ 0 };
    if (read_picture(path, &picture, NULL)) return -1;
    status = pc5_background_compose(&picture, background);
    pc5_image_free(&picture);
    return status;
}

int pc5_cover_own(const char *dir, const char *const *names, char *out, size_t size)
{
    static const char *const kinds[] = { ".jpg", ".jpeg", ".png" };
    DIR *folder;
    struct dirent *entry;
    int found = -1;

    if (!dir || !names || !(folder = opendir(dir))) return -1;
    while (found && (entry = readdir(folder))) {
        const char *dot = strrchr(entry->d_name, '.');
        if (!dot || entry->d_name[0] == '.') continue;
        for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]) && found; k++) {
            if (pc5_casecmp(dot, kinds[k])) continue;
            for (size_t n = 0; names[n] && found; n++) {
                size_t length = strlen(names[n]);
                char base[PC5_PATH];
                if (!length || length != (size_t)(dot - entry->d_name) || length >= sizeof(base)) continue;
                memcpy(base, entry->d_name, length);
                base[length] = 0;
                if (!pc5_casecmp(base, names[n]) && !pc5_join(out, size, dir, entry->d_name) && pc5_is_file(out))
                    found = 0;
            }
        }
    }
    closedir(folder);
    return found;
}

const char *pc5_picture_name(const Pc5Buf *original)
{
    return original && original->len > 4 && original->data[0] == 0x89 && original->data[1] == 'P' ?
           "cover-original.png" : "cover-original.jpg";
}
