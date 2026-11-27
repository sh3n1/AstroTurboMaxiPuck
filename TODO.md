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
8. **Losing and finding the link.** *Done 2026-11-21, tested on the board.* Each side
   sends something at least once a second (the app a `view`, or `state` before its map has
   drawn; the puck `ping`), and takes 4 s of silence as a lost link: the puck shows a new
   QR code with "Link lost: scan again", the app a "Puck disconnected" toast and "Link
   lost" on its Remote page. Test: link, then switch the puck off, or close the app.

## 2. The main screen

Layout in `docs/screen-layout.md`. The app's look: rings at 0.5°, 2° and 4°, a green
crosshair on target, light-blue chevrons, never orange. Big text, red at full strength at
night, no grey.

*Tasks 9 to 14 done 2026-11-22, tested on the board (`main/screen.c`): the readout along
the top edge over a dark band, rings without labels, guidance, the bottom third shown by a
tap on the map for 15 s, Noto Sans for Greek letters (`main/fonts/`). Screenshots over the
cable: `tools/screenshot.py`; the log's last 20 minutes: `tools/puck_log.py`.*

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

15. **App sends the catalogue.** *Done 2026-11-21, tested: 2.1 MB with the faint stars, kept
    in a 4 MB partition, not sent again on the next link.* The app builds
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
16. **Puck projects and draws the stars.** *Done 2026-11-21, tested: the sky matches the
    phone's; star sizes right. Still too slow to look smooth (see Improvements).*
    `main/sky_draw.c`: a full-screen canvas in PSRAM behind everything; each new view
    turns, refracts and projects the stars (brightest first, to the view's limit) and the
    constellation lines as the app does, with its tints, sizes, fading below the horizon,
    and night red. Things to check on the board: the sky matches the phone's, star sizes
    (`STAR_SCALE`), and whether 30 frames a second keep up. Depends on 9, 15.
17. **Moving objects.** *Done 2026-11-21, tested: the Moon with its phase.* The app sends
    `objects` (`puck-objects.ts`, tested): the Moon, planets and bright asteroids with their
    labels, sizes and phases, once linked and once a minute. The puck reads them
    (`main/moving.c`, with the JSON readers now in `main/json_read.c`) and draws each as a
    disc of its size over the stars. Still to do: the Moon's and Venus's phase, turned to the
    Sun, and the labels (task 18). Depends on 16.
18. **The rest of the map.** *Mostly done 2026-11-21, tested on the board.*
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
    - *Built 2026-11-22, tested:* the planets' own icons (Mercury, Mars,
      Jupiter, Saturn, Uranus, Neptune, Pluto, Ceres, the asteroids), sent by the app
      flattened into lines (`icon`, the app's `puck-icons.ts`; `main/sky_icons.c`); the
      selected target's label first and in full below the horizon, its size circle in its
      own colour (`targetInfo` carries `label`, `kind` and `sizeArcmin`).
    - Found done already: the pointer and chevrons to an off-screen target (`draw_pointer`),
      the reticle's solving pulse, the faint stars in the eyepiece view, and Greek letters.
    - Not needed: deep-sky icons (the app's map draws rings and size circles for them, as
      the puck does), the user's own targets (the app has none: every target is in the
      catalogue or the solar system), and the ring labels (decided 2026-11-22).
    - Later: `main/link.c` is near 600 lines; its message reading could move to a file.

## 4. The menu

Decided 2026-11-22, in `docs/screen-layout.md`: watch-style (Wear OS, watchOS) chips with
Material Symbols icons, and what each item does.

19. **Menu screen.** *First version 2026-11-22 (Zoom, Night, Settings, Brightness), tested;
    the user found it too industrial.* Restyle as a watch: chips, icons, the column that
    snaps and shrinks at the edges, the title along the top edge. All items shown.
20. **Night mode** item. *Built 2026-11-22: a bubble, filled when on; the long press on
    the map is gone.* Depends on 14, 19.
21. **Settings → Brightness**: − and +, kept in NVS (`main/brightness.c`). *Built
    2026-11-22.*
22. **Sensors**: a switch for the phone's motion sensors. *Built 2026-11-22 (the
    `sensors` command), tested.*
