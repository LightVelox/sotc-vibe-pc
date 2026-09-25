import hashlib, struct, sys


def summary(path, upto):
    data = open(path, "rb").read()
    pos = 8
    checkpoints = []
    hashes = []
    ops = []
    while pos + 5 <= len(data):
        op = data[pos]
        size = struct.unpack_from("<I", data, pos + 1)[0]
        payload = data[pos + 5:pos + 5 + size]
        pos += 5 + size
        if op in (1, 10, 13):
            tick = struct.unpack_from("<Q", payload, 0)[0]
            if tick > upto:
                break
            vsize = struct.unpack_from("<I", payload, 64)[0]
            checkpoints.append((tick, hashlib.sha1(payload[:64] + payload[64:]).hexdigest()[:16]))
            if tick >= upto:
                break
        elif op == 14:
            hashes.append(payload)
        else:
            ops.append(op)
    return checkpoints, hashlib.sha1(b"".join(hashes)).hexdigest()[:16], len(hashes), hashlib.sha1(bytes(ops)).hexdigest()[:16]


a, b = sys.argv[1], sys.argv[2]
ca = summary(a, 1 << 60)
upto = min(ca[0][-1][0], summary(b, 1 << 60)[0][-1][0])
sa = summary(a, upto)
sb = summary(b, upto)
print("checkpoints", "SAME" if sa[0] == sb[0] else "DIFF", len(sa[0]), "hashes", "SAME" if sa[1:3] == sb[1:3] else "DIFF", sa[2], sb[2], "ops", "SAME" if sa[3] == sb[3] else "DIFF")
