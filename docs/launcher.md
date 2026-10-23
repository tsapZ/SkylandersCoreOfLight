# The launcher (phase 1, first version 2026-10-22)

`coreoflight.exe`: one screen with a card per game, in release order. It starts each game's own
exe and gets out of the way while the game runs; no game needs it.

## What it does

- **Finds the games** (#6), for each in this order:
  1. a path in `coreoflight.ini` next to the launcher (`ssa = D:\Games\Spyro\ssarecompiled.exe`,
     keys `ssa`, `giants`, `swapforce`, `trapteam`, `superchargers`, `imaginators`; a folder works
     too);
  2. where its installer put it: `InstallLocation` of its uninstall entry (`<AppId>_is1`, each game
     its own AppId), which also finds a game installed into another folder or a library folder;
  3. the installer's default folder, `%LOCALAPPDATA%\Programs\<Game> Recompiled\`;
  4. next to the launcher (the exe itself, or a folder named like the repository or the app);
  5. a developer's layout above the launcher: `<Repo>\<Repo>\out\build\win-amd64-release\<exe>`.
- **First-run setup** (`src/setup_ui.cpp`; the owner's wish, 2026-10-23, for friends trying the
  games): on a start with no game installed and `setup_done` not set in `coreoflight.toml`.
  - *Willkommen*: what the launcher is, that the discs are the player's own, the language (left /
    right), A on to the discs, B skips the setup.
  - *Deine Discs*: the six games, nothing looked for unasked (the owner's wish). A on a game
    opens the launcher's file browser (pad and keyboard) for its ISO, or an ISO is dropped on the
    window; it is known by its default.xex and goes to its game's row ("Das ist die Disc von
    ..." when it is another game's). Then its folder and the one above (one level down) are looked
    through on a thread (`src/disc_search.cpp`, the reads are the ISOs' directories and
    default.xex only) and the other games' ISOs found there are filled in as "automatisch
    erkannt"; a wrong one is changed with A on its game. X ticks a game off and on, Y searches
    the whole PC (the file browser's search: Downloads, Desktop and Documents four folders deep,
    the drives two). Each row shows the ISO's size and path, or "Schon eingerichtet" /
    "Keine Disc gefunden". Below: "Braucht 23,6 GB · frei auf G: 640,8 GB" (each chosen disc
    without its system update, 150 MB per game, 256 MB to spare), in red with "Einrichten" off
    when the drive the games go to is too full.
  - *Einrichten*: each chosen game installed from its setup (when it isn't), then its disc set
    up, one after the other, with the import's progress bar; B stops after the step running
    (a disc being unpacked at once). *Fertig*: how many are ready; A to the cards.
  The launcher does the work (its install and its import); the setup only says what is next.
- **Installs a game** (the owner's wish, 2026-10-23): a card that says "Nicht installiert" shows
  "Installieren" when the launcher has the game's setup (`<Repo>Setup-<version>.exe` in a `games\`
  folder next to it, or for a developer in `<Repo>\<Repo>\installer\Output`). A runs that setup
  hidden and silent with `/NOROM=1` (the game without its disc), the menus' language, the screen's
  size and 2x scale for its settings file; after a few seconds the card asks for the disc, which the
  launcher's own import then sets up. With a library folder (`library = <folder>` in
  `coreoflight.ini`, or `--install_dir=<folder>`) the game goes to `<folder>\<Game> Recompiled`,
  else to the setup's default. If the setup stops, its reason comes from its log
  (`%TEMP%\CoreOfLight\<key>-setup.log`). The game stays an ordinary install: its own uninstall
  entry, Start menu entry and uninstaller, and it runs without the launcher.
  **Updates:** for a game its setup installed (its
  uninstall entry's InstallLocation holds the exe the launcher found), the entry's DisplayVersion
  is compared with the setup's version from its name (`<Repo>Setup-<version>.exe`; the setup
  with the highest version is the one taken, 0.1.0-alpha.2 < 0.1.0-beta.1 < 0.1.0). A newer one
  shows "Bereit · Update verfügbar" on the card, and A asks first: "Aktualisieren" runs the
  setup silently over the game where it is (no `/DIR`; the disc page is skipped and the settings
  file kept, as for an install), "Erst spielen" starts the game and doesn't ask again this launch.
  Checked 2026-10-23 in scratch: Swap Force installed without its disc, its entry's version set back
  to 0.1.0-alpha.2, the card offered the update, A brought the entry to 0.1.0-beta.1; then
  uninstalled, no entry left.
- **Checks the disc:** the game looks for `rom\` in the folder it runs in (an installed game next
  to its exe, a developer's build three folders up), so the launcher looks there for
  `rom\default.xex` and compares its SHA-256 with the hash the game's repository pins
  (`docs/game/default.xex.sha256`): "Bereit", "Andere Disc-Version" or "Disc fehlt".
- **The card** shows the active profile's play time ("Noch nicht gespielt" before), its
  achievements and when it last saved the game (the owner's choice, 2026-10-22: not the game's
  settings, which are on Y). The chosen card lifts and glows.
- **Sets up a disc** (#6): X (D) opens the launcher's own file browser (`src/file_browser.cpp`),
  which the pad drives as well as the keyboard and mouse: folders and .iso files from Downloads on,
  a folder with an unpacked disc ("Diesen Ordner einlesen"), the drives and the usual folders above
  a drive. With no game ready yet (the first start) the start screen says "Noch keine Disc
  eingelesen" with a button for it. Dropping an ISO on the window
  or on coreoflight.exe does the same. The launcher reads the image's file system itself (XDVDFS,
  src/xiso.cpp: a full Xbox 360 dump with the game partition 0xFD90000 into it, or a trimmed image),
  knows the game by its default.xex's SHA-256 (the hash each game pins) and unpacks the disc into
  the rom\ folder that game reads, with a progress bar; default.xex comes last, so a folder left
  half done never counts as a disc. B doesn't quit meanwhile: it asks, and a second B within a few
  seconds stops the unpacking (an older disc's default.xex in the folder goes then, so the mix
  never counts as a disc). The disc's `$SystemUpdate` folder (the console's, about 100 MB) is left
  out, as the installers do, and a drive without room for the disc (plus 256 MB) gets a message
  before anything is written. A disc that is set up already, a game that isn't installed and an
  unknown disc each get their own message. A folder with an
  unpacked disc (default.xex in it) works the same way: O picks one with the Windows dialog, or
  drop it on the window; its files are copied into the rom folder. The ISO itself stays where it
  is. **"Discs suchen"** (Y in the browser, or its first entry above the drives) looks for the
  discs by itself on a thread of its own: every .iso of 100 MB or more in Downloads, Desktop and
  Documents (four folder levels down) and on the fixed and removable drives (two levels; Windows',
  the programs' and hidden folders left out), each read like a disc being set up and listed by
  game when its default.xex is one of the six ("Trap Team · D:\ISOs\..."); A sets it up.
  Checked 2026-10-23 on the owner's PC: all eight discs among 9 ISO files in 644 folders, in about
  3 s.
- **The title music** (`src/music.cpp`): while a game's card is chosen its title music plays,
  faded in and looped, through Windows' MCI. It comes straight from the game's disc: the track its
  title screen streams, decoded by the game itself (skylanders-portal `title_music.h`). Each game
  leaves it on its first start; for a game that hasn't run yet, choosing its card (or setting up
  its disc) starts the game hidden with `--export_title_music=<file>`, which writes it in about a
  second and quits before a window opens (the owner found the earlier minute-long recording of the
  title screen too much, 2026-10-22). The file is `Documents\Skylanders\Pictures\<game>\title_music.wav`.
- **The launcher's own settings** (LB / Q, `src/hub_settings.cpp`), in the same screen as a game's,
  saved in `Documents\Skylanders\coreoflight.toml`: the menus' language (Automatisch / Deutsch /
  English), whether the figure library opens on the owner's figures or on all, the "Hab ich" marks,
  the title music, the animations.
- **Animations:** every screen slides in from the left and fades in (0.32 s), the start screen
  rises when it comes back; off with the setting.
- **The profile's name in the games:** started from here, a game gets `--user_name=<profile name>`
  (the SDK's XAM answers it as the gamertag, e.g. Swap Force's corner at the top left).
- **Each game's own settings** (#7): Y (Tab, a right click) on a card opens the game's settings
  with exactly the pages of its F4 menu (Grafik, Bildrate, Spiel, Portal, Steuerung), their texts,
  options and explanations. The launcher doesn't know them itself: it starts the game's exe with
  `--settings_schema=<file>` (skylanders-portal `settings_schema.h`), and the game writes its pages
  with the values it would start with and quits after about 2.5 s, before it opens a window or
  checks its disc. The chosen card's settings are read ahead. Left / right (A, a click) changes a
  setting, LB / RB (Q / E) switches the page, Y saves the changes into the game's settings file
  line by line as the game's menu does (comments and the rest stay), B goes back (with unsaved
  changes it asks: A saves, X throws them away, B goes on editing). A preset (Leistung / Qualität / Beste) sets its
  settings together. A game built before the flag existed isn't asked (the launcher looks for the
  flag's name in the exe first, so it never starts a whole game by mistake). Since these are set
  here before starting, a game started from the launcher gets `--started_by_launcher=true`: its F4
  menu then leaves out the Grafik page and every setting that only applies at start-up (#10, the
  owner's wish: changing them in the middle of a game does nothing). A game started on its own
  keeps its full menu; an older build just ignores the flag.
- **Starts on the game the active profile saved last** (else the newest installed one); after a
  game ends or a disc is set up the chosen card stays.
- **Starts the game** (#5) in that folder with A / Enter / a click on the chosen card, hides
  itself while it runs and comes back when the game ends.
- Counts the figure files in the shared folder `Documents\Skylanders\Figures`.
- **DirectInput pads** (the Logitech Dual Action and the like, which XInput doesn't see) drive the
  launcher as well: read through Windows' joystick API with SDL's layout for the Dual Action
  (1-4 = X A B Y, 5 / 6 shoulders, 7 / 8 triggers, 9 Back, 10 Start, the hat as the d-pad);
  XInput pads, which show up there too, are left out by their HID path ("IG_"), `src/pad_state.h`.
- **The connected controllers** are shown left of the profile at the top right, as on the Xbox:
  each pad's number (its XInput slot, 1-4) and, for a pad on batteries, three bars
  (`XInputGetBatteryInformation`, asked every ten seconds; red at one bar).
- **The figure library** (#8, Start / F): every figure in that folder with its level, XP to the
  next level, gold, play time, upgrades, nickname and the day it was last played, by game (LB /
  RB), read with skylanders-portal's figure code (its `skylanders_portal_figures` target, built
  without the games' SDK, AES and MD5 from Windows). Every card shows the figure's element lightly
  (its colour fading in from the left edge); the last tab, **"Magische Objekte"**, has the magic
  items and adventure packs of every game, by game, and the Skylander tabs leave them out. X
  switches to every Skylander of a game, the
  owner's marked ("11 von 54 dabei"), each with the progress of the owner's figure. Y on a
  Skylander without a file marks it **"Hab ich"** (I own it: on the shelf, not as a file); it then
  counts as owned and shows in "Meine Skylander" as "hab ich (ohne Datei)", and Y takes the mark
  back. The marks are the profile's, `Skylanders\owned.txt` next to its figure folder (one
  `<id>\t<variant>` a line); the games never read it. An Imaginators creation crystal shows its
  Imaginator's name and battle class (kept on the figure, skylanders-portal
  `FigureStats::creation_class`); a Trap Team trap the villain caught in it ("Gefangen: Chompy
  Mage", skylanders-portal `ReadTrapVillain`); a figure with hero points has them ("64 / 100"),
  and every played figure the consoles it has been on ("War auf: Xbox 360"). In the upgrade tree
  the d-pad moves to the next upgrade in its direction as the tree is drawn: along a path, to the
  path beside it, between the rows. The pictures
  are the games' own: each game, while it runs, leaves the figure pictures it reads from its disc
  in `Documents\Skylanders\Pictures\<game>` as `<id>-<variant>.png` (skylanders-portal
  `StartFigurePictureCache`), and the library takes each figure's picture from its own game (a Series 1 Spyro from Spyro's Adventure, a Series 2 one from Giants; the owner's wish, 2026-10-22), else the newest game's; a
  figure no played game has a picture of gets its element's colour and first letter. The figure's
  hat shows by its game name, with its picture and bonuses: the games leave `hats.txt` and
  `hat-<n>.png` there too. A (Enter) on one of the owner's figures opens its **upgrade tree** as
  the games' details view has it: the attacks, basic upgrades, soul gem and Wow Pow, the two paths
  side by side, the bought ones bright, the chosen one's picture, price and text in the panel; the
  games leave the trees of the owner's figures (`upgrades-<id>-<variant>.txt` and their pictures).
  Copies the
  games keep in a `real` folder before they write to a real figure are left out.
  `figures = <folder>` in coreoflight.ini uses another figure folder (as the games'
  `portal_figures_dir`); the pictures are then in the `Pictures` folder next to it.
- **Profiles** (#4, Back / P / a click on the profile at the top right), like an Xbox account:
  a name, a Skylander's picture (Y: the profile's own figures and each game's hero, those a game
  has left a picture of), their own saves, achievements and figures. The main profile is the data
  as the games keep it without the launcher (`Documents\<game>recomp`,
  `Documents\Skylanders\Figures`), so nothing changes for it. Every other profile is a folder
  `Documents\Skylanders\Profiles\<id>` (its `profile.ini` has the name and picture) that stands
  in for Documents: the launcher starts a game with `--user_data_root=<folder>\<game>recomp`, the
  game then keeps the profile's figures in `<folder>\Skylanders\Figures` and its pictures in
  `<folder>\Skylanders\Pictures` by itself (the games put both next to their user data's
  folder), and `--cache_root=Documents\<game>recomp\cache` keeps the main profile's shader
  cache, so a new profile doesn't build it again. A new profile can start with the current
  profile's figures made new (the same Skylanders back at the start, new serial numbers), with
  copies of them as they are, or with none (Y); B there gives the new profile up again. In the
  figure library, Y (C) copies the chosen figure's
  file, as it is, into another profile's figure folder (the same sub-folder, "Name (2).dump" if the
  name is taken). X renames it with an on-screen keyboard (the pad moves over the keys, A types,
  X deletes, Y a space, LB capitals, Start done; a keyboard types straight in), Back deletes it into the
  recycle bin after asking, with "Behalten" chosen first (left / right to "Löschen", then A). `Profiles\profiles.ini` remembers the active profile and the main profile's name
  and picture. The games themselves still call their player "User".
- **Achievements** (RB / E, for the chosen game; LB / RB switch the game): the game's own list
  with its pictures, the active profile's unlocked ones bright with their date, how many and the
  gamerscore; each card shows "Erfolge 2 / 46". Up / down move a row, left / right between the
  two columns. The list comes from the game: while it runs it
  leaves `achievements.txt` and the title's pictures (`ach-<n>.png`, the PNGs of its XDBF) in its
  picture folder (skylanders-portal `StartAchievementCache`); what is unlocked is the profile's
  `<game>recomp\achievements\<title id>.toml`, which the SDK keeps.
- **Achievement info** (#14): A (or a click) on an achievement shows it large: its picture, the
  name, the whole description and the locked one when the game has another, the gamerscore,
  "Freigeschaltet am 20.10.2026 um 09:08" or "Noch nicht freigeschaltet", and how many of the
  game's are unlocked; A or B closes it.
- **Platinum trophy** (the owner's wish, 2026-10-23): once every achievement of a game is
  unlocked, its screen shows a platinum cup with the date of the last one in place of the bar, the
  card a small cup behind "Erfolge 38 / 38" and the profile card one per such game. When a game
  ends with its last achievement, a "PLATIN-TROPHÄE!" banner shows at the top for 12 s.
- Each card also says when the active profile last saved the game (the newest file in its save
  folder, `<game>recomp\B13EBABEBABEBABE`).
- **Play time** per game and profile, on the card ("Gespielt 3 Std. 12 Min."): the time from
  start to end of each game the launcher started, for the profile it was started with, kept in
  `Skylanders\playtime.txt` next to the profile's figure folder (`<game key>\t<seconds>`). A game
  started on its own isn't counted.
- `--documents=<folder>` runs the launcher on a copy: that folder in place of Documents for the
  figures, pictures and profiles, and the games it starts get their user data there too.
- Controller (d-pad or stick, A, B), keyboard (arrows, Enter, Escape) and mouse; German or English
  by Windows' display language.
- **The games' own dashboard art** from the owner's discs (`src/nxe_art.cpp`): each disc's
  `rom\nxeart` is an STFS package with the Xbox 360 dashboard's pictures of the game. Its
  `nxeslot.jpg` (the logo tile, 420x320) is at the top of the game's card in place of the colour
  band and the name, and its `nxebg.jpg` (1920x1080) fills the window, darkened, while the game is
  chosen. They are read once and kept next to the games' pictures
  (`Documents\Skylanders\Pictures\<game>`), so nothing of them is in the repository; a game without
  a disc keeps the plain card.
- Its icon (#11), in Explorer, on shortcuts, the window and the taskbar, is our own drawing of the
  Core of Light (`src/coreoflight.ico`, drawn by `tools/make_icon.ps1`; no art from the discs). The
  games get their own logos from their discs (skylanders-portal `skylanders_game_icon`).

## What it doesn't do yet

- Settings that take a free value (no list of options) are shown but not changed here; the
  game's own menu (F10) has them.
- The profile's picture in the games (only the name is passed); online co-op (#1-#3).

## Build

From a Visual Studio 2022 x64 developer prompt:

    git submodule update --init
    cmake --preset release
    cmake --build --preset release

The exe is `out\build\release\coreoflight.exe`. It is a plain Win32 / Direct3D 11 program with
Dear ImGui (thirdparty/imgui, 1.92.5, the version the games' SDK uses), so it builds in seconds
and needs neither the SDK nor the games' sources.

## Checked (2026-10-22)

- Achievements: Trap Team, run once on a copy, left 46 achievements with 46 pictures and their
  German names; with two of them unlocked in the copy's `trapteamrecomp\achievements\41560911.toml`
  the card said "Erfolge 2 / 46" and the screen "2 von 46 Erfolgen · 20 / 1000 G", the two bright
  with their dates.
- Platinum (2026-10-23), on a copy with 37 of Spyro's Adventure's 38 achievements unlocked: a dim
  cup before the bar; a game started from the launcher, the 38th written into the copy's unlock
  file while it ran, the game closed: the banner "PLATINUM TROPHY! Spyro's Adventure: every
  achievement unlocked" came up, the card showed "38 / 38" with the cup and the screen "PLATINUM
  TROPHY · 2026-10-20". (Messages set when a game ends, such as "the game ended unexpectedly",
  used to vanish at once: ImGui's clock jumps over the time the game ran. They count from the
  first frame after it now.)
- First-run setup (2026-10-23), the bundled launcher on empty documents with the install folder
  on G:: welcome, "Automatisch suchen" found 9 ISOs and all six games, "Braucht 44,9 GB · frei
  auf G: 653,2 GB"; with only Spyro's Adventure ticked (7,8 GB) it installed the game, set up its
  disc with the progress bar, said "1 Spiel(e) bereit", and the card was "Bereit";
  `setup_done = true` was written. Choosing by hand: with Spyro's Adventure's ISO dropped on the
  window (`isotest\Skylanders\Spyro\`), the look next to it filled in Giants and Swap Force from
  `Skylanders\Giants\` and `Skylanders\Swap Force\` as "automatisch erkannt" (23,6 GB).
- A colleague's first start (2026-10-23): the launcher alone in a folder with the six beta.2
  setups in `games\`, on empty documents. Spyro's Adventure installed from its card in 12 s, its
  ISO given as a dropped file was set up in 76 s, the game started from its card into its German
  title screen and the card said "Gespielt < 1 Min." afterwards. With an install folder of about
  150 characters the disc's longest paths passed MAX_PATH and unpacking stopped half way with
  "cannot write"; it now says "path too long" before writing anything.

- Play time, on a copy with `coreoflight.ini` pointing Giants at a stand-in exe that runs 61 s:
  after it ended, `playtime.txt` had the line "giants, a tab, 61" and the card "Gespielt 1 Min.".

- "Hab ich", on a copy without figure files: Y on Camo in "Alle Skylander" marked it ("1 von 597
  dabei", the tile "hab ich (ohne Datei)", the hint then "Hab ich nicht"), "Meine Skylander" showed
  it, and Y there took the mark back (`owned.txt` empty again).

- Profiles, on a copy (`--documents`): a new profile with its figures made new (29 figure files,
  only their headers left), renamed by keyboard, given a picture; the launcher shows it at the top
  right and counts its figures. Started from the launcher (a stand-in exe that writes its command
  line), the game got `--user_data_root=<copy>\Skylanders\Profiles\p2\giantsrecomp` and
  `--cache_root=<copy>\giantsrecomp\cache`; Giants itself, started that way, put its pictures in
  the profile's `Skylanders\Pictures\Giants` and its shader cache in the main one.

- Disc set-up: the Trap Team ISO on the developer layout said "die Disc ist schon eingerichtet" and
  chose Trap Team. In a test folder (coreoflight.ini pointing Spyro's Adventure at a stand-in exe)
  the Spyro's Adventure ISO unpacked in 38 s: the same 1,142 files with the same sizes as the
  owner's rom folder, default.xex and two large archives with the same SHA-256; the card turned
  "Bereit".
- Settings: Trap Team's five pages, a change, the page switch and the question about unsaved
  changes; every game writes its pages in 1.1 to 2.5 s.

- In the developer layout all six games were found, their discs "Bereit" (the hashes match), the
  cards show each game's settings; arrows move the choice.
- With `coreoflight.ini` pointing one game at a stand-in exe (a copy of a system program, so no
  game ran against real saves): A started it in its folder, the launcher hid while it ran and came
  back when it was closed; the other games showed "Nicht installiert".
