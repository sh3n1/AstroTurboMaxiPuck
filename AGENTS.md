# AGENTS.md

Guidance for coding agents working on the puck.

## What it is

AstroTurboMaxiPuck is a hand remote for **AstroTurboMaxiSolver**, the smartphone telescope
finder (a PWA). The phone sits on the telescope as the finder, reading its sensors and plate
solving; the puck, a small round screen in the hand, shows the **full sky map** as the
telescope moves, and guides to the target. It takes the place of the second phone in the
app's "Two phones" mode.

- **Device:** Waveshare ESP32-S3-Touch-AMOLED-1.75: a round 466×466 AMOLED (CO5300 driver over
  QSPI), CST9217 touch, ESP32-S3 with 8 MB PSRAM and 16 MB flash, QMI8658 IMU, AXP2101 power
  chip with a battery header, PWR and BOOT buttons.
- **The app:** at `C:/Users/Jose/Documents/AstroFinder`, also on GitHub at
  https://github.com/sh3n1/AstroTurboMaxiSolver. This folder sits inside it, ignored by its
  git, and `.claude/settings.json` gives agents read access to it. Its own `AGENTS.md` has the
  app's rules; the ones that apply here are repeated below.
- **Current state:** an empty ESP-IDF project (2026-11-20): `main/main.c` only logs the chip,
  flash and PSRAM as a bring-up check. Build steps are in `README.md`.

## Decisions taken

- The puck shows the **full sky map**, not just guidance arrows.
- **No browser simulator** of the puck: work on the real device.
- **Astronomy stays on the phone.** The app already works out where everything is
  (`src/app/core/astronomy/astronomy.ts`, with astronomy-engine). The puck draws what the phone
  sends: don't port the astronomy to the ESP32. Use its two cores for the link (core 0) and
  drawing (core 1).
- The phone must keep running while the puck is used: the app's screen wake lock is needed.
- **Screen layout and menu:** see [docs/screen-layout.md](docs/screen-layout.md). The map
  mirrors the phone's view with no panning on the puck.

## Open decisions (ask the user before building)

1. **Where the firmware lives:** this folder as its own git repository, or a `firmware/` folder
   in the app's repository. The app's `AGENTS.md` says PWA only, so either needs the user's
   go-ahead and a note there.
2. **Toolchain:** ESP-IDF (Apache-2.0) chosen, the project is set up in this folder. LVGL (MIT)
   proposed for drawing, still to be approved as a dependency. Not the Arduino core, which is
   LGPL.
3. **The link to the phone:**
   - **WebRTC over Wi-Fi**, as the app's Two phones mode does (see below). Works with iPhones
     too. Needs a shared network: the ESP32's own access point, or the phone's hotspot. The ESP32
     has no camera, so pairing is one-way: the puck shows a QR code with its IP address, ICE
     credentials and DTLS fingerprint, and the phone builds the session from it. No certificate
     problem: WebRTC's DTLS uses self-signed certificates, checked by fingerprint.
   - **Bluetooth (Web Bluetooth):** simpler, but Android only, and only about 10 to 30 KB/s.
4. **How the map is drawn**, discussed so far:
   - (A) The phone sends JPEG frames: simplest, but choppy.
   - (B) The catalogue is sent once (about 200 KB of main stars, deep-sky objects and lines;
     under 1 MB with the deep stars), then a 3×3 rotation, J2000 to view, at 20 to 30 Hz; the
     puck projects and draws.
   - (C) The phone sends the projected starfield (about 4 bytes a star, 3 to 5 KB a frame) at
     about 10 Hz, plus the rotation at 30 Hz, and the puck turns the last frame with the newest
     rotation in between ("timewarp"). The earlier lean, if a WebRTC trial works.
5. **Licences to check** before using them: the WebRTC library on the ESP32 (libpeer, or
   Espressif's), Waveshare's example code, ESP-SR.

Suggested first step: bring-up of the screen, the touch and the battery; then a first link to
the phone; only then the map.

## The app's side

- **Two phones:** `docs/two-phones.md` in the app: pairing by QR codes with no server
  (`AF1:` prefix, deflate-raw and base64url, no STUN or TURN, same Wi-Fi or a hotspot), and the
  messages over one JSON data channel. The finder sends `pose` (where the telescope points),
  `hello`, `site`, `solveLog`; the screen sends `target`, `night`, `solveNow`, `alignment`.
  The puck would take the screen's part.
- **Code:** `src/app/core/remote/` (`remote-link.ts` for codes and messages,
  `remote.service.ts`, `pose-glide.ts` for smoothing the pose between messages).
- **Sky map drawing,** to match the look: `src/app/core/sky-map/` (`draw-sky.ts`,
  `draw-targets.ts`, `sky-style.ts`) and the theme colours in `src/app/core/theme.ts`.
- **The app's look:** the reticle is three plain rings at 0.5°, 2° and 4°; the target is a
  crosshair of four short rounded strokes, green when on target; guidance is a trail of fading
  light-blue chevrons with a dashed line; never orange guidance. Night mode is full-strength red.
- **Agreeing the link:** what the puck and the app send each other should be written down in
  the app's repository (a `docs/puck.md`), so changing one side doesn't break the other.

## Rules carried over from the app

- **Licence:** the app is GNU GPL version 3 only (`GPL-3.0-only`), copyright "The
  AstroTurboMaxiSolver authors", and public. The puck's firmware should be the same. Any
  library or code must be GPL-compatible (MIT, BSD, Apache-2.0, GPL-3.0); never
  "non-commercial", GPL-2.0-only, or with extra restrictions. Check the licence before using
  anything, and keep required attributions.
- **The site is non-commercial** while it uses non-commercial data (Open-Meteo, the light
  pollution atlas): nothing may earn money.
- **Language:** code comments, documentation and every Markdown file in English, whatever
  language the conversation is in. The user writes in Spanish or B2 English.
- **Code style:** clean, simple, human readable and well commented. Don't reinvent the wheel.
  Files under about 500 lines, never over 800.
- **Ask before:** adding a dependency, committing or pushing, flashing anything that could
  brick the device, changing the app's PWA-only rule.
- **Never:** commit secrets (such as Wi-Fi passwords), force-push `main`.
- **Git:** `main` for releases, `develop` for work, short-lived feature branches, merged with
  `--no-ff`. Commit messages short and imperative.
- **Readability:** many users are past forty and read the screen at arm's length in the dark:
  big text, no grey text in night mode, red at full strength.

## Memory

An agent's notes from the app's sessions are in
`C:/Users/Jose/.claude/projects/c--Users-Jose-Documents-AstroFinder/memory/`, especially
`esp32-remote-idea.md`. On the first session here, read them and copy what matters for the puck
into this project's own memory.
