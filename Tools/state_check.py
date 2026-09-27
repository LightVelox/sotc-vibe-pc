import filecmp, os, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.abspath(sys.argv[1])
save_field = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
load_field = int(sys.argv[3]) if len(sys.argv) > 3 else 300
window = sys.argv[4] if len(sys.argv) > 4 else "1360:5"
os.makedirs(out, exist_ok=True)
state = os.path.join(out, "check.state").replace("\\", "/")
start, count = (int(v) for v in window.split(":"))
target = start + count + 1


def run(name, extra):
    env = dict(os.environ)
    env["PS2X_PAD_SCRIPT"] = "290v:cross,375v:cross,770v:down,800v:cross"
    if "--prod" not in sys.argv:
        env["PS2X_GS_GPU"] = "0"
        env["PS2X_MTVU"] = "0"
    env["PS2X_GS_RECORD"] = "%s:%s" % (window, os.path.join(out, name + ".gsr").replace("\\", "/"))
    env["PS2X_GS_RECORD_HASH"] = "1"
    timeline = os.path.join(out, name + "_timeline.txt")
    if os.path.exists(timeline):
        os.remove(timeline)
    env["SOTC_TIMELINE"] = timeline.replace("\\", "/")
    env.update(extra)
    t0 = time.perf_counter()
    p = subprocess.Popen([os.path.join(REPO, "build", "port", "bin", "sotc.exe")], cwd=REPO, env=env,
                         stdout=open(os.path.join(out, name + "_stdout.txt"), "w"), stderr=subprocess.STDOUT)
    while p.poll() is None:
        time.sleep(1)
        try:
            lines = open(timeline).read().split("\n")
            fields = [int(l.split()[0]) for l in lines if l and l[0].isdigit()]
            if fields and fields[-1] >= target:
                break
        except OSError:
            pass
        if time.perf_counter() - t0 > 900:
            break
    time.sleep(2)
    p.kill()
    print("%s elapsed %.1f s" % (name, time.perf_counter() - t0))


if "--skip-save" not in sys.argv:
    if os.path.exists(state):
        os.remove(state)
    run("saved", {"PS2X_STATE_SAVE_AT": "%d:%s" % (save_field, state)})
    print("state", os.path.getsize(state) if os.path.exists(state) else -1)
if "--same-process" in sys.argv:
    again = os.path.join(out, "again.state").replace("\\", "/")
    run("loaded", {"PS2X_STATE_SAVE_AT": "%d:%s" % (save_field, again), "PS2X_STATE_LOAD_AT": "%d:%s" % (load_field, again)})
else:
    run("loaded", {"PS2X_STATE_LOAD_AT": "%d:%s" % (load_field, state)})
a = os.path.join(out, "saved.gsr")
b = os.path.join(out, "loaded.gsr")
same = os.path.exists(a) and os.path.exists(b) and filecmp.cmp(a, b, shallow=False)
print("IDENTICAL" if same else "DIFFERENT", os.path.getsize(a) if os.path.exists(a) else -1, os.path.getsize(b) if os.path.exists(b) else -1)
