#!/usr/bin/env python3
import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import types

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "Tools"))

_mb_path = os.path.join(REPO, "Tools", "measure_boot.py")
mb = types.ModuleType("mb")
mb.__dict__["__file__"] = _mb_path
_src = open(_mb_path).read()
exec(compile(_src[:_src.index("def main():")], _mb_path, "exec"), mb.__dict__)
from audio_check import close_window  # noqa: E402

ISO = os.path.join(REPO, "Game", "SHADOW_COLOSSUS (PAL).iso")
EXE = os.path.join(REPO, "build", "port", "bin", "sotc.exe")
DEFAULT_STATE = os.path.join(REPO, "build", "saved_states", "colossus1_prefight.state")
HOOK = "0x1c35b0"
SCHED_FRAME = 0x1DC9EC
BOSS_FLAGS_WORD = 0x12E5914
BOSS_DEATH_CALLBACK = 0x1381408
BOSS_FLAG_IDS = [45, 44, 40, 48, 41, 46, 54, 53, 42, 51, 49, 50, 43, 52, 47, 55]
BOSS_NAMES = ["Valus", "Quadratus", "Gaius", "Phaedra", "Avion", "Barba", "Hydrus", "Kuromori",
              "Basaran", "Dirge", "Celosia", "Pelagia", "Phalanx", "Cenobia", "Argus", "Malus"]


def load_symbols():
    table = {}
    with open(os.path.join(REPO, "Analysis", "ADDRESS_MAP.generated.csv")) as f:
        for row in csv.reader(f):
            if len(row) > 4 and row[0].startswith("0x"):
                address = int(row[0], 16)
                table[row[3]] = address
                if row[4]:
                    table[row[4]] = address
    return table


SYMBOLS = load_symbols()


def resolve(name):
    if re.fullmatch(r"(0x)?[0-9a-fA-F]+", name) and name not in SYMBOLS:
        return "%x" % int(name, 16)
    if name not in SYMBOLS:
        raise SystemExit("unknown guest function: " + name)
    return "%x" % SYMBOLS[name]


def expand(steps):
    out = []
    for step in steps:
        field, op, *args = step
        prefix = "+%d" % field
        if op == "warp":
            out.append("%s:call:%s:a0=%d" % (prefix, resolve("bossStatTimeAttackJumpSekiban"), int(args[0]) - 1))
        elif op == "kill":
            out.append("%s:call:%s" % (prefix, resolve("GAMEUTIL_get_boss_obj")))
            out.append("+%d:call:%x:a0=$v0" % (field + 2, BOSS_DEATH_CALLBACK))
        elif op == "call":
            out.append(":".join([prefix, "call", resolve(args[0])] + list(args[1:])))
        elif op in ("save", "peek", "poke", "pokef", "pokeb", "quit"):
            out.append(":".join([prefix, op] + [str(a) for a in args]))
        else:
            raise SystemExit("unknown step " + op)
    return ";".join(out)


def prepare_exe(workdir):
    os.makedirs(workdir, exist_ok=True)
    shutil.copy2(EXE, os.path.join(workdir, "sotc.exe"))
    cards = os.path.join(REPO, "build", "port", "bin", "memcards")
    target = os.path.join(workdir, "memcards")
    if os.path.isdir(cards) and not os.path.isdir(target):
        shutil.copytree(cards, target)
    return os.path.join(workdir, "sotc.exe")


