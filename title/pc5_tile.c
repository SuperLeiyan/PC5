/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a home screen tile that is an app of its own (pc5_tile.h). */
#include "pc5_tile.h"
#include "pc5_home.h"
#include "pc5_json.h"
#include "third_party/psfwd/psfwd.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

/* An absolute path with no "." or ".." part. */
static int absolute_path(const char *path)
{
    if (path[0] != '/') return 0;
    for (const char *part = path; part; part = strchr(part + 1, '/')) {
        const char *name = part + 1, *end = strchr(name, '/');
        size_t n = end ? (size_t)(end - name) : strlen(name);
        if ((n == 1 && name[0] == '.') || (n == 2 && name[0] == '.' && name[1] == '.')) return 0;
    }
    return 1;
}

int pc5_tile_read_all(const char *app, char game[PC5_ID], char *runtime, size_t runtime_size, char *error, size_t size)
{
    char path[PC5_PATH], format[32];
    Pc5Buf text = { 0 };
    Pc5Json json = { 0 };
    uint32_t value;
    int status = -1;

    game[0] = 0;
    if (runtime && runtime_size) runtime[0] = 0;
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
    /* Where its Wine and PC5's files are: PC5's folder. None: they are in the tile. */
    value = pc5_json_get(&json, 0, "runtime");
    if (value && runtime && runtime_size &&
        (json.tokens[value].type != PC5_JSON_STRING || pc5_json_text(&json, value, runtime, runtime_size) ||
         !absolute_path(runtime))) {
        game[0] = runtime[0] = 0;
        snprintf(error, size, "The tile doesn't say where PC5 is: make it again.");
        goto done;
    }
    status = 1;
done:
    pc5_json_free(&json);
    pc5_buf_free(&text);
    return status;
}

int pc5_tile_read(const char *app, char game[PC5_ID], char *error, size_t size)
{
    char runtime[PC5_PATH];
    return pc5_tile_read_all(app, game, runtime, sizeof(runtime), error, size);
}

/* ---- making a tile ------------------------------------------------------------------ */

static int tile_id(const char *id)
{
    if (strlen(id) != 9 || memcmp(id, "PPSA", 4)) return 0;
    for (int i = 4; i < 9; i++)
        if (id[i] < '0' || id[i] > '9') return 0;
    return 1;
}

int pc5_tile_lapy_patch(uint8_t *data, size_t size, const char *title_id, char *error, size_t error_size)
{
    static const char old[] = PC5_TITLE_ID;
    size_t at[8], length[8];
    int sites = 0, compares = 0, strings = 0;

    _Static_assert(sizeof(old) == 10, "a title ID is nine characters");
    if (!title_id || !tile_id(title_id)) {
        snprintf(error, error_size, "A tile's ID is PPSA and five digits.");
        return -1;
    }
    for (size_t i = 0; i + 10 <= size; i++) {
        /* movabs $"PPSA1101", %reg, then xor $'8', %reg on the ninth character. */
        if (data[i] == 0x48 && data[i + 1] >= 0xb8 && data[i + 1] <= 0xbf && !memcmp(data + i + 2, old, 8)) {
            size_t imm = i + 2, j;
            for (j = imm + 8; j + 3 < size && j < imm + 48; j++)
                if (data[j] == 0x48 && data[j + 1] == 0x83) break;
            if (j + 3 >= size || j >= imm + 48 || (data[j + 2] & 0xf8) != 0xf0 || data[j + 3] != (uint8_t)old[8] ||
                sites + 2 > 8)
                goto unknown;
            at[sites] = imm, length[sites++] = 8;
            at[sites] = j + 3, length[sites++] = 1;
            compares++;
        } else if (!memcmp(data + i, old, 9) && data[i + 9] == 0 && sites < 8) {
            at[sites] = i, length[sites++] = 9;
            strings++;
        }
    }
    if (compares != 2 || strings != 1) goto unknown;
    for (int s = 0; s < sites; s++) memcpy(data + at[s], title_id + (length[s] == 1 ? 8 : 0), length[s]);
    return 0;
unknown:
    snprintf(error, error_size, "PC5's lapy.elf isn't the one this PC5 can make tiles from (%d compares, %d names).",
             compares, strings);
    return -1;
}

