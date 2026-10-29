# Plan: home screen tiles that run their game themselves

Idea: [discussion #159](https://github.com/bornaradusin/PC5/discussions/159).
Based on PC5 1.9.6 (bornaradusin/PC5 `main` at `753a51f`).

## The idea

When PC5 makes a home screen tile for a game, the tile becomes a full app of its own. It is
a copy of PC5's runtime under the tile's own app ID, and it starts that one game. Starting
the tile goes straight into the game, as a PS4 or PS5 game starts from Home. PC5 stays the
place where games are installed and configured. The tiles only launch.

Today a tile is a PS5 Forwarder Format forwarder. It asks a launcher payload to start PC5
(`PPSA11018`) with `--rom <game> --exit-after-game`. PC5 then shows its screens, prepares
the game and restarts itself into it.

## Decisions

| Question | Decision |
|---|---|
| Where do the game, its prefix, its saves and its settings live? | Where they are now, in `/data/prospero-win/` (`prefixes/<game>/`, `profiles/`, `pc5/settings.txt`). Nothing about the game moves into the tile. |
| What does the tile folder hold? | A copy of PC5's installed app folder (eboot, Wine, the Windows template, Mono, fonts), its own `sce_sys/`, a patched `lapy.elf` and `pc5-tile.json`. It never writes to its own folder. |
| How does the tile get `/data`? | Through its own copy of Lapy's one-shot helper, patched on the console to the tile's app ID. |
| Does PC5 need a new build per tile, or a list of IDs? | No. One build serves any tile ID; PC5 patches the ID in when it makes the tile. |
| How many app starts and Lapy runs per launch? | One, except for games that start through their own launcher program, which need one restart. |
| Old forwarder tiles? | They are still recognised during the change, and removed once the new tiles are proven on a console. |

## How a tile works today

1. `pc5_home_add` (`title/pc5_home.c`) writes `/data/homebrew/PPSA99xxx/`. The folder holds:
   - the format's prebuilt forwarder `eboot.bin` and `sce_module/libc.prx`, copied from `pc5/forwarder/`;
   - `forwarder.json`, with `target: PPSA11018` and `--rom <game> --exit-after-game`;
   - `sce_sys/param.json` and `icon0.png`.
2. The player quits PC5. ShadowMountPlus scans `/data/homebrew` and registers the folder as a tile.
3. Starting the tile runs the forwarder. It asks the launcher payload on `127.0.0.1:10199` to start PPSA11018. It first loads the payload through elfldr on port 9021 if the payload isn't running (`psfwd_launch`).
4. PC5 starts in its screens mode, which is one Lapy run:
   - `--rom` only sets `launch.start` (patch `0012`), so PC5 runs its full startup;
   - the first `S_START` frame finds the game, and `S_LAUNCH` prepares it;
   - `play()` restarts the title with `profile=<game> exit=1`.
5. PC5 starts again in game mode, which is the second Lapy run, and runs Wine.
6. When the game ends, `exit_after` closes the title to the home screen. Since 1.9.6 this goes through `close_title_to_home` (patch `0053`), which calls `sceSystemServiceKillApp` on the title's own app id instead of `exit()`.

## How a tile will work

### Making a tile (PC5, △ › Appearance › Home screen tile)

1. **Pick a free ID** in `PPSA99200`–`PPSA99899`, for example `PPSA99437` (`psfwd_new_title_id`, unchanged).
2. **Find PC5's installed folder** on the console's disks (`/data/homebrew/PPSA11018`, or a USB drive), as the updater does (`pc5_upgrade_find`). Refuse with a clear message if there are two copies.
3. **Copy it into `/data/homebrew/.staging-PPSA99437/`.** The staging name means ShadowMountPlus never sees a half-made tile.
   - The copy runs file by file, with a progress screen. It is about 1.1 GB: Wine 437 MB, the Windows template 363 MB, Mono 219 MB, Mesa Zink 41 MB.
   - Skip `pc5/forwarder/`, `pc5/sounds/` and anything else only PC5's screens use.
   - Mark `eboot.bin` and every `.prx` file executable (755), or the console refuses them.
4. **Patch the copy of `lapy.elf`** to `PPSA99437`. See "The Lapy patch".
5. **Write the tile's identity:**
   - `sce_sys/param.json`: PC5's own, with `titleId: PPSA99437`, `conceptId: 99437`, `contentId: UP0000-PPSA99437_00-PC5TILE000000000` and `titleName` set to the game's name.
   - `sce_sys/icon0.png`: from the game's cover (`pc5_home_icon`, unchanged).
   - `pic0.dds` and `pic1.dds`: from the game's background, when it has one. Otherwise PC5's own.
   - `pc5-tile.json`: `{"format": "pc5-tile/1", "game": "<game id>", "pc5": "1.9.6"}`.
6. **Publish it** by renaming the staging folder to `/data/homebrew/PPSA99437/`.
7. **Tell the player** to quit PC5 so that ShadowMountPlus registers the tile, as now.

### Starting the game from the tile

1. The console starts app `PPSA99437`, with the tile's folder as its `/app0`.
2. **The eboot learns its own ID** from `/app0/sce_sys/param.json`, before `/data` is granted, while `/app0` is still visible. If that file can't be read, it uses the built-in `PPSA11018`. Every path built from the ID uses this value, including the sandbox runtime path `/mnt/sandbox/<ID>_00N/app0` (`find_sandbox_app0`). The log says which: `PW_WINE64 title id=PPSA99437 from=param.json built=PPSA11018`. *(prospero-win patch `0054`, done.)*
3. **One Lapy run.** The eboot sends `/app0/lapy.elf`, the tile's patched copy, to elfldr on 9021. The helper checks the app ID (`PPSA99437`) and the process ID, grants `/data` and exits. *(Unchanged code.)*
4. **It opens `/data/prospero-win`**, finds its runtime and loads the game list. *(Unchanged.)*
5. **Tile mode.** `/app0/pc5-tile.json` exists, so the eboot:
   - takes the game id from it;
   - switches to game mode for that game, with `exit_after = 1`;
   - shows PC5's loading screen (`pc5_game_loading`);
   - runs the preparation (`pc5_tile_prepare`, below).

   It doesn't open PC5's screens. *(New.)*
6. **It carries straight on into Wine in the same process.** `main()` already does this after its screens branch in game mode, so there is no restart and no second Lapy run.
   - `pc5_game_begin` repairs the prefix from the tile's own `pc5/prefix` and sets the game's options.
   - Wine loads from the tile's `win/`.
   - The game reads and writes `/data/prospero-win/prefixes/<game>/`.
7. **While it runs**, the console shows the game's tile name and icon, not PC5's.

If the game is missing, or preparing it fails, the tile shows the reason on PC5's loading screen and closes to Home. It never opens PC5's screens.

### Closing

When the game quits, or the player holds touchpad + Options, `exit_after` sends the title to
`close_title_to_home` (patch `0053`). That function ends the title by its own app id, so it
works for any ID unchanged.

A game whose own launcher hands off to another program (patch `0029`) restarts the title. It
restarts `/app0/eboot.bin`, which is the tile's own, so it stays inside the tile. That costs
one extra Lapy run, as it does in PC5 today.

### Changing a game's settings in PC5

Controller mode, frame generation, resolution, memory and the global settings are read from
`/data/prospero-win` at every start. The tile sees a change at its next start, with nothing to
copy. PC5 rewrites the tile's `sce_sys` only when the game's name, cover or background changes.

### When PC5 updates

When PC5 starts, beside `pc5_upgrade_tidy`, it reads every `/data/homebrew/*/pc5-tile.json`.
For each tile made by another PC5 version, it re-copies the runtime and re-patches `lapy.elf`,
staged and renamed as when making a tile. It keeps the tile's `sce_sys/` and `pc5-tile.json`
and rewrites only the version. A tile that finds no PC5 runtime it can use shows a message and
closes; it doesn't crash.

### Removing a tile

Turning the tile off, or uninstalling the game, deletes `/data/homebrew/PPSA99437/`. The game's
data in `/data/prospero-win` stays, unless the game is uninstalled. The player may still need
to delete the tile from the home screen, as now.

## The Lapy patch

PC5 ships Lapy's one-shot helper (`lapy.elf`, MIT licence), built on a PC for PPSA11018. The
helper grants `/data` only to the app ID built into it (`helper_target_title_matches`:
`sceKernelGetAppInfo(pid)` must return that ID). It is sent to elfldr at each start, handles
one request and exits, so tiles never leave a copy running.

The console has no compiler, but the ID is plain bytes in the file. In the 1.9.5 release's
`lapy.elf` (155,032 bytes; 1.9.6 pins the same Lapy commit), the ID sits in five places. The
compiler split each of the two checks into an 8-byte constant and a 1-byte compare:

| File offset | Bytes | Holds | Code |
|---|---|---|---|
| `0x727c` | 8 | `PPSA1101` | `run_helper_request`, `movabs` |
| `0x729a` | 1 | `8` | `run_helper_request`, `xor $imm8` |
| `0x7772` | 8 | `PPSA1101` | `helper_target_title_matches`, `movabs` |
| `0x7787` | 1 | `8` | `helper_target_title_matches`, `xor $imm8` |
| `0x192c8` | 9 | `PPSA11018` | `.rodata`, its log line |

Writing `PPSA99437` there on a copy changed 15 bytes. No `PPSA11018` was left, the
disassembly compared against `PPSA99437`, and the file was still a valid ELF. Every tile ID is
`PPSA` and five digits, the same length, so the patch always fits.

The list is fixed per Lapy build. It doesn't grow with games or tiles.

- **At build time** (`tools/build.sh`, piece `app`), a new `tools/lapy-title.py`:
  - finds the places in `lapy.elf`: the `movabs` immediates holding the first 8 characters, the `xor $imm8` holding the 9th, and the plain string;
  - patches a test ID onto a copy and checks the result;
  - writes `pc5/lapy-title.txt` (the offsets, lengths and the SHA-256 of the unpatched file);
  - fails the build if the places can't be found, as when a new Lapy is compiled differently.
- **On the console**, PC5:
  - checks that its `lapy.elf` matches the SHA-256 in `pc5/lapy-title.txt`;
  - checks that each listed place holds the bytes it should;
  - writes the tile's ID there.

  Any mismatch means no tile, with a message.

The eboot doesn't check `lapy.elf` at runtime. It reads `/app0/lapy.elf` and streams it to
elfldr (`pw_lapy_elevation.c`); the SHA-256 pin is checked only at build time
(`build_native.sh`).

## Work items

| # | What | Where | New or changed |
|---|---|---|---|
| 1 | The eboot reads its app ID at runtime from its own `param.json`. The sandbox path, the restart through the sandbox eboot and the log identity use it. (The Lapy helper finds the ID from the kernel itself, so the request needs no change.) **Done:** patch `0054`, host test `test_pw_title_id` | `native/wine64_main.c`, `native/pw_title_id.h` (header only, so `build_native.sh` is unchanged), `tests/test_pw_title_id.c`, as `patches/prospero-win/0054-…`; `tests/test.sh` runs the test | New patch |
| 2 | Tile mode: read `pc5-tile.json`, go to game mode, prepare behind the loading screen, run Wine in the same process | The same patch, plus `title/pc5_tile.c` | New |
| 3 | `pc5_tile_prepare(root, app, id)`: recover an interrupted remove, find the game, copy Mono, refresh the profile, remember it as last played. `S_LAUNCH` calls it too, so both paths stay the same | `title/pc5_tile.c`, `title/pc5_ui.c` | Moved out of `S_LAUNCH` |
| 4 | Making, finding and removing tiles: copy the runtime, patch Lapy, write `sce_sys` and `pc5-tile.json`, stage and rename. `tile_of` recognises both kinds | `title/pc5_home.c`, `title/pc5_home.h`, `title/pc5_ui.c` (progress screen) | Changed |
| 5 | Lapy places at build time | `tools/lapy-title.py`, `tools/build.sh`, `tools/make-app.py` (requires `pc5/lapy-title.txt`) | New |
| 6 | Refresh tiles after a PC5 update | `title/pc5_tile.c`, called beside `pc5_upgrade_tidy` | New |
| 7 | Tests: tile layout, Lapy patching (good file, wrong file, wrong bytes), finding both kinds of tile, refresh on version change, `pc5_tile_prepare` | `tests/test_pc5.c`, `tests/test.sh` (the `lapy-title.py` check) | New |
| 8 | Console checklist and wiki | `tests/PS5-CHECKLIST.md`, `wiki/Home-screen-tiles.md`, `wiki/Files-and-folders.md`, `wiki/Troubleshooting.md` | Changed |
| 9 | Remove forwarder tiles once the new ones are proven: `pc5/forwarder/`, psfwd's launcher use, `src_forwarder` (keep psfwd's ID and `param.json` helpers, or replace them) | Several | Later |

