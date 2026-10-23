#pragma once

// "Problem melden" (problem_report.cpp; the owner's wish, 2026-10-23: a log to send along): the
// latest logs and settings of every game and of their setups, the launcher's own settings and a
// few facts about the PC (Windows, processor, memory, graphics cards), in one ZIP on the desktop to
// send with a description of what happened. The Windows user name and the computer's name are
// taken out of every file (%USERPROFILE%, %USERNAME%, %COMPUTERNAME%).

#include <filesystem>
#include <string>
#include <vector>

namespace coreoflight {

struct Found;

// Writes the report; its path, or empty (and `error`, in the menus' language) if it couldn't.
std::filesystem::path WriteProblemReport(const std::vector<Found>& found,
                                         const std::filesystem::path& launcher_dir, bool german,
                                         std::string& error);

}  // namespace coreoflight
