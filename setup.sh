#!/usr/bin/env bash
# Linux counterpart of setup.bat: checks the system, verifies the disc image, builds the port and offers to start it.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

DISCSIZE=4635918336

fail() {
    echo
    echo "  $1"
    exit 1
}

ask() {
    local reply
    read -r -p "    $1 [y/N] " reply
    [[ "$reply" =~ ^[Yy] ]]
}

echo
echo " =================================================================="
echo "  Shadow of the Colossus - PC port setup (Linux)"
echo " =================================================================="
echo

echo "[1/6] Checking the recompiler sources..."
if [[ -f External/PS2Recomp/ps2xRecomp/CMakeLists.txt ]]; then
    echo "    Found."
else
    command -v git >/dev/null || fail "Git is needed to fetch the PS2Recomp sources. Install git, then run ./setup.sh again."
    echo "    Fetching PS2Recomp - one-time download..."
    git submodule update --init --recursive || fail "Fetching the PS2Recomp sources failed. Check the error above and your connection."
    echo "    Done."
fi

echo "[2/6] Checking Python..."
python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null \
    || fail "Python 3.10 or newer is required to generate the game code (python3 on PATH)."
echo "    Found $(python3 --version)."

echo "[3/6] Checking the C++ build tools..."
missing=()
for tool in cmake ninja c++ pkg-config; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
for module in gl x11 xrandr xinerama xcursor xi; do
    pkg-config --exists "$module" 2>/dev/null || missing+=("$module (development package)")
done
if (( ${#missing[@]} )); then
    echo "    Missing: ${missing[*]}"
    echo "    On Debian/Ubuntu:  sudo apt install build-essential cmake ninja-build pkg-config \\"
    echo "                         libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev"
    echo "    On Fedora:         sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config mesa-libGL-devel \\"
    echo "                         libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel"
    echo "    On Arch:           sudo pacman -S base-devel cmake ninja libglvnd libx11 libxrandr libxinerama libxcursor libxi"
    fail "Install the items above, then run ./setup.sh again."
fi
grep -qw avx2 /proc/cpuinfo || fail "This port needs a CPU with AVX2 (Intel Haswell / AMD Excavator or newer)."
echo "    Found $(cmake --version | head -1), ninja $(ninja --version), $(c++ --version | head -1)."

echo "[4/6] Checking the disc image..."
mkdir -p Game build
ISOPATH=""
wrong=0
shopt -s nullglob nocaseglob
for f in Game/*.iso; do
    size=$(stat -c %s "$f")
    if [[ "$size" == "$DISCSIZE" ]]; then
        ISOPATH="$f"
        break
    fi
    echo "    $f: wrong size, $size bytes - skipping."
    wrong=1
done
shopt -u nullglob nocaseglob
if [[ -z "$ISOPATH" ]]; then
    echo
    (( wrong )) && echo "    The Game folder does not contain the disc image this port needs." \
                || echo "    No disc image was found in the Game folder."
    echo "    It runs exactly one build of the game:"
    echo
    echo "        Shadow of the Colossus (Europe, Australia)"
    echo "        SCES-53326, version 1.00 (PAL)"
    echo "        4,635,918,336 bytes, about 4.3 GB"
    echo
    echo "    Other regions (including the NTSC US and Japanese discs), later revisions"
    echo "    and trimmed or patched dumps are rejected: the recompiled code matches this one disc."
    fail "Copy your disc image into Game/ as a .iso file, then run ./setup.sh again."
fi
echo "    Accepted \"$ISOPATH\" - size matches SCES-53326 v1.00. The boot ELF and"
echo "    modules inside the image are verified when the game code is generated."

echo "[5/6] Building the code recompiler - a few minutes the first time..."
./configure.sh >/dev/null || fail "Configuring failed. Scroll up for the actual error."
./build.sh ps2_recomp || fail "Building ps2_recomp failed. Scroll up for the actual error."
./build.sh vu1_recomp || fail "Building vu1_recomp failed. Scroll up for the actual error."

echo "[6/6] Building the port..."
stamp() {
    printf '%s\n' "$(basename "$ISOPATH")" "$(stat -c %s "$ISOPATH")" \
        "$(stat -c %Y Port/recomp/sotc.toml Port/profile/SCES-53326_v1.00.json Tools/sotc_link.py)"
}
if [[ -f build/generated_state.txt && -f Port/generated/register_functions.cpp && -f Port/generated/vu1/vu1_programs.cpp ]] \
    && [[ "$(cat build/generated_state.txt)" == "$(stamp)" ]]; then
    echo "    Generated game code is already current - skipping generation."
else
    echo "    Generating game code from the disc image - thousands of C++ files, this takes a few minutes..."
    python3 Tools/generate.py --iso "$ISOPATH" || fail "Generating the game code failed. Scroll up for the actual error."
    stamp > build/generated_state.txt
    echo "    Re-configuring with the generated game code..."
    ./configure.sh >/dev/null || fail "Configuring failed. Scroll up for the actual error."
fi
echo "    Compiling the port now - up to an hour on the first build, only a few seconds afterwards."
./build.sh || fail "Compiling the port failed. Scroll up for the actual error. If the compiler was killed, rerun with fewer jobs: SOTC_JOBS=2 ./setup.sh"

echo
echo " =================================================================="
echo "  Setup complete."
echo " =================================================================="
echo
echo "  The game is ready at build/port/bin/sotc."
echo "  From now on, start it with ./run.sh."
echo
if ask "Start the game now?"; then
    exec ./run.sh
fi
