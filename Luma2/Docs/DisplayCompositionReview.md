# Display composition: modes, formats and display modes

How Luma 2.0's display composition handles the swapchain modes, the real buffer's format and color space, the display mode and the composition settings; which combinations work, which don't and should, and a plan. Read from the code at `7e93133f` (Step 8). "Verified" means checked in FF7R during Step 5; everything else is read from the code and not run.

## The moving parts

| Part | Where it's decided | Values |
|---|---|---|
| Swapchain mode | `Options::swapchain.mode`, setting `Luma.Swapchain.Mode` (Test and Development) | **Mirror**: the game draws into Luma's textures (the game's format, with an FP16 mirror when the upgrade rules match it), composition always writes the real buffer. **InPlace**: the game draws into the real buffer, upgraded to the output mode's format; composition reads a copy, and runs only when something needs it. **Native**: the game's format and color space; composition never runs, only the overlay draws |
| Output mode | `Luma.Swapchain.OutputMode` (Auto, HDR10, scRGB, SDR), `Options::swapchain.hdr_output`, Windows HDR | Mirror and InPlace only. HDR10: R10G10B10A2, PQ BT.2020. scRGB: R16G16B16A16_FLOAT, linear BT.709. SDR: 8 bit (10 if the game's is), G22 BT.709. Auto: `hdr_output` with Windows HDR on, SDR without. Mirror switches at any present; InPlace at the game's next resize |
| Native output | `DescribeNativeOutput` | From the game's color space, else its format: HDR10, scRGB or SDR. Used by the overlay's encode only |
| Composition output encoding | `GetEncoding(output, format)` | `Pq` (HDR10), `Linear` (FP16), `Srgb` (8 and 10 bit SDR, encoded with gamma 2.2 despite the name) |
| Display mode (`LumaSettings.DisplayMode`) | `DisplayMode::Write` | 0 SDR (every white level 80 nits), 1 HDR (peak and paper whites from the settings), 2 SDR in HDR (peak = paper = UI white = Paper White) |
| What sets the display mode | output mode, the `Luma.Display.Mode` setting (HDR, SDR), Windows HDR, the game's own switch (`Game::Display::SetHdr`) | HDR output and the game in HDR: 1. The user's SDR on an HDR output (or InPlace waiting for a resize): 2 with Windows HDR on, 0 with it off. SDR output, the game in SDR, or Native: 0 |
| The game's image (what composition reads) | the mod's shaders and `EARLY_DISPLAY_ENCODING` | Linear (`POST_PROCESS_SPACE_TYPE` 1; with early encoding, already scaled by paper white), gamma (`POST_PROCESS_SPACE_TYPE` 0), or, with the mod inactive, the game's own encoding (gamma, linear or PQ, from the game's format and color space) |
| Mod active | `Composition::IsModActive` | Any replacement or variant published. Off: the shader's fallback paths decode the vanilla image |
| Composition settings | `Options::composition` defaults, `Luma.Development.Composition.*` (Development) | post-process space, early display encoding, vanilla encoding, gamma correction (sRGB, 2.2, hybrids), gamut mapping, UI draw type, the stage switches, split view, 2x zoom |
| Debug draw | Trace (Development) | Drawn by the composition pass, first thing in `Compose` |

## What composition does, in order

1. **CPU, every present** (`DXGISwapchain::Compose`): follows the output mode (Mirror switches buffers), then fills `Inputs`: the output encoding, whether the game's format is linear, whether its color space is PQ, `always` (Mirror), `off` (Native), debug draw active, the Mirror size ratio. `Composition::Plan` returns nothing when `off`; otherwise, outside Mirror, only when re-encoding, gamma correction, paper white scaling, UI, gamut mapping, scaling or a debug view is needed.
2. **Shader** (`Luma_DisplayComposition.hlsl`):
   * Debug draw: the debug texture, times paper white, replaces the pixel.
   * Mod inactive: decode the vanilla image (PQ to linear, sRGB or 2.2 gamma to linear, or the gamma correction of a linear SDR image), saturate unless HDR (Development), times paper white.
   * Mod active: the UI composite (UI draw type 3), then by display mode. SDR (0): saturate, then the transfer from the vanilla encoding to the output's (2.2 for an 8 or 10 bit buffer, sRGB for scRGB, where Windows adds sRGB). HDR and SDR in HDR (1, 2): decode a gamma image, or gamma-correct a linear one; 2 saturates; the split view; gamut mapping; times paper white.
   * `EncodeOutput`: PQ BT.2020 for HDR10, gamma 2.2 for 8 and 10 bit SDR, nothing for scRGB.