22a. **View**: steps the phone through the sky map, eyepiece view and field ring, as the
    app's view tile. *Built 2026-11-22 (the `view` command, and the view in `state`),
    tested.*
22b. **Zoom.** *Done 2026-11-21, tested (works, not yet smooth).* The puck zooms on its own,
    apart from the phone (decided 2026-11-21): `main/zoom.c` keeps its field, and shows +,
    −, ✓ and ✗ over the map; ✓ keeps the new field, ✗ goes back. The rules (5° to 120°,
    steps of 1.5, starting at 20°, the star limits) come from the app in `mapRules`. In the
    phone's eyepiece view the puck follows the eyepiece's field. Later: keep the field in
    NVS.
22c. **Lists**: the phone's lists, then a list's targets; choosing one makes it the target,
    and the arrows step through that list. *Built 2026-11-22, tested:* the
    `lists`, `listTargets` and `play` messages (the app's `docs/puck.md`, "Lists",
    `puck-lists.ts`; the puck's `main/lists.c`). Offered: Best right now, In the reticle,
    the user's lists, Messier and Caldwell; only the targets above the horizon, up to 60.
    The pages are in `main/menu_lists.c`, built a few chips at a time (60 at once held the
    screen for a second); LVGL has 512 KB more in PSRAM (`main/board.c`), as 60 chips
    overflowed its 64 KB and restarted the puck. *Also built 2026-11-22, tested:* each
    target as the phone's rows, with its icon (the app's own drawn from `icon` messages, or
    the closest Material Symbol) and its height and direction badge ("42° SE").
22d. **Alignment**, guided as the app's: the easiest bright star or the target, guidance,
    "Align" when centred, and "Remove alignment". *Fixed 2026-11-23, to test on the board:* it guided
    to the star like a target, which led the phone's camera onto it, not the telescope, so the
    solve measured no offset. Now the star isn't made the target, and a page covers the map as
    the app's Align page: "Centre Vega", where it is, a big round Align button, ✗ below.
    *Built 2026-11-22:*
    the `alignStars`, `alignChoose`, `alignNow`, `alignRemove` and `alignStatus` messages
    (the app's `docs/puck.md`, "Alignment", `puck-align.service.ts`); the page in
    `main/menu_lists.c`, ✗ and Align over the map in `main/align.c`.
23. **Settings → Wi-Fi → Client**: the puck's own network for a moment, a QR code to join
    it, and a setup web page (ESP-IDF's `esp_http_server`) listing the networks seen and
    taking the password, kept in NVS. Also at start-up with no network saved or found.
    *Tested on the board 2026-11-22* (`main/wifi.c`, `main/wifi_setup.c`): the page is a
    captive portal (a DNS answering every name with the puck, other addresses sent to the
    page, DHCP option 114), so the phone opens it by itself and stays on a network with no
    internet; the password has a Show button. The chip
    restarts into setup; a saved network not joined in a minute opens it too, and setup with
    no phone for 5 minutes goes back to the saved network. menuconfig's network is only the
    fallback now. Up to 5 networks kept (`main/wifi_networks.c`); the puck scans and joins
    the strongest in range, and rescans after losing it.
24. **Settings → Wi-Fi → AP**: a switch; the puck's own network, a Wi-Fi QR code to join
    it, then the pairing QR. Depends on 23. *Built 2026-11-22, tested:*
    "AstroPuck-XXXX", its password made once and kept in NVS; the switch restarts the puck.
25. **Settings → Meade LX200 Repeater**: a switch; LX200 over TCP port 4030: where the telescope
    points, for Stellarium or SkySafari, and their GoTo becomes the target. *Tested with Stellarium
    2026-11-23:* it connects, shows the telescope and its GoTo sets the target (the colons
    inside `:Sr18:36:56#` were read as new commands; fixed). **Still to look at:** its
    crosshair jumps about during and after a GoTo; Stellarium's log would show why. *Built 2026-11-22* (`main/lx200.c`; the app's `puck-lx200.ts`, docs/puck.md
    "LX200"): `:GR#`, `:GD#` (both precisions), `:Sr`, `:Sd`, `:MS#`, and harmless answers to
    the site and version questions. The phone sends the pointing of the date once a second
    while it's on, and makes the nearest object within 2° the target on a GoTo. Set the
    planetarium to JNow. The chip shows the address to give it.

## 5. Power

Decided 2026-11-27: the puck will never have a battery of its own; it runs from USB (a
power bank). No battery level, charging or AXP2101 work.

27. **Power saving** (perhaps): screen off after a while, wake on touch or movement
    (QMI8658 IMU).

## Improvements

- **The puck's map with sky up** (decided at the second field test, 2026-11-26). The app
  sends the puck a roll worked out from the top of the phone's screen (`screenUpDirection`
  and `viewRollDeg` in `sensor.service.ts`, then `puck-view.ts`), so when the phone's screen
  rotates the puck's map and arrow turn 90°; likely the "wrong" arrow of the first test.
  The puck is in the hand: up on it is always up in the sky, horizon level, whatever the
  phone's screen or mount. App side only.
