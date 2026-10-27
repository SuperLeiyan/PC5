/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: finding a game's cover online, from public sources only, with no
 * account and no key: Steam's store, GOG's catalogue, PCGamingWiki, Wikipedia
 * and Wikidata, in that order, after the player's own picture when there is
 * one. Each can be turned off (Settings › System › Picture sources). Each
 * lookup is one request, so the screen can move between them. */
#ifndef PC5_COVER_H
#define PC5_COVER_H
#include "pc5.h"

/* How PC5 asks the network for something (the console's HTTPS is
 * pc5_net.c over pc5_net_ps5.c; the tests answer from files). */
typedef struct Pc5Http {
    void *context;
    /* GET url into out (at most max bytes). 0 with the HTTP status in
     * *status, or -1 when no answer came at all. */
    int (*get)(void *context, const char *url, Pc5Buf *out, size_t max, int *status);
    /* GET url, its body handed to sink as it arrives (only when the final status is
     * 200), never held whole: 0 with the status, -1 when it failed or was stopped.
     * NULL where there is none. */
    int (*download)(void *context, const char *url, const Pc5Sink *sink, int *status);
} Pc5Http;

/* The sources, as bits of Pc5CoverHint's off. */
enum {
    PC5_PICTURES_STEAM = 1u << 0,
    PC5_PICTURES_GOG = 1u << 1,
    PC5_PICTURES_PCGAMINGWIKI = 1u << 2,
    PC5_PICTURES_WIKIPEDIA = 1u << 3,
    PC5_PICTURES_WIKIDATA = 1u << 4,
    PC5_PICTURE_SOURCES = 5
};
/* A source's name ("Steam", "GOG", "PCGamingWiki", "Wikipedia", "Wikidata") and its key in
 * the settings file ("steam", ...), by its index (its bit's). */
const char *pc5_picture_source_name(int index);
const char *pc5_picture_source_key(int index);

/* What PC5 knows about the game before asking. */
typedef struct Pc5CoverHint {
    char folder[PC5_NAME];      /* the folder's name, as on the USB drive */
    char product[PC5_NAME];     /* the program's ProductName, or "" */
    char program[PC5_NAME];     /* the .exe's name without .exe */
    uint32_t steam_appid;       /* from a steam_appid.txt in the folder, or 0 */
    /* The player's own picture for it (a JPEG or PNG file), offered first;
     * with picture_only nothing is asked online. "" for none. */
    char picture[PC5_PATH];
    int picture_only;
    uint32_t off;               /* PC5_PICTURES_* not to ask */
} Pc5CoverHint;

enum { PC5_COVER_CHOICES = 12 };

typedef struct Pc5CoverChoice {
    char source[16];            /* a source's name (pc5_picture_source_name), or "your picture" */
    char title[128];            /* the game's name there */
    char url[PC5_PATH];         /* its cover picture: https://..., or the player's file */
    char background[PC5_PATH];  /* the game's wide background picture there (https://...), or "" */
    uint32_t steam_appid;       /* Steam: the app, for its asset list */
    int score;
} Pc5CoverChoice;

typedef struct Pc5CoverSearch {
    Pc5CoverChoice choices[PC5_COVER_CHOICES];
    int count;
    /* where the search is */
    char queries[3][PC5_NAME];
    int query_count, step, requests, answered;
    int answered_stores;          /* of those, Steam's and GOG's (which have backgrounds) */
    char wikidata[5][16];         /* Wikidata's items for the first query, for its next step */
    int wikidata_count;
    Pc5CoverHint hint;
} Pc5CoverSearch;

/* The words to search for, best first: the product name, the folder's name
 * cleaned of release tags and versions, the program's name. */
int pc5_cover_queries(const Pc5CoverHint *hint, char queries[3][PC5_NAME]);
/* How alike two game names are, 0 to 100 (word overlap, "GTA" matching
 * "Grand Theft Auto", edition words ignored). */
int pc5_cover_similarity(const char *a, const char *b);

void pc5_cover_search_start(Pc5CoverSearch *search, const Pc5CoverHint *hint);
/* One request. 1 while there is more to ask, 0 when done (choices best
 * first). A source that does not answer is skipped. */
int pc5_cover_search_step(Pc5CoverSearch *search, const Pc5Http *http);
/* A choice's picture, fetched and made a cover (pc5_cover_compose). original,
 * when not NULL, gets the picture's file as it was downloaded (JPEG or PNG). */
int pc5_cover_fetch(const Pc5CoverChoice *choice, const Pc5Http *http, Pc5Image *cover, Pc5Buf *original);
/* A game's background from a choice (its background, or Steam's from the app's asset
 * list), or from url alone with appid 0, made 16:9 (pc5_background_compose). */
int pc5_background_fetch(const char *url, uint32_t steam_appid, const Pc5Http *http, Pc5Image *background);
/* The player's own background, from its file. */
int pc5_background_read(const char *path, Pc5Image *background);
/* Where a game's downloaded picture is kept, by its kind: "cover-original.jpg" or ".png". */
const char *pc5_picture_name(const Pc5Buf *original);

/* The player's own cover in dir: a file named one of names (NULL-terminated)
 * with .jpg, .jpeg or .png, matched without regard to case, into out. 0, or -1
 * when there is none. */
int pc5_cover_own(const char *dir, const char *const *names, char *out, size_t size);

/* text with everything but letters, digits and -._~ as %XX. */
int pc5_url_encode(const char *text, char *out, size_t size);

#endif
