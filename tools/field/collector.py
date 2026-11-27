# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors
#
# The field test collector (docs/field-tests.md): gathers the app's events, photos and
# solves, the puck's log lines and the telescope camera's pictures into one session folder,
# and passes the agent's commands to the app.
#
#   python tools/field/collector.py [--camera zwo|folder|none] [--name terrace]
#
# HTTP on 127.0.0.1:8445, only this laptop: the phone reaches it through the app's dev
# server proxy (scripts/dev-proxy.json), the agent through tools/field/ctl.py. It replaces
# the app's scripts/device-report-receiver.mjs while it runs, on the same port. The puck's
# lines come as UDP broadcasts on port 4031, from a field build (CONFIG_PUCK_FIELD_LOG).

import argparse
import base64
import json
import re
import socket
import threading
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from scope_camera import open_camera
from session import Session, now_ms, save_data_url

HTTP_PORT = 8445
PUCK_LOG_PORT = 4031
MAX_BODY_BYTES = 40_000_000  # a solve report carries its raw frame, about 10 to 30 MB
COMMAND_WAIT_S = 20  # how long the app's ask for commands waits for one
ANSI = re.compile(r"\x1b\[[0-9;]*m")

session: Session
camera = None
camera_lock = threading.Lock()

# Commands for the app, in order; the app asks for those after the last it has.
commands: list[dict] = []
commands_changed = threading.Condition()

# When each side was last heard, for the status.
last_heard = {"app": 0, "puck": 0}

# Each plate solve's folder, by the app's id for it: its telescope picture, frame and photo.
solve_folders: dict[str, Path] = {}
solve_folders_lock = threading.Lock()


def folder_for_solve(solve_id: str | None, trigger: str = "") -> Path:
    """A solve's folder: under the step under way, if any, as solves/<time>-<trigger>."""
    with solve_folders_lock:
        if solve_id and solve_id in solve_folders:
            return solve_folders[solve_id]
        base = session.step_folder(None) if session.step else session.folder
        stamp = datetime.now().strftime("%H%M%S")
        folder = base / "solves" / f"{stamp}-{trigger or 'solve'}"
        folder.mkdir(parents=True, exist_ok=True)
        if solve_id:
            solve_folders[solve_id] = folder
        return folder


def scope_picture(folder: Path, about: dict, exposure_ms=None, gain=None) -> dict:
    """The telescope camera's picture into `folder`; a fault mustn't end the night."""
    with camera_lock:
        try:
            result = camera.capture(folder, exposure_ms, gain)
        except Exception as error:
            result = {"ok": False, "reason": repr(error)}
    session.event("scope", "capture", **about, **result)
    return result


def queue_command(command: dict) -> int:
    with commands_changed:
        command = {"id": len(commands) + 1, **command}
        commands.append(command)
        commands_changed.notify_all()
    session.event("agent", "command", command=command)
    return command["id"]


def save_solve_report(report: dict) -> dict:
    """A live-solve report (the app's solve-report.ts): its picture and raw frame as files."""
    folder = folder_for_solve(report.get("fieldSolve"), report.get("trigger") or "")
    base = folder / "solve"
    image = save_data_url(report.get("image") or "", base)
    report["image"] = str(image) if image else None
    if isinstance(report.get("raw"), str):
        raw = base.with_suffix(".f32")  # 32-bit floats, little-endian, row by row
        raw.write_bytes(base64.b64decode(report["raw"]))
        report["raw"] = str(raw)
    base.with_suffix(".json").write_text(json.dumps(report, indent=1, ensure_ascii=False), "utf-8")
    result = report.get("result") or {}
    session.event(
        "app",
        "solveReport",
        solve=report.get("fieldSolve"),
        step=session.step["number"] if session.step else None,
        trigger=report.get("trigger"),
        outcome=report.get("outcome"),
        message=report.get("message"),
        durationMs=report.get("durationMs"),
        starCount=result.get("starCount"),
        sensorsBefore=report.get("sensorsBefore"),
        sensorsAfter=report.get("sensorsAfter"),
        file=str(base.with_suffix(".json")),
    )
    return {"saved": str(base.with_suffix(".json"))}


