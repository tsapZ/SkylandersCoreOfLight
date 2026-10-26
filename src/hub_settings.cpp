#include "hub_settings.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
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

fs::path HubSettingsFile() { return SkylandersDir() / "coreoflight.toml"; }

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
  s.figures_progress = IsOn(get("figures_progress"), s.figures_progress);
  s.title_music = IsOn(get("title_music"), s.title_music);
  if (const std::string v = get("music_volume"); !v.empty() && std::all_of(v.begin(), v.end(), ::isdigit) && v.size() < 4) {
    s.music_volume = std::clamp(std::stoi(v), 0, 100);
  }
  s.animations = IsOn(get("animations"), s.animations);
  s.fullscreen = IsOn(get("fullscreen"), s.fullscreen);
  s.auto_backup = IsOn(get("auto_backup"), s.auto_backup);
  s.auto_update_games = IsOn(get("auto_update_games"), s.auto_update_games);
  s.setup_done = IsOn(get("setup_done"), s.setup_done);
  s.games_dir = get("games_dir");
  s.old_data_kept = IsOn(get("old_data_kept"), s.old_data_kept);
  for (int p = 0; p < 2; ++p) {
    const std::string v = get(p == 0 ? "pad_rumble_p1" : "pad_rumble_p2");
    if (!v.empty() && v.size() < 4 && std::all_of(v.begin(), v.end(), ::isdigit)) {
      s.pad_rumble[p] = std::clamp(std::stoi(v), 0, 100);
    }
  }
  return s;
}

bool HubGerman(const HubSettings& s) {
  if (s.language == "german") return true;
  if (s.language == "english") return false;
  return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN;
}

void GamesFolderLabels(std::string& en, std::string& de) {
  const std::u8string u8 = GamesFolder().u8string();
  const std::string path(u8.begin(), u8.end());
  en = GamesFolderChosen() ? path : "Default: " + path;
  de = GamesFolderChosen() ? path : "Standard: " + path;
}

