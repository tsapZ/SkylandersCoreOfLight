#pragma once

// Profiles like an Xbox account (#4, profiles.cpp): a name, a picture (one of the Skylanders'), and
// their own saves, achievements and figures.
//
// The main profile is the data as the games keep it without the launcher: Documents\<game>recomp
// (saves, achievements) and Documents\Skylanders\Figures. Every other profile is a folder
// Documents\Skylanders\Profiles\<id> that stands in for Documents: the launcher starts a game with
// --user_data_root=<folder>\<game>recomp, and the game itself then keeps that profile's figures in
// <folder>\Skylanders\Figures and its pictures in <folder>\Skylanders\Pictures (the games put
// both next to their user data's folder). The shader cache stays the main profile's
// (--cache_root), so a new profile doesn't build it again. Nothing in the games changes: the game
// still calls its player "User".

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace coreoflight {

struct Game;

struct Profile {
  std::string id;    // its folder under Profiles; empty for the main profile
  std::string name;  // UTF-8
  bool has_picture = false;
  uint16_t picture_id = 0, picture_variant = 0;  // a figure's picture (FigurePicture)
  std::filesystem::path root;  // what stands in for Documents: Documents itself for the main one
};

// The profiles, the main one first, read from Documents\Skylanders\Profiles.
std::vector<Profile> LoadProfiles(bool german);
// The profile the launcher uses; read again by LoadProfiles.
const Profile& ActiveProfile();
bool IsMainProfile(const Profile& p);
// Makes it the one used (remembered in Profiles\profiles.ini).
void SetActiveProfile(const Profile& p);

// A new profile's folder with its own empty figure folder; its id, or empty on failure.
std::string CreateProfile(const std::string& name);
// Name and picture written to its profile.ini (the main profile's to profiles.ini).
bool SaveProfile(const Profile& p);
// The profile's folder into the recycle bin (saves and figures with it); never the main one.
bool DeleteProfile(const Profile& p);

// The profile's figure folder and its games' picture folder (Skylanders\Pictures).
std::filesystem::path ProfileFiguresDir(const Profile& p);
std::filesystem::path ProfilePicturesDir(const Profile& p);
// Whether the profile has a save of the game (its user data's save folder has something).
bool HasSave(const Profile& p, const std::filesystem::path& exe);
// When the profile last saved the game: the newest file in that save folder, as a Windows file
// time; 0 if there is none.
uint64_t LastSaved(const Profile& p, const std::filesystem::path& exe);

// Copies of `from`'s figures into `to`'s figure folder, the same file names: as they are, or as
// new figures (the same Skylanders back at the start, new serial numbers). Figures already there
// stay. The number copied.
int CopyFigures(const Profile& from, const Profile& to, bool as_new);

// The command line the launcher adds for the active profile when it starts a game's exe: nothing
// for the main profile.
std::wstring ProfileArguments(const std::filesystem::path& exe);

}  // namespace coreoflight
