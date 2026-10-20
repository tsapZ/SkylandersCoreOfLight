# How other recomps and ports do 4K, ultrawide, high FPS and AA (research, German)

Web research of 2026-10-20 for the owner's interest and as a checklist for all games. Claims link
their source; what was not verified is marked. Ideas for our games are at the end. (Recherche auf
Deutsch, Quellen am Ende.)

## Überblick

Die Projekte fallen in drei Gruppen:

1. **Eigener Renderer statt Xenos-Emulation** (Unleashed, Marathon, Skate 3 ab v2.0): Das Spiel
   rendert selbst in der Zielauflösung; Optionen wie Schattenauflösung, MSAA-Stufe und Blur-Qualität
   werden direkt im Renderer oder per Hook gesetzt.
2. **Emulierter Xenos-Pfad (ReXGlue/Xenia)** (GiantsRecomp, DimensionsRecomp, Xenia, unsere Spiele):
   hohe Auflösung über Render-Target-Upscaling plus FSR/CAS.
3. **N64-Recomp und Dekompilate (RT64 bzw. Fast3D)**: Auflösung vom Spiel entkoppelt, hohe FPS meist
   per Frame-Interpolation, die Spiellogik läuft im Originaltakt.

Für uns am wichtigsten: TheBiemGamers GiantsRecomp hat einen fertigen HUD-Ultrawide-Hook, und die
Cemu-Graphic-Packs für die Wii-U-Skylanders listen für Swap Force alle auflösungsabhängigen Buffer,
rund 55 Shader-Fixes und bekannte High-FPS-Bugs samt Fix.

## 1. Unleashed Recompiled (XenonRecomp + XenosRecomp + plume)

