#pragma once

// The launcher's own short log (hub_log.cpp): its start, each game's start and end, the window's
// focus and the controllers coming and going, so a report from a PC the owner can't try (SteamOS
// under Proton, a colleague's: the pad did nothing in the launcher after a game, 2026-10-24) says
// what happened. %LOCALAPPDATA%\CoreOfLight\coreoflight.log; "Problem melden" takes it along.

#include <filesystem>
#include <string>

namespace coreoflight {

std::filesystem::path HubLogFile();
// A line with the time in front; the file starts afresh once it is past 256 KB.
void HubLog(const std::string& line);
// Wine's version when the launcher runs under Wine / Proton, else empty.
std::string WineVersion();

}  // namespace coreoflight
