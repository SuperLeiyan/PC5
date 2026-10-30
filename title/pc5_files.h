/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's file manager, without its screens: a folder's items, and copying, moving and deleting
 * them a step at a time, as an install copies a game, so the screens keep drawing (pc5_ui.c
 * shows them, from a game's menu for its own folder and from Settings for the whole console).
 *
 * A file is copied to "<name>.pc5part" beside where it goes and renamed into place when it
 * is whole, so a copy stopped part way, or a console turned off, leaves no half file under
 * its own name. A move within one drive is a rename; across drives it copies, then deletes
 * what it copied once every file is in place. */
#ifndef PC5_FILES_H
#define PC5_FILES_H
#include "pc5.h"
#include "pc5_library.h"

enum { PC5_FILES_NAME = 256 };

typedef struct Pc5FileItem {
    char name[PC5_FILES_NAME];
    int dir;                  /* a folder, or a link to one */
    uint64_t size;            /* a file's bytes */
} Pc5FileItem;

typedef struct Pc5FileList {
    Pc5FileItem *items;       /* folders first, then files, each by name, case aside */
    int count;
    int error;                /* errno when the folder couldn't be read, else 0 */
} Pc5FileList;

/* path's items ("." and ".." left out). 0, or -1 with list->error set (an empty list). */
int pc5_files_list(const char *path, Pc5FileList *list);
void pc5_files_list_free(Pc5FileList *list);

/* 1 when path is root itself or inside it, by its parts ("/data/a" is in "/data", not
 * "/data2"); a path with a "." or ".." part is in nothing. */
int pc5_files_within(const char *root, const char *path);

/* 0 when name can be a new or renamed item's name; -1 with why, a sentence, when it can't:
 * empty, "." or "..", over 255 bytes, a '/' or a character exFAT refuses (\ : * ? " < > |
 * and control characters), or ending in a space or a dot. */
int pc5_files_name_ok(const char *name, char *why, size_t size);

/* name, or "name (2).ext", "name (3).ext"... the first that dir doesn't have, into out. */
int pc5_files_free_name(const char *dir, const char *name, char *out, size_t size);

/* How many of sources' names dest has already (what a paste asks about): an item there itself
 * counts only when with_self is set (a copy beside itself; a move of it there is nothing to do). */
int pc5_files_conflicts(const char *const *sources, int count, const char *dest, int with_self);

/* What a delete would take: files and folders in all (the items themselves too), their bytes;
 * partial when it stopped after about budget_ms, the counts then a part. */
typedef struct Pc5FilesSize { uint32_t files, folders; uint64_t bytes; int partial; } Pc5FilesSize;
void pc5_files_measure(const char *const *paths, int count, Pc5FilesSize *size, int budget_ms);

typedef enum Pc5FilesOp { PC5_FILES_COPY, PC5_FILES_MOVE, PC5_FILES_DELETE } Pc5FilesOp;
/* What a paste does with a name dest has already: Replace (a file overwritten, a folder's
 * contents merged into the one there, a file and a folder in each other's place deleted
 * first), Skip (that item left out) or Keep both (the new one named "name (2)"). */
typedef enum Pc5FilesConflict { PC5_FILES_REPLACE, PC5_FILES_SKIP, PC5_FILES_KEEP_BOTH } Pc5FilesConflict;
typedef enum Pc5FilesState { PC5_FILES_WORKING, PC5_FILES_DONE, PC5_FILES_FAILED, PC5_FILES_STOPPED } Pc5FilesState;

typedef struct Pc5FilesTask Pc5FilesTask;

typedef struct Pc5FilesJob {
    Pc5FilesOp op;
    Pc5FilesState state;
    char error[PC5_ERROR];    /* why it failed, a sentence */
    /* Progress: bytes copied of all, items finished of all (folders made, files copied,
     * items moved or deleted). */
    uint64_t done_bytes, total_bytes;
    uint32_t done_items, total_items;
    uint32_t skipped;         /* items left out as Skip asked */
    char current[PC5_FILES_NAME];   /* the item being worked on, for the screen */
    /* the work, made by pc5_files_begin */
    Pc5FilesTask *tasks;
    uint32_t task_count, task_at;
    Pc5Buf names;
    int in, out;              /* the file being copied, and its .pc5part */
    uint64_t copied;
    Pc5Remove remove;
    int removing, stop;
    uint8_t *chunk;
} Pc5FilesJob;

/* Starts op on sources (absolute paths, count of them): into the folder dest for a copy or a
 * move, dest unused for a delete. It plans the work first (it reads the folders), so an error
 * that the work would meet late (a folder into itself, too little room) is said at once.
 * 0, or -1 with job->error; either way pc5_files_end. */
int pc5_files_begin(Pc5FilesJob *job, Pc5FilesOp op, const char *const *sources, int count, const char *dest,
                    Pc5FilesConflict conflict);
/* Works for about budget_ms; 1 while there is more, 0 once job->state says how it ended. */
int pc5_files_step(Pc5FilesJob *job, int budget_ms);
/* Asks it to stop: the file being copied is deleted, what is finished stays, and a move
 * deletes nothing of its sources. */
void pc5_files_stop(Pc5FilesJob *job);
void pc5_files_end(Pc5FilesJob *job);

#endif
