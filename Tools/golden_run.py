import filecmp, os, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLD = os.path.join(REPO, "Analysis", "oracle", "gs_golden")
out = os.path.abspath(sys.argv[1])
scenes = sys.argv[2].split(",") if len(sys.argv) > 2 else ["menu_language", "menu_hz", "loading", "logo", "cutscene"]
windows = {"menu_language": "250:5", "menu_hz": "320:4", "loading": "540:4", "logo": "900:5", "cutscene": "1360:5"}
last = {"menu_language": 256, "menu_hz": 325, "loading": 545, "logo": 906, "cutscene": 1366}
os.makedirs(out, exist_ok=True)
spec = ";".join("%s:%s" % (windows[s], os.path.join(out, s + ".gsr").replace("\\", "/")) for s in scenes)
env = dict(os.environ)
env["PS2X_PAD_SCRIPT"] = "290v:cross,375v:cross,770v:down,800v:cross"
env["PS2X_GS_RECORD"] = spec
env["PS2X_GS_RECORD_HASH"] = "1"
env["SOTC_TIMELINE"] = os.path.join(out, "timeline.txt").replace("\\", "/")
env["SOTC_TIMELINE_WATCH"] = "1dc7ac,1dc9d8"
target = max(last[s] for s in scenes)
t0 = time.perf_counter()
p = subprocess.Popen([os.path.join(REPO, "build", "port", "bin", "sotc.exe")], cwd=REPO, env=env,
                     stdout=open(os.path.join(out, "stdout.txt"), "w"), stderr=subprocess.STDOUT)
tl = os.path.join(out, "timeline.txt")
while p.poll() is None:
    time.sleep(1)
    try:
        lines = open(tl).read().split("\n")
        fields = [int(l.split()[0]) for l in lines if l and l[0].isdigit()]
        if fields and fields[-1] >= target:
            break
    except OSError:
        pass
    if time.perf_counter() - t0 > 900:
        break
time.sleep(2)
p.kill()
print("elapsed %.1f s" % (time.perf_counter() - t0))
for s in scenes:
    a = os.path.join(out, s + ".gsr")
    b = os.path.join(GOLD, s + ".gsr")
    same = os.path.exists(a) and filecmp.cmp(a, b, shallow=False)
    print(s, "IDENTICAL" if same else "DIFFERENT", os.path.getsize(a) if os.path.exists(a) else -1, os.path.getsize(b))
