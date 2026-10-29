/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: what a Windows program says about itself, read from its PE file:
 * 32- or 64-bit, the graphics libraries it imports, its product name and its
 * icon. Only the parts needed are read; nothing is run. */
#ifndef PC5_PE_H
#define PC5_PE_H
#include "pc5.h"

/* Libraries a game imports (directly or delay-loaded) that say how it draws. */
enum {
    PC5_IMPORT_D3D8 = 1u << 0,
    PC5_IMPORT_D3D9 = 1u << 1,
    PC5_IMPORT_D3D10 = 1u << 2,    /* d3d10, d3d10_1, d3d10core */
    PC5_IMPORT_D3D11 = 1u << 3,
    PC5_IMPORT_DXGI = 1u << 4,
    PC5_IMPORT_D3D12 = 1u << 5,
    PC5_IMPORT_OPENGL = 1u << 6,   /* opengl32 */
    PC5_IMPORT_VULKAN = 1u << 7,   /* vulkan-1 */
    PC5_IMPORT_DDRAW = 1u << 8,
    PC5_IMPORT_DOTNET = 1u << 9,   /* mscoree: a .NET Framework program */
    /* the libraries a program draws with */
    PC5_IMPORT_DRAWING = PC5_IMPORT_D3D8 | PC5_IMPORT_D3D9 | PC5_IMPORT_D3D10 | PC5_IMPORT_D3D11 | PC5_IMPORT_DXGI |
                         PC5_IMPORT_D3D12 | PC5_IMPORT_OPENGL | PC5_IMPORT_VULKAN | PC5_IMPORT_DDRAW,
};

typedef enum Pc5Api {
    PC5_API_GDI,        /* none of the above: Windows' own drawing */
    PC5_API_DDRAW,
    PC5_API_OPENGL,
    PC5_API_VULKAN,
    PC5_API_D3D8,
    PC5_API_D3D9,
    PC5_API_D3D10,
    PC5_API_D3D11,
    PC5_API_D3D12,
} Pc5Api;

typedef struct Pc5PeInfo {
    int bits;              /* 32 or 64 */
    int gui;               /* a window program, not a console one */
    int dll;               /* a library, not a program */
    uint32_t imports;      /* PC5_IMPORT_* */
    char product[PC5_NAME];      /* ProductName, UTF-8, or "" */
    char description[PC5_NAME];  /* FileDescription, UTF-8, or "" */
} Pc5PeInfo;

/* 0 for a Windows x86 or x86-64 program or library, -1 otherwise (not PE,
 * another CPU, unreadable). */
int pc5_pe_read(const char *path, Pc5PeInfo *info);
/* The libraries a program draws with that it names in its own data, as PC5_IMPORT_* bits:
 * a program that loads Direct3D or Vulkan itself at run time (LoadLibrary) imports none
 * of them. Names in ASCII or UTF-16, any case; reads at most 1 GiB of the file. */
uint32_t pc5_pe_named_libraries(const char *path);
/* The most capable API among the imports. */
Pc5Api pc5_api_from_imports(uint32_t imports);
/* 1 when the program at path (a path on the disk, or relative, either slash) is an Unreal
 * Engine game's, in a Binaries\Win64 folder, whose imports don't name Direct3D 12 but whose
 * code does (d3d12.dll): Unreal Engine 5 loads d3d12.dll itself and draws with it, while
 * its imports name only Direct3D 11 or DXGI. imports: the program's own. */
int pc5_pe_unreal_d3d12(const char *path, uint32_t imports);
/* "DirectX 11", "OpenGL", ... */
const char *pc5_api_name(Pc5Api api);
/* The program's icon, the largest it has, as pixels; -1 if it has none. */
int pc5_pe_icon(const char *path, Pc5Image *icon);

#endif
