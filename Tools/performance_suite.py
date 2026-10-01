import argparse
import json
from pathlib import Path
import subprocess
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("group", choices=["shrine", "isolate", "hash", "heavy"])
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--vsync", choices=["0", "1"], default="1")
    parser.add_argument("--only")
    parser.add_argument("--window-fields", type=int, default=1200)
    args = parser.parse_args()
    if args.repeats <= 0 or args.window_fields <= 0:
        parser.error("--repeats and --window-fields must be positive")
    args.out.mkdir(parents=True, exist_ok=False)
    runs = []
    scenes = ["colossus01", "colossus02", "colossus13"] if args.group == "heavy" else ["rotate"] if args.group == "isolate" else ["rotate", "move"]
    if args.only:
        scenes = [scene for scene in scenes if scene in args.only.split(",")]
    if not scenes:
        parser.error("--only must select at least one scene in this group")
    for scene in scenes:
        for trial in range(1, args.repeats + 1):
            variants = ["baseline", "all"] if args.group != "isolate" else ["baseline", "presentation", "compact", "queue", "unpack", "clip", "all"]
            if trial % 2 == 0:
                variants.reverse()
            for variant in variants:
                path = args.out / f"{scene}-{variant}-{trial}"
                state = args.state
                first, last, end = 3400, 4600, 4700
                start = 3100
                if args.group == "heavy":
                    state = args.state / (scene + "_fight.state")
                    with state.open("rb") as handle:
                        header = handle.read(20)
                    if header[:8] != b"2XSTATE1":
                        raise ValueError("Unknown saved-state header")
                    saved_field = struct.unpack_from("<Q", header, 12)[0]
                    first, last, end, start = saved_field + 600, saved_field + 1800, saved_field + 1900, saved_field + 100
                last, end = first + args.window_fields, first + args.window_fields + 100
                pad = str(start) + "v:" + ("ldown" if scene == "move" else "rright") + ":2400"
                command = [sys.executable, str(ROOT / "Tools/performance_capture.py"), variant, "--scene", "manual",
                           "--state", str(state.resolve()), "--pad-script", pad, "--out", str(path.resolve()),
                           "--seconds", "90", "--end-field", str(end), "--vsync", args.vsync]
                if args.group == "heavy":
                    command.extend(["--screenshot-fields", str(saved_field + 300) + "," + str(last + 10)])
                if args.group == "hash":
                    command.append("--hash-images")
                print("Running", path, flush=True)
                subprocess.run(command, check=True, cwd=ROOT)
                exit_status = json.loads((path / "exit.json").read_text())
                if exit_status["exit_code"] != 0:
                    raise RuntimeError(f"Capture did not close normally: {path}")
                subprocess.run([sys.executable, str(ROOT / "Tools/frame_report.py"), str(path.resolve()),
                                "--first-field", str(first), "--last-field", str(last)], check=True, cwd=ROOT,
                               stdout=subprocess.DEVNULL)
                report = json.loads((path / f"report-{first}-{last}.json").read_text())
                runs.append({"scene": scene, "variant": variant, "trial": trial, "path": str(path), "report": report})
                (args.out / "results.json").write_text(json.dumps(runs, indent=2))
                print("Updates/s:", report["game_scheduler_updates_per_second"], "submit p95:",
                      report["host_submit_ms"]["p95"], "host interval p99:",
                      report["host_swap_interval_ms"]["p99"], flush=True)


if __name__ == "__main__":
    main()
