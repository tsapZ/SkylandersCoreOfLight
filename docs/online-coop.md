# Online co-op with two real portals (proposal)

Status: proposal, nothing built (Phase 4, #1 #2 #3). Building it is a separate decision.
Written for SwapForceRecomp and moved here (its #28, #29, #30): every recompiled game has the
same software portal from `skylanders-portal`, so the design applies to all of them.

## Goal

Two players in two homes play a recompiled Skylanders game together (Swap Force first). The
host runs the game; the guest sees and
hears it and plays with their own controller, and puts **their own real figures on their own
portal**. Progress the game writes (XP, gold, upgrades, hats) ends up on the guest's real figure,
as if it had been on the host's portal.

## Shape

```
 guest PC                                        host PC
 ┌───────────────────────┐   video, audio,       ┌──────────────────────────────┐
 │ Parsec / Steam client │◄──controller──────────┤ Parsec / Steam host          │
 │                       │   (their channel)     │                              │
 │ portal client         │                       │ the game (its own exe)       │
 │  ├ real USB portal    │   figure channel      │  ├ software portal           │
 │  │ (skylanders-portal)│◄─────────────────────►│  │  slots for the guest      │
 │  └ figure journal     │   (ours, small)       │  └ write callback            │
 └───────────────────────┘                       └──────────────────────────────┘
```

- **Picture, sound, controller:** an existing remote play tool (below). The guest's controller
  arrives on the host as a normal controller, so the game, the portal menu and the player menu
  see a second player like a local one.
- **Figures:** our own small channel, not the remote play tool. The guest runs a *portal client*
  (a small exe built from the same `skylanders-portal` library this app uses): it reads the
  guest's real portal (the same USB code as `portal_mode = usb`) and sends figure events; the host
  puts them into its software portal (`SoftwarePortal::PlaceFigure` / `RemoveFigure`, as the
  portal menu does today), in slots that belong to the guest's player.
- **Writes back:** the host's software portal already reports every block the game writes
  (`SoftwarePortal::SetWriteCallback`). The host forwards them; the portal client writes them to
  the real figure (the USB portal's write path).

Nothing of the game changes; the host side is a new portal source next to the menu, the guest
side a separate small program.

## Remote play options (#2)

| | Parsec | Steam Remote Play Together |
|---|---|---|
| Controllers | guest pads arrive through Parsec's virtual USB driver as an Xbox 360 pad (default) or a DualShock 4 [1] | through Steam Input, as a Steam virtual pad |
| Needs | Parsec on both PCs; the host shares the screen or the app | the game in the host's Steam library (a non-Steam shortcut works for Remote Play, not reliably for "Together"), Steam on the guest |
| USB devices other than pads | not documented in Parsec's own help; third-party blogs claim more [2], unverified | not supported |
| Latency | built for low latency, up to 240 FPS streams [2] | "works really well" with a good connection [2] |

Conclusion: neither passes a Portal of Power through reliably, which is why figures get their own
channel. Parsec is the better fit: no Steam dependency, the guest's pad is a plain Xbox 360 pad on
the host, and 240 FPS streams suit the high frame rate work. Steam Remote Play is the fallback
when both players already use Steam. A third option without any streaming tool would be two
copies of the game in a network session, but the games have no online play; that would mean
writing netcode for an engine we don't know well enough.

[1] Parsec help, "Setup Gamepad" (support.parsec.app, article 32381705301908).
[2] AnyViewer remote gaming comparisons 2026 (anyviewer.com), secondary sources.

## Figure channel

- **Transport:** one TCP connection from the guest's portal client to the host (port configurable,
  default e.g. 27015). The guest needs to reach the host: port forwarding, or a VPN both already
  use (Tailscale, ZeroTier). A relay is possible later; the data is tiny (1 KB per figure,
  16 bytes per write).
- **Pairing:** the host shows a short code in the settings menu; the client sends it first.
  Nothing but figure messages is accepted, every size is checked, at most 8 guest figures.
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

What is not covered: two hosts using the same real figure at the same time (the second `place`
of a UID that is already placed is refused), traps and their villains (they write differently,
see the fork's `trap_villain`), and the Imaginators' creation crystals.

## Effort

| Part | Size |
|---|---|
| Portal client (USB portal from the fork, TCP, journal, a small window with status) | medium |
| Host `NetworkPortal` source, pairing in the settings menu, menu integration | medium |
| Write-back with sequence and final check, disconnect handling | medium |
| Tests: two PCs, one real portal each; a fake client for scripted runs | small to medium |

First step if this goes ahead: the fake client (sends a figure dump over TCP) and the host
source, testable on one PC with the existing scripted runs, before any real portal is involved.
