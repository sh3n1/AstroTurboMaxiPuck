# TODO

Tasks to build the puck described in `AGENTS.md` and `docs/screen-layout.md`, in order: each
task only depends on tasks above it. "App" marks work in the app's repository
(AstroTurboMaxiSolver). **Ask** marks a decision for the user before building.

## Done

- Bring-up: screen, touch, LVGL on core 1 (`main/board.c`).
- Wi-Fi in station mode (`main/wifi.c`).
- WebRTC with libpeer, our copy with fixes (`components/libpeer/README.md`).
- One-QR-code pairing with the app (`main/link.c`; app: `puck-pairing.ts`, "Link to the
  puck" in Two phones). The puck shows the app's Alt and Az.
- SCTP partial reliability in libpeer: Forward-TSN announced and handled, unordered messages
  taken after a gap. Tested 2026-11-21 dropping every 20th message: poses kept arriving at
  about 19 a second.
- SCTP messages in pieces put back together, up to 16 KB (`max-message-size:16384` in the
  app's answer). Tested 2026-11-21 with a 5 KB message.

## 1. The link

3. **Write the contract in the app's `docs/puck.md`** (App). *Written 2026-11-21, to
   review.* Map option B: the QR format (`AFP1:`, 82 bytes), `auth`, three channels (control,
   `view`, catalogue), the 68-byte `view` (two quaternions: J2000 to local, local to view),
   how the puck projects (refraction, stereographic, as the app), the catalogue format and
   transfer (kept in flash by its SHA-256 `id`), and `objects` for what moves. The data
   licences allow it: the catalogue reaches the puck only over the link, never in the
   firmware, and carries the app's credits.
4. **The phone's screen while linked to the puck.** *Decided and written 2026-11-21, to
   test with the puck.* The phone works as usual, with no special view and no finder mode:
   the puck is only a remote. The puck's link is now its own (`puck-link.service.ts` in the
   app), apart from Two phones, so a screen phone of two devices can have both. Until task
   5 it sends a JSON `pose` on the `view` channel, which the test firmware shows.
5. **App sends the view** (App, `puck-link.service.ts`): when linked to the puck, send the
   binary `view` of `docs/puck.md` at about 30 Hz on its own unreliable channel, and
   `targetInfo` (the target's name) on every change. Depends on 3, 4.
6. **Puck reads the view**: parse `view` and `targetInfo` in `main/link.c`, replacing the
   test's text search, and hand them to the screen through a small shared state, safe
   between core 0 (link) and core 1 (drawing). Depends on 3, 5.
7. **Puck sends commands** (App receives): `solveNow` (reticle tap), `step` (left and right
   arrows), `night`. The app handles `step` with its playlist (`playlist.service.ts`).
   Depends on 3, 6.
8. **Losing and finding the link**: the puck notices a silent link (no `view` for 4 s),
   says so, and shows a new QR code; the app shows "link lost" as for two phones.

## 2. The main screen

Layout in `docs/screen-layout.md`. The app's look: rings at 0.5°, 2° and 4°, a green
crosshair on target, light-blue chevrons, never orange. Big text, red at full strength at
night, no grey.

9. **Screen framework**: one LVGL screen with the zones (top edge, centre, bottom third),
   the app's colours as one palette, and a night palette (full-strength red). Replace the
   bring-up test screen. Depends on 6.
10. **Top edge**: Alt, Az and FOV written along the curve (LVGL's arc label). Depends on 9.
11. **Reticle and guidance**: the three rings scaled by the field of view, the target
    crosshair (green when on target), and the guidance chevrons pointing to the target.
    Depends on 9.
12. **Tap on the reticle plate solves**: send `solveNow`, and show the solving state (the
    rings fade in and out, as in the app). Depends on 7, 11.
13. **Bottom third, media-player style**: the target's name on one line; left arrow, Menu,
    right arrow; the distance bar (further left is further away); the angular distance in
    degrees. Left and right send `step`. Depends on 7, 9.
14. **Night mode**: switch palettes, in step with the app's `night` message both ways.
    Depends on 7, 9.

## 3. The sky map

15. **App sends the catalogue** (App): the stars (and later deep-sky objects and lines) the
    phone's map uses, in a compact binary form, with a version. The puck keeps it in flash
    (its own partition) and asks for it only when its version differs. **Ask:** how deep
    (main stars only, about 200 KB, or the deep stars too, under 1 MB), and how the phone's
    rectangular map fits the round screen. Depends on 3, 5.
16. **Puck projects and draws the stars** as the background, behind the reticle: rotate each
    star by the newest `view`, project, and draw with the app's colours and sizes by
    magnitude (mirroring `sky-style.ts`), only the stars bright enough for the field of view.
    Depends on 9, 15.
17. **Moving objects** (App and puck): the Moon, planets and comets as a small list of
    J2000 positions, sent on link and every minute or so. Depends on 16.
18. **The rest of the map**: constellation lines, deep-sky objects, planets, the horizon and
    labels, added to the catalogue or the moving list. Depends on 17.

## 4. The menu

Items in `docs/screen-layout.md`. Each needs its own design: what it shows on the puck, and
which messages it needs.

19. **Menu screen**: opened by the Menu button, big round-friendly items, back to the map.
    Depends on 13.
20. **Night mode** item: the same switch as task 14. Depends on 14, 19.
21. **Settings → Brightness**: `board_set_brightness()`, kept in NVS. Depends on 19.
22. **Sensors, Eyepiece, Lists, Alignment**: what each does on the puck, and the messages to
    the app. **Ask** for each. Depends on 19.
22b. **Zoom**: zoom the map in and out from the puck. The puck mirrors the phone's map, so it
    asks the app to change its field of view (a new `zoom` message), and the new view comes
    back in `view`. Zooming isn't panning: the puck still can't move the map. **Ask:** how
    (buttons in the menu, a step at a time, or preset fields of view). Depends on 6, 7, 19.
23. **Settings → Wi-Fi → Client**: choose the network and enter its password without
    menuconfig. Typing on a round screen is hard: perhaps sent from the app over the link,
    or set up through the puck's own access point. **Ask.** Depends on 19.
24. **Settings → Wi-Fi → AP**: the puck's own access point, for the field without a router:
    the phone joins it. Pairing then works the same. Depends on 23.
25. **Meade LX200 Repeater**: **Ask** what it should do. Perhaps the puck answers LX200
    commands over Wi-Fi (TCP), so planetarium apps such as Stellarium or SkySafari see where
    the telescope points. Depends on 6, 19.

## 5. Power

26. **Battery** (AXP2101, XPowersLib, MIT, keep its attribution): battery level on screen,
    charging, low-battery warning. Waits for a battery.
27. **Power saving**: screen off after a while, wake on touch or movement (QMI8658 IMU),
    PWR button to sleep and switch off. Depends on 26.

## Improvements

- **ICE credentials from the hardware random generator.** libpeer seeds them from the clock
  (`utils_random_string` with `srand(time(NULL))`), and the puck has no real clock, so they
  are predictable. Someone on the same Wi-Fi could guess them and keep taking the pairing
  slot (they can't pair without the secret, but they could stop the user from pairing). Use
  `esp_fill_random`, as the secret does.
- **Hide the QR code after a few minutes of waiting**, with a tap on the screen to show it
  again (with fresh credentials and secret), so it isn't left on screen for anyone to scan.
- **Remove the lite pairing check logging** in `components/libpeer/src/agent.c` once pairing
  is known to be reliable.
- **Pair again without a new QR code** after the puck or the app restarts (keep the
  certificate and a pairing key in NVS and the app's storage).
- **Strip libpeer's audio and video code**, so libsrtp (`sepfy/srtp`) and its compiler
  workaround in `CMakeLists.txt` can go.
- **Weak Wi-Fi signal** (−91 dBm on the first test): measure the link near the router; the
  worst round trip was 444 ms.
- **App: split `remote.service.ts`** (about 590 lines, the app's limit is about 500). The
  puck's part is already out, in `puck-link.service.ts`.
- **App: end-to-end test** for pairing with the puck, faking the puck's side.
- **Faster full builds on Windows**: Defender exclusions for `C:\Espressif`, `C:\esp` and
  `build/` (the user's choice).