def save_other_report(report: dict) -> dict:
    """The app's other device reports (sky test, device test), kept as they come."""
    folder = session.step_folder(None)
    path = folder / f"report-{now_ms()}.json"
    path.write_text(json.dumps(report, indent=1, ensure_ascii=False), "utf-8")
    session.event("app", "deviceReport", file=str(path))
    return {"saved": str(path)}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):  # quiet: everything worth keeping is in the session
        pass

    def reply(self, body: dict, status: int = 200) -> None:
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def body(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_BODY_BYTES:
            raise ValueError("too large")
        return json.loads(self.rfile.read(length) or b"{}")

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/field/commands":
            last_heard["app"] = now_ms()
            after = int(parse_qs(url.query).get("after", ["0"])[0])
            with commands_changed:
                commands_changed.wait_for(lambda: len(commands) > after, timeout=COMMAND_WAIT_S)
                waiting = commands[after:]
            return self.reply({"commands": waiting})
        if url.path == "/ctl/status":
            now = now_ms()
            return self.reply(
                {
                    "session": str(session.folder),
                    "camera": camera.description,
                    "appHeardMsAgo": now - last_heard["app"] if last_heard["app"] else None,
                    "puckHeardMsAgo": now - last_heard["puck"] if last_heard["puck"] else None,
                    "step": session.step_status(),
                    "info": session.info,
                }
            )
        self.reply({"error": "not found"}, 404)

    def do_POST(self):
        try:
            body = self.body()
        except ValueError as error:
            return self.reply({"error": str(error)}, 400)
        path = urlparse(self.path).path

        # From the app, through the dev server's proxy.
        if path == "/field/hello":
            last_heard["app"] = now_ms()
            session.update_info(app=body)
            session.event("app", "hello", **body)
            return self.reply({"session": session.folder.name, "lastCommand": len(commands)})
        if path == "/field/events":
            last_heard["app"] = now_ms()
            for event in body.get("events", []):
                session.event("app", event.pop("kind", "?"), **event)
            return self.reply({"ok": True})
        if path == "/field/solve-start":
            # A plate solve starts on the phone: the telescope's picture now, in the same
            # moment as the phone's frame. The reply doesn't wait for it.
            last_heard["app"] = now_ms()
            folder = folder_for_solve(body.get("id"), body.get("trigger", ""))
            session.event("app", "solveStart", **body, folder=str(folder))
            about = {"solve": body.get("id")}
            threading.Thread(target=scope_picture, args=(folder, about), daemon=True).start()
            return self.reply({"folder": str(folder)})
        if path == "/field/context":
            last_heard["app"] = now_ms()
            if body.get("solve"):
                folder, name = folder_for_solve(body["solve"], body.get("trigger", "")), "context"
            else:
                folder, name = session.step_folder(None), f"context-{now_ms()}"
            saved = save_data_url(body.pop("image", ""), folder / name)
            session.event("app", "context", **body, file=str(saved) if saved else None)
            return self.reply({"saved": str(saved) if saved else None})
        if path == "/device-report":
            last_heard["app"] = now_ms()
            if body.get("kind") == "live-solve":
                return self.reply(save_solve_report(body))
            return self.reply(save_other_report(body))

        # From the agent, on this laptop only (the proxy doesn't pass /ctl).
        if path == "/ctl/step":
            # A step starts as the agent says what to do: the solves on the way go in it.
            folder = session.start_step(int(body["step"]), body.get("label", ""))
            return self.reply({"folder": str(folder)})
        if path == "/ctl/done":
            # The user says the step is done: the telescope's picture of where it ended,
            # and what the app shows at that moment. No solve: the app solves by itself
            # once the telescope stops.
            if not session.step:
                return self.reply({"error": "no step under way"}, 400)
            number = session.step["number"]
            folder = session.step_folder(None) / f"done-{datetime.now():%H%M%S}"
            folder.mkdir(parents=True, exist_ok=True)
            queue_command({"t": "snapshot", "step": number})
            about = {"step": number, "done": True}
            result = scope_picture(folder, about, body.get("exposureMs"), body.get("gain"))
            session.event("user", "done", step=number, folder=str(folder))
            return self.reply({"folder": str(folder), "scope": result})
        if path == "/ctl/scope":
            folder = session.step_folder(None) / f"scope-{datetime.now():%H%M%S}"
            folder.mkdir(parents=True, exist_ok=True)
            about = {"step": body.get("step")}
            return self.reply(scope_picture(folder, about, body.get("exposureMs"), body.get("gain")))
        if path == "/ctl/note":
            session.note(body["text"], body.get("step"))
            return self.reply({"ok": True})
        if path == "/ctl/info":
            session.update_info(**body)
            return self.reply({"ok": True})
        if path == "/ctl/command":
            return self.reply({"id": queue_command(body)})
        self.reply({"error": "not found"}, 404)


def listen_to_puck() -> None:
    """The puck's log lines, broadcast on the Wi-Fi by a field build."""
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp.bind(("0.0.0.0", PUCK_LOG_PORT))
    while True:
        data, (address, _) = udp.recvfrom(2048)
        last_heard["puck"] = now_ms()
        for line in ANSI.sub("", data.decode("utf-8", "replace")).splitlines():
            if line.strip():
                session.puck_line(line)
                session.event("puck", "log", ip=address, line=line)


def main() -> None:
    global session, camera
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera", choices=["zwo", "folder", "none"], default="none")
    parser.add_argument("--folder", default="", help="with --camera folder: where its pictures appear")
    parser.add_argument("--exposure-ms", type=float, default=500)
    parser.add_argument("--gain", type=int, default=200)
    parser.add_argument("--port", type=int, default=HTTP_PORT, help="the dev proxy expects 8445")
    parser.add_argument("--name", default="", help="a word for the session's folder, such as the place")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2] / "field-sessions"))
    args = parser.parse_args()

    session = Session(Path(args.root), args.name)
    camera = open_camera(args.camera, args.folder, args.exposure_ms, args.gain)
    session.update_info(scopeCamera=camera.description)
    session.event("collector", "start", camera=camera.description)
    threading.Thread(target=listen_to_puck, daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"Session: {session.folder}")
    print(f"Telescope camera: {camera.description}")
    print(f"Waiting for the app (proxy to 127.0.0.1:{args.port}) and the puck (UDP {PUCK_LOG_PORT})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        session.event("collector", "stop")
        camera.close()


if __name__ == "__main__":
    main()
