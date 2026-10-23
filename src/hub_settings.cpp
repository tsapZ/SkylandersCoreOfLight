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
  s.fullscreen = IsOn(get("fullscreen"), s.fullscreen);
  s.setup_done = IsOn(get("setup_done"), s.setup_done);
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
      "A game's title music while its card is chosen, straight from the game's disc.",
      "Die Titelmusik eines Spiels, solange seine Karte gew\xC3\xA4hlt ist, direkt von der Disc des "
      "Spiels.",
      settings.title_music));
  page.settings.push_back(Switch("animations", "Animations", "Animationen",
                                 "Screens slide and fade in.", "Bildschirme gleiten und blenden ein.",
                                 settings.animations));
  page.settings.push_back(Switch("fullscreen", "Fullscreen", "Vollbild",
                                 "The launcher fills the screen, for the TV. F11 or Alt+Enter switch it too.",
                                 "Der Hub f\xC3\xBCllt den Bildschirm, f\xC3\xBCr den Fernseher. F11 oder "
                                 "Alt+Enter schalten auch um.",
                                 settings.fullscreen));
  // A button: the report (problem_report.h), made by the launcher when A is pressed on it.
  GameSetting report;
  report.cvar = "report_problem";
  report.en = "Report a problem";
  report.de = "Problem melden";
  report.help_en = "Packs the latest logs and settings of every game, of their setups and of the launcher, "
                   "and a few facts about this PC (Windows, processor, memory, graphics card), into one ZIP "
                   "on the desktop. Your Windows user name and the computer's name are taken out. Send it "
                   "along with what happened.";
  report.help_de = "Packt die letzten Logs und Einstellungen aller Spiele, ihrer Setups und des Hubs sowie "
                   "ein paar Angaben zum PC (Windows, Prozessor, Arbeitsspeicher, Grafikkarte) in eine "
                   "ZIP-Datei auf dem Desktop. Dein Windows-Benutzername und der Name des PCs werden "
                   "entfernt. Schick sie mit und schreib dazu, was passiert ist.";
  report.type = "action";
  report.options = {{"", "Make ZIP", "ZIP erstellen"}};
  page.settings.push_back(report);
  out->pages.push_back(std::move(page));
  return out;
}

}  // namespace coreoflight
