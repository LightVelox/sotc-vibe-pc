#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET="${1:-sotc}"
# Each compiler process on the generated game code can take 1-2 GB, so the job count is limited by memory.
if [[ -z "${SOTC_JOBS:-}" ]]; then
    MEM_GB=$(awk '/^MemTotal:/ {print int($2 / 1048576)}' /proc/meminfo)
    SOTC_JOBS=$(( MEM_GB / 2 ))
    CPUS=$(nproc)
    (( SOTC_JOBS > CPUS )) && SOTC_JOBS=$CPUS
    (( SOTC_JOBS < 1 )) && SOTC_JOBS=1
fi
exec cmake --build "$HERE/build/port" --target "$TARGET" -j "$SOTC_JOBS"
