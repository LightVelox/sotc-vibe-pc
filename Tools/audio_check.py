import argparse
import array
import ctypes
import os
from pathlib import Path
import subprocess
import time
import wave


def close_window(pid):
    user32 = ctypes.windll.user32
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def visit(window, unused):
        owner = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(ctypes.c_void_p(window), ctypes.byref(owner))
        if owner.value == pid:
            user32.PostMessageW(ctypes.c_void_p(window), 0x0010, 0, 0)
        return True

    user32.EnumWindows(callback_type(visit), 0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--field", type=int, default=2000)
    parser.add_argument("--load", type=Path)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    timeline = output / "timeline.txt"
    if timeline.exists():
        timeline.unlink()
    env = dict(os.environ)
    env["PS2X_AUDIO_DUMP"] = str(output / "audio.wav")
    env["PS2X_PAD_SCRIPT"] = "290v:cross,375v:cross,770v:down,800v:cross"
    env["SOTC_TIMELINE"] = str(timeline)
    env["PS2X_MEMCARD"] = "0"
    env["SOTC_VIDEO_MODE"] = "PAL"
    if args.load:
        env["PS2X_STATE_LOAD_AT"] = "1:" + str(args.load.resolve())
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    last_field = 0
    with (output / "stdout.txt").open("w") as log:
        executable = args.exe.resolve() if args.exe else repo / "build/port/bin/sotc.exe"
        process = subprocess.Popen([str(executable)], cwd=repo, env=env,
                                   stdout=log, stderr=subprocess.STDOUT, startupinfo=startup)
        started = time.monotonic()
        try:
            while process.poll() is None and time.monotonic() - started < args.timeout:
                time.sleep(1)
                if timeline.exists():
                    lines = timeline.read_text(errors="replace").splitlines()
                    fields = [int(line.split()[0]) for line in lines if line and line[0].isdigit()]
                    if fields:
                        last_field = fields[-1]
                        if last_field >= args.field:
                            break
        finally:
            if process.poll() is None:
                close_window(process.pid)
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    with wave.open(str(output / "audio.wav"), "rb") as audio:
        samples = array.array("h", audio.readframes(audio.getnframes()))
        peak = max((abs(sample) for sample in samples), default=0)
        nonzero = sum(sample != 0 for sample in samples)
        print(f"field={last_field} exit={process.returncode} rate={audio.getframerate()} "
              f"channels={audio.getnchannels()} frames={audio.getnframes()} peak={peak} nonzero={nonzero}")
        if process.returncode != 0 or last_field < args.field or audio.getframerate() != 48000 or audio.getnchannels() != 2 or peak == 0:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
