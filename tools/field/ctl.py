# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors
#
# The agent's side of a field test (docs/field-tests.md): talks to the collector. Every
# plate solve the app makes, automatic ones too, brings its own telescope picture and
# normal photo by itself; these only mark the steps and add to them.
#
#   python tools/field/ctl.py status
#   python tools/field/ctl.py step 3 Capella        (as the step is announced)
#   python tools/field/ctl.py done [--exposure-ms 800] [--gain 250]   (the user says "done")
#   python tools/field/ctl.py scope [--exposure-ms 2000]   (another telescope picture)
#   python tools/field/ctl.py note "it jumped while centring"
#   python tools/field/ctl.py info '{"plan": …}'
#   python tools/field/ctl.py command '{"t": "config", "photoEverySolve": false}'
#   python tools/field/ctl.py command '{"t": "solve"}'   (a solve now, if one is wanted)

import argparse
import json
import os
import sys
import urllib.request

# The collector's port: 8445, as the app's dev proxy expects, unless FIELD_PORT says.
COLLECTOR = f"http://127.0.0.1:{os.environ.get('FIELD_PORT', '8445')}"


def call(path: str, body: dict | None = None, timeout: float = 120) -> dict:
    data = None if body is None else json.dumps(body).encode("utf-8")
    request = urllib.request.Request(COLLECTOR + path, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read())


def show(value: dict) -> None:
    print(json.dumps(value, indent=1, ensure_ascii=False))


def camera_settings(args) -> dict:
    body = {}
    if args.exposure_ms:
        body["exposureMs"] = args.exposure_ms
    if args.gain is not None:
        body["gain"] = args.gain
    return body


def main() -> None:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="what", required=True)
    commands.add_parser("status")
    p = commands.add_parser("step")
    p.add_argument("step", type=int)
    p.add_argument("label")
    for name in ("done", "scope"):
        p = commands.add_parser(name)
        p.add_argument("--exposure-ms", type=float)
        p.add_argument("--gain", type=int)
    p = commands.add_parser("note")
    p.add_argument("text")
    p = commands.add_parser("info")
    p.add_argument("json")
    p = commands.add_parser("command")
    p.add_argument("json")
    args = parser.parse_args()

    try:
        if args.what == "status":
            show(call("/ctl/status"))
        elif args.what == "step":
            show(call("/ctl/step", {"step": args.step, "label": args.label}))
        elif args.what in ("done", "scope"):
            show(call(f"/ctl/{args.what}", camera_settings(args)))
        elif args.what == "note":
            show(call("/ctl/note", {"text": args.text}))
        elif args.what == "info":
            show(call("/ctl/info", json.loads(args.json)))
        elif args.what == "command":
            show(call("/ctl/command", json.loads(args.json)))
    except OSError as error:
        sys.exit(f"The collector didn't answer ({error}): is tools/field/collector.py running?")


if __name__ == "__main__":
    main()
