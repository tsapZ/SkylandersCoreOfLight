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
  s.auto_backup = IsOn(get("auto_backup"), s.auto_backup);
  s.setup_done = IsOn(get("setup_done"), s.setup_done);
  s.games_dir = get("games_dir");
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
  page.settings.push_back(Switch(
      "auto_backup", "Back up saves", "Spielst\xC3\xA4nde sichern",
      "When a game starts and ends and every 60 minutes while it runs, a copy of each of its saves that "
      "changed (about 100 KB each; the newest 10 stay). RT on a card shows them and puts one back.",
      "Beim Start und Ende eines Spiels und alle 60 Minuten w\xC3\xA4hrend es l\xC3\xA4uft eine Kopie jedes "
      "Spielstands, der sich ge\xC3\xA4ndert hat (je etwa 100 KB; die neuesten 10 bleiben). RT auf einer "
      "Karte zeigt sie und holt einen zur\xC3\xBC" "ck.",
      settings.auto_backup));
  // A button: the updates from GitHub (updates.h), looked for when A is pressed on it.
  GameSetting updates;
  updates.cvar = "check_updates";
  updates.en = "Check for updates";
  updates.de = "Nach Updates suchen";
  updates.help_en = "Asks GitHub for new versions of Core of Light and of every game (it does so by itself once "
                    "a day). A newer Core of Light offers to update itself; a game's update shows on its card.";
  updates.help_de = "Fragt bei GitHub nach neuen Versionen von Core of Light und aller Spiele (das passiert auch "
                    "von selbst einmal am Tag). Ein neueres Core of Light bietet an, sich zu aktualisieren; das "
                    "Update eines Spiels steht auf seiner Karte.";
  updates.type = "action";
  updates.options = {{"", "Check now", "Jetzt suchen"}};
  page.settings.push_back(std::move(updates));
  // A button: the report (problem_report.h), made by the launcher when A is pressed on it.
  GameSetting report;
  report.cvar = "report_problem";
  report.en = "Report a problem";
  report.de = "Problem melden";
  report.help_en = "Packs the latest logs and settings of every game, of their setups and of the launcher, "
                   "and a few facts about this PC (Windows, processor, memory, graphics card), into one text "
                   "file on the desktop. Your Windows user name and the computer's name are taken out. Send it "
                   "along with what happened.";
  report.help_de = "Packt die letzten Logs und Einstellungen aller Spiele, ihrer Setups und des Hubs sowie "
                   "ein paar Angaben zum PC (Windows, Prozessor, Arbeitsspeicher, Grafikkarte) in eine "
                   "Textdatei auf dem Desktop. Dein Windows-Benutzername und der Name des PCs werden "
                   "entfernt. Schick sie mit und schreib dazu, was passiert ist.";
  report.type = "action";
  report.options = {{"", "Make the report", "Bericht erstellen"}};
  page.settings.push_back(report);
  out->pages.push_back(std::move(page));
  return out;
}

}  // namespace coreoflight
