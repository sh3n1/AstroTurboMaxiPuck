# Linking the puck to a phone

How the puck pairs with the AstroTurboMaxiSolver app, as decided on 2026-11-21. Nothing here is
built yet, on either side. The full link contract (channels, messages, binary view layouts) is
in the app's repository, `docs/puck.md`, which also holds these rules: change that file first,
then both sides.

## Who the puck links to

**Where the telescope points always comes from the phone on the telescope.** What changes is
the way it reaches the puck:

- **One phone, in normal mode,** on the telescope: the puck links straight to it.
- **The app's Two phones mode:** the puck links to the **screen phone**, which passes on what
  the finder on the telescope sends it.
- **Never the finder itself:** the puck can't link to one, and a finder can't link to a puck.
  The firmware should refuse such a link, and say so on the screen.

Why: the puck is a remote, and a remote of a phone whose screen is locked, as the finder's is,
makes no sense. The screen phone, by contrast, already takes control of the finder, so the
puck works through it.

- **The phone the puck links to stays as it is.** Linking the puck doesn't put it in finder or
  any other mode, and doesn't lock or take over its screen: the puck is only a remote over it.

## Both ways

- **Puck to phone:** what the user changes on the puck acts on the phone it links to (and, in
  Two phones, through it on the finder, as the screen's own changes do), such as choosing the
  next target, a plate solve from a tap on the reticle, or night mode. The puck's zoom is
  its own, and doesn't change the phone's.
- **Phone to puck:** what changes on the phone updates the puck's view, such as the target, the
  map's view, or night mode.

## In the app: the Remote page

- The app's menu tile and page **Two phones is renamed Remote**.
- The page has two sections:
  - **Two devices:** the two buttons it has today, this phone is the finder, and this phone is
    the screen.
  - **Puck:** a button to link a puck.