- **Guidance the mount can follow** (asked at the second field test, 2026-11-26). The arrow
  points along the shortest path on the sky (`targetDirectionDeg` in `puck-view.ts`, the
  great circle), which near the zenith goes over it: an alt-az mount (a Dobson) can't
  follow that. Proposed: aim the arrow at a point a little way along the mount's own path,
  a straight line in altitude and azimuth (azimuth the short way round, altitude never past
  90°); the distance shown stays the true one. Works for every alt-az mount whatever the
  telescope. The phone's own guidance should match. Alt-az only for now (decided
  2026-11-26: no equatorial mount to test with); later, an equatorial mount would guide in
  hour angle and declination, behind a mount setting the app doesn't have yet.
- **Field tests: plate solve the telescope camera's pictures** (asked at the first field
  test, 2026-11-25). Today the ZWO only says whether the target is in its small field
  (0.27 x 0.15 deg at 1200 mm); solved, it would say exactly where the telescope pointed,
  so the app's error could be measured in degrees at every solve. Options: astrometry.net's
  online service (free account and API key, pictures marked not public, 1 to several
  minutes each, so afterwards rather than live), or ASTAP on the laptop (free, needs its
  star database for such narrow fields). Without tracking, short exposures (about 0.3 s)
  and high gain, so stars don't trail. Also: align the finder with the camera before a
  session, so a target centred in the finder is in the camera's field.
