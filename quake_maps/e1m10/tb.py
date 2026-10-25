"""Drive an open TrenchBroom through the documented command-line control client.

    python tb.py load <map>                      # load a .map (discarding editor changes)
    python tb.py view x y z yaw pitch [png]      # absolute camera pose (degrees), optional capture
    python tb.py frame [png]                     # frame all geometry, optional capture
    python tb.py status
"""
import json
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CLIENT = ROOT.parents[1] / "build-vs18/app/Debug/TrenchBroomCommandLineClient.exe"
SOCKET = "e1m10"
_id = 0


def call(command, **kw):
    global _id
    _id += 1
    req = json.dumps({"id": _id, "command": command, **kw}) + "\n"
    p = subprocess.run([str(CLIENT), "--socket", SOCKET], input=req, text=True, capture_output=True)
    if p.returncode:
        raise RuntimeError(p.stderr + p.stdout)
    r = json.loads(p.stdout)
    if not r["ok"]:
        raise RuntimeError(r)
    return r["result"]


def capture(name):
    out = Path(name) if Path(name).is_absolute() else ROOT / "progress" / name
    out.parent.mkdir(exist_ok=True)
    return call("capture_viewport", path=str(out), overwrite=True)


def view(x, y, z, yaw, pitch):
    c = call("camera_move", translation=[0, 0, 0])
    d = c["direction"]
    old_yaw = math.degrees(math.atan2(d[1], d[0]))
    old_pitch = math.degrees(math.asin(max(-1, min(1, d[2]))))
    p = c["position"]
    # Level the camera first so yaw is applied about the world up axis.
    call("camera_move", pitch_degrees=-old_pitch)
    return call("camera_move", translation=[x - p[0], y - p[1], z - p[2]],
                yaw_degrees=yaw - old_yaw, pitch_degrees=pitch)


if __name__ == "__main__":
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "load":
        print(call("load_map", path=str(Path(args[0]).resolve()), discard_changes=True))
    elif cmd == "view":
        print(view(*map(float, args[:5])))
        if len(args) > 5:
            print(capture(args[5]))
    elif cmd == "frame":
        print(call("frame_geometry"))
        if args:
            print(capture(args[0]))
    else:
        print(call(cmd))
