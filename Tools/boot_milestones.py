import bisect, os, sys
import numpy as np
from PIL import Image

d = sys.argv[1]
mode = sys.argv[2]


def load_events():
    ev = []
    for line in open(os.path.join(d, "events.txt")):
        p = line.split()
        ev.append((float(p[0]), p[1:]))
    return ev


events = load_events()
origin = float([e for t, e in events if e[0] == "perf_counter_origin"][0][1])

field_t = []
field_n = []
counter_t = []
counter_v = []
if mode == "native":
    for line in open(os.path.join(d, "timeline.txt")):
        p = line.split()
        if p[0] in ("start", "scan", "scanhalf"):
            continue
        field_n.append(int(p[0]))
        field_t.append(float(p[1]) - origin)
        counter_t.append(float(p[1]) - origin)
        counter_v.append((int(p[2], 16), int(p[3], 16)))
else:
    for t, e in events:
        if e[0] == "counter":
            counter_t.append(t)
            counter_v.append((int(e[1]), int(e[2])))


def at(ts, vs, t):
    i = bisect.bisect_right(ts, t) - 1
    return vs[i] if i >= 0 else None


def classify(a):
    h, w, _ = a.shape
    if mode == "pcsx2":
        a = a[int(h * 0.12):int(h * 0.93), int(w * 0.10):int(w * 0.90)]
    else:
        a = a[:, int(w * 0.10):int(w * 0.90)]
    r, g, b = [a[:, :, i].astype(np.float32) for i in range(3)]
    m = (r + g + b) / 3
    mean = m.mean()
    if g.mean() > 60 and g.mean() > r.mean() * 2:
        hh, ww = m.shape
        left = (m[:, :int(ww * 0.4)] > 150).sum()
        mid = (m[:, int(ww * 0.4):int(ww * 0.6)] > 150).sum()
        return "lang_menu" if mid > left else "hz_menu", mean
    if mean > 90:
        return "logo", mean
    bright = (m > 180).sum()
    if mean < 12 and bright > 20:
        return "text", mean
    if mean < 3.5:
        return "black", mean
    return "scene", mean


rows = []
for f in sorted(os.listdir(os.path.join(d, "frames"))):
    t = float(f.split("_")[1][:-4])
    a = np.asarray(Image.open(os.path.join(d, "frames", f)).convert("RGB"))
    c, mean = classify(a)
    rows.append((t, c, mean))

prev = None
for t, c, mean in rows:
    if c != prev:
        fld = at(field_t, field_n, t) if field_t else None
        cnt = at(counter_t, counter_v, t)
        print("%8.3f  %-10s mean=%6.2f field=%s counter=%s" % (t, c, mean, fld, cnt))
        prev = c
for t, e in events:
    if e[0] == "press":
        print("%8.3f  press %s counter=%s" % (t, e[1], e[3]))