## Console tests

### Test 1: a hand-made tile with PC5 1.9.6 (passed, 2026-10-29)

This was one hand-made tile with no new code. It checked the two things nobody had published
a result for: PC5's eboot starting under another ID, and a patched `lapy.elf` granting `/data`
to that ID.

1. Over FTP, copy `/data/homebrew/PPSA11018/` to `/data/homebrew/PPSA99437/`.
2. In the copy's `sce_sys/param.json`, set:
   - `titleId` to `PPSA99437`;
   - `conceptId` to `99437`;
   - `contentId` to `UP0000-PPSA99437_00-PC5TILE000000000`;
   - `titleName` to `PC5 tile test`.
3. Replace the copy's `lapy.elf` with `test-artifacts/lapy-PPSA99437.elf`. It was made from the 1.9.6 release's `lapy.elf` by `patch-lapy-title.py`, both in this folder (source SHA-256 `50342e63…`, patched `42b949aa…`).
4. Check that `eboot.bin` and the `.prx` files are still 755 after the upload.
5. Go to the home screen and wait for ShadowMountPlus to register the tile, then start it.
6. Copy `/data/prospero-win/logs/` off the console.

**Results** (`test-artifacts/session-7.log`):

| Question | Result | Log |
|---|---|---|
| Does the console start PC5's eboot under another ID? | **Yes.** The tile started and opened the video, the controller, the keyboard and the mouse | `seq=9 launcher video=ok pad=ok hid=ok frame=1 apps=2` |
| Does the patched `lapy.elf` grant `/data` to `PPSA99437`? | **Yes.** The helper completed with no error, the process was elevated, and it read both games from `/data/prospero-win` | `seq=3 data_mount … helper_completed=1 helper_errno=0 data_after=1 elevated_after=1`, `seq=4 library status=ok … games=2` |
| What fails without work item 1? | **As predicted.** The eboot looked for its own files under the built-in ID's sandbox, `/mnt/sandbox/PPSA11018_00N/app0`, and didn't find PC5's files. It fell back to prospero-win's plain blue "Prospero Win" launcher, listing the 2 games, instead of PC5's screens | `seq=6 pc5 files missing from the app` |