- **Plate solving feedback on the puck** (asked at the first field test, 2026-11-25).
  *Done 2026-11-25, tested:* the reticle pulses while the phone solves; a failed solve
  (the app's `solveLog`) turns the rings amber with an X in the middle for 1.5 s, blinking
  instead at night (`screen_solve_failed` in `main/screen.c`). Not to be done (decided
  2026-11-27): the reason, the stars found, the time since the last good solve.

- **Field tests** (asked 2026-11-24): a guided night under the sky, recorded in full, before
  polishing more (docs/field-tests.md). *Built 2026-11-24, tried on the laptop with fake
  data:* the collector, the agent's commands, the report (`tools/field/`); the app's record
  and commands in development (`field-test.service.ts`, `field-log.ts`, a normal photo in
  `camera.ts`), and the night's plan (`scripts/field-plan.mjs`); the puck's log over the
  Wi-Fi in a field build (`main/field_log.c`, `CONFIG_PUCK_FIELD_LOG`) and its Wi-Fi signal
  every 5 s. *To do:* install the ZWO packages, flash the field build, and try a session by
  day (the link, the photos, the camera) before the first night.

- **The QR codes red and dim, brightened by taps** (asked 2026-11-23, changed 2026-11-25:
  a white QR code after a lost link dazzled at night). *Built 2026-11-25, tested:* every QR code comes up red at the screen's lowest brightness, day or night; each
  tap on the screen brightens it a step: five in red, then five in white, then back to the
  first (`QR_BRIGHTNESS` in `main/main.c`, `main/qr_view.c`); the status lines under it red
  too while it is, and the puck stays red after linking until the app's state says
  otherwise (`has_state`, built 2026-11-26, to flash and test). Brightnesses 10, 30, 90, 150 and 255 (3 was too dim, tested 2026-11-25). It shows only once the screen
  has dimmed, so it never flashes. To tune on the board: the five brightnesses, and that the
  phone's camera reads the dimmest red. The code is also bigger: a 2-square border, not the
  standard's 4, and nothing light beyond it.
- **Start-up screen.** *Done 2026-11-22, tested.* The colour test is gone; the app's logo
  floats in the middle over the landing page's twinkling stars and shooting star
  (`main/splash.c`, the logo made by `tools/make_logo.py`), until the first QR code. All in
  red on black since 2026-11-25, night mode or not, with the status lines red too: the logo
  through a red filter (its brightness in red), made at start-up; and at brightness 30 of 255
  (`SPLASH_BRIGHTNESS`), whatever the menu's setting. The
  saved brightness applies from the start. No white flash at start-up (2026-11-22,
  tested): the panel starts off at brightness 0 (Waveshare's commands switched it on at
  full brightness over whatever its memory held), and comes on after LVGL's first frame. Still there: the 2 s wait at start-up for a
  cable monitor (the log history keeps those lines anyway).
- **ICE credentials from the hardware random generator.** *Done 2026-11-21, to test on
  the board.* libpeer seeded them from the clock, which the puck doesn't have, so someone on
  the same Wi-Fi could guess them and keep taking the pairing slot. Now `esp_random`.
- **Smoother map.** *Done 2026-11-21, tested: "awesome".* From 125 to 175 ms a frame down
  to 46 to 60 ms: labels rendered once and kept (`main/sky_labels.c`), everything drawn
  straight into the pixels with no LVGL layers (`main/sky_pixels.c`), only the objects the
  field shows looked at, `-O2`, and the view glides between messages (`main/view_glide.c`,
  as the app's `pose-glide.ts`), the zoom too. `main/sky_draw.c` was split (stars into
  `main/sky_stars.c`).
- **Quicker still.** What a frame spends now: the grid and lines 7 to 27 ms, the fill
  13 ms (the PSRAM's speed), the stars 9 ms, labels 5 to 8 ms. To try: fewer grid points,
  a bigger data cache if internal RAM allows.
- **Resend lost messages from the puck.** *Done 2026-11-21, tested on the board with every
  10th chunk held back (`SCTP_TEST_DROP_SENT_EVERY`): each was resent once and acknowledged.*
  Every DATA chunk the puck sends is kept until a SACK covers it, and resent after 300 ms,
  then twice as long each time, up to 2 s (`components/libpeer/README.md`).
- **The QR code's slow heartbeat.** *Done 2026-11-21, removed 2026-11-25:* its swell to
  full brightness dazzled at night; the taps' brightness steps replace it.
- **The Wi-Fi signal log line in field builds only** (found 2026-11-27). `main/link.c` logs
  the signal every 5 s in every build, though `docs/field-tests.md` says only the field
  build adds it. Put it behind `CONFIG_PUCK_FIELD_LOG`, or keep it everywhere and fix the
  doc. It went in with the failed-solve commit (5ad5344), as it's in the same file.
- **Remove the lite pairing check logging** in `components/libpeer/src/agent.c` once pairing
  is known to be reliable.
- **Reconnect without a new QR code.** *Built 2026-11-22, to test on the board.* The puck
  keeps the same code (port, ICE credentials, certificate, secret) until it restarts
  (`peer_connection_keep_identity` in libpeer, `new_code_needed` in `main/link.c`), and the
  app has a Reconnect button. Later perhaps: across restarts too (the certificate and a
  pairing key in NVS and the app's storage).
- **Strip libpeer's audio and video code**, so libsrtp (`sepfy/srtp`) and its compiler
  workaround in `CMakeLists.txt` can go.
- **Weak Wi-Fi signal** (−91 dBm on the first test): measure the link near the router; the
  worst round trip was 444 ms.
- **App: split `remote.service.ts`** (about 590 lines, the app's limit is about 500). The
  puck's part is already out, in `puck-link.service.ts`.
- **App: end-to-end test** for pairing with the puck, faking the puck's side.
- **Faster full builds on Windows**: Defender exclusions for `C:\Espressif`, `C:\esp` and
  `build/` (the user's choice).
