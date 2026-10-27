# Online co-op, PC to PC (proposal)

Status: proposal, nothing built (Phase 4, #1 #2 #3). Building it is a separate decision.
Written for SwapForceRecomp and moved here (its #28, #29, #30): every recompiled game has the
same software portal from `skylanders-portal`, so the design applies to all of them.

## Goal

Two players in two homes play a recompiled Skylanders game together. The host runs the game; the
guest sees and hears it, plays with their own controller, and brings **their own figures**: files
from their figure folder, or the real figures on their own portal. Progress the game writes (XP,
gold, upgrades, hats) ends up on the guest's figure, as if it had been on the host's portal.

## Direction (2026-10-27)

- **Our own connection, made by Core of Light.** No Parsec or Steam in between, and nothing
  hosted: no server, no relay, no lobby list. The two PCs connect directly, 1:1.
- **A "lobby" is a join code.** The host's Core of Light shows a code (address, port, a key); the
  guest pastes it, e.g. from Discord. If the host can't be reached directly, the guest sends a
  reply code back and both sides open the connection at the same time.
- **The guest needs no copy of the game,** only Core of Light: it shows the stream, sends the pad
  and lends the figures.
- **No netplay.** Two copies of the game kept in sync don't work for these ports: they are
  multithreaded, timing-dependent and have no savestates. Co-op mods that sync two copies (Super
  Mario 64, Ocarina of Time) build on full decompilations.
- SuperChargers and Imaginators have Xbox Live online code of their own (racing, among other
  things). Making it run is a separate research track: it needs the 360's network and session
  layer in the SDK.

## Shape

```
 host PC: the game                                 guest PC: Core of Light (no game)
 +--------------------------------------+          +--------------------------------+
 | presenter (after the menus) -> H.264 | picture  | viewer: H.264 decoder, window  |
 | audio output -> Opus ----------------+- sound ->| Opus -> speakers               |
 | network pad = player 2 <-------------+-- pad ---| the guest's controller         |
 | software portal: guest's slots <-----+-figures->| figure files / real portal     |
 +--------------------------------------+  one UDP +--------------------------------+
                                          connection, encrypted
```

- **Picture:** taken from the SDK's presenter after the menus are drawn (so the portal menu is in
  the stream) and encoded by the GPU through Windows' own H.264 encoders (NVIDIA, AMD, Intel).
- **Sound:** the SDK already has a tap on its audio output; it goes out as Opus.
- **Pad:** a network input device merges in like the scripted input does and plays as player 2,
  so the game, the portal menu and the player menu see a second local player.
- **Figures:** a reliable lane on the same connection (below). The game always talks to the
  host's own software portal: it tolerates no delay (late portal replies made Swap Force reset its
  portal every 20 s), while figure data is small and not urgent (1 KB when placed, about 200 bytes
  per save).
- **Latency:** about the ping plus 15-35 ms in a first version, plus 8-15 ms tuned; Parsec is about
  the ping plus 10 ms. Fine for co-op Skylanders.

## Finding each other without a server

1. **Same network:** Core of Light lists hosts on the LAN.
2. **Port opened automatically:** UPnP, NAT-PMP or PCP on the host's router. Works for roughly a
   third to a half of home routers (a FRITZ!Box allows it per device only).
3. **IPv6:** no NAT, and common in Germany (about 74% of traffic). The firewall still needs a
   pinhole or both sides sending at once (the reply code).
4. **Both sides at once** (invite code plus reply code) for most of the rest.
5. **When none of that works** (both behind carrier NAT without IPv6, mobile hotspots: roughly
   10-30% of pairs): forward the port by hand, or a VPN the players run themselves (Tailscale,
   ZeroTier). A public STUN server would bring that down to about 5-15%, but it's a third party,
   so at most an opt-in setting.

The key in the code encrypts and authenticates the connection; a code works once.

## Phases

| Step | What | Effort (weeks) |
|---|---|---|
| 1 LAN | picture, sound and pad over plain UDP on the same network, the viewer in Core of Light | 3-5 |
| 2 Robust | loss handling (FEC, resends), bitrate adapting, encryption, the figure lane | 3-4 |
| 3 Internet | join and reply codes, port opening, IPv6, diagnostics with plain advice | 2-4 |
| 4 Hub and figures | "Play together" and "Join" pages; figure files, then the real portal | 3-5 |
| 5 Polish | other codecs, more GPUs and networks tested | 2-6 |

