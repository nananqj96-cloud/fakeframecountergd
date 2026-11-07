# Fake Frame Counter (`nananqj96.fake-frame-counter`)

A Geometry Dash **Geode** mod that displays showcase-style **frame-window circles (`1F`, `4F`, `12F`, etc.)**, plays **descending pitched ding chimes**, tracks **per-category counts in a compact top-right HUD**, and supports **Mega Hack macro import & playback** directly in-game.

---

## Supported Versions

| Component | Version |
| :--- | :--- |
| **Geometry Dash** | `2.2081` (Windows 64-bit, Android 64/32-bit, macOS, iOS) |
| **Geode SDK / Loader** | `v5.11.2` (`>=5.11.2`) |
| **Dependencies** | `geode.node-ids` (`>=v1.23.3`) |

---

## Important Disclosure: Fake vs. Experimental Real Mode

### 1. Fake / Visual Estimate Mode (Primary — Default)
- **What it does:** Works immediately and independently in both **live play** and **macro playback** on any level without requiring a pre-scan.
- **How labels are generated:** Frame-window numbers (`1F` through `12F+`) in this mode are **simulated visual estimates** calculated from:
  1. Nearest hazard clearance (`m_hazardCollisionObjects`) and solid obstacle clearance (`m_solidCollisionObjects`),
  2. Gamemode, mini-icon status, vertical velocity (`m_yVelocity`), and player speed (`m_playerSpeed`),
  3. Orb/pad/portal interaction flags and click-to-click cadence,
  4. A power-law quantile bijection over a golden-ratio Weyl sequence that preserves realistic spread across all enabled categories (`1F`–`12F+`) while biasing tight, high-speed, close-hazard maneuvers toward smaller frame numbers (`1F–4F`) and open movements toward larger windows (`10F–12F`).
- **Clean Gameplay Screen:** By design, **no `"fake"`, `"simulated"`, or `"estimate"` text is shown on the gameplay screen or top-right counter** during play or macro playback so your screen stays clean. Disclosure is provided in the mod settings (`mod.json`), the in-game `FFC` menu popup, `about.md`, and this README.

### 2. Real Analysis Mode (Experimental Macro Scan)
- **What it does:** When you load a macro at 0% (normal mode, non-platformer, no StartPos) and click **`Exp. Real Scan`** in the FFC Popup, the mod runs a two-phase deterministic step-perturbation simulation (`src/core/RealAnalyzer.*`):
  1. **Reference Pass:** Steps the macro from tick `0` at fixed `1 / TPS` (`GJBaseGameLayer::getModifiedDelta`) and records reference trajectory snapshots (`x`, `y`, `yVelocity`, `levelTime`) at every input tick.
  2. **Perturbation Probing:** For each candidate input, shifts that input by `d = -1, -2, ..., -N` and `d = +1, +2, ..., +N` ticks, advances the simulation for a survival horizon, and checks whether the player survives (`!m_isDead`) and stays within trajectory tolerance of the unperturbed run. The surviving tick count is converted to 60 FPS frame equivalents (`1F`–`12F`).
- **Why it is marked Experimental & Limitations:**
  - In live play, true frame-window calculation would require cloning and stepping the entire `PlayLayer` physics state dozens of frames into the future on every click, which is not feasible at 240 FPS without frame drops or side effects on Cocos2d/FMOD state.
  - Even for macro scanning, levels with heavy random triggers, unseeded RNG, or complex camera/keyframe triggers may diverge when re-simulated from 0% without full mid-attempt state serialization.
  - In Experimental Real Mode, **unverified inputs are never filled in with fake estimates**—only genuinely measured macro inputs are labeled.

---

## Features

