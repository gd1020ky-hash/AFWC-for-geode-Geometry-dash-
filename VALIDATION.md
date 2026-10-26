# Validation status — Frame Forge source beta

## Passed in the authoring environment

- Eight offline Python helper tests: valid plan, booleans rejected as numbers, finite-number checks, numeric bounds, unexpected keys rejected, point-count limits, duplicate/non-finite JSON constants rejected, and no-overwrite/explicit-overwrite file behavior.
- A mocked Ollama request/response contract check: correct `/api/generate` endpoint, non-streaming schema-constrained request, and valid plan conversion. **This was a mock, not an AI model inference.**
- `mod.json` parses, and settings referenced by the C++ source are defined in it.
- Workflow YAML parses; manual dispatch, the portable-test prerequisite, and separate Android32/Android64 targets are present.
- Selected API names/signatures checked against the pinned Geode SDK and 2.2081 bindings. This is source inspection, not compiler validation.
- Auto decoration was upgraded to a deterministic two-frequency glow ribbon with seeded variation and recurring size accents; portable tests now check determinism, seed variation, object count and an accent point. These new C++ assertions have not been executed in this authoring environment.
- The Forge popup renders the approved badge as a registered PNG Geode sprite, with the original SVG retained. Menu source, resource registration, PNG dimensions, and archive integrity were checked. Android compilation and in-game rendering/touch behavior remain unverified.

## NOT passed / NOT performed

- The SDK-independent C++ tests were written but **not executed here**. The sandbox has no installed C/C++ compiler. An attempted portable compiler extraction initially rejected an absolute loader symlink. After excluding that link, its executable failed with `Exec format error`; no successful compilation resulted.
- The Geode integration, including the redesigned menu, has **not** been compiled or linked for either Android ABI here.
- No Geometry Dash session, Android device, menu rendering/touch layout, replay consistency, music timing, input-window behavior, paste/undo behavior, custom OGG playback or editor output has been runtime-tested.
- No real local AI model was invoked; generated decoration quality has not been evaluated.
- No GitHub repository or workflow run was created, no artifact was downloaded from CI, and no mod/level was published.

## Next required verification

Run the included workflow in your own repository. It first compiles and executes `core_tests.cpp`, then builds both Android packages. Fix actual compiler errors rather than treating this draft as guaranteed compatible. Only a successful workflow run produces candidate `.geode` files. Test the matching ABI on a backed-up local level in Practice Mode; a successful compile is still not a gameplay test.

The deliverable is a **source ZIP**, not an installable mod. Target: GD 2.2081 Android with Geode loader 5.11.2. An actual Geode loader 1.9.0 is not supported by this draft.
