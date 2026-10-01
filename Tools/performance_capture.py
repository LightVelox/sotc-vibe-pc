import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from performance_window import capture_window, exercise_window
from performance_resources import Resources


ROOT = Path(__file__).resolve().parents[1]
SWITCHES = ["PS2X_GS_DIRECT_PRESENT", "PS2X_GS_COMPACT_QUEUE", "PS2X_VIF_SIMD_UNPACK", "PS2X_VU1_SIMD_CLIP"]
VARIANTS = {
    "baseline": ([0, 0, 0, 0], 256),
    "presentation": ([1, 0, 0, 0], 256),
    "compact": ([0, 1, 0, 0], 256),
    "queue": ([0, 0, 0, 0], 8),
    "unpack": ([0, 0, 1, 0], 256),
    "clip": ([0, 0, 0, 1], 256),
    "all": ([1, 1, 1, 1], 8),
    "directqueue": ([1, 0, 0, 0], 8),
    "transport": ([1, 1, 0, 0], 8),
}
BOOT_PAD = "290v:cross,770v:down,800v:cross"
GAME_PAD = BOOT_PAD + ",1250v:start,1450v:start,1550v:cross,1700v:cross,2300v:start"


def close_window(pid):
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_ssize_t)
    user32.EnumWindows.argtypes = [callback_type, ctypes.c_ssize_t]
    user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
    user32.IsWindowVisible.argtypes = [ctypes.c_void_p]
    user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]

    @callback_type
    def visit(window, unused):
        owner = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(window):
            user32.PostMessageW(window, 0x0010, 0, 0)
        return True

    user32.EnumWindows(visit, 0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("variant", choices=VARIANTS)
    parser.add_argument("--scene", choices=["shrine", "rotate", "intro", "manual"], default="shrine")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--seconds", type=float, default=115)
    parser.add_argument("--end-field", type=int)
    parser.add_argument("--trace-vu-field", type=int)
    parser.add_argument("--vsync", choices=["0", "1"], default="1")
    parser.add_argument("--hash-images", action="store_true")
    parser.add_argument("--state", type=Path)
    parser.add_argument("--pad-script")
    parser.add_argument("--profile-thread", choices=["GameThread", "VUThread", "GIFThread", "GSThread"])
    parser.add_argument("--profile-window", default="70:15")
    parser.add_argument("--copy-symbols", action="store_true")
    parser.add_argument("--snapshots", default="")
    parser.add_argument("--screenshot-fields", default="")
    parser.add_argument("--save-state", help="Field at which to save a private state")
    parser.add_argument("--verify-vu", action="store_true")
    parser.add_argument("--exercise-window", action="store_true")
    parser.add_argument("--source-bin", type=Path)
    parser.add_argument("--setting", action="append", default=[])
    parser.add_argument("--camera-test")
    parser.add_argument("--camera-trace", action="store_true")
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    if args.state and args.scene != "manual" and args.pad_script is None:
        parser.error("Use --scene manual with a saved state, or supply its field-based --pad-script")
    source = (args.source_bin or ROOT / "build/port/bin").resolve()
    out = args.out or ROOT / "build/performance" / (time.strftime("%Y%m%d-%H%M%S") + "-" + args.variant + "-" + args.scene)
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = out / "bin"
    binary.mkdir()
    for name in ["sotc.exe", "sotc.ini"] + (["sotc.pdb"] if args.copy_symbols else []):
        if (source / name).exists():
            shutil.copy2(source / name, binary / name)
    for dll in source.glob("*.dll"):
        shutil.copy2(dll, binary / dll.name)
    if (source / "memcards").is_dir():
        shutil.copytree(source / "memcards", binary / "memcards")
    values, chunks = VARIANTS[args.variant]
    settings = dict(zip(SWITCHES, map(str, values)))
    settings.update({
        "PS2X_GS_QUEUE_CHUNKS": str(chunks),
        "PS2X_GS_SHADER_CACHE_DIR": str(ROOT / "build/performance/shader-cache"),
        "PS2X_FRAME_HASH": "1" if args.hash_images else "0",
        "PS2X_FRAME_TIMES": str(out / "frames.csv"),
        "PS2X_SCREENSHOT_DIR": str(out),
        "PS2X_SCREENSHOT_FIELDS": args.screenshot_fields,
        "SOTC_TIMELINE": str(out / "timeline.txt"),
        "SOTC_TIMELINE_WATCH": "1dc9ec,1477270,1f8a98,1f8a9c",
        "PS2X_MTVU_STATS": "1",
        "PS2X_GS_GPU_STATS": "1",
        "PS2X_AUDIO_STATS": "1",
        "PS2X_GS_GPU": "1",
        "PS2X_GS_THREAD": "1",
        "PS2X_MTVU": "1",
        "PS2X_VU1_RECOMP": "1",
        "PS2X_EE_HOST_PACING": "1",
        "PS2X_VU1_HOST_PACING": "1",
        "SOTC_VIDEO_MODE": "NTSC",
        "SOTC_VSYNC": args.vsync,
        "PS2X_MEMCARD": "1" if args.state else "0",
    })
    pads = {"shrine": GAME_PAD + ",2700v:ldown:1500", "rotate": GAME_PAD + ",2700v:rright:1500", "intro": BOOT_PAD, "manual": ""}
    settings["PS2X_PAD_SCRIPT"] = args.pad_script if args.pad_script is not None else pads[args.scene]
    environment = dict(os.environ)
    environment["_NT_SYMBOL_PATH"] = str(source)
    for key in ["PS2X_STATE_LOAD_AT", "PS2X_STATE_SAVE_AT", "SOTC_DEBUG_SCRIPT", "SOTC_POKE", "SOTC_PROFILE", "SOTC_CAMERA_TEST", "SOTC_CAMERA_TRACE", "SOTC_TRACE_CALLS", "PS2X_VU1_VERIFY", "PS2X_GS_RECORD", "PS2X_GS_TRACE", "PS2X_GS_GPU_PROF"]:
        environment.pop(key, None)
    if args.state:
        private_state = out / "input.state"
        shutil.copy2(args.state, private_state)
        settings["PS2X_STATE_LOAD_AT"] = "1:" + str(private_state).replace("\\", "/")
    if args.camera_test:
        settings["SOTC_CAMERA_TEST"] = args.camera_test
    if args.camera_trace:
        settings["SOTC_CAMERA_TRACE"] = str(out / "camera.csv")
    for setting in args.setting:
        key, separator, value = setting.partition("=")
        if not separator or not key.startswith(("SOTC_", "PS2X_")):
            parser.error("--setting requires SOTC_KEY=value or PS2X_KEY=value")
        settings[key] = value
    if args.profile_thread:
        settings["SOTC_PROFILE"] = args.profile_window
        settings["SOTC_PROFILE_THREAD"] = args.profile_thread
    if args.save_state:
        settings["PS2X_STATE_SAVE_AT"] = args.save_state + ":" + str(out / "capture.state").replace("\\", "/")
    if args.verify_vu:
        settings["PS2X_VU1_VERIFY"] = "1"
    if args.trace_vu_field is not None:
        settings["PS2X_VU1_TRACE"] = str(out / "trace.bin") + ":" + str(args.trace_vu_field) + ":4"
    environment.update(settings)
    with (binary / "sotc.exe").open("rb") as executable:
        binary_hash = hashlib.file_digest(executable, "sha256").hexdigest() if hasattr(hashlib, "file_digest") else hashlib.sha256(executable.read()).hexdigest()
    (out / "settings.json").write_text(json.dumps({"variant": args.variant, "scene": args.scene, "source_bin": str(source), "executable_sha256": binary_hash, "settings": settings}, indent=2))
    print("Capture:", out, flush=True)
    print("Keep the game visible; close it normally or let the time limit close its window.", flush=True)
    started = time.perf_counter()
    with (out / "stdout.txt").open("w") as log:
        process = subprocess.Popen([str(binary / "sotc.exe")], cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
        resources = Resources(process, out / "resources.csv")
        snapshots = sorted(float(value) for value in args.snapshots.split(",") if value)
        deadline = started + args.seconds
        actions = [(8, "save"), (10, "load"), (13, "fullscreen"), (18, "fullscreen"), (20, "resize"),
                   (27, "minimize"), (29, "restore")] if args.exercise_window else []
        while process.poll() is None and time.perf_counter() < deadline:
            resources.sample()
            if actions and time.perf_counter() - started >= actions[0][0]:
                at, action = actions.pop(0)
                try:
                    state = exercise_window(process.pid, action)
                    print("Window action:", action, "at", at, state, flush=True)
                except Exception as error:
                    print("Window action failed:", action, error, flush=True)
            if args.end_field is not None and (out / "timeline.txt").exists():
                with (out / "timeline.txt").open("rb") as timeline:
                    timeline.seek(max(0, (out / "timeline.txt").stat().st_size - 2048))
                    lines = timeline.read().splitlines()
                fields = [int(line.split()[0]) for line in lines[1:] if line.split() and line.split()[0].isdigit()]
                if fields and fields[-1] >= args.end_field:
                    break
            if snapshots and time.perf_counter() - started >= snapshots[0]:
                at = snapshots.pop(0)
                try:
                    capture_window(process.pid, out / f"capture-{at:g}.png")
                except Exception as error:
                    print("Snapshot failed:", error, flush=True)
            time.sleep(0.05)
        resources.close()
        if process.poll() is None:
            close_window(process.pid)
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait()
    (out / "exit.json").write_text(json.dumps({"exit_code": process.returncode, "wall_seconds": time.perf_counter() - started}, indent=2))
    print("Exit code:", process.returncode)
    print("Report with: python Tools/frame_report.py", str(out), "--first-field 3000 --last-field 3900")


if __name__ == "__main__":
    main()
