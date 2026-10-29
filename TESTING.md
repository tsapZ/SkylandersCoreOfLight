<div align="center">

<img src=".github/images/status.png" alt="Test status of the six games" width="100%">

</div>

# Test status

Each game gets played automatically, on its own and from copies of real saves. It loads every level and plays it for a while, puts every Skylander and magic item on the portal as a brand new figure, and plays the end credits to the end. Crashes, script errors, missing files and the frame rate go into a report. This page shows how the last runs ended.

It doesn't replace playing: quests, bosses and two players still need people. [Report a problem](https://github.com/tsapZ/SkylandersCoreOfLight/issues/new/choose) if you find one.

| Game | Tested build | Levels | Figures | End credits |
|---|---|---|---|---|
| Spyro's Adventure | 0.3.0-beta.3+ (2026-10-28) | **69 of 69** | **45 of 45** | not yet |
| Giants | 0.3.0-beta.6, not out yet (2026-10-29) | **24 of 24** | not yet | not yet |
| Swap Force | 0.3.0-beta.6, not out yet (2026-10-29) | **18 of 18** | not yet | not yet |
| Trap Team | 0.3.0-beta.6, not out yet (2026-10-29) | **26 of 26** | not yet | not yet |
| SuperChargers | 0.3.0-beta.6, not out yet (2026-10-29) | **14 of 14** | not yet | not yet |
| Imaginators | 0.3.0-beta.6, not out yet (2026-10-29) | **40 of 40** | not yet | not yet |

## Spyro's Adventure

Tested build: 0.3.0-beta.3+ (2026-10-28)

| What | Result |
|---|---|
| Story levels | 27 of 27 loaded and played |
| The Ruins (hub) | 1 of 1 loaded and played |
| Heroic Challenges | 32 of 32 loaded and played |
| Arenas | 9 of 9 loaded and played |
| Lowest frame rate in a level | 36 FPS |
| Skylanders, each as a new figure | 32 of 32 |
| Variants, each as a new figure | 5 of 5 |
| Magic items, each as a new figure | 8 of 8 |

Found on the way:

- Oilspill Island crashed while it loaded ([#42](https://github.com/tsapZ/SSARecomp/issues/42)): fixed in 0.3.0-beta.3. Every level loads now.
- Two of the game's own scripts slip (one asks for something that isn't there). The game catches both and plays on, as it does on the console.

## Giants

Tested build: 0.3.0-beta.6, not out yet (2026-10-29)

| What | Result |
|---|---|
| Levels | 24 of 24 loaded and played |
| Lowest frame rate in a level | 59 FPS |

Found on the way:

- In the first run of all the story levels, one hub stage crashed while loading, as the 23rd level in a row. The test itself had caused that (it's fixed); the second run loaded all 24 without a problem.

## Swap Force

Tested build: 0.3.0-beta.6, not out yet (2026-10-29)

| What | Result |
|---|---|
| Levels | 18 of 18 loaded and played |
| Lowest frame rate in a level | 56 FPS |

## Trap Team

Tested build: 0.3.0-beta.6, not out yet (2026-10-29)

| What | Result |
|---|---|
| Levels | 26 of 26 loaded and played |
| Lowest frame rate in a level | 59 FPS |

## SuperChargers

Tested build: 0.3.0-beta.6, not out yet (2026-10-29)

| What | Result |
|---|---|
| Levels | 14 of 14 loaded and played |
| Lowest frame rate in a level | 57 FPS |

Found on the way:

- The test also tried a few zones the game lists but never sends you to on its own (one without its map on the disc, and the elemental gates' areas, which belong to a level). Asked for alone, they end the game. A player can't get there, so they aren't counted here.

## Imaginators

Tested build: 0.3.0-beta.6, not out yet (2026-10-29)

| What | Result |
|---|---|
| Levels | 40 of 40 loaded and played |
| Lowest frame rate in a level | 53 FPS |
