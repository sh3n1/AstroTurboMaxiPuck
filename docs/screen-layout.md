# Screen layout

How the puck's round 466×466 screen is laid out, and what each part does.

## Main screen

- **Background:** the phone's sky map, with no panning. The puck shows exactly what is visible
  on the phone's screen; there is no way to move the map from the puck.
- **Top edge:** the Alt, Az and FOV values, written along the curve of the screen.
- **Centre:** the reticle and crosshair, with directional arrows towards the target.
- **Tapping the reticle** starts a plate solve, as it does in the phone app.
- **Bottom third:** controls in the style of a media player, from top to bottom:
  1. The target object, on a single line of text.
  2. Left arrow, Menu button, right arrow.
  3. A progress-style bar showing the distance to the object: further left means further from
     the object, further right means closer to it.
  4. At the bottom, centred, the angular distance to the object in degrees.
- **Left and right arrows:** move to the previous or next item in the current list.
- **Menu button:** opens the menu.

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
  and a target becomes the target. The ⏮ ⏭ arrows then step through that list.
- **Alignment:** guided, as the app's: it offers the target (while it is up) and the four
  easiest bright stars, each with where it is and how bright. One chosen becomes the
  target, so the map guides to it, and ✗ and Align show over the map where the zoom's ✗
  and ✓ are, with the phone's words above the reticle ("Centre Vega"). Align, once it is
  centred in the eyepiece, plate solves on the phone; the result shows for a few seconds,
  or why it failed, with Align to try again. While aligned, the page also shows what on,
  and "Remove alignment", which asks again before removing.
- **Zoom:** shows +, −, ✓ and ✗ over the sky map; ✓ keeps the new field, ✗ goes back. The
  puck's own zoom, apart from the phone's; it still can't pan the map. In the phone's
  eyepiece view it follows the eyepiece's field.
- **Night mode:** a switch, in step with the phone. Only here: no long press on the map
  (decided 2026-11-22).
- **Settings**
  - **Brightness:** the value with − and + either side, and under them a bar (empty on
    the left, full on the right, as the distance bar) that can be dragged; 10 % to 100 %,
    kept in the puck. Not a ring: the title along the top edge hid part of it.
  - **Wi-Fi**
    - **Client:** the network the puck joins. With no link to type on, the puck opens its
      own network for a moment and shows a QR code to join it; a small web page served by
      the puck (ESP-IDF's web server) lists the networks it sees and takes the password,
      kept in the puck's NVS. Also offered at start-up when no network is saved or found.
    - **AP:** a switch: the puck makes its own network, for the field without a router, and
      shows a Wi-Fi QR code (name and password) for the phone to join, then the pairing QR.
  - **Meade LX200 Repeater** (in Settings, decided 2026-11-22): a switch: the puck answers
    LX200 commands over Wi-Fi (TCP port 4030), so planetarium apps (Stellarium, SkySafari)
    show where the telescope points, and a GoTo from them becomes the target on the phone
    and the puck, which then guide to it.
