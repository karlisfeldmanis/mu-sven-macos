#!/bin/bash
# MU Online Remaster — robust launch script
# Starts server, waits for it to be ready, then starts client

set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
SERVER_DIR="$ROOT/server/build"
CLIENT_DIR="$ROOT/client/build"
SERVER_BIN="$SERVER_DIR/MuServer"
CLIENT_BIN="$CLIENT_DIR/MuRemaster"
PORT=44405

# ── Colors ──
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# ── Kill old instances ──
echo -e "${YELLOW}[Launch] Stopping old processes...${NC}"
pkill -f MuServer 2>/dev/null || true
pkill -f MuRemaster 2>/dev/null || true
sleep 0.5

# ── Verify symlinks ──
echo -e "${YELLOW}[Launch] Checking Data symlinks...${NC}"
if [ ! -L "$CLIENT_DIR/Data" ]; then
    echo -e "${YELLOW}[Launch] Creating client Data symlink...${NC}"
    ln -sfn "$ROOT/client/Data" "$CLIENT_DIR/Data"
fi
if [ ! -L "$SERVER_DIR/Data" ]; then
    echo -e "${YELLOW}[Launch] Creating server Data symlink...${NC}"
    ln -sfn "$ROOT/client/Data" "$SERVER_DIR/Data"
fi

# ── Verify binaries exist ──
if [ ! -f "$SERVER_BIN" ]; then
    echo -e "${RED}[Launch] Server binary not found at $SERVER_BIN${NC}"
    echo -e "${YELLOW}[Launch] Building server...${NC}"
    cd "$SERVER_DIR" && cmake .. && ninja
fi
if [ ! -f "$CLIENT_BIN" ]; then
    echo -e "${RED}[Launch] Client binary not found at $CLIENT_BIN${NC}"
    echo -e "${YELLOW}[Launch] Building client...${NC}"
    cd "$CLIENT_DIR" && cmake -DCMAKE_BUILD_TYPE=Release .. && ninja
fi

# ── Verify terrain data ──
if [ ! -f "$CLIENT_DIR/Data/World1/EncTerrain1.att" ]; then
    echo -e "${RED}[Launch] FATAL: Data/World1/EncTerrain1.att not found!${NC}"
    echo -e "${RED}         Make sure client/Data/ contains the game assets.${NC}"
    exit 1
fi

# ── Start server (from its build directory so relative paths work) ──
echo -e "${YELLOW}[Launch] Starting server...${NC}"
cd "$SERVER_DIR"
./MuServer > "$ROOT/server/server_log.txt" 2>&1 &
SERVER_PID=$!
echo -e "${GREEN}[Launch] Server PID: $SERVER_PID${NC}"

# ── Wait for server to listen on port ──
echo -e "${YELLOW}[Launch] Waiting for server to listen on port $PORT...${NC}"
MAX_WAIT=10
for i in $(seq 1 $MAX_WAIT); do
    if lsof -i :$PORT -sTCP:LISTEN >/dev/null 2>&1; then
        echo -e "${GREEN}[Launch] Server is ready! (took ${i}s)${NC}"
        break
    fi
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}[Launch] Server crashed! Check server/server_log.txt${NC}"
        tail -20 "$ROOT/server/server_log.txt"
        exit 1
    fi
    sleep 1
done

if ! lsof -i :$PORT -sTCP:LISTEN >/dev/null 2>&1; then
    echo -e "${RED}[Launch] Server failed to start within ${MAX_WAIT}s${NC}"
    kill $SERVER_PID 2>/dev/null
    exit 1
fi

# ── Start client (from its build directory) ──
echo -e "${YELLOW}[Launch] Starting client...${NC}"
cd "$CLIENT_DIR"
MU_LAUNCHED=1 ./MuRemaster > "$ROOT/client/client_log.txt" 2>&1 &
CLIENT_PID=$!
echo -e "${GREEN}[Launch] Client PID: $CLIENT_PID${NC}"
echo -e "${GREEN}[Launch] All systems go!${NC}"
echo -e "${GREEN}  Server: PID $SERVER_PID (log: server/server_log.txt)${NC}"
echo -e "${GREEN}  Client: PID $CLIENT_PID (log: client/client_log.txt)${NC}"
echo ""
echo -e "${YELLOW}Press Ctrl+C to stop both processes${NC}"

# ── Wait and cleanup on exit ──
trap "echo -e '${YELLOW}\n[Launch] Shutting down...${NC}'; kill $SERVER_PID $CLIENT_PID 2>/dev/null; exit 0" INT TERM

# Wait for client to exit, then stop server
wait $CLIENT_PID 2>/dev/null
echo -e "${YELLOW}[Launch] Client exited, stopping server...${NC}"
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
echo -e "${GREEN}[Launch] Done.${NC}"