def run_game(exe, out, state, script, end_rel, captures_every, pad="", extra_env=None, stall=60.0, maxtime=1200.0, stop_bit=None, tail=300):
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        p = os.path.join(out, f)
        if os.path.isfile(p):
            os.remove(p)
    env = dict(os.environ)
    env.update({
        "SOTC_ISO": ISO,
        "PS2X_STATE_LOAD_AT": "1:" + os.path.abspath(state).replace("\\", "/"),
        "PS2X_MEMCARD": "1",
        "SOTC_VIDEO_MODE": "NTSC",
        "PS2X_MTVU_STATS": "1",
        "PS2X_GS_GPU_STATS": "1",
        "PS2X_AUDIO_STATS": "1",
        "SOTC_DEBUG_HOOK": HOOK,
        "SOTC_DEBUG_SCRIPT": script,
        "SOTC_TIMELINE": os.path.join(out, "timeline.txt").replace("\\", "/"),
        "SOTC_TIMELINE_WATCH": "%x,%x" % (SCHED_FRAME, BOSS_FLAGS_WORD),
        "PS2X_PAD_SCRIPT": pad,
    })
    env.update(extra_env or {})
    stdout = open(os.path.join(out, "stdout.txt"), "w")
    p = subprocess.Popen([exe], cwd=REPO, env=env, stdout=stdout, stderr=subprocess.STDOUT)
    t0 = time.perf_counter()
    tl_path = os.path.join(out, "timeline.txt")
    fh = None
    hwnd = None
    first = None
    last = 0
    tlast = t0
    next_cap = None
    status = "ok"
    while True:
        if p.poll() is not None:
            status = "exited 0x%08x" % (p.returncode & 0xFFFFFFFF)
            break
        now = time.perf_counter()
        if now - t0 > maxtime:
            status = "timeout"
            break
        if hwnd is None:
            ws = mb.windows_of(p.pid)
            if ws:
                hwnd = ws[0]
        if fh is None:
            try:
                fh = open(tl_path)
            except OSError:
                time.sleep(0.05)
                continue
        for line in fh.readlines():
            parts = line.split()
            if parts and parts[0].isdigit():
                f = int(parts[0])
                if f != last:
                    last = f
                    tlast = now
                    if first is None and f > 100:
                        first = f
                        next_cap = f
                if stop_bit is not None and first is not None and len(parts) >= 4 and not (int(parts[3], 16) >> stop_bit) & 1:
                    end_rel = min(end_rel, f - first + tail)
                    stop_bit = None
        if first is not None and last > 100 and now - tlast > stall:
            status = "stall"
            break
        if hwnd is not None and next_cap is not None and captures_every and last >= next_cap:
            try:
                mb.grab(hwnd).save(os.path.join(out, "cap_%06d.png" % (last - first)))
            except Exception as error:
                with open(os.path.join(out, "capture_errors.txt"), "a") as log:
                    log.write("%d %r\n" % (last - first, error))
            next_cap += captures_every
        if first is not None and last - first >= end_rel:
            break
        time.sleep(0.005)
    if p.poll() is None:
        try:
            close_window(p.pid)
            p.wait(timeout=15)
        except Exception:
            p.kill()
            p.wait()
    stdout.close()
    return {"status": status, "first_field": first, "last_field": last, "elapsed": time.perf_counter() - t0}


def dark_captures(out, window):
    if not window:
        return None
    try:
        from PIL import Image, ImageStat
    except ImportError:
        return None
    files = [f for f in sorted(os.listdir(out)) if f.startswith("cap_") and window[0] <= int(f[4:-4]) <= window[1]]
    if not files:
        return 0, 0
    dark = 0
    for f in files:
        if ImageStat.Stat(Image.open(os.path.join(out, f)).convert("L")).mean[0] < 6.0:
            dark += 1
    if files and dark * 2 > len(files):
        return dark, len(files)
    return None


