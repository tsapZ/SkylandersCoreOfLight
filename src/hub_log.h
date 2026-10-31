#pragma once

// The launcher's own log (hub_log.cpp): what it did and decided, so a report from a PC the owner
// can't try says what happened: its start (version, Windows or Wine, time zone), the discs it read
// and set up, the update checks and installs, each game's start and end (with the game's own
// message when it stopped with one), the data moves, and the window's focus around a game (SteamOS
// under Proton, a colleague's: the pad did nothing in the launcher after a game, 2026-10-24). One
// line per event: time, level, "area: what happened". %LOCALAPPDATA%\CoreOfLight\coreoflight.log,
// the one before in coreoflight.old.log; "Problem melden" takes it along.

#include <filesystem>
#include <string>

namespace coreoflight {

std::filesystem::path HubLogFile();
// "<time> [info] <line>"; past 256 KB the file becomes coreoflight.old.log and a new one starts.
void HubLog(const std::string& line);
// The same as a warning (something didn't work, the launcher went on) or an error (it couldn't do
// what was asked, or ended): "[warn]", "[error]", so they stand out in a report.
void HubWarn(const std::string& line);
void HubError(const std::string& line);
// Wine's version when the launcher runs under Wine / Proton, else empty.
std::string WineVersion();
// "10.0.26200": the real one (RtlGetVersion), not the one the manifest lets Windows report.
std::string WindowsVersion();

}  // namespace coreoflight
