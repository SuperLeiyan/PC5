# PC5

<p align="center"><img src="art/icon0.png" width="200" alt="PC5's icon"></p>

A launcher for your own PC games on a PS5 that runs homebrew. PC5 shows your games as a row
of covers, installs games from a USB drive in one copy each, updates them from a newer copy
with their saves kept, and uninstalls them with or without their saves. It runs the games with [prospero-win](https://github.com/mpereiraesaa/prospero-win)
(Wine on the PS5). The app is `PPSA11018`, its tile is called **PC5**. This is version 1.8.7.
Temporary hotfix for 1.8.3 

**1. Open /data/prospero-win/pc5/settings.txt on the console (FTP).**

**2. Change the startup_sound = startup_sound = OFF (add the line if it isn’t there), and save.**

No game is part of PC5, and PC5 downloads none: you install the games you own.

<p align="right">
  <a href="https://www.buymeacoffee.com/bornaradusin">
    <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png"
         alt="Buy Me a Coffee"
         width="140">
  </a>
</p>

## Getting started

1. Download `PPSA11018.zip` from [the releases](https://github.com/bornaradusin/PC5/releases),
   unzip it, copy the `PPSA11018` folder to the console the way you add any homebrew app, and
   start **PC5** from Home.
2. Put each game's installed folder (the one with its `.exe`) at the top of an **exFAT** USB
   drive, or in a `Games` folder there.
3. In PC5 press **Options** › **Game maintenance** › **Install games from USB**, then choose the game and press
   **✕** to play. Hold the **touchpad** and **Options** for two seconds to leave a game.

Each game has a menu (**△**) for its controller mode, frame generation, resolution, aspect
ratio, memory, cover, background, home screen tile, program, updating and uninstalling. Up to four players can play a game
together: each turns on their own controller and signs in with it on the console.

## The wiki

Everything else is in the [wiki](https://github.com/bornaradusin/PC5/wiki):

| | |
| --- | --- |
| [Installing and updating](https://github.com/bornaradusin/PC5/wiki/Installing-and-updating) | putting PC5 on the console, and updating it |
| [Installing games](https://github.com/bornaradusin/PC5/wiki/Installing-games) | USB drives, several games in a row, choosing the right program, launchers |
| [Playing games](https://github.com/bornaradusin/PC5/wiki/Playing-games) | the game's menu: frame generation, resolution, aspect ratio, memory, .NET |
| [Controllers](https://github.com/bornaradusin/PC5/wiki/Controllers) | Xbox controller, DualSense, keyboard & mouse, button mapping, more than one player, typing |
| [Settings](https://github.com/bornaradusin/PC5/wiki/Settings) | the overlay, vibration, light bar, adaptive triggers, updates |
| [Launch options](https://github.com/bornaradusin/PC5/wiki/Launch-options) | a game's command line (`launch.txt`) |
| [Home screen tiles](https://github.com/bornaradusin/PC5/wiki/Home-screen-tiles) | a game on the PS5's home screen |
| [Covers](https://github.com/bornaradusin/PC5/wiki/Covers) | where covers and backgrounds come from, and your own |
| [Files and folders](https://github.com/bornaradusin/PC5/wiki/Files-and-folders) | what PC5 keeps where on the console |
| [Troubleshooting](https://github.com/bornaradusin/PC5/wiki/Troubleshooting) and [FAQ](https://github.com/bornaradusin/PC5/wiki/FAQ) | when something doesn't work |
| [Game compatibility](https://github.com/bornaradusin/PC5/wiki/Game-compatibility) | games players have tried; [add one](https://github.com/bornaradusin/PC5/discussions/new?category=game-compatibility) |

## What runs

PC5 reads each game's program to see whether it is 32- or 64-bit and what it draws with:
Direct3D 8 to 11 run through DXVK (with PC5's patch), Direct3D 12 through VKD3D-Proton,
Vulkan on RADV, OpenGL on Mesa Zink, DirectDraw through Wine's own Direct3D on Mesa Zink,
and plain Windows drawing on Wine itself.
.NET Framework games run with Wine Mono. Games that need a store's launcher or client
running (Steam, EA app, Ubisoft Connect, Rockstar Games Launcher), online DRM, anti-cheat or
kernel drivers don't run. Whether a game runs is only known by trying it: PC5 makes no
promise about any game.

## Reporting a problem

Report problems as [GitHub issues](https://github.com/bornaradusin/PC5/issues), not in
ProsperoStore's comments, with the log attached. PC5 keeps a log of each game's newest session
in `/data/prospero-win/logs/`, as a zip with the game's own logs and crash files from that
session. When a game crashes, PC5 says so the next time it opens; **△** there shows a QR code
that opens GitHub's form for a new issue, already filled in. See
[Reporting a problem](https://github.com/bornaradusin/PC5/wiki/Reporting-a-problem). Ideas for
something PC5 could do go in [Discussions › Ideas](https://github.com/bornaradusin/PC5/discussions/new?category=ideas).

## Building

On **Debian 13 or Ubuntu 24.04** (x86-64), as root:

```sh
apt-get install -y $(cat tools/packages.txt)
tools/build.sh
```

The app is `release/PPSA11018/` and `release/PPSA11018.zip`, with the source of everything in
it as `release/PPSA11018-sources.tar.xz` and Wine Mono's own source archive beside it
(`release/wine-mono-11.3.0-src.tar.xz`); `PC5_RELEASE` names another folder for them.
Everything else stays in `.build/`. The build is in pieces (the script's first lines name
them): each fetches what it is built from, prints a line a minute with what it is doing and
writes `.build/logs/<piece>.log`, and `tools/build.sh <piece>` runs one on its own. A piece
whose key (`tools/build.sh key <piece>`, a fingerprint of all it is built from) hasn't changed
since it was built is skipped. Elsewhere the script runs itself in Docker (`tools/Dockerfile`).
A first build takes a few hours, most of it Wine.

GitHub's build (Actions › Build › Run workflow) runs the same pieces, each on a machine of its
own in the build image, keeps each under its key and builds only those whose key changed, then
makes a draft release to check and publish.

`tests/test.sh` runs the host checks (after `tools/build.sh sources`), `tests/test.sh --shots`
also draws every screen, and `tests/PS5-CHECKLIST.md` is what to try on a console.

**Releasing**: the update check reads the newest release's tag (`v1.9.0`). Raise `PC5_VERSION`
in `title/pc5.h` and `contentVersion` in `sce_sys/param.json`, and attach `PPSA11018.zip`,
`PPSA11018-update.zip`, `PPSA11018-sources.tar.xz` and `wine-mono-11.3.0-src.tar.xz` to the
release (the build's draft has them). PC5's own updater (`title/pc5_upgrade.c`) installs the
release's assets named `PPSA11018-update.zip` (only the files that differ from the newest
published release, `tools/make-update.py`, when the console has the rest) or
`PPSA11018.zip`, checked against the size and SHA-256 GitHub lists for them, so those names
must stay. Two builds of one commit give the same files (`SOURCE_DATE_EPOCH` in
`tools/build.sh`), so the update zip holds only what really changed.

## How it is made

- `title/`: PC5's own code, plain C built into prospero-win's title: the screens
  (`pc5_ui.c`, drawn by `pc5_draw.c`), the library and installs (`pc5_library.c`), USB
  folders, Windows programs, covers, HTTPS, settings, updates, logs and reports, home
  screen tiles, the game's side and the console's.
- `patches/prospero-win/`: PC5's patches to prospero-win v0.2.0 and its Wine, each with
  a message saying what it does.
- `patches/dxvk/` and `patches/radv/`: PC5's patches to DXVK and RADV.
- `tools/`: the build, the Windows template, the font atlas and the app; `art/make-art.py`
  makes the icon and the Home background from the pictures beside it.

## Credits and licence

PC5's own code is free software under the GNU LGPL 2.1 or later (`LICENSE`), like
prospero-win. The app as built holds the PS5 boilerplate's startup code and `libc.prx`
(GPL 3.0 or later), so the app as a whole is under the GPL 3.0 or later, as prospero-win's
own release is; its source is `PPSA11018-sources.tar.xz`, with Wine Mono's in
`wine-mono-11.3.0-src.tar.xz`.

It is built on prospero-win by mpereiraesaa and the projects it uses: Wine, DXVK, Mesa
(RADV, Zink; including mihawk-99's PS5 work), the PS5 payload SDK and boilerplate, and the
Lapy helper. Also Wine patches from PS5_MihawkOS by mihawk-99 (LGPL 2.1 or later: thread
alerts without a descriptor, mains power, a missing program, dnsapi, dxcore; the console's
free memory), a logging patch from NFS Black Box by lavavex (LGPL 2.1 or later), movie and
controller fixes by SuperLeiyan (LGPL 2.1 or later, the DXVK one zlib), FAudio's resampler
fix as Leo Lobato backported it to prospero-win (zlib, as FAudio), Wine Mono
(MIT, its parts under their own licences, `LICENSES/wine-mono/COPYING`), FFmpeg (LGPL 2.1 or
later), ps5-framegen by Swordpdf (GPL 3.0 or later, its shaders from AMD's FSR3 under MIT),
VKD3D-Proton (LGPL 2.1 or later), Mbed TLS (Apache 2.0 or GPL 2.0 or later), Mozilla's root
certificates (MPL 2.0), stb_image and stb_image_write (public domain / MIT), Project
Nayuki's QR Code generator (MIT, `LICENSES/qrcodegen/LICENSE`), the PS5 Forwarder Format by
heydemoura (its tile program and psfwd, GPL 3.0 or later), the Liberation fonts (SIL OFL
1.1, `LICENSES/liberation-fonts/LICENSE`), Droid Sans Fallback (Apache 2.0,
`LICENSES/droid-sans-fallback/NOTICE`) and the Inter font (SIL OFL 1.1, `fonts/OFL.txt`).
Blur5 and sahin were inspiration only; no code was taken from them. See `NOTICE.md`.

PC5 is not made by or connected to Sony, Valve, GOG, GitHub, PCGamingWiki or the Wikimedia Foundation.
