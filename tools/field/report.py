# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors
#
# Sums up a field test session (docs/field-tests.md) into report.md in its folder: the
# steps, the plate solves, the link and the puck's drawing. The numbers to start from; the
# agent reads the pictures and the timeline for the rest.
#
#   python tools/field/report.py field-sessions/<session>

import json
import math
import re
import statistics
import sys
from pathlib import Path


def separation_deg(a: dict | None, b: dict | None) -> float | None:
    """The angle between two alt-az positions ({altDeg, azDeg})."""
    if not a or not b:
        return None
    alt1, az1, alt2, az2 = map(math.radians, (a["altDeg"], a["azDeg"], b["altDeg"], b["azDeg"]))
    cos = math.sin(alt1) * math.sin(alt2) + math.cos(alt1) * math.cos(alt2) * math.cos(az1 - az2)
    return math.degrees(math.acos(max(-1.0, min(1.0, cos))))


def spread(values: list[float], unit: str = "", places: int = 1) -> str:
    """"median 1.2 (0.4 to 3.1) °, 12 values", or "none"."""
    if not values:
        return "none"
    f = f"{{:.{places}f}}"
    return (
        f"median {f.format(statistics.median(values))}{unit} "
        f"({f.format(min(values))} to {f.format(max(values))}), {len(values)} values"
    )


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("Usage: python tools/field/report.py <session folder>")
    folder = Path(sys.argv[1])
    events = [json.loads(line) for line in (folder / "events.jsonl").read_text("utf-8").splitlines() if line]
    info = json.loads((folder / "session.json").read_text("utf-8"))
    of = lambda source, kind: [e for e in events if e["source"] == source and e["kind"] == kind]  # noqa: E731
    out: list[str] = [f"# Field test {folder.name}", ""]

    minutes = (events[-1]["rx"] - events[0]["rx"]) / 60000 if events else 0
    app = info.get("app", {})
    out += [
        f"- Length: {minutes:.0f} minutes, {len(events)} events.",
        f"- Phone: {app.get('browser', '?')}",
        f"- Equipment (the app's): `{json.dumps(app.get('equipment'), ensure_ascii=False)}`",
        f"- Telescope camera: {info.get('scopeCamera', '?')}",
        "",
        "Findings may depend on this setup: check any fix against others (docs/field-tests.md).",
        "",
    ]

    # Steps: from the agent's announcing one to the next. In each, the app's solves (each
    # with its telescope picture and normal photo), how far the app thought the target was
    # when the user said done, and what the user said.
    out += [
        "## Steps",
        "",
        "| Step | Solves | Target distance at done (app) | Telescope picture at done | User's comments |",
        "|---|---|---|---|---|",
    ]
    steps = of("agent", "step")
    for i, step in enumerate(steps):
        n = step["step"]
        until = steps[i + 1]["rx"] if i + 1 < len(steps) else math.inf
        within = lambda e: step["rx"] <= e["rx"] < until  # noqa: E731
        solved = [e.get("outcome", "?") for e in of("app", "solveReport") if within(e)]
        done = [e for e in of("app", "snapshot") if e.get("step") == n and within(e)]
        distance = done[-1].get("targetDistance") if done else None
        pictures = [e for e in of("scope", "capture") if e.get("done") and e.get("step") == n and within(e)]
        notes = [e["text"] for e in of("user", "note") if within(e)]
        out.append(
            f"| {n} {step.get('label', '')} | {', '.join(solved) or '-'} "
            f"| {f'{distance:.2f}°' if distance is not None else '-'} "
            f"| {'yes' if any(p.get('ok') for p in pictures) else 'no'} | {'; '.join(notes)} |"
        )
    out.append("")

    # Plate solves, automatic and asked for.
    reports = of("app", "solveReport")
    corrections = [d for r in reports if (d := separation_deg(r.get("sensorsBefore"), r.get("sensorsAfter"))) is not None]
    attempts = of("app", "solve")
    outcomes: dict[str, int] = {}
    for a in attempts:
        outcomes[a.get("outcome", "?")] = outcomes.get(a.get("outcome", "?"), 0) + 1
    out += [
        "## Plate solves",
        "",
        f"- Attempts: {outcomes or 'none'}",
        f"- Time per attempt: {spread([a['durationMs'] / 1000 for a in attempts if a.get('durationMs')], ' s')}",
        f"- Stars found: {spread([a['starCount'] for a in attempts if a.get('starCount') is not None], '', 0)}",
        f"- How far a solve moved the sensors' pointing: {spread(corrections, '°', 2)}",
        "",
    ]

    # The link, from both sides.
    puck_lines = [e["line"] for e in of("puck", "log")]
    signal = [int(m[1]) for line in puck_lines if (m := re.search(r"Wi-Fi signal (-?\d+) dBm", line))]
    frame_ms = [int(m[1]) / 1000 for line in puck_lines if (m := re.search(r"Drawing the sky: (\d+) us", line))]
    resent = [int(m[1]) for line in puck_lines if (m := re.search(r"(\d+) resent", line))]
    losses = of("app", "puckLost")
    out += [
        "## Link",
        "",
        f"- Status changes: {' → '.join(e.get('status', '?') for e in of('app', 'puckStatus')) or 'none'}",
        f"- Lost links (app): {len(losses)}",
        *[f"  - {e.get('record')}" for e in losses],
        f"- Lost links (puck): {sum('the link is lost' in line for line in puck_lines)}",
        f"- Silences over 1.5 s from the puck: {spread([e['ms'] for e in of('app', 'puckSilence')], ' ms', 0)}",
        f"- Page stalls on the phone: {spread([e['ms'] for e in of('app', 'pageStall')], ' ms', 0)}",
        f"- Wi-Fi signal at the puck: {spread([float(s) for s in signal], ' dBm', 0)}",
        f"- SCTP chunks resent by the puck (running total): {resent[-1] if resent else 'none'}",
        f"- Puck log lines received: {len(puck_lines)}",
        "",
        "## Puck drawing",
        "",
        f"- Time per frame: {spread(frame_ms, ' ms', 0)}",
        "",
    ]

    report = "\n".join(out)
    (folder / "report.md").write_text(report, "utf-8")
    sys.stdout.reconfigure(encoding="utf-8")  # a Windows console may not take "→" or "°"
    print(report)


if __name__ == "__main__":
    main()