- **Auflösung:** keine GPU-Emulation; eigener Übersetzungs-Renderer, XenosRecomp übersetzt die
  Shader nach HLSL ([README](https://github.com/hedge-dev/UnleashedRecomp),
  [XenosRecomp](https://github.com/hedge-dev/XenosRecomp)). `SetResolution` schreibt Viewport ×
  `ResolutionScale` in die Device-Struktur des Spiels, ein Override (`sub_82E9F048`) umgeht die
  Sonderbehandlung von Render-Targets über 1024×1024
  ([video.cpp](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/gpu/video.cpp)).
  ResolutionScale 0,25-2,0 relativ zur Fenstergröße
  ([options_menu.cpp](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/ui/options_menu.cpp)).
- **Post-Processing:** DoF-Gauß-Filter 3×3 bis 9×9 je nach Zielhöhe, „Enhanced“ Motion Blur,
  präziseres Reverse-Z gegen Z-Fighting, bikubische GI-Texturen; Schattenauflösung 512-8192 per
  Mid-Asm-Hook; 2D-Primitive über 720p linear gefiltert
  ([video_patches.cpp](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/patches/video_patches.cpp)).
- **Ultrawide:** AspectRatio Auto/Wide/Narrow/OriginalNarrow; HUD pro UI-Element per Tabelle verankert
  (`ALIGN_*`, `STRETCH`, `SCALE`, `EXTEND_*`), `UIAlignmentMode` Edge/Centre; Cutscenes standardmäßig im
  Original; FMV-Quad-UVs korrigiert
  ([aspect_ratio_patches.cpp](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/patches/aspect_ratio_patches.cpp)).
- **Hohe FPS:** echte Logik, 15-241 (241 = unbegrenzt), Delta-Time pro System geklemmt,
  FPS-abhängiger Kamera-Lerp, ein Boss mit festem 30-FPS-Schritt, Ladebildschirme auf 30
  ([fps_patches.cpp](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/patches/fps_patches.cpp)).
- **AA:** MSAA 2/4/8x (Standard 4x, Original 2x), nur auf Targets, die das Spiel ohnehin mit MSAA
  anlegt; nicht unterstützte Stufen ausgegraut; Transparency AA (Alpha-to-Coverage).
- **Menü:** WindowSize, Monitor, AspectRatio, ResolutionScale, Fullscreen, VSync, FPS, Brightness,
  AntiAliasing, TransparencyAntiAliasing, ShadowResolution, GITextureFiltering, MotionBlur,
  XboxColorCorrection, CutsceneAspectRatio, UIAlignmentMode; nur in der Config: GraphicsAPI,
  GraphicsDevice, TripleBuffering, MaxFrameLatency, AnisotropicFiltering, DepthOfFieldQuality, ShowFPS.
  Jede Option mit Vorschaubild. Bei iGPU/wenig VRAM automatisch Low-End-Defaults.

## 2. Marathon Recompiled (Sonic '06)

Wie Unleashed, dazu `ReflectionResolution` (1/8 bis voll, Standard 1/2) und `RadialBlur`
([config_def.h](https://github.com/sonicnext-dev/MarathonRecomp/blob/main/MarathonRecomp/user/config_def.h)).

## 3. Skate 3 Recomp 2.0 (ReXGlue-Fork)

- Ab v2.0 ein nativer, datengetriebener Szenen-Renderer: liest pro Frame die MeshContext-Daten des
  Spiels aus dem Gastspeicher und zeichnet mit portierten Material-Shadern in die Ausgabetextur des
  Spiels; der emulierte Renderer bleibt als Fallback (live oder F5 umschaltbar). Mehr als doppelte FPS
  bei etwa einem Viertel der Leistungsaufnahme
  ([Release v2.0.0](https://github.com/mchughalex/skate3recomp/releases/tag/v2.0.0),
  [skate3_native_scene.h](https://github.com/mchughalex/skate3recomp/blob/HEAD/src/skate3_native_scene.h)).
- Optionen: Render Scale bis 3x, MSAA bis 8x, Schatten bis 8192 mit PCSS, AO, HDR-Bloom,
  volumetrisches Licht, Sichtweite bis 5x; Framerate-Cap mit „Auto“ gegen Judder.
- Ultrawide: Hor+ und verbreiterte CPU-Culling-Ebenen im recompilierten Cull-Code
  ([skate3_ultrawide_guest.h](https://github.com/mchughalex/skate3recomp/blob/HEAD/src/skate3_ultrawide_guest.h)).

## 4. GiantsRecomp (TheBiemGamer)

- `resolution`, `resolution_scale` 1-8, `present_effect` bilinear/cas/fsr
  ([README](https://github.com/TheBiemGamer/GiantsRecomp)).
- Ultrawide ([ultrawide_hook.cpp](https://github.com/TheBiemGamer/GiantsRecomp/blob/HEAD/src/hooks/ultrawide_hook.cpp)):
  `ultrawide_fov` verbreitert `BuildPerspectiveProjectionMatrix` symmetrisch (Hor+);
  `ultrawide_hud` hookt `BuildOrthographicProjectionMatrix` nur für die UI-Kamera (lr == 0x8219118C)
  und setzt die Breite des Bildschirm-Objekts (+180) auf Höhe × Seitenverhältnis, so verankert das
  Layout des Spiels Randelemente am echten Rand. Das Gastbild bleibt 1280 breit (anamorph gestreckt);
  Cutscene-Filme noch gestreckt.

## 5. Weitere ReXGlue-Ports

- **DimensionsRecomp:** F4-Menü in Display/Performance/Graphics/Audio/Controls, oranges „nach
  Neustart“-Label; Resolution scale, Output resolution cap, Anisotropie, DoF-Pass abschaltbar;
  Upscaling Bilinear/FSR1/CAS; 60-FPS-Unlock; geplant ein nativer D3D12-Renderer
  ([README](https://github.com/NeverCookFirst/DimensionsRecomp)).
- **ReXGlue-SDK:** `swap_post_effect` none/fxaa/fxaa_extreme; intern getrennte X/Y-Skalierung
  (`draw_resolution_scale_x/_y`, ob per Cvar einstellbar: unverifiziert).

## 6. Zelda64Recomp und BanjoRecomp (N64Recomp + RT64)

- RT64: ein Renderer in Originalauflösung schreibt zurück in den Speicher des Spiels, ein zweiter
  spielt dieselben Draws in hoher Auflösung ab ([RT64](https://github.com/rt64/rt64)). Menü:
  Original/2x/Auto plus Downsampling (SSAA) 2x/4x
  ([graphics.rml](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/dev/assets/config_menu/graphics.rml)).
- Ultrawide: Aspect Original/Expand, HUD Original/16:9/Expand über Zusatzbefehle in den Displaylisten.
- Hohe FPS: Frame-Interpolation (Matrizen zerlegt und einzeln interpoliert), Framerate
  Original/Display/Manual, ohne Einfluss aufs Gameplay. MSAA 2x/4x.

## 7. Ship of Harkinian / 2 Ship 2 Harkinian, sm64ex, Perfect Dark

- **SoH:** Internal Resolution 50-200 %, Advanced Resolution Editor (feste Höhe 240p-2160p,
  Pixel-Perfect, N64-Modus), HUD-Anker pro Element, Widescreen Actor Culling, Matrix-Interpolation
  20-360 FPS, MSAA 1-8
  ([SohMenuSettings.cpp](https://github.com/HarbourMasters/Shipwright/blob/develop/soh/soh/SohGui/SohMenuSettings.cpp)).
- **sm64ex:** HUD-Makros ab Bildrand, optionaler 60-FPS-Interpolationspatch
  ([gfx_dimensions.h](https://github.com/sm64pc/sm64ex/blob/nightly/include/gfx_dimensions.h)).
- **Perfect Dark:** MSAA bis 16x, Framerate-Limit, Uncap Tickrate bis 240, HUD Centering, Detail-
  Texturen, Glare/Overexposure ([optionsmenu.c](https://github.com/fgsfdsfgs/perfect_dark/blob/port/port/src/optionsmenu.c)).

## 8. Emulatoren zum Vergleich

- **Xenia Canary:** `draw_resolution_scale_x/_y`, „still a hack that games are completely unaware
  of“ (Bloom, Blur, Shadow-Maps können Streifen zeigen); Seitenverhältnis/60 FPS nur per Spielpatch
  ([xenia.jp](https://xenia.jp/updates/2022/01/29/presenting-the-presentation-update-amd-fidelityfx-fsr-fxaa-cas-vrr.html)).
- **RPCS3:** Resolution Scale in Prozent, „Minimum scalable dimension“, FSR + RCAS.
- **Cemu:** Auflösung über Graphic Packs: `[TextureRedefine]` erkennt Texturen/Targets an Größe und
  Format und setzt `overwriteWidth/Height`.
- **Dolphin:** Internal Resolution, MSAA/SSAA, Scaled EFB Copy, Widescreen Hack (UI oft gestreckt).

## 9. Cemu-Graphic-Packs für Skylanders

- **Swap Force (Wii U):** Szene nativ 1024×576, Ausgabe/HUD 1280×720. Das Pack definiert Ausgabe,
  Szene, DoF (512×576, 512×288), Bloom (512×288, 256×144), Schatten (512×2048) und Spielerschatten
  (256×512) neu und rendert schon im Standard-Preset die Szene direkt in 1280×720, also unser
  1040×620-Experiment. Etwa 55 Ersatz-Shader: Transparenz-Dither, Blur-Kernel (Bloom, DoF, Motion
  Blur, Fell, AO/Schatten), Decals, Charakterschatten, AA-Offsets, und einer heißt „Fixing issues with
  the skipped upscale step“
  ([Graphics/rules.txt](https://github.com/cemu-project/cemu_graphic_packs/blob/master/src/SkylandersSwapForce/Graphics/rules.txt),
  [Enhancements/rules.txt](https://github.com/cemu-project/cemu_graphic_packs/blob/master/src/SkylandersSwapForce/Enhancements/rules.txt)).
  Presets: FXAA an/aus, Motion Blur, Schatten 50-400 %, DoF aus, Bloom 0-150 %, Anisotropie.
- **FPS:** Swap Force wartet zwei VBlanks pro Frame; „Infinite Fall“-Fix setzt den Schwellwert auf
  `20*30/FPS` (SwapForceRecomp hat denselben Fix als `fix_stuck_falling`), ebenso für SuperChargers und
  Imaginators. **Giants, Trap Team und Spyro's Adventure haben einen Delta-Time-Patch gegen
  „speed-up above 120 FPS“**
  ([Trap Team patch_dt.asm](https://github.com/cemu-project/cemu_graphic_packs/blob/master/src/SkylandersTrapTeam/Mods/FPS/patch_dt.asm)).
- **Seitenverhältnis:** Giants und Trap Team patchen globale Floats: Seitenverhältnis, `hudX`/`hudY`
  (z. B. hudX = 1720 bei 21:9), FOV
  ([Giants patches.txt](https://github.com/cemu-project/cemu_graphic_packs/blob/master/src/SkylandersGiants/Graphics/patches.txt)).
- Vorbehalt: Wii U ist nicht 360 (360: 1040×620); die Liste ist ein Hinweis, keine 1:1-Vorlage.

## Ideen für unsere Skylanders-Ports

1. **Szene direkt in Ausgabeauflösung** (Swap Force #41): die Cemu-Liste als Checkliste für abhängige
   Buffer und Shader-Fixes. Der „skipped upscale step“ passt zu unserem Befund vom 2026-10-20: bei
   genau 1280×720 stürzt das 360-Spiel ab, bei 1264×704 nicht.
2. **HUD-Ultrawide wie GiantsRecomp:** Ortho-Projektion der UI-Kamera bzw. Breite des
   Bildschirm-Objekts setzen, Option „UI: Rand / 16:9“; alternativ die globalen `hudX`-Floats der
   Cemu-Packs suchen (Giants, Trap Team).
3. **Culling bei Ultrawide verbreitern** (Skate 3, SoH), sonst verschwinden Objekte am Rand.
4. **High-FPS-Bugs aus den Cemu-Packs:** Delta-Time über 120 FPS bei Giants, Trap Team, Spyro's
   Adventure prüfen; Ladebildschirme auf 30 begrenzen wie Unleashed.
5. **FPS-Option** „Original (30) / Bildschirm / Manuell“ (Zelda64Recomp) und eine Auto-Begrenzung gegen
   Judder (Skate 3).
6. **Effekt-Schalter** wie im Cemu-Pack: DoF, Motion Blur, Bloom-Stärke, Anisotropie, Spiel-AA.
7. **Ultrawide-Schärfe:** getrennte X/Y-Skalierung des SDK für anamorphe Bilder (unverifiziert).
8. **Low-End-Defaults und GPU-Wahl**, Optionen mit Vorschaubild (Unleashed, Skate 3).
9. **Cutscenes/FMVs:** unverzerrt pillarboxen statt strecken (Unleashed, Banjo).
10. **Langfristig ein nativer Renderer** wie Skate 3 (mit emuliertem Fallback); sehr hoher Aufwand.

## Quellen

Alle Links stehen im Text; Hauptquellen: UnleashedRecomp, MarathonRecomp, skate3recomp, GiantsRecomp
(TheBiemGamer), DimensionsRecomp, rexglue-sdk, Zelda64Recomp, RT64, BanjoRecomp, Shipwright,
2ship2harkinian, sm64ex, perfect_dark, xenia.jp, xenia-canary/game-patches, RPCS3, Cemu und
cemu_graphic_packs (Skylanders Swap Force, Giants, Trap Team, SuperChargers, Imaginators), Dolphin.
