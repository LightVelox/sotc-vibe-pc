import argparse
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROFILE = os.path.join(ROOT, "Port", "profile", "SCES-53326_v1.00.json")
LINK_OUT = os.path.join(ROOT, "build", "link")
GENERATED = os.path.join(ROOT, "Port", "generated")
RECOMP_DIR = os.path.join(ROOT, "Port", "recomp")


def find_iso(explicit):
    if explicit:
        return explicit
    env = os.environ.get("SOTC_ISO")
    if env:
        return env
    for path in glob.glob(os.path.join(ROOT, "Game", "*.iso")):
        if os.path.getsize(path) == 4635918336:
            return path
    sys.exit("No disc image found. Put your own SCES-53326 image in Game/ or pass --iso <path>.")


def find_recompiler():
    candidates = [
        os.path.join(ROOT, "build", "port", "External", "PS2Recomp", "ps2xRecomp", "ps2_recomp.exe"),
        os.path.join(ROOT, "build", "ps2recomp-tools", "ps2xRecomp", "ps2_recomp.exe"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    sys.exit("ps2_recomp.exe not found. Run configure.bat, then: build.bat ps2_recomp")


def run(cmd, cwd=ROOT):
    print("+", " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    r = subprocess.run(cmd, cwd=cwd)
    if r.returncode:
        sys.exit(r.returncode)


def sync_tree(src, dst):
    changed = removed = 0
    wanted = set(os.listdir(src))
    for name in wanted:
        a = os.path.join(src, name)
        b = os.path.join(dst, name)
        new = open(a, "rb").read()
        if os.path.exists(b) and open(b, "rb").read() == new:
            continue
        open(b, "wb").write(new)
        changed += 1
    for name in os.listdir(dst):
        if name not in wanted and os.path.isfile(os.path.join(dst, name)):
            os.remove(os.path.join(dst, name))
            removed += 1
    print(f"generated sources: {len(wanted)} total, {changed} written, {removed} removed")


def main():
    ap = argparse.ArgumentParser(description="Regenerate the recompiled game code from your own disc image")
    ap.add_argument("--iso")
    ap.add_argument("--verify-ram", help="optional PCSX2 RAM dump to verify module relocation against")
    args = ap.parse_args()

    iso = find_iso(args.iso)
    link_cmd = [sys.executable, os.path.join(ROOT, "Tools", "sotc_link.py"), "--profile", PROFILE, "--iso", iso,
                "--out", LINK_OUT, "--emit-header", os.path.join(GENERATED, "sotc_layout_generated.h")]
    if args.verify_ram:
        link_cmd += ["--verify-ram", args.verify_ram]
    staging = os.path.join(ROOT, "build", "generated_staging")
    if os.path.isdir(staging):
        shutil.rmtree(staging)
    os.makedirs(staging)
    os.makedirs(GENERATED, exist_ok=True)
    link_cmd[-1] = os.path.join(staging, "sotc_layout_generated.h")
    run(link_cmd)
    toml = open(os.path.join(RECOMP_DIR, "sotc.toml")).read()
    toml = toml.replace('output = "../generated"', 'output = "' + staging.replace("\\", "/") + '"')
    staged_toml = os.path.join(RECOMP_DIR, "sotc.staging.toml")
    open(staged_toml, "w").write(toml)
    try:
        run([find_recompiler(), os.path.basename(staged_toml)], cwd=RECOMP_DIR)
    finally:
        os.remove(staged_toml)
    sync_tree(staging, GENERATED)
    run([sys.executable, os.path.join(ROOT, "Tools", "verify_reloc_sites.py"),
         os.path.join(LINK_OUT, "sotc_runtime_relocs.csv"), GENERATED, os.path.join(LINK_OUT, "sotc_functions.csv")])
    shutil.copyfile(os.path.join(LINK_OUT, "sotc_address_map.csv"), os.path.join(ROOT, "Analysis", "ADDRESS_MAP.generated.csv"))
    print("Generated code is up to date. Next: build.bat")


if __name__ == "__main__":
    main()