def analyse(out, result, colossus=None, freeze_limit=300, fight_window=None):
    text = open(os.path.join(out, "stdout.txt"), errors="replace").read()
    rows = []
    for line in open(os.path.join(out, "timeline.txt")):
        parts = line.split()
        if len(parts) >= 4 and parts[0].isdigit():
            rows.append((int(parts[0]), int(parts[2], 16), int(parts[3], 16), float(parts[1])))
    first = result.get("first_field") or 0
    rows = [r for r in rows if r[0] >= first]
    longest = 0
    longest_at = None
    run_start = None
    for i in range(1, len(rows)):
        if rows[i][1] == rows[i - 1][1]:
            if run_start is None:
                run_start = rows[i - 1][0]
            if rows[i][0] - run_start > longest:
                longest = rows[i][0] - run_start
                longest_at = run_start - first
        else:
            run_start = None
    fps = [float(m) for m in re.findall(r"\[mtvu\] field \d+: ([\d.]+) fields/s", text)][1:]
    drawn = [float(m) for m in re.findall(r"([\d.]+) drawn/s", text)][1:]
    underruns = sum(int(m) for m in re.findall(r"(\d+) underruns", text)[1:])
    game_fps = []
    if fight_window:
        window = [r for r in rows if first + fight_window[0] <= r[0] <= first + fight_window[1]]
        for i in range(0, len(window) - 1, 120):
            chunk = window[i:i + 121]
            if len(chunk) > 60 and chunk[-1][3] > chunk[0][3]:
                game_fps.append((chunk[-1][1] - chunk[0][1]) / (chunk[-1][3] - chunk[0][3]))
    report = {
        "game_fps_min": round(min(game_fps), 1) if game_fps else None,
        "game_fps_avg": round(sum(game_fps) / len(game_fps), 1) if game_fps else None,
        "status": result["status"],
        "fields": (result["last_field"] or 0) - first,
        "wall_s": round(result["elapsed"], 1),
        "crash_lines": [l[:300] for l in text.splitlines() if "missing-target" in l or "terminate" in l.lower() or ("Game thread returned" in l and "window close requested" not in l)][:3],
        "longest_freeze_fields": longest,
        "freeze_at": longest_at,
        "fields_per_s_min": min(fps) if fps else None,
        "fields_per_s_avg": round(sum(fps) / len(fps), 1) if fps else None,
        "drawn_per_s_min": min(drawn) if drawn else None,
        "drawn_per_s_avg": round(sum(drawn) / len(drawn), 1) if drawn else None,
        "audio_underruns": underruns,
        "script_lines": [l.split("[debug-script] ", 1)[1] for l in text.splitlines() if "[debug-script] field" in l],
    }
    if colossus and rows:
        bit = BOSS_FLAG_IDS[colossus - 1] - 32
        report["boss_dead"] = ((rows[-1][2] >> bit) & 1) == 0
    problems = []
    if report["status"] not in ("ok",):
        problems.append(report["status"])
    if report["crash_lines"]:
        problems.append("crash")
    if longest > freeze_limit:
        problems.append("freeze %d fields at +%s" % (longest, longest_at))
    if colossus and not report.get("boss_dead"):
        problems.append("boss not dead")
    boss_obj = [l for l in report["script_lines"] if "done" in l and ":call:%s" % resolve("GAMEUTIL_get_boss_obj") in l]
    if boss_obj and boss_obj[0].endswith("v0=0x0 f0=" + boss_obj[0].split("f0=")[-1]):
        problems.append("no colossus object at kill time")
    dark = dark_captures(out, fight_window)
    if dark and dark[1] == 0:
        problems.append("no screen captures during the fight")
    elif dark:
        problems.append("dark screen %d of %d fight captures" % dark)
    report["problems"] = problems
    report["pass"] = not problems
    return report


