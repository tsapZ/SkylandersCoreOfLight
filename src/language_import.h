#pragma once

// Imaginators in another language, from the player's own PS3 disc of the game (the owner's wish,
// 2026-10-25: "nur die PS3-ISO einbinden, der Hub holt sich die Sprachpakete"). The European Xbox
// 360 disc that the port is built for only has Spanish and Italian; the European PS3 disc has ten
// languages. The PS3 disc's archives are the same as the 360's (same paths, same IGA format,
// big-endian), only their platform data differs, so the text, the voices and the movies' spoken
// track can be taken from it, as ImaginatorsRecomp's tools/german_text.py does (this is a C++ port
// of it, so nobody needs Python):
//
//   - text: every 360 archive with Spanish text (`*_es.lng`) gets the PS3 twin's files of the
//     language appended (stored), and its Spanish entries are pointed at them (IGZ platform word
//     6 -> 5). The game falls back to Spanish when its language isn't on the disc, so it shows the
//     imported one;
//   - the licence: the 360 disc has it in every language already (softwareLicense.pak);
//   - the 360's own texts (xenonPlatformStrings_es.lng: title screen, save slots, Xbox Live), which
//     the PS3 disc has as ps3/pcPlatformStrings: a copy made by key where the discs' Spanish agree,
//     a few written here;
//   - the voices (`<bank>_es.pak`): the PS3 banks are MP3, which this game's FMOD can play;
//   - the movies' spoken track (Bink 2, the same audio packets on both discs).
//
// Everything changed can be undone: before an archive is touched, its length and its original
// table of contents entries go into rom\archives\german_text_backup\ (the same files as the Python
// tool, so either can undo the other's run), the original movies into its movies\ folder.
//
// The PS3 disc is read straight from its .iso (its ISO 9660 directory; the image also has UDF),
// or from a folder with its PS3_GAME\USRDIR (or that folder's archives) unpacked.

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace coreoflight {

// Whether `source` (an .iso or a folder) is a PS3 disc of Imaginators.
bool IsImaginatorsPs3Disc(const std::filesystem::path& source);
// Whether the file is a PS3 disc image of any game (its volume is "PS3VOLUME").
bool IsPs3Disc(const std::filesystem::path& iso);

// The language imported into Imaginators' disc folder (`rom`): "en", "de", "?" for an import
// without a note (the Python tool's, or one that didn't finish), empty when the disc is as it came.
std::string ImportedLanguage(const std::filesystem::path& rom);

class LanguageImport {
 public:
  // The languages offered: their code ("en"), their own name ("Deutsch"), English and German names.
  struct Language {
    const char* code;
    const char* own;
    const char* name_en;
    const char* name_de;
  };
  static const Language* Languages(size_t* count);

  // Starts on a thread of its own: `source` the PS3 disc (.iso or folder), `rom` the installed
  // Imaginators' disc folder. An earlier import of another language is undone first.
  void Start(const std::filesystem::path& source, const std::filesystem::path& rom, const std::string& language,
             bool german);
  // Undoes every import into `rom` (also on a thread of its own).
  void StartRestore(const std::filesystem::path& rom, bool german);
  bool running() const { return shared_ && !shared_->done; }
  // How far it is, 0..1, and what it does now (for the note at the bottom right).
  float progress() const { return shared_ ? shared_->progress.load() : 0.0f; }
  std::string step() const;
  // What came of it, once, when it is done; else empty.
  std::string TakeSummary();

 private:
  struct Shared {
    std::atomic<bool> done{false};
    std::atomic<float> progress{0.0f};
    std::mutex mutex;
    std::string step;
    std::string summary;
    bool taken = false;
  };
  std::shared_ptr<Shared> shared_;
};

}  // namespace coreoflight
