Short answers to questions people often ask about PC5, with links to the pages that explain more.

## General

**Does PC5 come with games?**
No. PC5 contains no games and downloads none. You install the games you own, from a USB drive.

**Which games run?**
Whether a game runs is only known by trying it. PC5 makes no promise about any game. Games that need Steam, Epic, EA or Ubisoft's launcher running, online DRM or anti-cheat don't run. See [What runs and what doesn't](Installing-games#what-runs-and-what-doesnt).

**Is there a list of games that work?**
Yes: the [Game compatibility list](Game-compatibility). A game that runs comes from a player's report in Discussions: to add one, open [the report form](https://github.com/bornaradusin/PC5/discussions/new?category=game-compatibility), fill it in and post it, and the game appears in the list within a minute or two. A game that doesn't run comes from its [issue](https://github.com/bornaradusin/PC5/issues): being looked at while it is open, then resolved or unresolved as it is closed.

**Does PC5 need internet?**
No. PC5 itself only goes online to find covers and to check for a newer version. Without internet, games get a cover made from their icon, or [your own picture](Covers#using-your-own-cover).

**Do I need a Steam, GOG or other account?**
No. Covers come from public sources with no account and no key, and PC5 doesn't run any store's launcher.

**How many games can PC5 hold?**
Up to 128, as long as the console has the space.

**What does PPSA11018 mean?**
It is PC5's title ID: the name of the app's folder and of the release's zip file.

## Installing

**Can I install from the console's own storage or the network?**
No. PC5 installs games from USB drives only, formatted as exFAT. See [Installing games](Installing-games).

**Do I need to keep the USB drive plugged in to play?**
No. PC5 copies each game to the console. The USB drive is only read while installing, and never written to.

**Can I install a game's installer (setup.exe)?**
No. Install the game on a PC first, then copy the installed folder to the USB drive.

**PC5 picked the wrong .exe. What now?**
Choose the game, press **△** and use **Manage › Change program**. See [Choosing the right program](Installing-games#choosing-the-right-program).

**What happens if the console switches off during an install?**
The unfinished copy is deleted the next time PC5 starts. Install the game again.

## Playing

**How do I leave a game?**
Hold the **touchpad** and **Options** together for two seconds. See [Leaving a game](Playing-games#leaving-a-game).

**Can I use a keyboard and mouse?**
Yes. A USB keyboard and mouse work in every controller mode. For games without controller support, set the game's controller to **Keyboard & mouse** and map the buttons to keys. See [Controllers](Controllers).

**Can more than one player play?**
Yes, up to four, in a game with local multiplayer: each player turns on their own controller and signs in with it on the console, as another user or a guest. See [More than one player](Controllers#more-than-one-player).

**Can I make a game run at 60 fps?**
For a game that keeps 30 fps, turn on **△ › Graphics › Frame generation**. It shows the game at 60 fps with frames made in between. See [Frame generation](Playing-games#frame-generation).

**Do .NET games run?**
Since 1.5.0, yes: .NET Framework games and tools (XNA through FNA, MonoGame, WinForms) run with Wine Mono, the open-source stand-in for Microsoft's .NET that Wine itself uses. The first time a .NET game starts, PC5 sets it up once for every game. Programs that mix .NET and C++ (C++/CLI) don't run, and some WPF doesn't. See [.NET games](Playing-games#net-games).

**How do I type into a game?**
Click the touchpad with two fingers: the PS5's on-screen keyboard opens over the game, and what you type is typed into the game when you choose Done. See [Typing in a game](Controllers#typing-in-a-game).

**Can I make a 4:3 game fill the screen?**
Yes: **△ › Graphics › Aspect ratio › Stretch**. The picture gets wider than it was drawn. See [Aspect ratio](Playing-games#aspect-ratio).

**My game has its own launcher. Does it work?**
Since 1.6.0, usually: when the launcher starts the game, PC5 closes the launcher and starts the game in its place. A launcher that needs a store's client (Steam, Epic, EA, Ubisoft) still doesn't work. See [What runs and what doesn't](Installing-games#what-runs-and-what-doesnt).

**Can a game run at 1440p or 4K?**
Yes, a game drawn with Direct3D, Vulkan or OpenGL: **△ › Graphics › Resolution**. 1080p is the default. 4K is four times the work of 1080p, so a game that is already slow gets slower. See [Resolution](Playing-games#resolution).

**A game shows empty boxes instead of Japanese, Chinese or Korean text.**
Update PC5. Since 1.5.0, games find Windows' Chinese, Japanese and Korean fonts (Droid Sans Fallback stands in for them), and Arial, Times New Roman and Courier New (Liberation). A game that draws text with its own font isn't changed.

**A game says the PC doesn't meet its minimum specs, or has too little memory.**
Update PC5. Before 1.4.0, games were told the console had about half a gigabyte of memory. From 1.4.0 they're told it has 16 GB, and how much of it is free.

**Which language do games use?**
The PS5's. Each game's Windows language is the console's, so a game that follows the Windows language follows the console.

**Can I start a game straight from the home screen?**
Yes, with a [home screen tile](Home-screen-tiles). It needs ShadowMountPlus and elfldr.

## Saves and updates

**Where are my saves?**
In the game's own Windows folder, `/data/prospero-win/prefixes/<game>/`. See [Files and folders](Files-and-folders).

**Does uninstalling a game delete its saves?**
You choose. **Keep saves** deletes the game and keeps its saves and settings for when you install it again; **Delete all** deletes everything, with no backup and no undo. See [Uninstalling a game](Installing-games#uninstalling-a-game).

**How do I update a game to a newer version?**
Put the newer copy's folder on the USB drive, then choose the game, press **△** and choose **Manage › Update**. Its saves, settings and options stay. See [Updating a game](Installing-games#updating-a-game).

**Do I lose my games when I update PC5?**
No. Games, saves and settings are in `/data/prospero-win`, not in the app's folder. Replace the app's folder and everything stays. See [Updating PC5](Installing-and-updating#updating-pc5).

**Do I need to reinstall games after updating PC5?**
No. When a new version brings something games need, each game gets it the next time it starts.

**How do I know a new version is out?**
With **Settings › System › Check when PC5 opens** on (the default), PC5 asks GitHub when it opens and shows a notification when a newer version is out.

## Problems

**A game crashed. What should I do?**
Update to the newest PC5 and try again. If it still crashes, [report it on GitHub](Reporting-a-problem) with the log PC5 kept.

**Where do I report a problem?**
As an issue on GitHub, <https://github.com/bornaradusin/PC5/issues>, with the game's log attached. Not in ProsperoStore comments. See [Reporting a problem](Reporting-a-problem).
