# Frame Forge — Android Geode source beta

**This download is source code, NOT an installable `.geode` binary.** The Android mod has not been compiled or tested in Geometry Dash in the authoring environment. A GitHub Actions workflow is included to perform the real compiler/linker checks and generate Android packages. Treat the first successful build as a beta to test on a backed-up local level.

## Target and version warning

- Geometry Dash **2.2081**, Android 32-bit or 64-bit.
- Geode **loader 5.11.2**, pinned in `mod.json`.
- The requested version was `2.2144(2.2081)` / Geode `1.9.0`. This project targets the `2.2081` part, NOT an independently verified `2.2144` build.
- A launcher application's version is not necessarily the Geode loader version inside the game. If your actual loader is 1.9.0, this source is not compatible with it. Do not merely edit the version number to force-load it.
- Official SDK source at tag `v5.11.2` explicitly lists GD 2.2081 for Android. Re-check your game and in-game loader before building or installing.

## Features and honest limits

| Feature | Implementation |
|---|---|
| Record/playback | Practice-only, timestamped button edges for player 1/2 and buttons 1..3. Reset from the level start; save/load versioned `.fmacro` files. |
| Automatic frame counter | GD engine simulation ticks and ticks between presses. Nominal 240 ticks/s. This is NOT your screen's rendered FPS. |
| Timing/window measurement | Automatic retries with the selected player-1 jump press AND matching release shifted by each integer offset. Reports survival to a fixed horizon, not an exact mathematical or full-level success window. |
| Custom sound effects | Optional input sounds plus custom timing-result sounds from the mod save directory. |
| Speed control | Practice simulation speed 0.1x..2x. Normal-mode speed is unchanged. |
| Music synchronization | Matches the background-music rate to practice speed. No pitch-preserving stretch, song-position seeking or drift correction. |
| Separate audio speed | Additional 0.25x..2x rate/pitch multiplier. Set to 1 for matched timing. |
| Free automatic level building | Inserts a capped starter cube layout of blocks and isolated spikes through native editor paste; no fee, no upload, no verification bypass. |
| Branded mod menu | A control-deck popup opens from the Forge buttons on the pause screen and editor. It groups practice and creation actions and includes a built-in cyan/gold Frame Forge logo sprite. |
| Auto decoration | One-click seeded glow-ribbon composition with flowing curves, organic variation, and recurring size accents (up to 200 objects). Deterministic, editor-native, and not AI. |
| Real optional AI-assisted decoration | Included Python helper asks an already-installed local Ollama model for glow positions/scales. Import its validated JSON in the editor. No trained model is bundled, and no claim of "best" decoration is made. |
| Free automatic compilation | Workflow builds both Android ABIs and saves packages as GitHub Actions artifacts. No account, repository, or workflow has been created/run on your behalf. GitHub quotas/terms apply. |

## Mod menu and logo

The approved cyan-and-gold badge is bundled as a PNG Geode sprite and rendered in the popup; the SVG source remains in `assets/`.

Open **Forge** from the pause screen for practice tools, or from the editor for level-creation tools. The redesigned control deck separates those action groups, includes a cyan-and-gold **FF / Frame Forge** wordmark, and shows which context is active. The archive also includes `assets/frame-forge-logo.svg` as a standalone vector brand mark. The in-game header is drawn with the game's built-in font; it does not depend on loading the SVG as a game texture.

The menu is source-level UI work only: this update has not been compiled or opened in Geometry Dash. Button layout, legibility and touch behavior still need device testing.

## Source layout

```
CMakeLists.txt
mod.json
main.cpp
ForgeCore.hpp
core_tests.cpp
ai_decorate.py
README.md
VALIDATION.md
LICENSE
assets/frame-forge-logo.svg
.github/workflows/build.yml
```

## Build for free using GitHub

