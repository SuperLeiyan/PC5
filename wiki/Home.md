PC5 is a launcher for your own Windows PC games on a PS5 that runs homebrew. It installs games from a USB drive, shows them as a row of covers, and runs them with [prospero-win](https://github.com/mpereiraesaa/prospero-win) (Wine on the PS5). The app's title ID is `PPSA11018` and its tile on the home screen is called **PC5**. This wiki describes the [newest release](https://github.com/bornaradusin/PC5/releases/latest).

![The PC5 library](images/library.png)
*The library: your games as a row of covers, with the chosen one in the middle.*

PC5 contains no games and downloads none. You install the games you own.

## What PC5 does

- **Installs games from a USB drive.** Each game is copied once to the console, checked against the drive, and gets its own Windows (its own saves, settings and shader cache). A game can also play straight from its drive, and games sent over FTP to PC5's INSTALL folder are installed as PC5 opens.
- **Shows your library as covers**, standing on a floor that mirrors them, the chosen game's background behind them. Covers come from Steam's store, GOG's catalogue, PCGamingWiki or Wikidata with no account and no key, and from SteamGridDB if you add your own key; each can be turned off. You can also use your own pictures.
- **Runs the games** with Wine, DXVK (Direct3D 8 to 11), VKD3D-Proton (Direct3D 12), Mesa (RADV for Vulkan, Zink for OpenGL) and Wine Mono (.NET Framework).
- **Lets you set each game up on its own:** its controller (Xbox controller, DualSense, or keyboard and mouse with your own button mapping), its resolution (1080p, 1440p or 4K) and aspect ratio, frame generation from 30 to 60 fps, launch options, and a tile on the PS5's home screen that is an app of its own.
- **Has settings for every game:** an overlay (FPS, frame time, CPU, temperatures, memory), vibration strength, the light bar and the adaptive triggers.
- **Lets up to four players play together**, each signed in on the console with their own controller.
- **Lets you type into games** with the PS5's on-screen keyboard: click the touchpad with two fingers.
- **Starts a game its launcher starts:** when a game's own launcher starts the game, PC5 starts it in the launcher's place.
- **Keeps the log** of each game's newest session as one zip, with the game's own logs and crash files, tells you when a game crashed, and opens GitHub's issue form for it with a QR code.

## Start here

| Page | What it covers |
| --- | --- |
| [Installing and updating PC5](Installing-and-updating) | Putting PC5 on the console, updating it, finding its version |
| [Installing games](Installing-games) | Preparing the USB drive, installing one or several games, on the console or a USB drive, the INSTALL folders, uninstalling |
| [Playing games](Playing-games) | The library, starting and leaving a game, the game menu, frame generation |
| [Controllers](Controllers) | The three controller modes, button mapping, a game's Windows pop-ups |
| [Settings](Settings) | Every row of PC5's Settings and what it does |
| [Covers](Covers) | Where covers come from, and using your own |
| [Home screen tiles](Home-screen-tiles) | A game's own tile on the PS5's home screen |
| [Launch options](Launch-options) | Starting a game with options, with `launch.txt` |
| [File manager](File-manager) | A game's own files, and the console's, from PC5 |
| [Web page](Web-page) | PC5 from a browser on a phone or a PC: sending games, launch options, logs |
| [Files and folders](Files-and-folders) | Where PC5 keeps games, saves, settings, covers and logs |
| [Troubleshooting](Troubleshooting) | PC5's messages, and what to do about them |
| [FAQ](FAQ) | Short answers to common questions |
| [Reporting a problem](Reporting-a-problem) | How to report a problem, and which log to attach |
| [Logs and debugging](Logs-and-debugging) | Where the logs are, a more detailed log for one game, starting a game another way |

## What you need

- A PS5 that can run homebrew apps.
- A USB drive formatted as **exFAT**, with your games' installed folders on it.
- An internet connection is optional. PC5 itself only uses it to find covers and to check for a newer PC5.
- For [home screen tiles](Home-screen-tiles) only: ShadowMountPlus (or another homebrew mounter that registers the apps in `/data/homebrew`) and elfldr.

## Good to know

- Whether a game runs is only known by trying it. PC5 makes no promise about any game. See [what runs and what doesn't](Installing-games#what-runs-and-what-doesnt), and what players found in the [Game compatibility list](Game-compatibility). Got a game running? [Add it to the list](https://github.com/bornaradusin/PC5/discussions/new?category=game-compatibility).
- Games that need Steam, Epic, EA or Ubisoft's launcher running, online DRM or anti-cheat don't run.
- To report a problem, open an issue on GitHub with the game's log. See [Reporting a problem](Reporting-a-problem).

## About the screenshots

The screenshots on this wiki were drawn by PC5's own code on a PC, with made-up sample games and covers. On the console the screens look the same, with your own games and covers.

PC5 is not made by or connected to Sony, Valve, GOG, GitHub or the Wikimedia Foundation.
