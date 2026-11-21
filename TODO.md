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
5. **App sends the view** (App, `puck-link.service.ts`). *Done 2026-11-21, tested with the
   puck: about 30 views a second.* The map publishes each frame (`map-view.service.ts`),
   `puck-view.ts` packs it into the 68-byte binary `view`, sent on its own unreliable
   channel, and `targetInfo` goes on every change.
6. **Puck reads the view.** *Done 2026-11-21, tested.* `main/link.c` reads `view` (on the
   channel found by its label) and `targetInfo`; `main/view_state.c` hands them from core 0
   to core 1, dropping late views; the test screen shows Alt, Az, the field of view, the
   target and its distance, and sizes the rings by the field of view. Greek letters in
   names don't show yet: the font lacks them (task 9).
7. **Puck sends commands.** *Done 2026-11-21, tested: the app answers in 30 to 140 ms.* On
   the test screen, a tap on the reticle sends `solveNow`, taps at the left and right edges
   `step`, and a long press `night`; `link_send_command` queues them for the link task. The
   app (`puck-link.service.ts`, `puck-messages.ts`) does each as its own button would, and
   sends `state` (night, solving), which the puck follows: night palette, dimmed rings while
   solving.
8. **Losing and finding the link.** *Written 2026-11-21, to test on the board.* Each side
   sends something at least once a second (the app a `view`, or `state` before its map has
   drawn; the puck `ping`), and takes 4 s of silence as a lost link: the puck shows a new
   QR code with "Link lost: scan again", the app a "Puck disconnected" toast and "Link
   lost" on its Remote page. Test: link, then switch the puck off, or close the app.

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

15. **App sends the catalogue.** *Written 2026-11-21, to test on the board.* The app builds
    it (`puck-catalog.ts`, tested; `puck-catalog.service.ts`): the map's stars to 6.5, the
    constellation lines and names, the fixed objects with the map's labels and label rules,
    and the data credits; about 505 KB in 32 parts. After `puckHello` it sends
    `catalogStart` and the parts on the catalogue channel, unless the puck holds the same
    `id`. The puck (`main/catalog.c`) collects it in PSRAM, checks the CRC, and a task of
    its own writes it to the new 2 MB `catalog` partition (`partitions.csv`: the flash needs
    its partition table written again, which `idf.py flash` does). Test: link, and the log
    shows "Receiving", "Writing", "kept", then on the next link "current". **Ask:** the
    faint stars (to magnitude 9, about 1.2 MB more) for the eyepiece view. For now the
    puck's diameter shows the width of the phone's map (`viewField`). Depends on 3, 5.
16. **Puck projects and draws the stars.** *Written 2026-11-21, to test on the board.*
    `main/sky_draw.c`: a full-screen canvas in PSRAM behind everything; each new view
    turns, refracts and projects the stars (brightest first, to the view's limit) and the
    constellation lines as the app does, with its tints, sizes, fading below the horizon,
    and night red. Things to check on the board: the sky matches the phone's, star sizes
    (`STAR_SCALE`), and whether 30 frames a second keep up. Depends on 9, 15.
17. **Moving objects.** *Written 2026-11-21, to test on the board.* The app sends
    `objects` (`puck-objects.ts`, tested): the Moon, planets and bright asteroids with their
    labels, sizes and phases, once linked and once a minute. The puck reads them
    (`main/moving.c`, with the JSON readers now in `main/json_read.c`) and draws each as a
    disc of its size over the stars. Still to do: the Moon's and Venus's phase, turned to the
    Sun, and the labels (task 18). Depends on 16.
18. **The rest of the map.** *Mostly written 2026-11-21, to test on the board.*
    `main/sky_objects.c` draws a top layer as the app's map does: the horizon and compass
    points, a small ring for deep-sky objects or their size circle (the Messier and bright
    ones), labels beside objects, planets and the Moon (the catalogue's labels and label
    rules; up to 48 labels, none overlapping, those above the horizon first), and the
    constellation names last. The shared maths and colours are in `main/sky_common.h`;
    labels use LVGL's Montserrat 20 (turned on in `sdkconfig.defaults`). Things to check on
    the board: that labels keep the frame quick (each is drawn five times, for its dark
    outline), and their size. The compass letters come from the app (`texts`, in its
    language). Depends on 17.
18b. **Show what the phone's map shows** (decided 2026-11-21: the same items, wherever the
    puck can technically draw them). Compared with what the app's map draws
    (`sky-canvas.component.ts`, `draw-sky.ts`, `draw-targets.ts`):
    - *Written 2026-11-21, to test on the board:* the alt-az grid; the Moon's and Venus's
      phase, turned towards the Sun; dashed size circles; the eyepiece's circle and the dark
      mask around it (the app's `state` now sends `eyepieceFieldDeg`); the selected target's
      green crosshair, placed from the `view`'s distance and direction.
    - Still to do:
      - the app's own icons for galaxies, nebulae, clusters, planets, Pluto, Ceres and the
        asteroids (`target-icons.ts`), instead of plain rings and discs;
      - the selected target's label first and in full below the horizon (the puck doesn't
        know which catalogue object it is: `targetInfo` could carry its label);
      - the pointer to an off-screen target (`drawPointer`) and the chevrons (task 11);
      - the reticle's solving pulse (task 12) and its ring labels ("2°");
      - the faint stars in the eyepiece view (task 15's question);
      - the user's own targets, not yet sent (`objects` could carry them);
      - Greek letters in labels, such as "κ Cyg" (the font lacks them; task 9).

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

- **ICE credentials from the hardware random generator.** *Done 2026-11-21, to test on
  the board.* libpeer seeded them from the clock, which the puck doesn't have, so someone on
  the same Wi-Fi could guess them and keep taking the pairing slot. Now `esp_random`.
- **Resend lost messages from the puck.** libpeer's SCTP never resends what the puck sends,
  so on a weak signal a command (a tap, a step) can be lost and the tap does nothing. Keep
  unacknowledged DATA chunks on the control channel and resend them until the SACK covers
  them.
- **The QR code's slow heartbeat.** *Written 2026-11-21, to test on the board.* While the
  QR code shows, the screen's brightness swells from dim (50 of 255) to full and back every
  3 s, so a phone camera dazzled by the white AMOLED gets a darker moment to read it
  (`beat_qr` in `main/main.c`). To tune on the board: the dim level and the period.
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
