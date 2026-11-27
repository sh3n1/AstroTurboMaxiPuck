# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors
#
# The telescope's own camera, the night's reference for where the telescope really points
# (docs/field-tests.md). Any camera will do, or none: these tests must not depend on one setup.
#   - ZwoCamera: a ZWO ASI camera through ZWO's SDK (the `zwoasi` package and the SDK's
#     ASICamera2.dll, which ASIStudio, N.I.N.A. and SharpCap install).
#   - FolderCamera: any camera whose own software saves pictures into a folder.
#   - NoCamera: no picture; the user says what the eyepiece shows.

import os
import shutil
import time
from pathlib import Path

# Where ZWO's SDK library is usually found on Windows, if ZWO_ASI_LIB doesn't say.
SDK_PLACES = [
    r"C:\Program Files\ASIStudio\ASICamera2.dll",
    r"C:\Program Files\N.I.N.A. - Nighttime Imaging 'N' Astronomy\External\x64\ASI\ASICamera2.dll",
    r"C:\Program Files (x86)\SharpCap 4.1\ASICamera2.dll",
]


class NoCamera:
    description = "none"

    def capture(self, folder: Path, exposure_ms: float | None = None, gain: int | None = None) -> dict:
        return {"ok": False, "reason": "no telescope camera"}

    def close(self) -> None:
        pass


class FolderCamera:
    """Waits for a new picture in a folder another program saves into, and copies it."""

    def __init__(self, folder: str, timeout_s: float = 60):
        self.folder = Path(folder)
        self.timeout_s = timeout_s
        self.description = f"folder {self.folder}"

    def capture(self, folder: Path, exposure_ms: float | None = None, gain: int | None = None) -> dict:
        asked = time.time()
        while time.time() - asked < self.timeout_s:
            newer = [p for p in self.folder.iterdir() if p.is_file() and p.stat().st_mtime > asked]
            if newer:
                newest = max(newer, key=lambda p: p.stat().st_mtime)
                time.sleep(0.5)  # let the other program finish writing it
                target = folder / f"scope{newest.suffix.lower()}"
                shutil.copy2(newest, target)
                return {"ok": True, "file": str(target), "from": str(newest)}
            time.sleep(0.5)
        return {"ok": False, "reason": f"no new picture in {self.folder} within {self.timeout_s:.0f} s"}

    def close(self) -> None:
        pass


class ZwoCamera:
    """The first ZWO ASI camera found, in colour (the SDK debayers) or mono."""

    def __init__(self, exposure_ms: float, gain: int):
        library = os.environ.get("ZWO_ASI_LIB") or next((p for p in SDK_PLACES if Path(p).exists()), None)
        if not library:
            raise RuntimeError("ZWO's ASICamera2.dll not found: set ZWO_ASI_LIB to its path")
        # zwoasi looks for the library as it is imported, and warns without this.
        os.environ["ZWO_ASI_LIB"] = library
        import zwoasi as asi  # only needed with a ZWO camera

        self.asi = asi
        asi.init(library)
        if asi.get_num_cameras() == 0:
            raise RuntimeError("no ZWO camera connected")
        self.camera = asi.Camera(0)
        properties = self.camera.get_camera_property()
        self.colour = bool(properties["IsColorCam"])
        self.description = f"{properties['Name']} ({properties['MaxWidth']}x{properties['MaxHeight']})"
        controls = self.camera.get_controls()
        # USB bandwidth as low as allowed: slow, single pictures are all we need, and reliable.
        self.camera.set_control_value(asi.ASI_BANDWIDTHOVERLOAD, controls["BandWidth"]["MinValue"])
        self.camera.disable_dark_subtract()
        self.camera.set_image_type(asi.ASI_IMG_RGB24 if self.colour else asi.ASI_IMG_RAW8)
        self.exposure_ms = exposure_ms
        self.gain = gain

    def capture(self, folder: Path, exposure_ms: float | None = None, gain: int | None = None) -> dict:
        import numpy as np
        from PIL import Image

        exposure_ms = exposure_ms or self.exposure_ms
        gain = self.gain if gain is None else gain
        self.camera.set_control_value(self.asi.ASI_GAIN, int(gain))
        self.camera.set_control_value(self.asi.ASI_EXPOSURE, int(exposure_ms * 1000))
        started = time.time()
        pixels = self.camera.capture()
        if self.colour:
            pixels = pixels[..., ::-1]  # the SDK gives blue, green, red
        target = folder / "scope.png"
        Image.fromarray(pixels).save(target)
        # A stretched copy to look at: faint stars become visible, as in a capture program.
        low, high = np.percentile(pixels, [50, 99.9])
        stretched = np.clip((pixels.astype(np.float32) - low) / max(high - low, 1) * 255, 0, 255)
        preview = folder / "scope-preview.jpg"
        Image.fromarray(stretched.astype(np.uint8)).save(preview, quality=90)
        return {
            "ok": True,
            "file": str(target),
            "preview": str(preview),
            "exposureMs": exposure_ms,
            "gain": gain,
            "seconds": round(time.time() - started, 2),
            "max": int(pixels.max()),
            "median": float(np.median(pixels)),
        }

    def close(self) -> None:
        self.camera.close()


def open_camera(kind: str, folder: str, exposure_ms: float, gain: int):
    if kind == "zwo":
        return ZwoCamera(exposure_ms, gain)
    if kind == "folder":
        return FolderCamera(folder)
    return NoCamera()