3. **The image is always read as linear or gamma when the mod is active.** `POST_PROCESS_SPACE_TYPE` decides it; the game's PQ color space (`game_pq`) is read only in the mod-inactive fallback.

## Which combinations work (FF7R, mod active unless said)

FF7R's output shaders (`Output_HDR`, `Output_SDR`) write the game's own encoding (PQ for HDR, gamma 2.2 for SDR) when early display encoding is off, and linear scRGB (paper white included) when it's on. FF7R sets it on.

| Mode | Output | Game writes | Result | Status |
|---|---|---|---|---|
| Mirror | HDR10, scRGB | linear (early encoding on) | correct; output switches live | verified |
| Mirror | SDR (Windows HDR off) | linear | correct, 80 nits SDR, G22 | verified |
| Mirror | HDR output, `Display Mode` SDR | linear | SDR in HDR at paper white | verified (switched live) |
| Mirror | HDR output, game's own HDR off (`Output_SDR`) | linear | display mode 0: 80 nits and the scRGB sRGB assumption on a PQ buffer, where SDR in HDR at paper white is expected | **doesn't work, should** (to confirm in game) |
| Mirror | any | the game's encoding (early encoding off) | PQ or gamma read as linear | **doesn't work, should** (the encoding is known from the color space) |
| Mirror | any | game format outside the upgrade rules | the game's image clipped at its own 8 or 10 bits | **doesn't work, should** in Development (1.0 forced the swapchain upgrade) |
| Mirror | any | mod inactive | vanilla decoded and shown | works (fallback) |
| InPlace | scRGB | linear | correct | built, not verified |
| InPlace | HDR10, SDR | linear | linear written to a 10 or 8 bit UNORM buffer: clipped above 1, negatives lost, banding | doesn't work; can't with a linear image |
| InPlace | HDR10, SDR | the game's encoding | composition re-encodes PQ or gamma as if linear | **doesn't work, should** (passthrough, or encode-only) |
| Native | game's HDR10 | linear (FF7R's default) | linear on a PQ buffer | doesn't work; the mod must write the game's encoding in Native |
| Native | game's HDR10 | the game's encoding | encoding right, but `SetUnmanaged` puts 80 nits in every white level and display mode 0, so the game's HDR is tonemapped for 80 nits | **doesn't work, should** |
| Native | game's SDR | the game's encoding | correct | works (nothing to do) |
| Native | any | debug draw, split view, 2x zoom | not drawn: composition is off | **doesn't work, should** (Development) |
| Native | any | HDR content while the game is in SDR | no HDR path: the buffer is the game's SDR one | **doesn't work, should** in Development (the forced upgrade) |
| Native | any | `Display Mode` toggle, paper white settings | hidden (no HDR output) | **doesn't work, should** when the game outputs HDR |

## What's missing, in short

