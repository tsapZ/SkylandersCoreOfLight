# Xbox-360-Decompilierung: Recherche und Einschätzung (2026-10-22)

Recherche der Nacht vom 21. auf den 22.10.2026 als Grundlage für Schritt 5 des Gesamtplans
(Decompilierung, „pro Spiel, mit dem kleinsten anfangen“; [roadmap.md](roadmap.md)). Belege sind
verlinkt; was nicht geprüft wurde, steht als solches da.

## Begriffe

- **Unsere Recompilation (ReXGlue):** jede PowerPC-Funktion des Spiels wird automatisch in C++
  übersetzt, das Ergebnis läuft nativ, ist aber Maschinencode in C++-Form (`ctx.r3.s64 = ...`), nicht
  lesbar oder änderbar wie Quellcode.
- **Matching-Decompilierung:** Menschen (oder Agenten) schreiben lesbares C/C++, das mit dem
  **originalen Compiler** Byte für Byte dieselben Funktionen ergibt. Der Byte-Vergleich ist der
  einzige harte Beweis, dass der Code stimmt.
- **Funktionale (non-matching) Decompilierung:** lesbarer Code, der sich gleich verhält, aber nicht
  byte-gleich ist; geprüft wird durch Tests und Vergleich mit dem Original.

## Stand der Szene

