# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors
#
# A field test session's folder (docs/field-tests.md): one timeline of events from the app,
# the puck and the agent, and a folder per step with its pictures.

import base64
import json
import re
import threading
import time
from datetime import datetime
from pathlib import Path


def now_ms() -> int:
    return int(time.time() * 1000)


def slug(text: str) -> str:
    """A short, safe folder name from a label such as "Capella" or "M 42"."""
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")[:40] or "step"


class Session:
    """Everything one night writes, safe to call from several threads."""

    def __init__(self, root: Path, name: str = ""):
        stamp = datetime.now().strftime("%Y-%m-%dT%H-%M")
        self.folder = root / (f"{stamp}-{slug(name)}" if name else stamp)
        self.folder.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()
        self.events = open(self.folder / "events.jsonl", "a", encoding="utf-8")
        self.puck_log = open(self.folder / "puck.log", "a", encoding="utf-8")
        self.info_path = self.folder / "session.json"
        self.info = json.loads(self.info_path.read_text("utf-8")) if self.info_path.exists() else {}
        self.info.setdefault("started", datetime.now().astimezone().isoformat())
        self.save_info()
        # The step being captured, where the app's photo and solve go as they arrive.
        self.step: dict | None = None

    def save_info(self) -> None:
        self.info_path.write_text(json.dumps(self.info, indent=1, ensure_ascii=False), "utf-8")

    def update_info(self, **fields) -> None:
        with self.lock:
            self.info.update(fields)
            self.save_info()

    def event(self, source: str, kind: str, **fields) -> None:
        """One line in the timeline. `rx` is the laptop's clock, `at` the sender's, if any."""
        line = {"rx": now_ms(), "source": source, "kind": kind, **fields}
        with self.lock:
            self.events.write(json.dumps(line, ensure_ascii=False) + "\n")
            self.events.flush()

    def puck_line(self, text: str) -> None:
        stamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        with self.lock:
            self.puck_log.write(f"{stamp} {text}\n")
            self.puck_log.flush()

    # Steps.

    def start_step(self, number: int, label: str) -> Path:
        folder = self.folder / "steps" / f"{number:02d}-{slug(label)}"
        folder.mkdir(parents=True, exist_ok=True)
        with self.lock:
            self.step = {"number": number, "label": label, "folder": folder, "started": now_ms()}
        self.event("agent", "step", step=number, label=label)
        return folder

    def step_folder(self, number: int | None) -> Path:
        """The folder for a result of step `number`; one outside any step goes in "loose"."""
        with self.lock:
            step = self.step
        if step and (number is None or number == step["number"]):
            return step["folder"]
        folder = self.folder / "loose"
        folder.mkdir(exist_ok=True)
        return folder

    def step_status(self) -> dict | None:
        with self.lock:
            if not self.step:
                return None
            return {k: (str(v) if isinstance(v, Path) else v) for k, v in self.step.items()}

    def note(self, text: str, number: int | None = None) -> None:
        self.event("user", "note", step=number, text=text)
        folder = self.step_folder(number)
        with open(folder / "notes.txt", "a", encoding="utf-8") as f:
            f.write(f"{datetime.now():%H:%M:%S} {text}\n")


def save_data_url(data_url: str, path_without_suffix: Path) -> Path | None:
    """Writes a "data:image/jpeg;base64,…" picture to a file; None if it isn't one."""
    match = re.fullmatch(r"data:image/(jpeg|png);base64,([A-Za-z0-9+/=]+)", data_url or "")
    if not match:
        return None
    path = path_without_suffix.with_suffix(".jpg" if match[1] == "jpeg" else ".png")
    path.write_bytes(base64.b64decode(match[2]))
    return path
