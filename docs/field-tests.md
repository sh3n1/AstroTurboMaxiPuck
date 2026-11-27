# Field tests

A guided night under the sky, recorded in full, to find what fails and why: the app's
pointing and plate solving, the puck's link and drawing, and the whole system together.
An agent (Claude Code on the laptop) leads the session step by step and reads everything as
it comes in; afterwards a report sums it up.

## The rule: test one setup, fix for every setup

A session uses one telescope, mount, camera and sky, but the app and the puck must work on
any telescope: any focal length, any mount (alt-az or equatorial, manual or GoTo), any
eyepiece, any phone. So:

- The tests may suit the user's own setup (their ZWO camera, their exposure, their
  telescope): they are tools for finding faults. Each session records the equipment the app
  has (telescope, focal length, eyepiece, field, phone), so every finding says what it came
  from.
- The telescope's own camera is only the night's reference for where the telescope really
  points: a ZWO camera through its SDK, any camera saving pictures into a folder, or none,
  with the user saying what the eyepiece shows.
- **What the results lead to is never tuned to one setup.** A fault found with one setup
  is fixed in general, for every telescope, mount, eyepiece and phone. The report says
  which findings may depend on the setup, to check with another.

## Never in a release

The tests are for development only, and nothing of them goes into what users get:

- **The app:** a production build swaps `field-test.ts` and `field-log.ts` for the empty
  `field-test.prod.ts` and `field-log.prod.ts` (`angular.json`, `fileReplacements`), so none
  of the recorder is in it; only the solver's general `withCameraFree` stays.
- **The puck:** the log over the Wi-Fi is only compiled in with `CONFIG_PUCK_FIELD_LOG=y`,
  off by default; a field build is kept apart (`dist/astroturbomaxipuck-field.bin`) and never
  goes on the landing page.
- **The tools** (`tools/field/`, the app's `scripts/field-plan.mjs`) run on the laptop only.

## What runs where

```
 phone (app from the laptop)        puck                     laptop
 ─────────────────────────         ──────                   ──────────────────────
 events, solves, photos ──HTTPS──▶ ng serve proxy ─────────▶ collector (Python)
                                    log lines ──UDP 4031────▶   ├─ session folder
                         ◀──────── commands (long poll)         ├─ telescope camera
                                                                └─ the agent, by HTTP
```

- **The app**, served from the laptop (`npm run start:phone`), in development mode only:
  `src/app/core/dev-tools/field-test.service.ts` finds the collector through the dev
  server's proxy (`/field/…`) and, while it answers, sends the app's events once a second
  and asks for commands. With no collector it does nothing.
- **The puck**, built with `CONFIG_PUCK_FIELD_LOG` (off by default, never in a release):
  every log line also goes out as a UDP broadcast on port 4031, so no address needs
  setting. Its log already holds the link's state, SCTP resends, lost links, memory and the
  time a frame takes; the field build adds the Wi-Fi signal.
- **The collector**, `tools/field/collector.py`: receives all of it, stamps it with the
  laptop's clock, and writes one session folder (`field-sessions/<date-time>/`, ignored by
  git). The agent drives it with `tools/field/ctl.py`.

## A step

The agent says what to do ("choose Capella on the puck and follow the guidance until it is
centred") and starts the step (`ctl.py step <n> <label>`). Then everything is automatic:

- **Every plate solve** the app makes, as it always does once the telescope stops (or for
  alignment, or a tap), brings three pictures into its own folder in the step:
  1. the **telescope camera's picture**, taken as the solve starts, in the same moment as
     the phone's frame (the app tells the collector at once);
  2. the **frame as the solver saw it** (the long exposure, the raw pixels) and its result;
  3. a **normal photo** from the phone's camera, with automatic exposure, as the solve ends:
     what is in front of it, such as trees, street lights, buildings, clouds. It closes the
     solver's camera for a moment, so the next solve takes a little longer to start; the
     agent can turn it off (`{"t": "config", "photoEverySolve": false}`).
- When the user says it is done, `ctl.py done`: the telescope camera's picture of where it
  ended, and what the app and the puck show at that moment. No solve is asked for: the app
  solves by itself once the telescope stops.
- The user comments as they go, in the conversation, and the agent notes each comment
  (`ctl.py note`) in the timeline, in the step under way.

## A night's plan

`scripts/field-plan.mjs` in the app makes the steps for the site and time from the app's
own astronomy: bright stars spread round the sky to align on, then targets low, middle and
high in each direction, the planets and the Moon if up, and steps for the link (walk away
with the puck, lock the phone a moment). The plan adapts to the night; it is never a fixed
list of objects.

## Files of a session

```
field-sessions/2026-11-24T21-30/
  session.json        site, equipment, versions, plan
  events.jsonl        every event, one JSON per line: {at, source, kind, ...}
  puck.log            the puck's log lines as received
  steps/03-capella/   notes.txt
    solves/214502-auto/   scope.png, scope-preview.jpg, solve.jpg, solve.f32, solve.json, context.jpg
    done-214530/          scope.png, scope-preview.jpg
  report.md           made afterwards by tools/field/report.py
```

## Setting up

See [tools/field/README.md](../tools/field/README.md): the laptop's setup, the field
build, and how a session goes, step by step.