The unpatched helper grants only `PPSA11018`, so it would have refused the tile.
`helper_completed=1` therefore also confirms that the tile ran as `PPSA99437`.

Both unknowns are answered. The design needs neither Lapy's resident service nor an upstream Lapy change.

### Test 2: the same tile with patch 0054 (to do)

This test needs a PC5 build with patch `0054`: `tools/build.sh`, or Actions › Build. The patch
changes only prospero-win's `native/`, `tests/`, `Makefile` and allowlist, not its `tools/` or
`wine/`. The Wine pieces therefore keep their keys, and only `title`, `package` and `app` rebuild.

1. Make the tile as in test 1, but copy the **new build's** app folder. Keep the patched `lapy.elf` and the edited `param.json`.
2. Start the tile.

Expected:
- `PW_WINE64 title id=PPSA99437 from=param.json built=PPSA11018` near the start of the log.
- No `pc5 files missing from the app`. PC5's own screens open, with your library.
- A game started from those screens runs, because Wine is found under `/mnt/sandbox/PPSA99437_00N/app0`.
- Closing it returns to PC5's screens inside the tile. Tile mode (work item 2) isn't there yet.
- PC5 itself (`PPSA11018`) logs `PW_WINE64 title id=PPSA11018 from=param.json` and works as before.

