// game_install.h's POSIX install/update transaction (games.cpp StartGameInstall): synthetic packages
// in a scratch folder, so nothing here needs a game, its disc or its saves.

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "game_install.h"

namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static std::string ReadFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void WriteFile(const fs::path& p, std::string_view data) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc).write(data.data(), std::streamsize(data.size()));
}

static bool Contains(const std::string& text, std::string_view needle) {
  return text.find(needle) != std::string::npos;
}

static int Count(const std::string& text, std::string_view needle) {
  int count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++count;
  return count;
}

static bool StatFile(const fs::path& p, struct stat& st) { return stat(p.c_str(), &st) == 0; }
static bool StatLink(const fs::path& p, struct stat& st) { return lstat(p.c_str(), &st) == 0; }

static void MakePackage(const fs::path& archive, const fs::path& root) {
  std::error_code ec;
  fs::remove(archive, ec);
  const std::string cmd = "tar -czf '" + archive.string() + "' -C '" + root.string() + "' .";
  CHECK(std::system(cmd.c_str()) == 0);
}

// Runs the script the launcher runs, with the same positional arguments (the game's exe is "game").
static int RunInstall(const fs::path& archive, const fs::path& dest, const std::string& toml,
                      const std::string& ver, const std::string& scale, const std::string& lang,
                      const std::string& res, const fs::path& log) {
  fs::create_directories(log.parent_path());
  std::string script = coreoflight::kGameInstallScript;
  std::vector<std::string> values = {archive.string(), dest.string(), toml, ver, scale, lang, res,
                                     log.string(),   "game"};
  const pid_t pid = fork();
  if (pid == 0) {
    std::vector<char*> argv = {const_cast<char*>("sh"), const_cast<char*>("-c"), script.data(),
                               const_cast<char*>("coreoflight-install")};
    for (std::string& value : values) argv.push_back(value.data());
    argv.push_back(nullptr);
    execv("/bin/sh", argv.data());
    _exit(127);
  }
  if (pid < 0) return -1;
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// The per-target lock file stays behind (unlinking it would race a waiting run); it is not an
// install leftover.
static bool HasLeftover(const fs::path& dir, std::string_view keep) {
  const std::string lock = "." + std::string(keep) + ".lock";
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (name != keep && name != lock) return true;
  }
  return false;
}

