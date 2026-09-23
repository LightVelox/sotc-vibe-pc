import re
import sys

n = int(sys.argv[2]) if len(sys.argv) > 2 else 5
data = open(sys.argv[1], "rb").read()
for m in re.finditer(rb"[\x20-\x7e\t]{%d,}" % n, data):
    print(f"{m.start():08x} {m.group().decode('ascii')}")
