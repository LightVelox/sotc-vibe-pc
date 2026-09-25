import glob, os, re, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(REPO, "Port", "generated")

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build", "vu0_corpus.inc")
comment = re.compile(r"^    // 0x([0-9a-f]+): 0x([0-9a-f]{8})\s+(\S+)(.*)$")
found = {}
for path in sorted(glob.glob(os.path.join(GEN, "*.cpp"))):
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    for i, line in enumerate(lines):
        m = comment.match(line)
        if not m:
            continue
        raw = int(m.group(2), 16)
        if (raw >> 24) not in (0x4A, 0x4B) or raw in found:
            continue
        j = i + 1
        if j < len(lines) and lines[j].strip().startswith("ctx->pc ="):
            j += 1
        body = []
        while j < len(lines):
            l = lines[j]
            if l.startswith("    // 0x") or l.startswith("label_") or l.strip().startswith("ctx->pc ="):
                break
            body.append(l)
            if len(body) == 1 and l.startswith("    {") and l.rstrip().endswith("}"):
                break
            j += 1
        text = "\n".join(body).strip()
        if not text or "dispatchGuestBranch" in text or "goto" in text or "return;" in text:
            continue
        found[raw] = (m.group(3) + m.group(4).split("#")[0].rstrip(), text)

os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
with open(out, "w", encoding="utf-8") as f:
    entries = sorted(found.items())
    for idx, (raw, (dis, text)) in enumerate(entries):
        f.write("static void op_%d(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)\n{\n    (void)rdram; (void)runtime;\n%s\n}\n" % (idx, text))
    f.write("static const Vu0CorpusEntry kCorpus[] = {\n")
    for idx, (raw, (dis, text)) in enumerate(entries):
        f.write("    {0x%08Xu, \"%s\", op_%d},\n" % (raw, dis.replace("\\", "").replace("\"", "'"), idx))
    f.write("};\n")
print("%d ops -> %s" % (len(found), out))