def contact_sheet(out, path, cols=6):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    files = sorted(f for f in os.listdir(out) if f.startswith("cap_"))
    if not files:
        return
    w, h = 256, 192
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w, rows * (h + 14)), "black")
    draw = ImageDraw.Draw(sheet)
    for i, f in enumerate(files):
        im = Image.open(os.path.join(out, f)).convert("RGB").resize((w, h))
        x, y = (i % cols) * w, (i // cols) * (h + 14)
        sheet.paste(im, (x, y + 14))
        draw.text((x + 2, y), "+" + str(int(f[4:-4])), fill="yellow")
    sheet.save(path)


def colossus_steps(n, fight_fields, save_state_dir):
    steps = [] if n == 1 else [(60, "warp", n)]
    if save_state_dir:
        steps.append((60 + fight_fields - 200, "save", os.path.join(save_state_dir, "colossus%02d_fight.state" % n).replace("\\", "/")))
    steps.append((60 + fight_fields, "kill"))
    return steps


def parse_range(text):
    result = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-")
            result.extend(range(int(a), int(b) + 1))
        elif part:
            result.append(int(part))
    return result


def cmd_colossi(a):
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    todo = parse_range(a.only)
    results = {}
    lock = threading.Lock()
    save_dir = os.path.abspath(a.save_states) if a.save_states else None
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)

    def worker(job):
        exe = prepare_exe(os.path.join(out, "_exe%d" % job))
        while True:
            with lock:
                if not todo:
                    return
                n = todo.pop(0)
            steps = colossus_steps(n, a.fight, save_dir)
            script = expand(steps)
            cdir = os.path.join(out, "c%02d" % n)
            end = 60 + a.fight + a.after
            state = a.state if n > 1 else a.valus_state
            print("[c%02d %s] start" % (n, BOSS_NAMES[n - 1]), flush=True)
            res = run_game(exe, cdir, state, script, end, a.every, stop_bit=BOSS_FLAG_IDS[n - 1] - 32)
            rep = analyse(cdir, res, colossus=n, fight_window=(60 + a.fight // 2, 60 + a.fight))
            rep["name"] = BOSS_NAMES[n - 1]
            contact_sheet(cdir, os.path.join(out, "c%02d_sheet.png" % n))
            with lock:
                results[n] = rep
                json.dump(results, open(os.path.join(out, "report.json"), "w"), indent=1, sort_keys=True)
            print("[c%02d %s] %s %s" % (n, BOSS_NAMES[n - 1], "PASS" if rep["pass"] else "FAIL", ", ".join(rep["problems"])), flush=True)

    threads = [threading.Thread(target=worker, args=(j,)) for j in range(a.jobs)]
    for t in threads:
        t.start()
        time.sleep(3)
    for t in threads:
        t.join()
    write_markdown(out, results)


def write_markdown(out, results):
    lines = ["| # | Colossus | Result | Problems | game frames/s in fight min/avg | fields/s min/avg | drawn/s min/avg | underruns | longest game stall |",
             "|---|---|---|---|---|---|---|---|---|"]
    for n in sorted(results):
        r = results[n]
        lines.append("| %d | %s | %s | %s | %s / %s | %s / %s | %s / %s | %s | %s |" % (
            n, r["name"], "PASS" if r["pass"] else "FAIL", ", ".join(r["problems"]) or "-",
            r.get("game_fps_min"), r.get("game_fps_avg"),
            r["fields_per_s_min"], r["fields_per_s_avg"], r["drawn_per_s_min"], r["drawn_per_s_avg"],
            r["audio_underruns"], r["longest_freeze_fields"]))
    open(os.path.join(out, "report.md"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


def cmd_script(a):
    steps = []
    for item in a.steps:
        parts = item.split(":")
        steps.append((int(parts[0]), parts[1], *parts[2:]))
    exe = prepare_exe(os.path.join(os.path.abspath(a.out), "_exe"))
    res = run_game(exe, os.path.abspath(a.out), a.state, expand(steps), a.end, a.every, pad=a.pad)
    rep = analyse(os.path.abspath(a.out), res)
    contact_sheet(os.path.abspath(a.out), os.path.join(os.path.abspath(a.out), "sheet.png"))
    print(json.dumps(rep, indent=1))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("colossi", help="warp to each colossus, let the fight run, kill it, check the aftermath")
    c.add_argument("out")
    c.add_argument("--state", default=DEFAULT_STATE, help="gameplay save state to start from")
    c.add_argument("--only", default="1-16")
    c.add_argument("--jobs", type=int, default=1)
    c.add_argument("--fight", type=int, default=3000, help="fields between the warp and the kill")
    c.add_argument("--after", type=int, default=7000, help="most fields to keep running after the kill; runs end 300 fields after the colossus is marked dead")
    c.add_argument("--valus-state", default=DEFAULT_STATE, help="state used for colossus 1, which is fought without a warp")
    c.add_argument("--every", type=int, default=300, help="capture interval in fields")
    c.add_argument("--save-states", default="", help="directory for a save state per colossus fight")
    c.set_defaults(func=cmd_colossi)
    s = sub.add_parser("script", help="run custom steps like 60:warp:3 2000:kill 2100:call:Name:a0=1")
    s.add_argument("out")
    s.add_argument("steps", nargs="+")
    s.add_argument("--state", default=DEFAULT_STATE)
    s.add_argument("--end", type=int, default=6000)
    s.add_argument("--every", type=int, default=300)
    s.add_argument("--pad", default="")
    s.set_defaults(func=cmd_script)
    a = ap.parse_args()
    a.func(a)


if __name__ == "__main__":
    main()
