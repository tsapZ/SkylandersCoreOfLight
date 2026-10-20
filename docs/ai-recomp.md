# Xbox-360-Recomp mit KI, native Renderer (Recherche, 2026-10-20)

Web-Recherche (German) für alle unsere Spiele: wie andere Projekte KI-Agenten einsetzen, wie ihre
nativen Renderer gebaut sind und was das für unseren Ablauf heißt. Unsichere Punkte sind markiert.
Die Messungen an Swap Force dazu: SwapForceRecomp `docs/native-renderer.md`.

## Kurz

- KI ist in der Xbox-360-Recomp-Szene verbreitet; mehrere ReXGlue/XenonRecomp-Ports legen Claude
  Code oder Codex offen, bei WWE SvR 2011 hat Claude Code auch den nativen D3D-Renderer geschrieben,
  Dead Rising 2: Case Zero ist damit vollständig durchspielbar.
- Die Gemeinschaft ist gespalten: N64Recomp, Zelda64Recomp und Banjo Recompiled verbieten KI-Code;
  der plume-Autor (Unleashed) lehnt KI öffentlich ab; HarbourMasters und die Melee-Decomp erlauben KI
  unter Regeln (offenlegen, kleine PRs, Review).
- Drei Renderer-Architekturen: (a) die D3D-Funktionen des Spiels hooken und auf plume umsetzen
  (Unleashed, re:Blue, WWE, reNut, Xerenge, DR2), am meisten wiederverwendet; (b) Szenen-Renderer aus
  Spielstrukturen (Skate 3 2.0), stark spielspezifisch; (c) spielspezifischer PM4-Executor nach einer
  Zählung (Forza Horizon „Pinyon Shift“).

## 1. KI in Projekten (Auswahl)

