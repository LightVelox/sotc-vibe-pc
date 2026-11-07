#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec cmake -S "$HERE" -B "$HERE/build/port" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@"
