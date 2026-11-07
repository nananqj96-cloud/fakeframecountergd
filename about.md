# Fake Frame Counter

**Fake Frame Counter** brings the iconic showcase-style frame-window circles (`1F`, `4F`, `12F`, etc.), pitched ding chimes, a compact top-right counter, and built-in **Mega Hack macro playback** directly into Geometry Dash.

## Disclosure: Fake / Visual Estimate Mode

By default, **Fake Frame Counter** runs in **Fake / Visual Estimate Mode**. In this primary mode, the frame-window numbers (`1F` through `12F+`) are **simulated visual estimates** generated from obstacle clearance, speed, gamemode dynamics, and configurable rules for entertainment and visual flair—**not** verified frame-perfect physics calculations.

To keep your gameplay screen and recordings completely clean, **no `"fake"`, `"simulated"`, or `"estimate"` disclaimer text is rendered on the gameplay overlay or counter**.

An optional **Experimental Real Analysis (Macro Scan)** mode is also included for deterministic 0% macro step-perturbation testing, with its limitations documented in the settings help and README.

## Highlights

- **World-Locked Circles & Labels**: Colored circles and matching frame labels (`1F`–`12F+`) attach to the level (`m_objectLayer`) and move naturally with the camera.
- **Configurable Color Tiers**:
  - <cr>`1F–4F`: Red</c>
  - <co>`5F–6F`: Orange</c>
  - <cy>`7F–9F`: Yellow</c>
  - <cb>`10F–12F+`: Blue</c>
- **Descending Pitched Ding Sounds**: `12F` plays the highest-pitched bell chime, stepping down progressively through `11F`, `10F`, ..., to `1F` (lowest pitch). Includes a slow, clear **Sound Preview Guide (`12F -> 1F`)**.
- **Smart Anti-Spam Filter**: Deduplicates held/same-tick/dual-mirror inputs, filters out non-meaningful releases in Cube/Ball/UFO/Spider, enforces minimum world spacing, and limits sustained spam while preserving rapid click bursts.
- **Mega Hack Macro Import & Playback**: Load and play `.mhr`, `.mhr.json`, `.gdr2`, `.gdr` (JSON & MessagePack), `.json` (TASBot), `.zbf`, `.re3`, `.xd`, and `.txt` macros directly in-game.
