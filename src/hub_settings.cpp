#include "hub_settings.h"

#include <windows.h>

#include <unordered_map>

#include "games.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

constexpr char kCategory[] = "Hub";

std::string Unquoted(const std::string& v) {
  return v.size() >= 2 && v.front() == '"' && v.back() == '"' ? v.substr(1, v.size() - 2) : v;
}

bool IsOn(const std::string& v, bool fallback) {
  if (v == "true" || v == "1") return true;
  if (v == "false" || v == "0") return false;
  return fallback;
}

std::string Bool(bool b) { return b ? "true" : "false"; }

GameSetting Switch(const char* key, const char* en, const char* de, const char* help_en, const char* help_de,
                   bool value) {
  GameSetting s;
  s.cvar = key;
  s.en = en;
  s.de = de;
  s.help_en = help_en;
  s.help_de = help_de;
  s.type = "bool";
  s.category = kCategory;
  s.value = Bool(value);
  return s;
}

}  // namespace

fs::path HubSettingsFile() { return DocumentsDir() / "Skylanders" / "coreoflight.toml"; }

HubSettings LoadHubSettings() {
  HubSettings s;
  const auto values = ReadIni(HubSettingsFile());
  auto get = [&](const char* key) {
    auto it = values.find(key);
    return it == values.end() ? std::string() : Unquoted(it->second);
  };
  if (const std::string language = get("language"); language == "german" || language == "english") {
    s.language = language;
  }
  s.figures_only_mine = IsOn(get("figures_only_mine"), s.figures_only_mine);
  s.figures_marks = IsOn(get("figures_marks"), s.figures_marks);
  s.title_music = IsOn(get("title_music"), s.title_music);
  s.animations = IsOn(get("animations"), s.animations);
  return s;
}

bool HubGerman(const HubSettings& s) {
  if (s.language == "german") return true;
  if (s.language == "english") return false;
  return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN;
}

std::shared_ptr<GameSettings> HubSettingsPages(const HubSettings& settings) {
  auto out = std::make_shared<GameSettings>();
  out->config_file = HubSettingsFile();
  GameSettingsPage page{"Core of Light", "Core of Light", {}};
  GameSetting language;
  language.cvar = "language";
  language.en = "Language";
  language.de = "Sprache";
  language.help_en = "The language of the launcher's menus. Automatic follows Windows. Each game's own "
                     "language is on its card (Y).";
  language.help_de = "Die Sprache der Men\xC3\xBCs im Hub. Automatisch folgt Windows. Die Sprache jedes "
                     "Spiels steht auf seiner Karte (Y).";
  language.type = "string";
  language.category = kCategory;
  language.value = settings.language;
  language.options = {{"auto", "Automatic", "Automatisch"}, {"german", "Deutsch", "Deutsch"},
                      {"english", "English", "English"}};
  page.settings.push_back(language);
  page.settings.push_back(Switch(
      "figures_only_mine", "Figures: only mine", "Figuren: nur meine",
      "On: the figure library opens on your figures. Off: on every Skylander, yours highlighted. X "
      "switches in the library too.",
      "An: Die Figuren-Bibliothek \xC3\xB6" "ffnet mit deinen Figuren. Aus: mit allen Skylandern, deine "
      "hervorgehoben. X wechselt auch in der Bibliothek.",
      settings.figures_only_mine));
  page.settings.push_back(Switch(
      "figures_marks", "\"I own it\" marks", "\xE2\x80\x9EHab ich\xE2\x80\x9C-Markierungen",
      "On: Y marks a Skylander you have no file of (a figure on the shelf) as yours. Off: only figure "
      "files count, and marks are left out.",
      "An: Y markiert einen Skylander, von dem du keine Datei hast (Figur im Regal), als deinen. Aus: "
      "Nur Figurendateien z\xC3\xA4hlen, Markierungen bleiben weg.",
      settings.figures_marks));
  page.settings.push_back(Switch(
      "title_music", "Title music", "Titelmusik",
      "A game's title music while its card is chosen, once the game has run to its title screen.",
      "Die Titelmusik eines Spiels, solange seine Karte gew\xC3\xA4hlt ist, sobald das Spiel einmal bis "
      "zum Titelbild lief.",
      settings.title_music));
  page.settings.push_back(Switch("animations", "Animations", "Animationen",
                                 "Screens slide and fade in.", "Bildschirme gleiten und blenden ein.",
                                 settings.animations));
  out->pages.push_back(std::move(page));
  return out;
}

}  // namespace coreoflight
