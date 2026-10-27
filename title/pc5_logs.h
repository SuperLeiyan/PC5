/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: keeping prospero-win's logs (<root>/logs) for a report. Each session
 * (PC5's, or a game's) writes session-N.log while it runs, since a crash
 * can't write its log afterwards. When PC5 opens it reads the sessions that
 * have ended. One with a problem is kept as a zip named for when and what
 * (issue-2026-10-22_18-47_grand-theft-auto-iv.zip), with the files the game
 * itself wrote while it ran (its own logs, crash reports and dumps) and a
 * page saying what PC5 saw. A game's newest session is kept the same way
 * (log-...zip) even when nothing went wrong, so a problem without a crash
 * still has its log; older ones of that game, and PC5's own sessions that
 * went well, are deleted. Logs older than seven days go, whatever they hold. */
#ifndef PC5_LOGS_H
#define PC5_LOGS_H
#include "pc5.h"

enum { PC5_LOG_DAYS = 7 };

typedef enum Pc5LogProblem {
    PC5_LOG_FINE,
    PC5_LOG_CRASHED,          /* the process ended without closing its log */
    PC5_LOG_GAME_CRASHED,     /* Wine reported an unhandled exception */
    PC5_LOG_NO_PICTURE,       /* the game closed within a minute, having shown nothing */
    PC5_LOG_FORCED,           /* the game didn't close when asked and was ended */
    PC5_LOG_PC5_PROBLEM,      /* PC5 logged a problem (an install or uninstall that failed) */
} Pc5LogProblem;

typedef struct Pc5LogIssue {
    Pc5LogProblem problem;
    char profile[PC5_ID];     /* the game's id, or "launcher" for PC5 */
    char detail[200];         /* Wine's line about the crash, or PC5's problem */
    char file[PC5_PATH];      /* where the log is kept */
    int64_t time;             /* when the session began (seconds since 1970) */
    int64_t ended;            /* about when its last line was written */
    int closed;               /* it has its end line (it didn't stop half-way) */
    char prefix[PC5_PATH];    /* the game's Wine prefix, as its log names it, or "" */
    char exe[PC5_PATH];       /* the program Wine ran (C:\Games\...), or "" */
    char firmware[32];        /* the console's, as the log names it, or "" */
    int files;                /* the game's own files kept with the log */
} Pc5LogIssue;

/* What a session's log says: the problem, if any. */
Pc5LogProblem pc5_log_classify(const char *text, size_t length, Pc5LogIssue *issue);

/* What PC5 saw, in a few words ("stopped with an error"), for a report. */
const char *pc5_log_problem_text(Pc5LogProblem problem);

/* Tidies root/logs as above, leaving the session now running alone. now is
 * the time in seconds since 1970 and offset_s the console's offset from UTC,
 * for the kept logs' names. Returns how many sessions had a problem; the
 * newest of them is in *newest (when not NULL). */
int pc5_logs_tidy(const char *root, int keep_all, int64_t now, int offset_s, Pc5LogIssue *newest);

/* The newest kept log of a game (issue-... or log-...), its path in out. 0, or -1 when there is none. */
int pc5_logs_newest(const char *root, const char *profile, char *out, size_t size);

/* The console's firmware ("13.60") from the kernel's version text (kern.version, which
 * names the release branch it was built from: "... r229358/releases/13.60: ..."), its major
 * number without a leading zero, as libkernel's number is written. 0, or -1 when the text
 * names none. */
int pc5_firmware_from_kernel(const char *version, char *out, size_t size);

#endif