Until tile mode exists, the tile shows the whole of PC5. Don't run an update or install a game
from inside the tile during this test. The updater looks for PC5's own `PPSA11018` folder, and
updating it from a copy is untested.

## Costs and limits

- **Disk:** about 1.1 GB per tile at first, on top of the game.
  - The Windows template is used only to repair a prefix, and Mono only by .NET games; dropping them brings a tile to about 0.5 GB.
  - Hard links on `/data` could bring it near zero. Both are later work, after the test.
- **elfldr is still needed**, because Lapy is loaded through it. The forwarder's launcher payload no longer is.
- **ShadowMountPlus registers new tiles** only after the player quits PC5, as now.
- **A tile runs the PC5 version it was last refreshed with.** A tile whose game breaks under a new PC5 could later be pinned, but not in the first version.

## Estimate

About 2–3 weeks of work after the console test passes, plus console testing of items 1, 2 and 6.

## Sources

- [Lapy (PS5-Lapy-JB-Daemon)](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon): one-shot helper, exact title, checks, service mode.
- [PS5 Forwarder Format](https://github.com/heydemoura/ps5-forwarder-format): one prebuilt eboot for every tile.
- [prospero-win getting started](https://github.com/mpereiraesaa/prospero-win/blob/main/docs/GETTING_STARTED.md): Lapy at each start through elfldr, execute permissions.
- [ProsperoLight's elevation notes](https://github.com/blackbearreloaded/ProsperoLight/blob/main/tooling/elevation/README.md): Lapy's MIT licence, and the try-the-service-first pattern.
