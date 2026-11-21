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
- **Current state (2026-11-21):** what is done and what is next is in `TODO.md`. In short:
  - **Tested on the board** (ESP-IDF v6.1.1, COM3, the ESP32-S3's own USB): the screen,
    touch and LVGL (`main/board.c`; colours and touch direction right; the touch chip is a
    CST9217, though Waveshare's wiki says FT3168 in one place); the Wi-Fi (`main/wifi.c`);
    the one-QR-code pairing with the app and libpeer fixes (`main/link.c`,
    `components/libpeer/README.md`); the binary `view` and `targetInfo` (`main/view_state.c`),
    shown on a test screen (`main/main.c`) with rings that follow the field of view; and the
    commands (reticle tap, step, night by long press) with the app's `state`.
  - **Also tested on the board (2026-11-21):** a lost link noticed both ways (`ping`, 4 s
    of silence); the sky catalogue (2.1 MB, faint stars included) sent by the app and kept in
    a 4 MB flash partition (`main/catalog.c`); the sky drawn behind the rings as the phone's
    (`main/sky_draw.c`, `main/sky_stars.c`, `main/sky_objects.c`, `main/sky_labels.c`,
    `main/sky_pixels.c`): stars, lines, grid, horizon, objects, labels,
    the Moon and planets (`main/moving.c`), the eyepiece and ring views; the puck's own zoom
    (`main/zoom.c`); night mode; the QR code's heartbeat. libpeer's SCTP now has a real
    receive window and keeps early packets; the Wi-Fi and lwIP buffers are in PSRAM.
  - **Smooth since 2026-11-21:** 46 to 60 ms a frame, and the view glides between
    messages (`main/view_glide.c`).
  - **Not yet good enough:** the puck never resends what it sends, which can still drop
    the link.
  - Build steps are in `README.md`, libraries and their licences in `docs/third-party.md`.
    The battery (AXP2101) waits until the user has one.

## Decisions taken

- The puck shows the **full sky map**, not just guidance arrows: **the same items as the
  phone's map**, wherever the puck can technically draw them (2026-11-21). What's missing
  is listed in `TODO.md`, task 18b.
- **Every translated word comes from the phone** (2026-11-21), in the app's `texts` message:
  the firmware holds no translations. Names and designations are never translated. Only
  what shows before any link (joining the Wi-Fi, the QR code) is in the firmware, in
  English.
- **No browser simulator** of the puck: work on the real device.
- **Astronomy stays on the phone.** The app already works out where everything is
  (`src/app/core/astronomy/astronomy.ts`, with astronomy-engine). The puck draws what the phone
  sends: don't port the astronomy to the ESP32. Use its two cores for the link (core 0) and
  drawing (core 1).
- The phone must keep running while the puck is used: the app's screen wake lock is needed.
- **Screen layout and menu:** see [docs/screen-layout.md](docs/screen-layout.md). The map
  follows where the phone points, with no panning on the puck, but **zooms on its own**
  (2026-11-21): the menu's Zoom shows +, −, ✓ and ✗ over the map, and nothing is sent to
  the phone. In the phone's eyepiece view the puck follows the eyepiece's field.
- **Linking to a phone:** see [docs/linking.md](docs/linking.md): the position always comes
  from the phone on the telescope, straight from it in normal mode, or through the screen
  phone in Two phones; never linked to the finder; changes go both ways.

## Open decisions (ask the user before building)

1. **Where the firmware lives:** decided, this folder as its own git repository (initialised
   2026-11-20, branches `main` and `develop`, no remote yet). The app's `AGENTS.md` notes it as
   the exception to its PWA-only rule. The link is drafted in the app's `docs/puck.md`.
2. **Toolchain:** decided, ESP-IDF (Apache-2.0) with LVGL 9 (MIT) through `esp_lvgl_port`. Not
   the Arduino core, which is LGPL. Not Waveshare's board package either: it pulls in a
   closed-source JPEG library that GPL-3.0 doesn't allow (see `docs/third-party.md`).
   LVGL draws on core 1.
3. **The link to the phone:** decided 2026-11-21: WebRTC with **libpeer** (MIT,
   github.com/sepfy/libpeer), kept as our own modified copy in this repository so pairing
   needs only the puck's QR code: fixed ICE credentials shown in the QR, the phone builds the
   puck's answer itself, and a secret from the QR in the first message replaces the DTLS
   fingerprint check. Not Espressif's `esp_peer` (closed library, Espressif-only licence). The
   puck **joins the user's Wi-Fi** (station mode), not its own access point; the Wi-Fi name
   and password go in `sdkconfig` (menuconfig), never in git. Options as first discussed:
   - **WebRTC over Wi-Fi**, as the app's Two phones mode does (see below). Works with iPhones
     too. Needs a shared network: the ESP32's own access point, or the phone's hotspot. The ESP32
     has no camera, so pairing is one-way: the puck shows a QR code with its IP address, ICE
     credentials and DTLS fingerprint, and the phone builds the session from it. No certificate
     problem: WebRTC's DTLS uses self-signed certificates, checked by fingerprint.
   - **Bluetooth (Web Bluetooth):** simpler, but Android only, and only about 10 to 30 KB/s.
4. **How the map is drawn:** decided 2026-11-21, option (B): the phone sends the catalogue
   once (about 200 KB of main stars, deep-sky objects and lines; under 1 MB with the deep
   stars), kept in the puck's flash with a version, then the rotation, J2000 to view, at
   about 30 Hz; the puck projects and draws, mirroring the app's `sky-style.ts`. The phone
   still does the astronomy (the rotation holds sidereal time, precession and the
   alignment) and sends the moving objects (Moon, planets) as a small list. Chosen because
   the S3 projects 10,000 stars in about 2 ms, and a late or lost 44-byte `view` costs
   nothing on a weak Wi-Fi. Not chosen: (A) JPEG frames, choppy and heavy; (C) projected
   stars at 10 Hz with "timewarp" in between, which smears on fast slews.
5. **Licences to check** before using them: the WebRTC library on the ESP32 (libpeer, or
   Espressif's), ESP-SR, XPowersLib for the AXP2101 (MIT, keep its attribution text). Waveshare's
   example code is Apache-2.0. Also to look into before Wi-Fi: ESP-IDF's Wi-Fi driver ships as
   closed binary libraries, as for every ESP32 firmware; how GPL-3.0 projects on the ESP32
   handle that. Decided 2026-11-21: no statement or linking exception, as other GPL-3.0
   ESP32 projects (Tasmota, Meshtastic) do.

The tasks still to do, in order of what depends on what, are in `TODO.md`.

## The app's side

- **Two phones:** `docs/two-phones.md` in the app: pairing by QR codes with no server
  (`AF1:` prefix, deflate-raw and base64url, no STUN or TURN, same Wi-Fi or a hotspot), and the
  messages over one JSON data channel. The finder sends `pose` (where the telescope points),
  `hello`, `site`, `solveLog`; the screen sends `target`, `night`, `solveNow`, `alignment`.
  The position always comes from the phone on the telescope: the puck links straight to it in
  normal mode, or to the screen phone in Two phones, never to the finder; the phone it links to
  stays as it is. See [docs/linking.md](docs/linking.md).
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
