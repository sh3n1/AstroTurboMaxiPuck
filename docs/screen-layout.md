# Screen layout

How the puck's round 466×466 screen is laid out, and what each part does.

## Main screen

- **Background:** the phone's sky map, with no panning. The puck shows exactly what is visible
  on the phone's screen; there is no way to move the map from the puck.
- **Top edge:** the Alt, Az and FOV values, written along the curve of the screen.
- **Bottom edge** (2026-11-23), while a target is selected and the player is hidden:
  one line, as the top's and on the same dark band, with its designations and name, then
  how far it is ("M42 · NGC 1976  Orion Nebula   12.4°"). Too long, the name is cut with
  "...", never the distance.
- **Centre:** the reticle and crosshair, with directional arrows towards the target.
- **Tapping the reticle** starts a plate solve, as it does in the phone app.
- **Player** (2026-11-23; it replaced the bottom third): a tap on the map, off the reticle,
  dims the map and shows a watch's media controls in the menu's bubbles, while the reticle
  and both edges' readouts hide:
  1. At the top, the list being observed through, with where in it ("3/110  Messier"), and
     under it the target's names.
  2. In the middle, the previous and next buttons left and right, faint when there is no
     list, and the menu button, big and filled, in the centre.
  3. At the bottom, ✗ to close it. It also closes after 15 s without a tap.

## Menu

Decided 2026-11-22. In the style of a watch (Wear OS, watchOS), not an industrial panel:

- **The main menu** is a honeycomb of round bubbles with big icons and no words, as a
  watch's app launcher (the Galaxy Watch's): rows of 2, 3 and 2, all on one screen. A bubble
  that is on (Sensors, Night mode) is filled bright. While a bubble is pressed, its
  word and state show along the top edge.
- **The pages within it** (Settings, Wi-Fi): each item is a wide rounded "chip", filled in a dark tint with no outline, a
  round coloured icon on the left (Material Symbols, Rounded), its word, and under it, in
  smaller text, its state where it has one ("On", "Sky map"). The column scrolls and snaps
  an item to the middle; items shrink and fade towards the top and bottom edges, following
  the round screen. The page's title is written along the top edge, as the main screen's
  readout. A small round button at the bottom closes the menu (✕) or goes up a page (←).
  Night mode: the same, in full-strength red on black and dark red.
- **Words** come from the phone (`texts`), as everywhere once linked.

The items, and what each does:

- **Sensors:** a switch: the phone's motion sensors on or off, as the app's "Sensors on".
- **View** (the app's eyepiece tile): steps the phone, and so the puck, through the sky map,
  the eyepiece view and the field ring, as the app's view tile; like it, the chip shows the view a tap switches to.
- **Lists:** the phone's lists (Best right now, Saved for later, …); one opens its targets,
  each as the phone's rows: its icon (the app's own, or the closest Material Symbol for
  stars and clusters) and how high and which way ("42° SE", green from 30° up, amber
  below); a target becomes the target. The ⏮ ⏭ arrows then step through that list.
- **Alignment:** guided, as the app's: it offers the target (while it is up) and the four
  easiest bright stars, each with where it is and how bright. Once one is chosen, a page
  covers the map, as the app's Align page: "Centre Vega" at the top, where it is and the
  hint under it, a big round Align button in the middle and ✗ at the bottom. No guidance:
  the map isn't aligned yet, so it would lead the camera onto the star, not the telescope;
  the user centres it in the eyepiece by eye. Align plate solves on the phone; the result
  shows for a few seconds (Done closes it at once), or why it failed, with Retry. While aligned, the page also shows what on,
  and "Remove alignment", which asks again before removing.
- **Zoom:** shows +, −, ✓ and ✗ over the sky map, as the menu's bubbles (✓ filled); ✓ keeps the new field, ✗ goes back. The
  puck's own zoom, apart from the phone's; it still can't pan the map. In the phone's
  eyepiece view it follows the eyepiece's field.
- **Night mode:** a switch, in step with the phone. Only here: no long press on the map
  (decided 2026-11-22).
- **Settings**
  - **Brightness:** the value with − and + either side, and under them a bar (empty on
    the left, full on the right, as the distance bar) that can be dragged; 10 % to 100 %,
    kept in the puck. Not a ring: the title along the top edge hid part of it.
  - **Wi-Fi**
    - **Client:** the network the puck joins. With no link to type on, the puck restarts
      into setup: its own network ("AstroPuck-XXXX") and a QR code to join it; once the
      phone is on it, a second QR code opens a small web page served by the puck
      (http://192.168.4.1/, ESP-IDF's web server) that lists the networks it sees and takes
      the password, kept in the puck's NVS. It keeps up to 5 networks (home, the phone's
      hotspot, a club's), each with Forget on the page, and joins the strongest of them in
      range (decided 2026-11-22). Saving restarts the puck. Setup also opens by itself when
      no network is set or none of the kept ones is joined in a minute; with no phone on it for 5 minutes, the puck tries the saved network again.
      In English, as everything before a link.
    - **AP:** a switch: the puck makes its own network, for the field without a router, and
      shows a Wi-Fi QR code (name and password) for the phone to join, then the pairing QR.
      The password is made once and kept, so the phone rejoins by itself. Switching
      restarts the puck.
  - **Meade LX200 Repeater** (in Settings, decided 2026-11-22): a switch: the puck answers
    LX200 commands over Wi-Fi (TCP port 4030), so planetarium apps (Stellarium, SkySafari)
    show where the telescope points, and a GoTo from them becomes the target on the phone
    and the puck, which then guide to it (the nearest object within 2°). While on, the chip
    shows the address to give the planetarium ("192.168.1.57:4030"); the planetarium is set
    to a Meade LX200 telescope, JNow (equinox of date).