/* A string token's text replaced: json's token, its span in text (quotes left). */
typedef struct Span {
    uint32_t start, end;
    const char *with;
    int escape;
} Span;

static int by_start(const void *a, const void *b)
{
    const Span *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start;
}

/* text with each span's text replaced (JSON-escaped when asked). */
static int splice(const char *text, size_t length, Span *spans, int count, Pc5Buf *out)
{
    size_t from = 0;

    qsort(spans, (size_t)count, sizeof(*spans), by_start);
    *out = (Pc5Buf){ 0 };
    for (int s = 0; s < count; s++) {
        if (spans[s].start < from || spans[s].end > length || pc5_buf_put(out, text + from, spans[s].start - from))
            goto fail;
        for (const unsigned char *c = (const unsigned char *)spans[s].with; *c; c++) {
            char escaped[8];
            int n;
            if (spans[s].escape && (*c == '"' || *c == '\\')) n = snprintf(escaped, sizeof(escaped), "\\%c", *c);
            else if (spans[s].escape && *c < 0x20) n = snprintf(escaped, sizeof(escaped), "\\u%04x", *c);
            else escaped[0] = (char)*c, n = 1;
            if (pc5_buf_put(out, escaped, (size_t)n)) goto fail;
        }
        from = spans[s].end;
    }
    if (pc5_buf_put(out, text + from, length - from) || pc5_buf_terminate(out)) goto fail;
    return 0;
fail:
    pc5_buf_free(out);
    return -1;
}

int pc5_tile_param(const char *text, size_t length, const char *title_id, const char *name, Pc5Buf *out)
{
    Pc5Json json = { 0 };
    char content[48];
    Span spans[4];
    uint32_t tokens[4], strings;
    int status = -1;

    *out = (Pc5Buf){ 0 };
    if (!tile_id(title_id) || !name || !name[0] || pc5_json_parse(&json, text, length, 1024)) goto done;
    strings = pc5_json_get(&json, 0, "localizedParameters");
    tokens[0] = pc5_json_get(&json, 0, "titleId");
    tokens[1] = pc5_json_get(&json, 0, "conceptId");
    tokens[2] = pc5_json_get(&json, 0, "contentId");
    tokens[3] = strings ? pc5_json_get(&json, pc5_json_get(&json, strings, "en-US"), "titleName") : 0;
    /* The content ID's label is the tile's own, sixteen characters as the console wants. */
    snprintf(content, sizeof(content), "UP0000-%s_00-PC5TILE000000000", title_id);
    const char *with[4] = { title_id, title_id + 4, content, name };
    for (int i = 0; i < 4; i++) {
        if (!tokens[i] || json.tokens[tokens[i]].type != PC5_JSON_STRING) goto done;
        spans[i] = (Span){ json.tokens[tokens[i]].start, json.tokens[tokens[i]].end, with[i], i == 3 };
    }
    status = splice(text, length, spans, 4, out);
done:
    pc5_json_free(&json);
    return status;
}

/* ---- the tile's folder: what the console starts, PC5's folder for the rest ---------- */

static void delete_tree(const char *path)
{
    Pc5Remove remove = { 0 };
    if (!pc5_exists(path) || pc5_tree_delete_begin(&remove, path)) return;
    while (pc5_remove_step(&remove, 50)) {}
    pc5_remove_end(&remove);
}

/* from's file copied to to, runnable (an eboot or a module). */
static int copy_program(const char *from, const char *to)
{
    Pc5Buf data = { 0 };
    int status = pc5_read_file(from, &data, 64u << 20) || pc5_write_file(to, data.data, data.len) || chmod(to, 0755);
    pc5_buf_free(&data);
    return status ? -1 : 0;
}

/* The same file: the same size and bytes. */
static int same_file(const char *a, const char *b)
{
    Pc5Buf x = { 0 }, y = { 0 };
    int same = !pc5_read_file(a, &x, 64u << 20) && !pc5_read_file(b, &y, 64u << 20) && x.len == y.len &&
               !memcmp(x.data, y.data, x.len);
    pc5_buf_free(&x);
    pc5_buf_free(&y);
    return same;
}