### 1. Live Play & Mega Hack Macro Playback
- **Live Play:** Jump, fly, wave, or dash normally in any level; meaningful inputs spawn a world-locked circle and matching frame-window label at the player's position.
- **Mega Hack Macro Import & Playback:**
  - Import and play macros directly from the in-game **FFC Popup** (`F8` or the `"FFC"` button on the Pause Menu).
  - Locks physics step delta (`GJBaseGameLayer::getModifiedDelta`) to `1 / TPS` during playback (configurable) and injects actions in `GJBaseGameLayer::processCommands` at exact `m_currentProgress` ticks.
  - Supports **9 macro formats** out of the box:
    - **Mega Hack Replay Binary** (`.mhr` — `"HACK"` magic header)
    - **Mega Hack Replay JSON** (`.mhr.json` — `meta.fps` + `events` array)
    - **GDReplayFormat v2 Binary** (`.gdr2` — `"GDR"` v2 delta-varint format used by Mega Hack v8/v9, Eclipse, xdBot)
    - **GDReplayFormat v1 JSON & MessagePack** (`.gdr` — used by Mega Hack 2.2 Replay & GDMegaOverlay)
    - **TASBot JSON** (`.json` — `fps` + `macro` array with `frame` and `player_1`/`player_2` click states)
    - **zBot Frame Binary** (`.zbf` — `f32 delta, f32 speedhack` + 6-byte records)
    - **ReplayEngine 3 Binary** (`.re3`)
    - **xdBot Pipe-Separated Text** (`.xd`)
    - **Plaintext Frame Action List** (`.txt` — `<fps>` header + `<frame> <down> <button> <p2>` lines)
  - Malformed, empty, or truncated files are rejected gracefully with a clear error message and never crash the game.

### 2. World-Locked Circles, Colors & Top-Right Counter
- **World-Locked Markers:** Circles and their labels (`1F`, `4F`, `12F`, etc.) are added to `PlayLayer::m_objectLayer` so they stay anchored to the exact spot in the level and scroll naturally with the camera.
- **Default Tier Colors (Configurable in Settings & Per Category):**
  - **`1F–4F`**: Red (`#FF3B30`)
  - **`5F–6F`**: Orange (`#FF9500`)
  - **`7F–9F`**: Yellow (`#FFCC00`)
  - **`10F–12F+`**: Blue (`#32ADE6`)
  - Each circle and its text label always use the **exact same matching color**.
- **Compact Top-Right Counter:**
  - Displays `Total: <N>` and the count for each enabled category (`1F: 0`, `2F: 0`, ..., `12F: 0`) in matching category colors.
  - Automatically formats into two compact columns when more than 6 categories are enabled so it never crowds the gameplay screen.

### 3. Detection & Anti-Spam Filter
How `src/core/MomentDetector.*` avoids circle spam while keeping rapid sequences intact:
1. **Duplicate & Hold Suppression:** Rejects repeated inputs on the same `(tick, button, player, down)` tuple and suppresses repeated `down=true` events while a button is already held (`DuplicateEvent`).
2. **Non-2P Dual Mirror Suppression:** In normal 1-player dual mode (`m_isDualMode && !m_twoPlayerMode`), a single click triggers `handleButton` for both Player 1 and Player 2 on the same tick. The filter suppresses the redundant P2 mirror event (`DualMirrorDuplicate`) so one click never spawns two simultaneous circles.
3. **Smart Gamemode Release Filter:** In **Cube, Ball, UFO, Spider, and Swing**, releasing the button has no physics effect (Unless holding a Dash Orb). By default (`Smart (Wave/Ship/Dash/Robot)`), releases are only eligible in **Wave**, **Ship**, active **Dash Orb** holds, or rising **Robot** jumps—preventing double-circle press+release clutter on every jump.
4. **Spatial Proximity Guard:** Enforces a configurable minimum world-space distance (`Min Circle Spacing`, default `18` units ≈ `0.6` blocks) between consecutive markers (`TooCloseInSpace`), bypassed only when clicking an orb/portal or reversing direction in Wave/Ship.
5. **Salience Threshold (`Selective` / `Balanced` / `Permissive`):** Computes a salience score from hazard proximity, solid proximity, vertical direction change, orb/pad/portal interaction, mini/speed state, and gamemode. Open-space fluff clicks below the threshold are ignored (`BelowSensitivityThreshold`).
6. **Burst-Aware Sliding-Window Density Limiter:** Caps sustained spam over a 1.0-second sliding window (`Max Moment Density`, default `8/s`) without imposing a long blanket cooldown, so genuine rapid bursts (e.g. double clicks or rapid wave corridors) are preserved.