int main() {
  const fs::path root = fs::temp_directory_path() / ("coreoflight_game_install_test_" + std::to_string(getpid()));
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  const fs::path games = root / "games";
  const fs::path dest = games / "ssa";
  const fs::path log = root / "install.log";

  // A new install: the package's Game table has no language, so it is added inside that table.
  const fs::path pkg1 = root / "pkg1";
  WriteFile(pkg1 / "game", "exe-1");
  WriteFile(pkg1 / "data.bin", "data-1");
  WriteFile(pkg1 / "coreoflight.toml",
            "[\"Game\"]\n"
            "portal_mode = \"usb\"\n"
            "resolution = \"1280x720\"\n"
            "resolution_scale = 1\n");
  const fs::path archive1 = root / "ssa-1.tar.gz";
  MakePackage(archive1, pkg1);
  CHECK(RunInstall(archive1, dest, "coreoflight.toml", "1.2.3", "2", "german", "1920x1080", log) == 0);
  CHECK(fs::is_regular_file(dest / "game"));
  CHECK(ReadFile(dest / "game") == "exe-1");
  CHECK(ReadFile(dest / "data.bin") == "data-1");
  const std::string fresh_toml = ReadFile(dest / "coreoflight.toml");
  CHECK(Count(fresh_toml, "[\"Game\"]") == 1);
  CHECK(Contains(fresh_toml, "language = \"german\""));
  CHECK(Contains(fresh_toml, "portal_mode = \"hybrid\""));
  CHECK(Contains(fresh_toml, "resolution = \"1920x1080\""));
  CHECK(Contains(fresh_toml, "resolution_scale = 2"));
  CHECK(ReadFile(dest / ".coreoflight-version") == "1.2.3\n");

  // A wrapped package with another settings file: the user's settings, saves and rom stay as they
  // are, with their permissions, and nothing an update leaves behind stays.
  const fs::path pkg2 = root / "pkg2" / "SSARecomp";
  WriteFile(pkg2 / "game", "exe-2");
  WriteFile(pkg2 / "data.bin", "data-2");
  WriteFile(pkg2 / "coreoflight.toml", "[\"Game\"]\nlanguage = \"english\"\n");
  const fs::path archive2 = root / "ssa-2.tar.gz";
  MakePackage(archive2, root / "pkg2");
  WriteFile(dest / "saves" / "user.sav", "SAVE");
  fs::permissions(dest / "saves" / "user.sav",
                  fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
  WriteFile(dest / "rom" / "default.xex", "ROM");
  const std::string user_toml =
      "[\"Game\"]\n"
      "language = \"english\"\n"
      "portal_mode = \"usb\"\n"
      "resolution = \"640x480\"\n"
      "resolution_scale = 1\n";
  WriteFile(dest / "coreoflight.toml", user_toml);
  CHECK(RunInstall(archive2, dest, "coreoflight.toml", "2.0.0", "2", "german", "1920x1080", log) == 0);
  CHECK(ReadFile(dest / "game") == "exe-2");
  CHECK(ReadFile(dest / "data.bin") == "data-2");
  CHECK(ReadFile(dest / "coreoflight.toml") == user_toml);
  CHECK(ReadFile(dest / "saves" / "user.sav") == "SAVE");
  CHECK(fs::status(dest / "saves" / "user.sav").permissions() ==
        (fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read));
  CHECK(ReadFile(dest / "rom" / "default.xex") == "ROM");
  CHECK(ReadFile(dest / ".coreoflight-version") == "2.0.0\n");
  CHECK(!HasLeftover(games, "ssa"));

  // A package without the game's exe is refused before anything is replaced.
  const fs::path pkg3 = root / "pkg3";
  WriteFile(pkg3 / "data.bin", "bad-data");
  const fs::path archive3 = root / "ssa-3.tar.gz";
  MakePackage(archive3, pkg3);
  fs::remove(log, ec);
  CHECK(RunInstall(archive3, dest, "coreoflight.toml", "3.0.0", "2", "german", "1920x1080", log) != 0);
  CHECK(ReadFile(dest / "game") == "exe-2");
  CHECK(ReadFile(dest / "data.bin") == "data-2");
  CHECK(ReadFile(dest / "coreoflight.toml") == user_toml);
  CHECK(ReadFile(dest / "saves" / "user.sav") == "SAVE");
  CHECK(!HasLeftover(games, "ssa"));
  // The failure leaves an actionable line the launcher surfaces (GameInstallError: "ROM: ").
  CHECK(Contains(ReadFile(log), "ROM: "));
  CHECK(Contains(ReadFile(log), "game"));

  // A run killed after moving the old folder aside: the next run puts it back, then updates.
  const fs::path backup = games / ".ssa.rollback";
  fs::rename(dest, backup, ec);
  CHECK(!ec);
  const fs::path pkg4 = root / "pkg4";
  WriteFile(pkg4 / "game", "exe-4");
  const fs::path archive4 = root / "ssa-4.tar.gz";
  MakePackage(archive4, pkg4);
  CHECK(RunInstall(archive4, dest, "coreoflight.toml", "4.0.0", "2", "german", "1920x1080", log) == 0);
  CHECK(ReadFile(dest / "game") == "exe-4");
  CHECK(ReadFile(dest / "coreoflight.toml") == user_toml);
  CHECK(ReadFile(dest / "saves" / "user.sav") == "SAVE");
  CHECK(!fs::exists(backup));
  CHECK(!HasLeftover(games, "ssa"));

  // A finished commit whose old copy was left behind: the old copy goes, the install stays.
  fs::rename(dest, backup, ec);
  CHECK(!ec);
  fs::create_directories(dest);
  WriteFile(dest / "game", "exe-5");
  CHECK(RunInstall(archive4, dest, "coreoflight.toml", "5.0.0", "2", "german", "1920x1080", log) == 0);
  CHECK(ReadFile(dest / "game") == "exe-4");
  CHECK(!fs::exists(backup));

  // A package with a bare [Game] table also gets the key in that table, not a second one.
  const fs::path pkg6 = root / "pkg6";
  WriteFile(pkg6 / "game", "exe-6");
  WriteFile(pkg6 / "coreoflight.toml", "[Game]\nportal_mode = \"usb\"\n");
  const fs::path archive6 = root / "ssa-6.tar.gz";
  MakePackage(archive6, pkg6);
  const fs::path bare = games / "bare";
  CHECK(RunInstall(archive6, bare, "coreoflight.toml", "6.0.0", "2", "german", "1920x1080", log) == 0);
  const std::string bare_toml = ReadFile(bare / "coreoflight.toml");
  CHECK(Count(bare_toml, "[Game]") == 1);
  CHECK(Contains(bare_toml, "language = \"german\""));

  // A second run for the same folder cannot start while the first holds the per-target lock, so it
  // cannot touch the shared rollback folder.
  const fs::path locked = games / "locked";
  CHECK(RunInstall(archive1, locked, "coreoflight.toml", "1.0.0", "2", "german", "1920x1080", log) == 0);
  const fs::path lock_path = games / ".locked.lock";
  const int lock_fd = open(lock_path.c_str(), O_RDWR | O_CREAT, 0600);
  CHECK(lock_fd >= 0);
  CHECK(flock(lock_fd, LOCK_EX | LOCK_NB) == 0);
  fs::remove(log, ec);
  CHECK(RunInstall(archive2, locked, "coreoflight.toml", "9.0.0", "2", "german", "1920x1080", log) != 0);
  CHECK(ReadFile(locked / "game") == "exe-1");
  CHECK(Contains(ReadFile(log), "another install"));
  flock(lock_fd, LOCK_UN);
  close(lock_fd);

  // A package that ships the game's own data (rom) is refused; the live rom stays.
  const fs::path guarded = games / "guarded";
  CHECK(RunInstall(archive1, guarded, "coreoflight.toml", "1.0.0", "2", "german", "1920x1080", log) == 0);
  WriteFile(guarded / "rom" / "default.xex", "ROM-REAL");
  const fs::path pkg_rom = root / "pkg_rom";
  WriteFile(pkg_rom / "game", "exe-rom");
  WriteFile(pkg_rom / "rom" / "default.xex", "REPLACED");
  const fs::path archive_rom = root / "ssa-rom.tar.gz";
  MakePackage(archive_rom, pkg_rom);
  fs::remove(log, ec);
  CHECK(RunInstall(archive_rom, guarded, "coreoflight.toml", "9.1.0", "2", "german", "1920x1080", log) != 0);
  CHECK(ReadFile(guarded / "game") == "exe-1");
  CHECK(ReadFile(guarded / "rom" / "default.xex") == "ROM-REAL");
  CHECK(Contains(ReadFile(log), "rom"));

  // Legitimately symlinked rom and saves survive an update as the same links (the package has
  // neither); moving them into the stage must rename the links, not copy their targets.
  const fs::path external_rom = root / "external-rom";
  WriteFile(external_rom / "default.xex", "LINKED-ROM");
  const fs::path external_saves = root / "external-saves";
  WriteFile(external_saves / "slot.sav", "LINKED-SAVE");
  const fs::path symlinked = games / "symlinked";
  CHECK(RunInstall(archive1, symlinked, "coreoflight.toml", "1.0.0", "2", "german", "1920x1080", log) == 0);
  fs::remove_all(symlinked / "rom", ec);
  fs::create_directory_symlink(external_rom, symlinked / "rom");
  fs::remove_all(symlinked / "saves", ec);
  fs::create_directory_symlink(external_saves, symlinked / "saves");
  struct stat rom_link_before{}, saves_link_before{};
  CHECK(StatLink(symlinked / "rom", rom_link_before));
  CHECK(StatLink(symlinked / "saves", saves_link_before));
  CHECK(RunInstall(archive2, symlinked, "coreoflight.toml", "2.1.0", "2", "german", "1920x1080", log) == 0);
  struct stat rom_link_after{}, saves_link_after{};
  CHECK(StatLink(symlinked / "rom", rom_link_after));
  CHECK(StatLink(symlinked / "saves", saves_link_after));
  CHECK(fs::is_symlink(symlinked / "rom"));
  CHECK(fs::is_symlink(symlinked / "saves"));
  CHECK(rom_link_after.st_dev == rom_link_before.st_dev && rom_link_after.st_ino == rom_link_before.st_ino);
  CHECK(saves_link_after.st_dev == saves_link_before.st_dev && saves_link_after.st_ino == saves_link_before.st_ino);
  CHECK(ReadFile(external_rom / "default.xex") == "LINKED-ROM");
  CHECK(ReadFile(external_saves / "slot.sav") == "LINKED-SAVE");
  CHECK(ReadFile(symlinked / "game") == "exe-2");

  // The game's own data is not copied: a sparse ROM and a save keep their device and inode across
  // an update, and the ROM stays sparse.
  const fs::path kept = games / "kept";
  CHECK(RunInstall(archive1, kept, "coreoflight.toml", "1.0.0", "2", "german", "1920x1080", log) == 0);
  WriteFile(kept / "saves" / "slot1.sav", "SAVE-KEEP");
  fs::create_directories(kept / "rom");
  {
    const int fd = open((kept / "rom" / "default.xex").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0);
    CHECK(ftruncate(fd, 512 * 1024 * 1024) == 0);
    CHECK(close(fd) == 0);
  }
  struct stat rom_before{}, save_before{};
  CHECK(StatFile(kept / "rom" / "default.xex", rom_before));
  CHECK(StatFile(kept / "saves" / "slot1.sav", save_before));
  CHECK(rom_before.st_blocks * 512 < rom_before.st_size);
  CHECK(RunInstall(archive2, kept, "coreoflight.toml", "2.0.0", "2", "german", "1920x1080", log) == 0);
  struct stat rom_after{}, save_after{};
  CHECK(StatFile(kept / "rom" / "default.xex", rom_after));
  CHECK(StatFile(kept / "saves" / "slot1.sav", save_after));
  CHECK(rom_after.st_dev == rom_before.st_dev && rom_after.st_ino == rom_before.st_ino);
  CHECK(save_after.st_dev == save_before.st_dev && save_after.st_ino == save_before.st_ino);
  CHECK(rom_after.st_size == rom_before.st_size);
  CHECK(ReadFile(kept / "saves" / "slot1.sav") == "SAVE-KEEP");
  CHECK(ReadFile(kept / "game") == "exe-2");
  CHECK(!fs::exists(games / ".kept.extract"));
  CHECK(!fs::exists(games / ".kept.rollback"));

  // A commit killed after moving the old install aside and its rom and saves into the staging
  // folder: the next run restores the old install and its data (same device and inode) before it
  // updates, and clears the staging folder only after that.
  const fs::path crashed = games / "crashed";
  CHECK(RunInstall(archive1, crashed, "coreoflight.toml", "1.0.0", "2", "german", "1920x1080", log) == 0);
  WriteFile(crashed / "saves" / "keep.sav", "OLD-SAVE");
  WriteFile(crashed / "rom" / "default.xex", "OLD-ROM");
  struct stat crashed_rom{}, crashed_save{};
  CHECK(StatFile(crashed / "rom" / "default.xex", crashed_rom));
  CHECK(StatFile(crashed / "saves" / "keep.sav", crashed_save));
  const fs::path crashed_work = games / ".crashed.extract";
  const fs::path crashed_backup = games / ".crashed.rollback";
  fs::create_directories(crashed_work / "stage");
  fs::rename(crashed / "rom", crashed_work / "stage" / "rom", ec);
  CHECK(!ec);
  fs::rename(crashed / "saves", crashed_work / "stage" / "saves", ec);
  CHECK(!ec);
  fs::rename(crashed, crashed_backup, ec);
  CHECK(!ec);
  // A failing run right after the interruption still restores the old install, data included.
  fs::remove(log, ec);
  CHECK(RunInstall(archive3, crashed, "coreoflight.toml", "2.1.0", "2", "german", "1920x1080", log) != 0);
  CHECK(ReadFile(crashed / "game") == "exe-1");
  CHECK(ReadFile(crashed / "rom" / "default.xex") == "OLD-ROM");
  CHECK(ReadFile(crashed / "saves" / "keep.sav") == "OLD-SAVE");
  struct stat crashed_restored_rom{}, crashed_restored_save{};
  CHECK(StatFile(crashed / "rom" / "default.xex", crashed_restored_rom));
  CHECK(StatFile(crashed / "saves" / "keep.sav", crashed_restored_save));
  CHECK(crashed_restored_rom.st_dev == crashed_rom.st_dev && crashed_restored_rom.st_ino == crashed_rom.st_ino);
  CHECK(crashed_restored_save.st_dev == crashed_save.st_dev && crashed_restored_save.st_ino == crashed_save.st_ino);
  CHECK(!fs::exists(crashed_backup));
  CHECK(!fs::exists(crashed_work));
  // The same recovery, then a good update: the old data moves through with its device and inode.
  CHECK(RunInstall(archive2, crashed, "coreoflight.toml", "2.2.0", "2", "german", "1920x1080", log) == 0);
  CHECK(ReadFile(crashed / "game") == "exe-2");
  CHECK(ReadFile(crashed / "rom" / "default.xex") == "OLD-ROM");
  CHECK(ReadFile(crashed / "saves" / "keep.sav") == "OLD-SAVE");
  struct stat crashed_rom_after{}, crashed_save_after{};
  CHECK(StatFile(crashed / "rom" / "default.xex", crashed_rom_after));
  CHECK(StatFile(crashed / "saves" / "keep.sav", crashed_save_after));
  CHECK(crashed_rom_after.st_dev == crashed_rom.st_dev && crashed_rom_after.st_ino == crashed_rom.st_ino);
  CHECK(crashed_save_after.st_dev == crashed_save.st_dev && crashed_save_after.st_ino == crashed_save.st_ino);
  CHECK(!fs::exists(crashed_backup));
  CHECK(!fs::exists(crashed_work));
  CHECK(!fs::exists(games / ".crashed.extract"));
  CHECK(!fs::exists(games / ".crashed.rollback"));

  fs::remove_all(root, ec);
  if (g_failures == 0) std::printf("game_install: all tests passed\n");
  return g_failures == 0 ? 0 : 1;
}
