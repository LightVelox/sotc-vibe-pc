import sys
d = sys.argv[1]
origin = None
for l in open(d + "/events.txt"):
    p = l.split()
    if p[1] == "perf_counter_origin":
        origin = float(p[2])
tl = {}
for l in open(d + "/timeline.txt"):
    p = l.split()
    if p[0].isdigit():
        tl[int(p[0])] = float(p[1]) - origin
marks = [int(x) for x in sys.argv[2].split(",")]
for a, b in zip(marks, marks[1:]):
    if a in tl and b in tl:
        print("fields %5d-%5d  %6.2f s -> %6.2f s  %5.1f fields/s" % (a, b, tl[a], tl[b], (b - a) / (tl[b] - tl[a])))