void DataFolderLabels(std::string& en, std::string& de) {
  const std::u8string u8 = SkylandersDir().u8string();
  const std::string path(u8.begin(), u8.end());
  std::error_code ec;
  const bool standard = !DocumentsMoved() && SkylandersDir() == DocumentsDir() / "Skylanders";
  en = standard ? "Default: " + path : path;
  de = standard ? "Standard: " + path : path;
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
      "figures_progress", "Figures: collection progress", "Figuren: Sammlungsstand",
      "For collectors: each tab of the figure library says how many of its figures you have, e.g. "
      "84/120, and the library shows every figure, the ones you don't have yet greyed.",
      "F\xC3\xBCr Sammler: Jeder Reiter der Figuren-Bibliothek zeigt, wie viele seiner Figuren du hast, "
      "z. B. 84/120, und die Bibliothek zeigt alle Figuren, die fehlenden ausgegraut.",
      settings.figures_progress));
  page.settings.push_back(Switch(
      "title_music", "Title music", "Titelmusik",
      "A game's title music while its card is chosen, straight from the game's disc.",
      "Die Titelmusik eines Spiels, solange seine Karte gew\xC3\xA4hlt ist, direkt von der Disc des "
      "Spiels.",
      settings.title_music));
  {
    GameSetting volume;
    volume.cvar = "music_volume";
    volume.en = "Music volume";
    volume.de = "Musik-Lautst\xC3\xA4rke";
    volume.help_en = "How loud the title music plays in the launcher, heard while you change it: left / right "
                     "(faster when held) or drag the bar. The games' own volume is in their settings.";
    volume.help_de = "Wie laut die Titelmusik im Hub spielt, schon beim Verstellen zu h\xC3\xB6ren: links / rechts "
                     "(gehalten schneller) oder den Balken ziehen. Die Lautst\xC3\xA4rke der Spiele steht in ihren "
                     "Einstellungen.";
    volume.type = "int";
    volume.category = kCategory;
    volume.value = std::to_string(settings.music_volume);
    // A slider 0-100 % (the owner, #24), no fixed steps.
    volume.slider_min = 0;
    volume.slider_max = 100;
    volume.slider_unit = " %";
    page.settings.push_back(std::move(volume));
  }
  page.settings.push_back(Switch("animations", "Animations", "Animationen",
                                 "Screens slide and fade in; sparks of light run round the chosen game's card and "
                                 "the profile picture.",
                                 "Bildschirme gleiten und blenden ein; Lichtfunken laufen um die gew\xC3\xA4hlte "
                                 "Spielkarte und das Profilbild.",
                                 settings.animations));
  page.settings.push_back(Switch("fullscreen", "Fullscreen (also at start)", "Vollbild (auch beim Start)",
                                 "The launcher fills the screen and starts that way, for the TV or a handheld. F11 or "
                                 "Alt+Enter switch it too; --fullscreen on its command line (Steam's launch options) "
                                 "starts it in fullscreen whatever this says.",
                                 "Der Hub f\xC3\xBCllt den Bildschirm und startet auch so, f\xC3\xBCr den Fernseher "
                                 "oder einen Handheld. F11 oder Alt+Enter schalten auch um; --fullscreen in seiner "
                                 "Befehlszeile (Steams Startoptionen) startet ihn immer im Vollbild.",
                                 settings.fullscreen));
  page.settings.push_back(Switch(
      "auto_backup", "Back up saves", "Spielst\xC3\xA4nde sichern",
      "When a game starts and ends and every 60 minutes while it runs, a copy of each of its saves that "
      "changed (about 100 KB each; the newest 10 stay). RT on a card shows them and puts one back.",
      "Beim Start und Ende eines Spiels und alle 60 Minuten w\xC3\xA4hrend es l\xC3\xA4uft eine Kopie jedes "
      "Spielstands, der sich ge\xC3\xA4ndert hat (je etwa 100 KB; die neuesten 10 bleiben). RT auf einer "
      "Karte zeigt sie und holt einen zur\xC3\xBC" "ck.",
      settings.auto_backup));
  // A button: the games folder (games.h GamesFolder), chosen in the launcher's folder browser; the
  // games are looked for there again at once (copied ones too: another drive, a network drive, a
  // Linux folder under Proton).
  GameSetting folder;
  folder.cvar = "choose_games_dir";
  folder.en = "Games folder";
  folder.de = "Spiele-Ordner";
  folder.help_en = "Where your games are and where new ones go. The launcher finds the games in this folder and "
                   "in its subfolders, with their disc, also ones copied there from another PC (another drive, a "
                   "network drive, a Linux folder when it runs with Proton). A game installed somewhere else stays "
                   "where it is.";
  folder.help_de = "Wo deine Spiele liegen und wohin neue kommen. Der Hub findet die Spiele in diesem Ordner und "
                   "seinen Unterordnern, mit ihrer Disc, auch von einem anderen PC dorthin kopierte (ein anderes "
                   "Laufwerk, ein Netzlaufwerk, ein Linux-Ordner, wenn er mit Proton l\xC3\xA4uft). Ein woanders "
                   "installiertes Spiel bleibt, wo es ist.";
  folder.type = "action";
  {
    std::string en, de;
    GamesFolderLabels(en, de);
    folder.options = {{"", en, de}};
  }
  page.settings.push_back(std::move(folder));
  // A button: the data folder (data_move.h), picked in the same folder browser and moved there with
  // everything in it.
  GameSetting data;
  data.cvar = "choose_data_dir";
  data.en = "Data folder";
  data.de = "Spieldaten-Ordner";
  data.help_en = "Where the games keep your saves, achievements and figures, and where the launcher keeps "
                 "its profiles and settings: one folder for all six games. Picking another one moves "
                 "everything there; the old folder is deleted only when you say so.";
  data.help_de = "Wo die Spiele deine Spielst\xC3\xA4nde, Erfolge und Figuren ablegen und der Hub seine "
                 "Profile und Einstellungen: ein Ordner f\xC3\xBCr alle sechs Spiele. Ein anderer Ordner "
                 "nimmt alles mit; der alte wird erst gel\xC3\xB6scht, wenn du es sagst.";
  data.type = "action";
  {
    std::string en, de;
    DataFolderLabels(en, de);
    data.options = {{"", en, de}};
  }
  page.settings.push_back(std::move(data));
  // Two buttons: everything that's the player's into one file on the desktop, and back on a new PC
  // (data_transfer.h, #40).
  GameSetting export_data;
  export_data.cvar = "export_data";
  export_data.en = "Export my data";
  export_data.de = "Meine Daten exportieren";
  export_data.help_en = "For a new PC: your figures, saves (with their copies), achievements, profiles, the "
                        "launcher's settings and each game's settings in one file on the desktop. Not in it: the "
                        "games (set them up from your discs again) and the pictures and HD packs (they come again).";
  export_data.help_de = "F\xC3\xBCr einen neuen PC: deine Figuren, Spielst\xC3\xA4nde (mit ihren Kopien), Erfolge, "
                        "Profile, die Einstellungen des Hubs und jedes Spiels in einer Datei auf dem Desktop. Nicht "
                        "drin: die Spiele (richte sie wieder von deinen Discs ein) und die Bilder und HD-Pakete "
                        "(die kommen wieder).";
  export_data.type = "action";
  export_data.options = {{"", "To the desktop", "Auf den Desktop"}};
  page.settings.push_back(std::move(export_data));
  GameSetting import_data;
  import_data.cvar = "import_data";
  import_data.en = "Import my data";
  import_data.de = "Meine Daten importieren";
  import_data.help_en = "Pick the file from \"Export my data\". What's already here stays: nothing is overwritten. "
                        "Only when a game has settings of its own here already, you're asked whether the file's "
                        "replace them.";
  import_data.help_de = "W\xC3\xA4hle die Datei aus \xE2\x80\x9EMeine Daten exportieren\xE2\x80\x9C. Was schon da "
                        "ist, bleibt: Nichts wird \xC3\xBC" "berschrieben. Nur wenn ein Spiel hier schon eigene "
                        "Einstellungen hat, wirst du gefragt, ob die aus der Datei sie ersetzen.";
  import_data.type = "action";
  import_data.options = {{"", "Choose the file", "Datei w\xC3\xA4hlen"}};
  page.settings.push_back(std::move(import_data));
  // A button: Imaginators figures from a folder (figure_import.h, #34), checked by the game itself.
  GameSetting import;
  import.cvar = "import_im_figures";
  import.en = "Import Imaginators figures";
  import.de = "Imaginators-Figuren importieren";
  import.help_en = "Imaginators' own toys (senseis, creation crystals, chests) can't be made, only copied from "
                   "real figures. Pick a folder with such figure files (subfolders count too): Imaginators "
                   "checks each one itself, and only the ones it accepts are added to your figures (with their "
                   "file names). A figure you have already stays as it is.";
  import.help_de = "Die eigenen Spielzeuge von Imaginators (Sensei, Kristalle, Truhen) lassen sich nicht "
                   "erstellen, nur von echten Figuren kopieren. W\xC3\xA4hle einen Ordner mit solchen "
                   "Figurendateien (Unterordner z\xC3\xA4hlen mit): Imaginators pr\xC3\xBC" "ft jede selbst, und nur "
                   "die, die es annimmt, kommen zu deinen Figuren (mit ihrem Dateinamen). Eine Figur, die du "
                   "schon hast, bleibt, wie sie ist.";
  import.type = "action";
  import.options = {{"", "Choose a folder", "Ordner w\xC3\xA4hlen"}};
  page.settings.push_back(std::move(import));
  // A button: Imaginators in another language from its PS3 disc (language_import.h; the owner's
  // wish, 2026-10-25); its value says which one it speaks (LauncherUi::LanguageLabel).
  GameSetting im_language;
  im_language.cvar = "im_language";
  im_language.en = "Imaginators language";
  im_language.de = "Imaginators-Sprache";
  im_language.help_en = "The European Xbox 360 disc of Imaginators only speaks Spanish and Italian. Your PS3 disc of "
                        "the game has more: pick English, German or French, then the PS3 disc (a decrypted .iso or "
                        "its unpacked folder), and the launcher puts that language's texts, voices and movies into "
                        "your Imaginators. About a minute; your saves stay. The disc's own languages come back here "
                        "too.";
  im_language.help_de = "Die europ\xC3\xA4ische Xbox-360-Disc von Imaginators spricht nur Spanisch und Italienisch. "
                        "Deine PS3-Disc des Spiels hat mehr: w\xC3\xA4hle Englisch, Deutsch oder Franz\xC3\xB6sisch, "
                        "dann die PS3-Disc (eine entschl\xC3\xBCsselte .iso oder ihren entpackten Ordner), und der Hub "
                        "bringt Texte, Stimmen und Filme dieser Sprache in dein Imaginators. Etwa eine Minute; "
                        "Spielst\xC3\xA4nde bleiben. Hier kommen auch die Sprachen der Disc zur\xC3\xBC" "ck.";
  im_language.type = "action";
  im_language.options = {{"", "Choose", "W\xC3\xA4hlen"}};
  page.settings.push_back(std::move(im_language));
  // The games' updates without asking (#27, the owner's wish: on unless someone wants to keep a version).
  page.settings.push_back(Switch(
      "auto_update_games", "Update games by themselves", "Spiele automatisch aktualisieren",
      "On: a game's new version, found by the daily check, is installed right away, one game after the "
      "other (a running game first ends).\nOff: the update waits on the game's card until you pick it.",
      "An: Die neue Version eines Spiels, gefunden bei der t\xC3\xA4glichen Suche, wird gleich installiert, "
      "ein Spiel nach dem anderen (ein laufendes erst nach dem Beenden).\nAus: Das Update wartet auf der "
      "Karte des Spiels, bis du es w\xC3\xA4hlst.",
      settings.auto_update_games));
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
  // The controllers (#35, the owner's idea: a click on a pad at the top): the vibration strength per
  // player, the same setting as each game's F4 > Controller, set here once for all games.
  GameSettingsPage pads{"Controller", "Controller", {}};
  for (int p = 0; p < 2; ++p) {
    GameSetting rumble;
    rumble.cvar = p == 0 ? "pad_rumble_p1" : "pad_rumble_p2";
    rumble.en = p == 0 ? "Vibration, player 1" : "Vibration, player 2";
    rumble.de = p == 0 ? "Vibration, Spieler 1" : "Vibration, Spieler 2";
    rumble.help_en = "How strongly this player's controller vibrates in every game (the same as F4 > Controller "
                     "in a game). The controller buzzes briefly at each change, so you feel the new strength; "
                     "saving writes it into all your games.";
    rumble.help_de = "Wie stark der Controller dieses Spielers in allen Spielen vibriert (wie F4 > Controller "
                     "im Spiel). Bei jeder \xC3\x84nderung vibriert er kurz, so sp\xC3\xBCrst du die neue "
                     "St\xC3\xA4rke; Speichern tr\xC3\xA4gt sie in alle deine Spiele ein.";
    rumble.type = "int";
    rumble.category = kCategory;
    rumble.value = std::to_string(settings.pad_rumble[p] < 0 ? 100 : settings.pad_rumble[p]);
    for (int v = 0; v <= 100; v += 10) {
      const std::string label = v == 0 ? std::string() : std::to_string(v) + " %";
      rumble.options.push_back({std::to_string(v), v == 0 ? "Off" : label, v == 0 ? "Aus" : label});
    }
    pads.settings.push_back(std::move(rumble));
  }
  out->pages.push_back(std::move(pads));
  return out;
}

}  // namespace coreoflight
