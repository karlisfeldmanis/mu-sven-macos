#!/bin/bash
# MU Remaster launcher — ensures correct working directory and data symlinks
set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
CLIENT_BUILD="$PROJECT_ROOT/client/build"
SERVER_BUILD="$PROJECT_ROOT/server/build"
CLIENT_DATA="$PROJECT_ROOT/client/Data"

# Ensure build directories exist
mkdir -p "$CLIENT_BUILD" "$SERVER_BUILD"

# Ensure Data symlink in client/build -> client/Data
if [ ! -L "$CLIENT_BUILD/Data" ] && [ ! -d "$CLIENT_BUILD/Data" ]; then
    ln -s "$CLIENT_DATA" "$CLIENT_BUILD/Data"
    echo "[run.sh] Created symlink: client/build/Data -> client/Data"
fi

# Ensure Data symlink in server/build -> client/Data
if [ ! -L "$SERVER_BUILD/Data" ] && [ ! -d "$SERVER_BUILD/Data" ]; then
    ln -s "$CLIENT_DATA" "$SERVER_BUILD/Data"
    echo "[run.sh] Created symlink: server/build/Data -> client/Data"
fi

usage() {
    echo "Usage: $0 [client|server|both]"
    echo "  client  - Launch game client"
    echo "  server  - Launch game server"
    echo "  both    - Launch server, then client"
    exit 1
}

MODE="${1:-both}"

case "$MODE" in
    client)
        echo "[run.sh] Launching client..."
        cd "$CLIENT_BUILD" && ./MuRemaster
        ;;
    server)
        echo "[run.sh] Launching server..."
        cd "$SERVER_BUILD" && ./MuServer
        ;;
    both)
        echo "[run.sh] Launching server..."
        cd "$SERVER_BUILD" && ./MuServer &
        SERVER_PID=$!
        sleep 1
        echo "[run.sh] Launching client..."
        cd "$CLIENT_BUILD" && ./MuRemaster
        # When client exits, stop server
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
        ;;
    *)
        usage
        ;;
esac
