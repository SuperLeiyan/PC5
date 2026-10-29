/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a home screen tile that is an app of its own (pc5_tile.h). */
#include "pc5_tile.h"
#include "pc5_json.h"

#include <string.h>

enum { STAGE_RECOVER, STAGE_FIND, STAGE_RUNTIME, STAGE_PROFILE, STAGE_READY };

/* A profile id as prospero-win takes one: a lower-case letter or digit, then
 * those, '-' and '_'. */
static int valid_id(const char *id)
{
    if (!id[0] || !((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= '0' && id[0] <= '9'))) return 0;
    for (const char *c = id + 1; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_')) return 0;
    return 1;
}

int pc5_tile_read(const char *app, char game[PC5_ID], char *error, size_t size)
{
    char path[PC5_PATH], format[32];
    Pc5Buf text = { 0 };
    Pc5Json json = { 0 };
    uint32_t value;
    int status = -1;

    game[0] = 0;
    if (error && size) error[0] = 0;
    if (!app || pc5_format(path, sizeof(path), "%s/" PC5_TILE_FILE, app)) {
        snprintf(error, size, "The tile's folder path is too long.");
        return -1;
    }
    if (!pc5_exists(path)) return 0;
    if (pc5_read_file(path, &text, 64 * 1024) || pc5_json_parse(&json, (const char *)text.data, text.len, 256)) {
        snprintf(error, size, "The tile's " PC5_TILE_FILE " can't be read.");
        goto done;
    }
    value = pc5_json_get(&json, 0, "format");
    if (!value || json.tokens[value].type != PC5_JSON_STRING || pc5_json_text(&json, value, format, sizeof(format)) ||
        strcmp(format, PC5_TILE_FORMAT)) {
        snprintf(error, size, "The tile was made by a PC5 this one doesn't know: make it again.");
        goto done;
    }
    value = pc5_json_get(&json, 0, "game");
    if (!value || json.tokens[value].type != PC5_JSON_STRING || pc5_json_text(&json, value, game, PC5_ID) ||
        !valid_id(game)) {
        game[0] = 0;
        snprintf(error, size, "The tile doesn't say which game it starts: make it again.");
        goto done;
    }
    status = 1;
done:
    pc5_json_free(&json);
    pc5_buf_free(&text);
    return status;
}

void pc5_tile_prepare_begin(Pc5TilePrep *prep, const char *root, const char *app, const char *id)
{
    memset(prep, 0, sizeof(*prep));
    pc5_copy(prep->root, sizeof(prep->root), root ? root : "");
    pc5_copy(prep->app, sizeof(prep->app), app ? app : "");
    pc5_copy(prep->id, sizeof(prep->id), id ? id : "");
    pc5_copy(prep->title, sizeof(prep->title), prep->id);
    prep->stage = STAGE_RECOVER;
}

static int fail(Pc5TilePrep *prep, const char *text)
{
    pc5_copy(prep->error, sizeof(prep->error), text);
    return -1;
}

/* The game found, and whether it can start; what it needs before it does. */
static int find(Pc5TilePrep *prep)
{
    Pc5Library *library = pc5_alloc(sizeof(*library));
    char program[PC5_PATH];
    int index, begun;

    if (!library) return fail(prep, "PC5 ran out of memory.");
    pc5_library_load(prep->root, library);
    index = pc5_library_find(library, prep->id);
    if (index < 0) {
        pc5_free(library);
        return fail(prep, "It isn't in PC5 any more. Take the tile off the home screen, or install the game again.");
    }
    pc5_copy(prep->title, sizeof(prep->title), library->games[index].title);
    prep->found = 1;
    prep->pc5 = library->games[index].pc5;
    if (!library->games[index].playable) {
        fail(prep, library->games[index].problem);
        pc5_free(library);
        return -1;
    }
    pc5_free(library);
    /* A .NET game started the first time: Wine Mono's runtime is copied to the console first. */
    prep->stage = STAGE_PROFILE;
    if (prep->pc5 && prep->app[0] && !pc5_game_program(prep->root, prep->id, program, sizeof(program))) {
        begun = pc5_runtime_begin(&prep->runtime, prep->root, prep->app, program);
        if (begun < 0) return fail(prep, prep->runtime.error);
        if (begun > 0) {
            prep->copying = 1;
            prep->stage = STAGE_RUNTIME;
        }
    }
    return 0;
}

int pc5_tile_prepare_step(Pc5TilePrep *prep, int budget_ms)
{
    uint64_t until = pc5_now_ms() + (uint64_t)(budget_ms > 0 ? budget_ms : 1);
    int step;

    if (prep->error[0]) return -1;
    while (prep->stage != STAGE_READY) {
        switch (prep->stage) {
        case STAGE_RECOVER:
            /* What a stopped install, update or uninstall left, as PC5 does when it opens. */
            if (pc5_recover_step(prep->root, &prep->remove, budget_ms)) return 0;
            pc5_remove_end(&prep->remove);
            prep->stage = STAGE_FIND;
            break;
        case STAGE_FIND:
            if (find(prep)) return -1;
            break;
        case STAGE_RUNTIME:
            step = pc5_runtime_step(&prep->runtime, budget_ms);
            if (step < 0) return fail(prep, prep->runtime.error);
            if (!step) return 0;
            pc5_runtime_end(&prep->runtime);
            prep->copying = 0;
            prep->stage = STAGE_PROFILE;
            break;
        case STAGE_PROFILE: {
            char error[PC5_ERROR] = "", path[PC5_PATH];
            /* Its profile from PC5's notes and its settings, as every start of a PC5 game. */
            if (prep->pc5 && pc5_profile_refresh(prep->root, prep->id, error, sizeof(error))) return fail(prep, error);
            /* The game PC5's library shows first next time. */
            if (!pc5_format(path, sizeof(path), "%s/pc5", prep->root) && !pc5_mkdirs(path) &&
                !pc5_format(path, sizeof(path), "%s/pc5/last.txt", prep->root))
                pc5_write_file(path, prep->id, strlen(prep->id));
            prep->stage = STAGE_READY;
            break;
        }
        default:
            return fail(prep, "PC5 lost its place readying the game.");
        }
        if (prep->stage != STAGE_READY && pc5_now_ms() >= until) return 0;
    }
    return 1;
}

void pc5_tile_prepare_end(Pc5TilePrep *prep)
{
    pc5_remove_end(&prep->remove);
    pc5_runtime_end(&prep->runtime);
}