| Projekt | KI-Nutzung | Was berichtet wird |
|---|---|---|
| [WWE SvR 2011](https://github.com/KaikoClanworth1/wwe-svr2011-pc) | „Claude Code wrote most of the port“, auch der native Renderer | eine 30-fps-Version bestand die automatischen Tests, der Spieltest fand kaputte Physik ([FEATURES.md](https://github.com/KaikoClanworth1/wwe-svr2011-pc/blob/HEAD/docs/FEATURES.md)) |
| [Dead Rising 2: Case Zero](https://github.com/wivi514/Dead_Rising_2_Case_Zero_Xenon_Recomp) | fast alle Commits mit Claude als Co-Author, 1.738 Commits 2026-08-04 bis 2026-10-14, 100 % durchspielbar | Gotcha-Liste mit über 600 Einträgen, ausgelagert, weil CLAUDE.md 308 KB groß wurde; z. B. „A zero is a detection failure, not a fact“ |
| [DimensionsRecomp](https://github.com/NeverCookFirst/DimensionsRecomp) (LEGO Dimensions) | Claude für Runtime, Installer, Mod-Tools | „A model cannot tell you a world failed to load“: der menschliche Spieltest entscheidet |
| [GoldenEye-Recomp](https://github.com/SunJaycy/GoldenEye-Recomp), [Silent Hill: Downpour](https://github.com/LittleBitUA/DownpourRecomp), [Xerenge](https://github.com/shipa-2/Xerenge), [reNut](https://github.com/masterspike52/reNut), [Fable 2](https://github.com/himdo/Fable-2-Recomp) | offengelegt | Bugs muss trotzdem ein Mensch finden; „never delegated: what renders incorrectly“ |
| [Skate 3 Recomp](https://github.com/mchughalex/skate3recomp) | keine Offenlegung (KI-Nutzung **unbestätigt**) | nativer Renderer in etwa 3 Wochen, eine Person |

Decomp allgemein: [Chris Lewis](https://blog.chrislewis.au/decompiling-a-nintendo-64-game-in-84-days/)
hat Snowboard Kids mit Claude in 84 Tagen dekompiliert (Experten griffen bei ~5 % ein; ohne sie
wohl bei 89-90 % stecken geblieben); die [Melee-Decomp](https://github.com/doldecomp/melee/wiki/FAQ)
nutzte KI auf der Schlussstrecke („tools produce candidates rather than verified answers“). Regeln:
[N64Recomp](https://github.com/N64Recomp/N64Recomp/blob/HEAD/CONTRIBUTING.md) „AI must not be used to
generate code for contributions“;
[HarbourMasters](https://github.com/HarbourMasters/code-of-conduct/blob/HEAD/AI_POLICY.md) „not Anti AI
… Anti Slop“; Kritik am Patchen „von der falschen Seite“
([VGC](https://www.videogameschronicle.com/news/banjo-kazooie-pc-port-decomp-developer-says-ai-generated-pc-ports-are-a-race-to-the-bottom-and-disrespectful/)).

## 2. Techniken

- Ghidra mit MCP für Agenten: [GhidraMCP](https://github.com/LaurieWired/GhidraMCP) (headless),
  [ReVa](https://github.com/cyberkaida/reverse-engineering-assistant),
  [pyghidra-mcp](https://github.com/clearbluejar/pyghidra-mcp). XEX:
  [XEXLoaderWV](https://github.com/zeroKilo/XEXLoaderWV), der
  [SaveEditors-Fork](https://github.com/SaveEditors/XEXLoaderWV/releases) legt `.pdata`-Einträge als
  Funktionen an. LLM4Decompile kann nur x86_64.
- D3D-Bibliothek finden: Aufrufer der `Vd*`-Kernel-Importe plus Call-Graph; WWE fand so 562
  Funktionen, ein Zähl-Build schrieb pro Funktion Aufrufe pro Frame und erzeugte GPU-Pakete in eine
  [`d3d_map.csv`](https://github.com/KaikoClanworth1/wwe-svr2011-pc/blob/HEAD/port/docs/native_renderer_phase1.md);
  37 Funktionen deckten 80 % der Aufrufe. Zwischen Spielen Namen übertragen: Ghidra Version Tracking
  oder [BSim](https://ghidra.re/ghidra_docs/GhidraClass/BSim/BSimTutorial_Intro.html).
- Emulator als Orakel: Skate 3 schaltet nativ/emuliert live mit F5 um und hat ein Debugmenü zum
  Bisektieren; DR2 vergleicht A/B mit demselben Binary.
- Automatisierung: Fable 2 hat einen TCP-Steuerkanal (press, script, game_state, screenshot mit
  avg_luma); WWE lässt nachts bis zu 4 Instanzen laufen, ein Bug ist Absturz, Hänger (20 s keine
  FPS-Zeile), Device lost oder ein schwarzes/einfarbiges/unverändertes Bild
  ([OVERNIGHT_TESTS](https://github.com/KaikoClanworth1/wwe-svr2011-pc/blob/HEAD/port/docs/OVERNIGHT_TESTS.md)).
- Mess-Disziplin ([Pinyon lessons](https://github.com/arcanite24/pinyon-shift/blob/HEAD/site/pages/lessons.md)):
  A/B im selben Lauf (dasselbe Build schwankt ~4 %), eingefrorene Saves, deterministisches
  Frame-Replay, „Measure the game before writing a renderer“.

## 3. Native Renderer

- **Unleashed Recompiled:** 42 Hooks auf D3D-Funktionen
  ([video.cpp](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/gpu/video.cpp)),
  Pipelines beim Laden kompiliert; öffentliche Historie ~5 Monate, 4-5 Kernleute; GPLv3.
  [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) (MIT) übersetzt Shader nach HLSL, ist aber
  auf Unleashed zugeschnitten (kein Memory Export, keine dynamische Registerindizierung).
  [plume](https://github.com/renderbag/plume) (MIT).
- **re:Blue** (Blue Dragon, vom ReXGlue-Gründer): `REX_HOOK` auf die D3D-Einstiegspunkte, Predicated
  Tiling wird zu einem Clear plus einem Resolve; teils generisch, teils spielspezifisch
  ([reblue](https://github.com/rexglue/reblue)).
- **WWE:** 467 Shader beim Anlegen abgefangen, XenosRecomp gepatcht; das D3D-Device hält einen Spiegel
  der GPU-Register, der ganze Zustand ist bei jedem Draw lesbar
  ([phase 2](https://github.com/KaikoClanworth1/wwe-svr2011-pc/blob/HEAD/port/docs/native_renderer_phase2.md)).
- **Dead Rising 2:** am 2026-08-06 von der PM4-Emulation auf den D3D-Weg gewechselt, weil alle
  schweren Fehler unter der D3D-Ebene lagen; „boots, renders and plays“ am 2026-08-11; 23
  XenosRecomp-Patches ([Plan](https://github.com/wivi514/Dead_Rising_2_Case_Zero_Xenon_Recomp/blob/HEAD/docs/d3d-translation-plan.md)).
- **Skate 3 2.0:** liest MeshContext und Modellinstanzen aus dem Gastspeicher, eigene HLSL-Ports der
  Materialien; ~3 Wochen; nicht übertragbar.
- **Pinyon Shift:** Hybrid nach Draw-Familien 91-95 ms gegen 23,5 ms emuliert, verworfen
  ([renderer.md](https://github.com/arcanite24/pinyon-shift/blob/HEAD/site/pages/renderer.md)).
- Schwerste Teile: EDRAM-Formate (7e3, D24F), Resolves (Größe, Write-back, Invalidierung), Memory
  Export, Predicated Tiling, Shader-Fehler (u. a. ein Deadlock auf NVIDIA), PSO-Ruckler.
- Wiederverwendbar ist das Muster (Hook-Liste, Device-Layouts, plume, Shader-Cache), nicht die
  Adressen; ob die Device-Layouts über XDK-Versionen stabil sind, ist **nicht belegt**.

## 4. Anleitungen

[XenonRecomp-README](https://github.com/hedge-dev/XenonRecomp),
[ReXGlue-Wiki](https://github.com/rexglue/rexglue-sdk/wiki), Interviews mit dem ReXGlue-Gründer
([Read Only Memo](https://readonlymemo.com/rexglue-xbox-360-recompilation-interview/): die GPU-Emulation
soll „over time“ durch natives Rendering ersetzt werden), die ausführlichsten Write-ups bei
[Pinyon Shift](https://github.com/arcanite24/pinyon-shift) (`site/pages/`),
[DR2](https://github.com/wivi514/Dead_Rising_2_Case_Zero_Xenon_Recomp) (`docs/`) und WWE.

## Empfehlungen für uns

1. Vor jeder Renderer-Entscheidung einen Zähl-Build (WWE, Pinyon).
2. Wenn nativ, dann der D3D-Weg (a), nicht der Szenen-Renderer: nur er zahlt sich über mehrere Spiele
   aus. Unleashed lesen, nicht kopieren (GPLv3); plume und XenosRecomp sind MIT.
3. Keinen Hybrid nach Draw-Familien; den emulierten Renderer als umschaltbaren Rückfall und Referenz
   behalten (Skate 3, DR2).
4. D3D-Namen einmal benennen und per Version Tracking/BSim auf die anderen Spiele übertragen.
5. Headless-Ghidra mit MCP (wir nutzen pyghidra schon).
6. Einen Steuerkanal für Live-Tests ergänzen (Fable 2) und nächtliche Läufe mit automatischen
   Fehlersignalen (WWE); unsere Input-Skripte sind die Basis.
7. Den Spieltest des Eigentümers als Pflicht behalten (WWE, DimensionsRecomp).
8. Agenten-Schleifen begrenzen und Wissen außerhalb von CLAUDE.md sammeln (DR2, Lewis).
9. Mess-Disziplin: A/B im selben Lauf, ein Frame ist eine Stichprobe, eine Null kann ein Messfehler
   sein (Pinyon, DR2).
10. KI-Nutzung offenlegen und wartbar schreiben, Änderungen über Hooks statt im generierten Code
    (HarbourMasters, Kritik in VGC/Kotaku).
