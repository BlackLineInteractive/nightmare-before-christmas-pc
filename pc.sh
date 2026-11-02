#!/bin/sh
# Native PC port of Oogie's Revenge: build and run.
#
#   ./pc.sh build [game.iso]   extract the disc (first time), build rom/extracted/default_pc.exe
#   ./pc.sh run                run it (Windows: from Git Bash / MSYS; macOS / Linux: through Wine)
#   ./pc.sh clean              remove build output, screenshots and traces
#
# Needs: python3, clang with the x86 backend (Apple clang works), Wine on macOS / Linux.
# CLANG=... and WINE=... override the tools found on PATH.
set -e
cd "$(dirname "$0")"
GAME=rom/extracted

find_wine() {
    [ -n "$WINE" ] && { echo "$WINE"; return; }
    command -v wine >/dev/null 2>&1 && { echo wine; return; }
    for app in "/Applications/Wine Devel.app" "/Applications/Wine Stable.app" "/Applications/Wine Staging.app" \
               "$HOME/Applications/Wine Devel.app" "$HOME/Applications/Wine Stable.app" \
               "/private/tmp/wine/Wine Devel.app" "/private/tmp/wine/Wine Stable.app"; do
        [ -x "$app/Contents/Resources/wine/bin/wine" ] && { echo "$app/Contents/Resources/wine/bin/wine"; return; }
    done
    echo "wine not found: install Wine (macOS: brew install --cask wine@devel) or set WINE=/path/to/wine" >&2
    exit 1
}

case "$1" in
build)
    if [ ! -f "$GAME/default.xbe" ]; then
        iso="${2:-$(ls rom/*.iso 2>/dev/null | head -1)}"
        [ -f "$iso" ] || { echo "put your game disc image in rom/ (or pass it: ./pc.sh build game.iso)" >&2; exit 1; }
        python3 tools/extract_xbox_iso.py "$iso" "$GAME"
    fi
    python3 tools/build_flat_pe.py
    ;;
run)
    [ -f "$GAME/default_pc.exe" ] || { echo "build first: ./pc.sh build" >&2; exit 1; }
    cd "$GAME"
    case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ./default_pc.exe ;;
    *) wine=$(find_wine) || exit 1; "$wine" default_pc.exe ;;
    esac
    ;;
clean)
    rm -rf build/pc
    rm -f "$GAME"/default_pc.exe "$GAME"/shot_*.bmp "$GAME"/tr__*.bmp "$GAME"/tex_*.bin "$GAME"/tex_*.bmp "$GAME"/port.log
    ;;
*)
    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
