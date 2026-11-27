# Field test tools

What they are for, and why they must not be tuned to one setup: [docs/field-tests.md](../../docs/field-tests.md).

## Once, on the laptop

Python 3.11 or newer. The collector alone needs nothing more. For a ZWO ASI telescope camera:

```
python -m venv tools/field/.venv
tools/field/.venv/Scripts/python -m pip install -r tools/field/requirements.txt
```

ZWO's `ASICamera2.dll` comes with ASIStudio, N.I.N.A. or SharpCap; `scope_camera.py` finds
it in their usual folders, or set `ZWO_ASI_LIB` to its path.

## Before going out

1. Flash the puck with a field build: `CONFIG_PUCK_FIELD_LOG=y` in `sdkconfig` (menuconfig,
   "AstroTurboMaxiPuck"), build, flash; then set it back to `n`, so a release never sends
   its log. A field build doesn't belong on the landing page. If `sdkconfig` is set back by
   copying an older file, run `idf.py reconfigure` before the next build: the build only
   rereads a `sdkconfig` newer than its own copy, and otherwise keeps the field log on.
   Check with `build/config/sdkconfig.h`, which must not define `CONFIG_PUCK_FIELD_LOG`.
2. Laptop, phone and puck on the same Wi-Fi. A network that keeps devices apart ("AP
   isolation", common on guest networks) stops the puck's broadcasts.
3. Stop anything else on port 8445 (the app's `scripts/device-report-receiver.mjs`): the
   collector takes its place.

## A session

On the laptop, three terminals:

```
tools/field/.venv/Scripts/python tools/field/collector.py --camera zwo --name terrace
    # or --camera folder --folder <where its pictures go>, or --camera none
cd <the app> && npm run start:phone
    # then on the phone open https://<laptop's address>:8443
python tools/field/ctl.py status
    # the app and the puck heard a moment ago? then the session is on
```

The agent then:

1. Makes the night's plan from the phone's site:
   `node scripts/field-plan.mjs --session <session folder>` (in the app), and saves it with
   `ctl.py info '{"plan": …}'`.
2. Goes through the steps with the user. For each: says what to do and `ctl.py step <n>
   <label>`. Each plate solve the app makes meanwhile brings its telescope picture and
   normal photo by itself. When the user says it is done, `ctl.py done`; then the agent
   looks at the pictures in the step's folder. Whatever the user comments, at any moment,
   goes in with `ctl.py note "…"`. A telescope picture too dark or too bright: `ctl.py
   done --exposure-ms …` or `ctl.py scope --exposure-ms …`.
3. Watches `events.jsonl` and `puck.log` between steps for lost links, stalls and slow
   frames, and asks the user about them while they still remember.
4. At the end: `python tools/field/report.py <session folder>`, then reads it with the
   pictures and writes down what to improve, saying which findings may come from this setup.

## Files

- `collector.py`: the HTTP side for the app and the agent, the UDP side for the puck.
- `session.py`: the session's folder and timeline.
- `scope_camera.py`: the telescope camera: ZWO, a folder, or none.
- `ctl.py`: the agent's commands.
- `report.py`: the summary.