1. Create your own GitHub repository and add these source files, preserving the folder layout above. A public repository on standard GitHub-hosted runners is the intended no-paid-service route; consult GitHub's current allowances.
2. **Keep `.github/workflows/build.yml` in that exact directory.** If a mobile uploader skips hidden folders, create the file manually at that path using the included workflow text. Do not upload the entire ZIP as one repository file.
3. Open the repository's **Actions** tab and run **Build Frame Forge for Android**, or push a source change. This does not publish the mod to the Geode index or upload a level.
4. The portable-tests job must pass; then Android32 and Android64 compile separately. A red build is NOT a usable package. Inspect the compiler error and fix the source before proceeding.
5. After success, download `frame-forge-Android32` or `frame-forge-Android64` from that run's artifacts. Extract the artifact ZIP and locate the actual `.geode` package inside.
6. Pick the package matching your GD process ABI (not merely whether your phone's CPU supports 64-bit). Use Geode's local mod-file installation/import option, then restart when requested.

The workflow uses Geode SDK 5.11.2, an official build-action commit and bindings commit pinned on 2026-10-25, and Android NDK 29.0.14206865. CI requires internet access. No compiled package is included in this source download.

## Recording and replay on Android

1. Open a level and enable **Practice Mode**.
2. Pause. The **Forge** button is at the bottom-left of the pause screen. Choose **Record**, close any remaining pause menu, then resume.
3. Recording starts from the beginning; active recording/replay clears practice checkpoints on reset. A reset starts a fresh take rather than appending to the old one.
4. Perform the inputs. Pause and choose **Forge > Stop + Save**. A timestamped `take-....fmacro` file is written to the mod save directory. Held buttons are closed with release edges in the saved file.
5. Choose **Play latest**, then resume. While playback is active, live gameplay button input is ignored. Pause/Stop remains available through the normal UI. Playback stops at the recorded endpoint; it is not noclip and will not guarantee a successful run.
6. With Replay filename blank, playback uses the current in-memory take or the latest timestamped saved file. To choose another saved take, set its basename in settings. Macros from an edited/different level are rejected by a non-cryptographic fingerprint of the name and serialized level data.

This recorder does not snapshot RNG, checkpoints, all player state, external mods, or audio offsets. It does not import GDR or other bots' macro formats. Disable physics/TPS-altering mods and inconsistent input modifications. Replays may diverge; validate locally before relying on timings.

## Measuring an empirical input window

1. Save a recording with at least a second of survival after the jump you want to inspect. By default the required tail is 240 ticks plus the 5-tick positive offset radius.
2. In Forge settings, set **Window: player-1 jump press number** (1 = first recorded player-1 jump), radius, and survival horizon.
3. Choose **Measure window**, close the pause UI, and resume. The mod replays from the start once per candidate offset. A default +/-5 radius means 11 real attempts, so the process takes time.
4. Each candidate shifts both the press and matching release equally, preserving hold duration. Shifts crossing another edge of the same control or leaving the recording are skipped.
5. A candidate passes only if no death is observed before the fixed horizon, or a practice completion is observed. The fixed horizon is original press tick + horizon setting + radius, identical across candidates.
6. Read the popup and timestamped `window-....txt` report. It lists each offset and the largest contiguous passing sampled run. Passing samples do not prove an exact jump window or that the rest of the level can be completed.

A later obstacle within the horizon can make an otherwise valid jump fail. RNG/nondeterministic triggers, other mods and drifting inputs can invalidate comparisons. There is no instant counterfactual full-level simulation here. Stop any trial through Forge. Do not manually reset during an ongoing trial; stop and restart the analysis instead.

## Speed and music

Set **Enable practice speed control** and choose the Game speed multiplier. The update timestep changes only in Practice Mode; menus and the global scheduler are not sped up. **Match music rate** applies the same factor to the background-music channel group. **Additional music speed** multiplies this rate separately. At 0.5x game speed and 1x additional audio speed, the intended music rate is 0.5x. Changing rate changes pitch. This is rate matching, not a guarantee of sample-accurate synchronization; trigger effects and other audio mods can interfere. Music pitch is restored when leaving play.

## Custom sounds and generated files

Use the save directory returned by Geode for mod ID `gdpro67.frame-forge`; use Geode's mod-data access/export facilities to locate it rather than guessing an Android filesystem path. Android scoped storage may require a computer or Geode-provided file access. This project does not request broad storage access.

Optional files in that directory:

- `press.ogg`: input-down sound; `release.ogg`: input-up sound. Enable Input sound effects. Without custom files, the game click sound is used.
- `window-pass.ogg` and `window-fail.ogg`: optional sounds after all timing trials.
- `ai-plan.json`: optional validated local-model result.
- `take-*.fmacro` and `window-*.txt`: generated recording and timing-report files.

Use sound files you own or have permission to use. There are no bundled third-party sounds.

## Free layout building and auto decoration

Open a BACKED-UP local editor level. Use the editor's top-right **Forge** button. **Auto layout** adds a starter strip of blocks with spaced single spikes; **Auto decorate** creates a flowing glow ribbon with a two-frequency curve, small seeded placement variation, and repeating large/medium visual accents. It uses at most 200 glow objects and the same seed reproduces the same layout. Both actions ask before insertion and use native paste/undo rather than replacing your level string. This is a structured procedural pass, not an AI judgment of what is “best.”

Actual placement follows GD's native paste relative to your current editor view; design coordinates are not guaranteed absolute world coordinates. Confirm, inspect, use native Undo if unsuitable, and test the result. Decorative points are placed high in their design coordinates, but the insertion offset and an existing level can put them elsewhere. No universal collision-free or playable layout is promised.

## Genuine AI-assisted decoration (optional, separate local model)

This feature is deliberately limited: the model arranges circular glow prefab 211 using x/y/scale values. It does not synthesize custom textures or turn a rough layout into a guaranteed rated-quality level.

1. On a computer with Python 3.9+ and Ollama already running, choose a local model you have installed that supports JSON-schema output. Model installation is your separate step; none is auto-downloaded and hardware requirements vary.
2. Run the included helper, replacing the placeholder model name:

   `python ai_decorate.py --model YOUR_INSTALLED_MODEL --prompt "cyan and violet wave-like glow rhythm" --x-start 600 --x-end 3480 --safe-y 270`

3. The helper calls `http://127.0.0.1:11434/api/generate` by default. It sends only your style prompt, schema and numeric bounds, not your level or files. A different `--ollama-url` sends that request to the endpoint YOU select. No paid API key is used. There is no Android-to-PC network request in the mod itself.
4. It validates 1..200 finite points, bounds, schema and scales, then writes `ai-plan.json`. Existing outputs are not overwritten unless you explicitly pass `--overwrite`. A regular local computer filesystem is needed for atomic no-overwrite output publication.
5. Copy `ai-plan.json` into the mod's save directory on Android. In the editor, choose **Import AI plan** and confirm insertion. Without a running model, only procedural decoration is available.

AI generation and Android import are separate steps, NOT a fully in-game one-tap model. Results depend on your model; no real inference or model-quality evaluation was performed in the authoring environment.

## Validation and troubleshooting

- **Observed result:** all eight offline helper tests passed; a mocked Ollama request/response contract check also passed. These are not real-model inference or in-game tests. See `VALIDATION.md` for the exact scope and unsuccessful compiler attempts.
- Run `python ai_decorate.py --self-test` for offline validator/output-file tests. These do not test a real model.
- `core_tests.cpp` tests macro validation/round trips, edge-shift rejection, fingerprints, and deterministic generators. It needs a C++20 compiler; the workflow runs it BEFORE attempting Android compilation.
- C++/Android compilation and in-game runtime behavior still require external verification. No device, game runtime or Android compiler was available while preparing this source.
- If CI fails, preserve its error text. Do not install partial output, force a version mismatch, or mark the build as tested. API mismatches need source correction, not a metadata-only version edit.

## Safety and scope

No payment bypass, account credential access, cloud AI account, server modification, automated level upload, automatic verification, anticheat bypass, or leaderboard-submission feature is included. Practice tools do not certify legitimate completions. Review local files, back up levels, and respect each game's/community's rules.

## Upstream compatibility references

- SDK: https://github.com/geode-sdk/geode/tree/v5.11.2
- SDK GD targets: https://github.com/geode-sdk/geode/blob/v5.11.2/CMakeLists.txt
- Bindings: https://github.com/geode-sdk/bindings/tree/c49e72063a34ce81b1134f84881e8a3f3cf36c22/bindings/2.2081
- Official build action: https://github.com/geode-sdk/build-geode-mod/tree/a0f7676e6fcd1a584f6d0c1c800239008412defc

MIT-licensed original source; see LICENSE. Geometry Dash, Geode and Ollama retain their own licenses and trademarks. This is an independent, unendorsed beta.