/* The program the console starts, its runtime, and Lapy's helper for the tile's ID, from
 * PC5's folder into dir; then pc5-tile.json naming the game and PC5's folder. */
static int program_files(const char *pc5, const char *dir, const char *title_id, const char *game, char *error,
                         size_t size)
{
    static const char *const programs[] = { "eboot.bin", "sce_module/libc.prx" };
    char from[PC5_PATH], to[PC5_PATH];
    Pc5Buf data = { 0 };
    int status = -1;

    if (pc5_format(to, sizeof(to), "%s/sce_module", dir) || pc5_mkdirs(to)) {
        snprintf(error, size, "PC5 can't make the tile's folders.");
        return -1;
    }
    for (size_t i = 0; i < sizeof(programs) / sizeof(programs[0]); i++)
        if (pc5_format(from, sizeof(from), "%s/%s", pc5, programs[i]) ||
            pc5_format(to, sizeof(to), "%s/%s", dir, programs[i]) || copy_program(from, to)) {
            snprintf(error, size, "PC5 can't copy its %s into the tile.", programs[i]);
            return -1;
        }
    /* lapy.elf: a copy, with the tile's ID in place of PC5's. */
    if (pc5_format(from, sizeof(from), "%s/lapy.elf", pc5) || pc5_read_file(from, &data, 16u << 20)) {
        snprintf(error, size, "PC5's folder has no lapy.elf.");
        goto done;
    }
    if (pc5_tile_lapy_patch(data.data, data.len, title_id, error, size)) goto done;
    if (pc5_format(to, sizeof(to), "%s/lapy.elf", dir) || pc5_write_file(to, data.data, data.len) || chmod(to, 0755)) {
        snprintf(error, size, "PC5 can't write the tile's lapy.elf.");
        goto done;
    }
    pc5_buf_free(&data);
    /* PC5's folder as the tile's runtime: a path in JSON, its quotes and backslashes escaped. */
    {
        Pc5Buf text = { 0 };
        int failed = pc5_buf_printf(&text, "{\n  \"format\": \"" PC5_TILE_FORMAT "\",\n  \"game\": \"%s\",\n  \"runtime\": \"",
                                    game);
        for (const char *c = pc5; !failed && *c; c++)
            failed = *c == '"' || *c == '\\' ? pc5_buf_printf(&text, "\\%c", *c) : pc5_buf_put(&text, c, 1);
        if (!failed) failed = pc5_buf_printf(&text, "\",\n  \"pc5\": \"" PC5_VERSION "\"\n}\n");
        if (failed || pc5_format(to, sizeof(to), "%s/" PC5_TILE_FILE, dir) || pc5_write_file(to, text.data, text.len)) {
            pc5_buf_free(&text);
            snprintf(error, size, "PC5 can't write the tile's " PC5_TILE_FILE ".");
            goto done;
        }
        pc5_buf_free(&text);
    }
    status = 0;
done:
    pc5_buf_free(&data);
    return status;
}

/* The tile's sce_sys: PC5's param.json as the tile's, and the game's picture. */
static int tile_sce_sys(const Pc5TileMake *make, const char *dir, const char *title_id, char *error, size_t size)
{
    char from[PC5_PATH], to[PC5_PATH];
    Pc5Buf data = { 0 }, text = { 0 };
    int status = -1;

    if (pc5_format(to, sizeof(to), "%s/sce_sys", dir) || pc5_mkdirs(to)) {
        snprintf(error, size, "PC5 can't make the tile's sce_sys.");
        return -1;
    }
    if (pc5_format(from, sizeof(from), "%s/sce_sys/param.json", make->pc5) || pc5_read_file(from, &data, 256 * 1024) ||
        pc5_tile_param((const char *)data.data, data.len, title_id, make->name && make->name[0] ? make->name : make->id,
                       &text) ||
        pc5_format(to, sizeof(to), "%s/sce_sys/param.json", dir) || pc5_write_file(to, text.data, text.len)) {
        snprintf(error, size, "PC5 can't write the tile's param.json from its own.");
        goto done;
    }
    pc5_buf_free(&data);
    if (pc5_home_picture(make->library_root, make->prefix ? make->prefix : make->id, &data) ||
        pc5_format(to, sizeof(to), "%s/sce_sys/icon0.png", dir) || pc5_write_file(to, data.data, data.len)) {
        snprintf(error, size, "PC5 can't make the tile's picture.");
        goto done;
    }
    status = 0;
done:
    pc5_buf_free(&data);
    pc5_buf_free(&text);
    return status;
}

