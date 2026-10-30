/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the redistributables a game carries (NVIDIA's PhysX System Software above all), installed
 * into the game's Windows without Windows Installer: the console can't start a setup program,
 * so PC5 reads the Windows Installer package (.msi) itself, as `msiexec /i <package> /quiet`
 * would install it for all users on 64-bit Windows, and copies its files, writes its registry
 * values and puts its folders in PATH. Custom actions, self-registration and per-user folders
 * are left out, and a file already in C:\windows is never replaced (Wine's own DLLs stay). */
#ifndef PC5_MSI_H
#define PC5_MSI_H
#include "pc5.h"

/* What a package installed. */
typedef struct Pc5MsiReport {
    char product[PC5_NAME];   /* its ProductName, or "" */
    int files;                /* written */
    int kept;                 /* left as they were: already in C:\windows */
    int skipped;              /* left out: a per-user folder, a condition, no cabinet for it */
    int registry;             /* registry values written */
    int path;                 /* folders put in PATH */
} Pc5MsiReport;

/* Installs the package in the file package, starting offset bytes into it (a setup program
 * may carry one), into the Windows at prefix_dir (its drive_c, system.reg and user.reg); the
 * folders it puts in PATH are added to prefix_dir/pc5/path.txt. 0, or -1 with why in error. */
int pc5_msi_install(const char *package, uint64_t offset, const char *prefix_dir, Pc5MsiReport *report, char *error,
                    size_t size);
/* The offset of a package a setup program carries as it is (uncompressed), or -1 for none.
 * Reads at most 512 MB of it. */
int64_t pc5_msi_embedded(const char *program);

/* A game's redistributables: the packages in its folder's redistributable folders
 * (_CommonRedist, Redist, PhysX, Support, ... a few folders deep) and at its top, and NVIDIA's
 * PhysX setup programs, which carry one. Those that only put back what Wine has (Visual C++,
 * DirectX, .NET, XNA) are left out. Each is looked at once: root/prefixes/<prefix>/pc5/redist.txt
 * lists what was, with its size and time, and what came of it. */
/* 1 when the game's folder has one not looked at yet, its name in name; 0 when none. */
int pc5_redist_pending(const char *root, const char *prefix, const char *game_dir, char *name, size_t size);
/* Installs those, each line of what happened to log: how many were installed, or -1 when one
 * failed (it is listed, and not tried again). */
int pc5_redist_install(const char *root, const char *prefix, const char *game_dir,
                       void (*log)(void *context, const char *line), void *context);

/* For the tests: a Windows Installer condition, with the properties of 64-bit Windows 7 and
 * those given ("NAME=value" lines): 1 true, 0 false; text that can't be read is true. */
int pc5_msi_condition(const char *condition, const char *properties);

#endif