1. **Composition doesn't know the image's encoding.** It infers it from `POST_PROCESS_SPACE_TYPE` when the mod is active and from the game's format and color space only when it isn't. A game writing its own encoding (FF7R with early encoding off) can't be composed.
2. **Native means unmanaged.** `SetUnmanaged` writes SDR at 80 nits, so a mod whose shaders read `LumaSettings` (FF7R's tonemapper reads peak, paper and UI white) can't do HDR in Native, and the Display settings are hidden.
3. **Native has no composition at all,** so no debug draw or development views.
4. **No forced upgrade in Development.** Mirror's game buffer is FP16 only when the game's rules cover its format, and Native never upgrades: the HDR content of an SDR game can't be shown, and the SDR and HDR toggle doesn't exist, as it did in 1.0's Development builds (always scRGB).
5. **The game's SDR on an HDR output** goes to display mode 0 (80 nits), not SDR in HDR.
6. **InPlace HDR10 and SDR** only make sense when the game writes its own encoding: with a linear image they lose range.
7. **The encodes can't be read back:** `view=swapchain` reads the game's image; nothing reads the real buffer after composition.

## Proposal

### The model

* **The source encoding is an input.** `Composition::Inputs` gets `source`: `Linear` (scaled by paper white or not), `Pq`, `Gamma` (sRGB or 2.2). Core resolves it per present: from the mod (`EARLY_DISPLAY_ENCODING`, `POST_PROCESS_SPACE_TYPE`) while the mod is active and writes Luma's space, and from the game's color space and format otherwise. The shader decodes every source to linear first (today's fallback paths, made the general first stage), then runs the stages, then encodes. "Mod active" stops choosing the decode.
* **Native is managed for the display mode, not for the buffers.** Native keeps the game's format and color space, but `DisplayMode` comes from the game's switch (`SetHdr`) and the display: HDR output when the game's color space is HDR10 or scRGB and the game says HDR, white levels from the Display settings, which are shown. `SetUnmanaged` stays only for a Native swapchain whose game isn't a Luma mod's (ReShade-only use).
* **Native composes only when needed, in place.** Development views (debug draw, split view, zoom) run composition on a copy of the real buffer: decode the native encoding, draw, encode back to it. Nothing runs otherwise, so Publishing keeps Native's zero cost.
* **Development forces the upgrade.** In Development builds the default swapchain mode is Mirror with an HDR output (scRGB or HDR10, setting), whatever the game's options say, and Mirror's game buffers always get an FP16 mirror (not only when the rules match). The game's image is decoded from its own encoding (the source model above), so the same game shaders run in Native in Publishing and in Mirror in Development. `Display Mode` (HDR, SDR, SDR in HDR) is shown in Development even while the game is in SDR, so HDR intermediate textures can be viewed and the final image toggled, as in 1.0.
* **Defaults per game.** FF7R: `mode = Native`, early display encoding off (its shaders write PQ and gamma), Development forced to Mirror. A mod that writes Luma's linear space keeps Mirror.

### Steps

1. **`Inputs::source` and the decode stage.** C++ resolution of the source encoding, the shader's decode generalised, unit tests of `Plan` for every mode, output and source. Mirror with early encoding off must then match early encoding on (FF7R, `luma_compare`).
2. **Native managed display mode.** `DisplayMode` from the game's switch and the display in Native; Display settings shown; FF7R in Native with early encoding off at the right paper white and peak.
3. **The game's SDR on an HDR output** goes to SDR in HDR at paper white, encoded with 2.2 emulation on PQ.
4. **Native composition in Development.** In place, on a copy, only while a development view is on; decode and encode the native encoding.
5. **Development forced upgrade.** Mirror plus HDR output by default in Development, FP16 game buffers always, `Display Mode` always shown in Development.
6. **InPlace.** Allowed for scRGB with a linear source; for HDR10 and SDR only with a native source (encode-only composition). Anything else falls back to Mirror with one warning.
7. **`view=output` readback** of the real buffer after composition, and one golden per verified row of the matrix (8B).
8. **FF7R defaults.** Native, early encoding off; the 8B cases then run Native in D3D11 and D3D12 against 1.0, and Development in Mirror.

### Decisions for the owner

* Is Native the default for every mod whose game has its own HDR output, or only for FF7R?
* Development's forced output: scRGB (1.0's) or HDR10 (closer to what users get)?
* In Native, does Luma ever change the game's buffers (the format, for a game whose HDR swapchain is 8 bit), or never?
* InPlace: keep (scRGB only, plus native passthrough) or retire it in favour of Mirror and Native?