- **Rock Band 3 (Xbox 360):** ein Fork mit dem originalen MSVC für Xbox 360
  (16.00.10224.00 aus dem XDK 2.0.11164) meldet 98,9 % der Spielcode-Funktionen byte-gleich, größtenteils
  KI-gestützt ([rb3-xenon](https://github.com/freeqaz/rb3-xenon)). Der Erfahrungsbericht nennt
  rund sieben Monate mit Agenten rund um die Uhr, 2.802 Commits in zehn Wochen, 44.226 von 69.301
  Funktionen perfekt (Spielcode 81,7 %, Engine 77,4 %, ganzes Binary mit SDK und Middleware 46,4 %),
  Dance Central 3 bei 92,44 % von 32.252 Funktionen
  ([Blog](https://freeqaz.com/blog/how-agent-swarms-decompile-games); der Beitrag sagt selbst, dass
  er ein ungeprüfter KI-Entwurf ist). Wichtig: Die 360-Version hatte **keine Symbole**, nur `.pdata`
  (Funktionsgrenzen). Die Namen kamen aus der fertigen **Wii-Decompilierung** desselben Spiels und
  einer **Debug-Map** von Dance Central 3.
- **Kinect Sports:** 86 Funktionen mit objdiff geprüft, rund 0,009 % des Codes
  ([Kinect-Sports-Decomp](https://github.com/SebaaMG/Kinect-Sports-Decomp)).
- **Banjo-Kazooie (XBLA):** Forschungsprojekt mit objdiff, braucht Visual Studio 2005 und das
  Xbox-360-SDK ([bk360](https://github.com/banjo360/bk360)).
- **Werkzeuge:** **jeff**, ein Port von encounters decomp-toolkit für Xbox 360: XEX entpacken,
  Funktionsgrenzen, Relocations, Aufteilen in COFF-Objekte, Anbindung an objdiff und decomp.me;
  PDB nur eingeschränkt, Linken und Vergleich des ganzen EXE noch nicht
  ([jeff](https://github.com/rjkiv/jeff)). **objdiff** kann PowerPC inklusive der Xenon-VMX128-Befehle
  ([objdiff](https://www.linuxlinks.com/objdiff-local-diffing-tool-decompilation-project/)). Ob
  decomp.me selbst eine Xbox-360-Plattform hat, war nicht zu klären.
- **Microsoft „Xbox Labs“:** ein neues Team, das die Decompilierungs- und Recompilierungs-Szene
  unterstützen und Werkzeuge teilen will; Einzelheiten fehlen
  ([TweakTown](https://www.tweaktown.com/news/114068/xbox-encourages-recomp-ai-craze-with-new-experimental-labs-division/index.html),
  [Root Nation](https://root-nation.com/en/games-en/game-news-en/en-microsoft-launches-xbox-labs/)).

## Die Hürden für uns

1. **Der Compiler.** Matching braucht den originalen MSVC für Xbox 360 aus dem XDK. Der ist nicht
   öffentlich, und wir besorgen ihn nicht. Damit bleibt für unsere Spiele die **funktionale**
   Decompilierung.
2. **Keine Symbole.** Wie bei Rock Band 3 hat die Retail-XEX keine Namen. Was Giants an
   Namenshilfen hat (gezählt im Speicherabbild, 2026-10-22): 393 RTTI-Klassennamen (fast alle
   Havok), 123 Quelldateipfade in Assert-Texten (Havok), rund 195 `tfb...`- und 513 `ig...`-Typnamen
   der Alchemy-Reflexion (Engine- und Spielklassen mit ihren Feldnamen, dieselbe Quelle, aus der die
   Menüs schon Hüte und Upgrades lesen), dazu alles, was wir beim Portieren schon benannt haben
   (Portal, Figuren, UI, Filme, Ultrawide). Eine Wii- oder 3DS-Decompilierung als Namensquelle gibt
   es nicht.
3. **Middleware:** Havok (Physik, Animation), Bink, die Alchemy-Engine und die XDK-Bibliotheken (D3D,
   XAudio, XAM). Havok und Bink würde man nicht decompilieren, sondern recompiliert lassen; D3D und
   XAudio sind die Stellen, die ein nativer Renderer bzw. die SDK-Laufzeit ohnehin ersetzt.
4. **Rechtliches:** eine Decompilierung ist aus dem Spiel abgeleiteter Code. Sie bleibt privat, wie
   alle Repos hier, und enthält keine Spieldaten; die Daten kommen weiter von der Disc des Besitzers.

## Der Ansatz, der zu uns passt: schrittweise auf der laufenden Recompilation

Statt bei null anzufangen, ersetzen wir in einem **laufenden** Spiel (GiantsRecomp) eine
recompilierte Funktion nach der anderen durch lesbaren C++-Code:

- Ausgangspunkt ist der vollständige recompilierte Code (jede Funktion existiert schon als C++, das
  Verhalten ist damit festgelegt) plus Ghidras Pseudocode für die Struktur.
- Eine ersetzte Funktion wird gegen die recompilierte geprüft: gleiche Eingaben (Register, Speicher),
  gleiche Ausgaben; dazu die skriptgesteuerten Läufe der Spiele (Level, Portal, Menüs) mit
  Bildschirmvergleich. Das Spiel bleibt in jedem Schritt spielbar.
- Reihenfolge nach Nutzen: zuerst der Spielcode, den wir schon verstehen (Figuren, Portal, UI,
  Kamera, Grafik-Einstellungen), dann die Alchemy-Engine; Havok, Bink und XDK bleiben recompiliert.
- Ziel ist kein byte-gleiches Spiel, sondern lesbarer, änderbarer Code: Mods, eigene Grafik- und
  Komfortfunktionen ohne Mid-Asm-Hooks, langfristig andere Plattformen (ARM) und der native Renderer.

## Grobe Aufwandsschätzung für Giants

Giants hat 23.632 Funktionen (so viele findet ReXGlue; die kleinste der drei ersten Discs). Eine
erste Bestandsaufnahme (2026-10-22, Ghidra-Projekt von GiantsRecomp, 12.493 von Ghidra erkannte
Funktionen mit 1,04 MB Code): jede Funktion nach den Texten eingeteilt, auf die sie verweist (1.293
direkt), die übrigen nach ihren Nachbarn im Adressraum, weil der Linker die Bibliotheken am Stück
ablegt:

| Herkunft | Funktionen | Code |
|---|---|---|
| Havok | 25 % | 22 % |
| Alchemy-Engine | 22 % | 25 % |
| CRT / XDK | 17 % | 19 % |
| Spiel (Toys for Bob) | 16 % | 17 % |
| Bink | 1 % | 1 % |
| nicht zuzuordnen | 18 % | 16 % |

Das ist eine Schätzung aus Texten und Nachbarschaft, keine Zählung nach Symbolen. Hochgerechnet auf
ReXGlues 23.632 Funktionen sind Spiel- und Engine-Code zusammen etwa 38 % plus ein Teil des nicht
Zuzuordnenden, also **etwa 8.000-10.000 Funktionen**; der Rest ist Havok, Bink, CRT und XDK.

| Weg | Tempo (Annahme) | Erster lesbarer Stand des Spielcodes |
|---|---|---|
| Von Hand | 20-50 Funktionen pro Tag und Person | 1-2 Personenjahre |
| KI-gestützt, jede Funktion gegen die Recompilation geprüft | 200-500 pro Tag (Vergleich: Rock Band 3 mit Agenten über 1.000 an Spitzentagen, aber mit Compiler und Namen) | 1-3 Monate durchgehender Arbeit |
| Danach: Namen, Strukturen, Aufräumen | etwa derselbe Aufwand noch einmal | |

Das ist eine Größenordnung, keine Zusage. Die tatsächliche Rate zeigt erst ein Pilot.

## Vorschlag

1. **Bestandsaufnahme verfeinern:** die erste Einteilung oben deckt nur Ghidras 12.493 Funktionen;
   mit ReXGlues vollständiger Liste und den Namenshilfen (Alchemy-Reflexion, unsere Hook-Namen)
   die Zahl für die Schätzung genauer machen.
2. **Pilot:** etwa 50 Funktionen aus dem Bereich, den wir kennen (Portal und Figuren), ersetzen und
   prüfen; danach die Rate messen und die Schätzung korrigieren.
3. Dann die Entscheidung des Besitzers, ob und wie weit es weitergeht.
