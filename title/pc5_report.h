/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: Report on GitHub. A kept log (pc5_logs.h) becomes the link to
 * GitHub's issue form for a game that doesn't run (PC5's own for PC5's
 * session), its title and fields filled in: the game, its program, PC5's
 * version, the console's firmware and what PC5 saw. The form asks the player
 * what happened and for the zip, which the console names beside the code. The
 * console shows it as a QR code for a phone; nothing leaves the console unless
 * the player sends the form and attaches the log. */
#ifndef PC5_REPORT_H
#define PC5_REPORT_H
#include "pc5.h"
#include "pc5_logs.h"

#define PC5_ISSUES_NEW "https://github.com/bornaradusin/PC5/issues/new"

enum { PC5_REPORT_URL = 2048 };

/* The link for this issue; game is its name in the library (or NULL). 0, or -1 when it doesn't fit. */
int pc5_report_url(const Pc5LogIssue *issue, const char *game, char *out, size_t size);

/* The kept log's file name alone (issue-...zip). */
const char *pc5_report_file(const Pc5LogIssue *issue);

#endif