Main risks: differences between GPU encoders, Windows N editions without media codecs, firewall
prompts, the security of an open UDP port (authenticate before reading anything), and the host's
upload speed. GPL projects (Sunshine, Moonlight, OBS, Dolphin) are for learning only; nothing is
copied from them into these MIT repositories.

## Until then: a remote play tool

Without any of this, two players can already play with a remote play tool and the host's figures.

| | Parsec | Moonlight + Sunshine | Steam Remote Play Together |
|---|---|---|---|
| Pad | arrives as an Xbox 360 pad [1] | arrives as a pad | through Steam Input |
| Other USB devices | experimental USB passthrough (validated for security keys only) | no | no |
| Cost | free plan (60 FPS) | free, open source | free, only for games in the Steam library |

[1] Parsec help, "Setup Gamepad" (support.parsec.app, article 32381705301908).

## Figure channel

- **Transport:** a reliable lane on the connection above (with a remote play tool instead: one
  TCP connection from a small portal client to the host). The data is tiny (1 KB per figure,
  16 bytes per write).
- **Pairing:** through the join code. Nothing but figure messages is accepted, every size is
  checked, at most 8 guest figures.
- **Messages:**

| From | Message | Content |
|---|---|---|
| client | `hello` | protocol version, pairing code, the player number the guest wants |
| client | `place` | client slot, the figure's 1 KB, its UID |
| client | `remove` | client slot |
| host | `write` | client slot, block number, 16 bytes, sequence number |
| client | `written` | sequence number (the block is on the real figure) |
| both | `ping` | every 2 s; 10 s silence = disconnected |

- **Host side:** a `NetworkPortal` source in the portal library, so every game gets it: owns guest slots in the software portal,
  marks them as the guest player's (`g_player_slots`), so the portal menu doesn't offer them to
  anyone else and the player menu shows the guest as their player. The menu could show "on the
  guest's portal" for those figures.

## Figure sync and write-back (#3)

The game reads and writes the figure while it is on the portal: it saves about every 30 seconds
and after events such as a level-up, and it writes the two save areas in turn (see
`figure_stats.h`). The rules:

1. **The host's copy is the truth while the figure is placed.** Writes apply to it at once (the
   game reads back what it wrote), and go into a queue to the guest with a sequence number.
2. **The client writes in order** and confirms each block. A write the real portal rejects is
   retried; the queue survives a short disconnect.
3. **On `remove` (lifted on the guest's portal):** the host takes the figure off its software
   portal like a lift, then sends any writes still queued, then a `final` with the whole 1 KB.
   The client compares it with what it wrote and fixes any block that differs.
4. **Disconnect:** the host keeps the guest's figures "on the portal" for 30 s (the game keeps
   playing them); if the guest doesn't come back, it lifts them (the game shows its portal screen
   for that player, as when a figure leaves). The unsent writes and the final 1 KB stay in the
   host's journal (`<figures>/remote/<uid>.dump` plus the queue) and are sent on the next
   connection with that UID; the client applies them only if the real figure is still at the
   state the writes started from (below).
5. **Don't overwrite newer progress:** before applying a journal, the client reads the real
   figure and compares the save areas' sequence counters with the ones the host started from. If
   the real figure has moved on (played elsewhere meanwhile), the journal is not applied; the
   client keeps it as a `.dump` next to a backup and says so.
6. **Backups:** the client saves the figure's 1 KB before the first write of a session
   (`<client folder>/backup/<uid>-<date>.dump`).

Traps and creation crystals need nothing special: the game writes them like any other figure.
Writes still queued when a figure is lifted from the guest's real portal wait in the journal and
reach the figure the next time it is placed. What is not covered: two hosts using the same real
figure at the same time (the second `place` of a UID that is already placed is refused).

## Effort

| Part | Size |
|---|---|
| Portal client (USB portal from the fork, TCP, journal, a small window with status) | medium |
| Host `NetworkPortal` source, pairing in the settings menu, menu integration | medium |
| Write-back with sequence and final check, disconnect handling | medium |
| Tests: two PCs, one real portal each; a fake client for scripted runs | small to medium |

First step if this goes ahead: the fake client (sends a figure dump over TCP) and the host
source, testable on one PC with the existing scripted runs, before any real portal is involved.
