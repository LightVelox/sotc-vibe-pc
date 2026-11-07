#!/usr/bin/env bash
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -x "$HERE/build/port/bin/sotc" ]]; then
    echo "The game has not been built yet. Run ./setup.sh first: it verifies"
    echo "your disc image, builds everything and offers to start the game."
    exit 1
fi
exec "$HERE/build/port/bin/sotc" "$@"