int pc5_tile_make(const Pc5TileMake *make, char out[16], char *error, size_t size)
{
    char stage[PC5_PATH], final[PC5_PATH];

    out[0] = 0;
    error[0] = 0;
    if (!make->tiles || !make->pc5 || !make->library_root || !make->id || !valid_id(make->id) ||
        !absolute_path(make->pc5)) {
        snprintf(error, size, "PC5 doesn't know where to make the tile.");
        return PC5_TILE_FAILED;
    }
    if (!pc5_home_find(make->tiles, make->id, out) && (pc5_mkdirs(make->tiles) || !psfwd_new_title_id(make->tiles, out))) {
        snprintf(error, size, "PC5 can't use %s: %s.", make->tiles, strerror(errno));
        out[0] = 0;
        return PC5_TILE_FAILED;
    }
    if (pc5_format(stage, sizeof(stage), "%s/.staging-%s", make->tiles, out) ||
        pc5_format(final, sizeof(final), "%s/%s", make->tiles, out)) {
        snprintf(error, size, "The tiles' folder path is too long.");
        out[0] = 0;
        return PC5_TILE_FAILED;
    }
    delete_tree(stage);
    if (pc5_mkdirs(stage) || program_files(make->pc5, stage, out, make->id, error, size) ||
        tile_sce_sys(make, stage, out, error, size)) {
        if (!error[0]) snprintf(error, size, "PC5 can't make %s: %s.", stage, strerror(errno));
        delete_tree(stage);
        out[0] = 0;
        return PC5_TILE_FAILED;
    }
    /* The old tile goes only once the new one is whole. */
    delete_tree(final);
    if (rename(stage, final)) {
        snprintf(error, size, "PC5 can't put the tile in place: %s.", strerror(errno));
        delete_tree(stage);
        out[0] = 0;
        return PC5_TILE_FAILED;
    }
    return PC5_TILE_MADE;
}

int pc5_tile_refresh(const char *tiles, const char *pc5, void (*log)(void *context, const char *line), void *context)
{
    char line[PC5_PATH + 200];
    DIR *dir;
    struct dirent *entry;
    int refreshed = 0;

    if (!tiles || !pc5 || !absolute_path(pc5) || !(dir = opendir(tiles))) return 0;
    while ((entry = readdir(dir))) {
        char folder[PC5_PATH], game[PC5_ID], runtime[PC5_PATH], error[PC5_ERROR], ours[PC5_PATH], theirs[PC5_PATH];
        if (!tile_id(entry->d_name) || pc5_join(folder, sizeof(folder), tiles, entry->d_name) ||
            pc5_tile_read_all(folder, game, runtime, sizeof(runtime), error, sizeof(error)) != 1 || !runtime[0])
            continue;
        /* Already this PC5's program, with this folder as its runtime. */
        if (!strcmp(runtime, pc5) && !pc5_format(ours, sizeof(ours), "%s/eboot.bin", pc5) &&
            !pc5_format(theirs, sizeof(theirs), "%s/eboot.bin", folder) && same_file(ours, theirs))
            continue;
        int status = program_files(pc5, folder, entry->d_name, game, error, sizeof(error));
        snprintf(line, sizeof(line), "PC5 tile refreshed %s game=%s runtime=%s%s%s", entry->d_name, game, pc5,
                 status ? " failed: " : "", status ? error : "");
        if (log) log(context, line);
        if (!status) refreshed++;
    }
    closedir(dir);
    return refreshed;
}

/* ---- readying a tile's game ------------------------------------------------------- */

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