### 4. Configurable Categories (`1F–12F+`), Pitched Ding Sounds & Sound Guide
- **Categories (`1F`–`12F` + Custom Numbers):**
  - Open **Categories (`1F-12F+`)** in the FFC Popup to enable or disable any individual category (`1F` through `12F`) or add custom frame numbers (e.g., `15F`, `20F`, `24F`, `30F`).
  - Only **enabled** categories are generated, played, and counted.
- **Descending Pitched Ding Sounds:**
  - Built-in synthesized bell chimes (`resources/ding_1f.wav` .. `resources/ding_12f.wav`) give each category a distinct pitch:
    - **`12F`**: **Highest pitch** (`1307.67 Hz`)
    - **`11F`**: `1171.21 Hz`
    - **`10F`**: `1049.00 Hz`
    - **`9F`**: `939.53 Hz`
    - **`8F`**: `841.49 Hz`
    - **`7F`**: `753.68 Hz`
    - **`6F`**: `675.03 Hz`
    - **`5F`**: `604.59 Hz`
    - **`4F`**: `541.50 Hz`
    - **`3F`**: `485.00 Hz`
    - **`2F`**: `461.95 Hz`
    - **`1F`**: **Lowest pitch** (`440.00 Hz`)
  - Includes volume control, quick mute, optional global custom sound file, and per-category custom sound assignment (`.wav`, `.ogg`, `.mp3`).
- **Sound Preview Guide (`12F -> 1F`):**
  - Click **`Sound Guide (12F->1F)`** in the FFC Popup to open an interactive audio-visual guide that steps slowly (`1.15s` per category) through all enabled categories in **descending order**:
    - `12-frame window` (`12F`, highest pitch) -> `11-frame window` (`11F`) -> `10-frame window` (`10F`) -> ... -> `1-frame window` (`1F`, lowest pitch).

---

## Installation & Usage

### Installing the `.geode` Package
1. Ensure **Geometry Dash `2.2081`** and **Geode Loader `v5.11.2`** are installed.
2. Download `nananqj96.fake-frame-counter.geode` from the [GitHub Releases](https://github.com/nananqj96-cloud/fakeframecountergd/releases) page (or the root of this repository).
3. Copy `nananqj96.fake-frame-counter.geode` into your Geometry Dash `geode/mods/` folder:
   - **Windows:** `C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\geode\mods\`
   - **Android:** `/storage/emulated/0/Android/media/com.robtopx.geometryjump/game/geode/mods/`
   - **macOS:** `~/Library/Application Support/GeometryDash/geode/mods/`
4. Launch Geometry Dash.

### In-Game Controls
- **Pause Menu:** Click the **`FFC`** button on the right side of the Pause Menu inside any level.
- **Keybinds (Configurable in Geode Settings):**
  - **`F8`**: Open the **Fake Frame Counter** popup menu.
  - **`F7`**: Toggle overlay visibility (`Circles` + `Top-Right Counter`) on/off.

---

## Building from Source & Running Tests

### Run the Standalone C++23 Unit Test Suite
The core logic (`src/core/`) is decoupled from Geode/Cocos2d headers and tested by `tests/core_tests.cpp`:
```bash
cmake -B build-tests -DFFC_BUILD_TESTS=ON
cmake --build build-tests
./build-tests/ffc_core_tests
```
Or directly with `g++`:
```bash
g++ -std=c++23 -O2 -Wall -Wextra -Werror -Isrc \
    src/core/Categories.cpp \
    src/core/MomentDetector.cpp \
    src/core/Macro.cpp \
    src/core/MacroPlayer.cpp \
    src/core/RealAnalyzer.cpp \
    tests/core_tests.cpp \
    -o build_core_tests && ./build_core_tests
```

### Build the `.geode` Mod Package
With the [Geode CLI](https://github.com/geode-sdk/cli) (`v4.0.0+`) and **Geode SDK `v5.11.2`** installed:
```bash
geode build
```

---

## License

Released under the [MIT License](LICENSE).
