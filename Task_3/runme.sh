#!/bin/bash
set -euo pipefail

RESULT="result.txt"
WORKDIR="/tmp/myinit_testenv_$$"
CONFIG="$WORKDIR/config.txt"
CONFIG_ONE="$WORKDIR/config_one.txt"
LOGFILE="/tmp/myinit.log"
PIDFILE="/tmp/myinit.pid"
BASE_DIR="$(cd "$(dirname "$0")" && pwd)"

rm -f "$RESULT" "$LOGFILE" "$PIDFILE"
rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"

make

# Три процесса с разными именами
ln -sf "$BASE_DIR/testproc" "$WORKDIR/proc1"
ln -sf "$BASE_DIR/testproc" "$WORKDIR/proc2"
ln -sf "$BASE_DIR/testproc" "$WORKDIR/proc3"

touch "$WORKDIR/in1" "$WORKDIR/in2" "$WORKDIR/in3"

cat > "$CONFIG" <<EOF
$WORKDIR/proc1 $WORKDIR/in1 $WORKDIR/out1
$WORKDIR/proc2 $WORKDIR/in2 $WORKDIR/out2
$WORKDIR/proc3 $WORKDIR/in3 $WORKDIR/out3
EOF

cat > "$CONFIG_ONE" <<EOF
$WORKDIR/proc1 $WORKDIR/in1 $WORKDIR/out1
EOF

cleanup() {
    if [ -f "$PIDFILE" ]; then
        DAEMON_PID=$(cat "$PIDFILE" 2>/dev/null || true)
        if [ -n "${DAEMON_PID:-}" ]; then
            kill -TERM "$DAEMON_PID" 2>/dev/null || true
            sleep 1
        fi
    fi
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

{
    echo "=== UNIX OS coursework: myinit test report ==="
    echo

    echo "Test 1: Start myinit with config of 3 processes"
    echo "Expected: daemon starts and launches exactly 3 child processes."
    ./myinit "$CONFIG"
    sleep 1

    if [ ! -f "$PIDFILE" ]; then
        echo "Actual: ERROR, pid file not found"
        exit 1
    fi

    DAEMON_PID=$(cat "$PIDFILE")
    echo "Actual: daemon pid = $DAEMON_PID"

    ps -ax -o pid=,ppid=,command= | awk -v ppid="$DAEMON_PID" '$2 == ppid {print}'
    CHILD_COUNT=$(ps -ax -o pid=,ppid=,command= | awk -v ppid="$DAEMON_PID" '$2 == ppid {count++} END {print count+0}')
    echo "Actual: child count = $CHILD_COUNT"
    echo

    echo "Test 2: Kill process number 2"
    echo "Expected: after one second myinit restarts it and total child count stays equal to 3."
    pkill -f "$WORKDIR/proc2"
    sleep 1

    ps -ax -o pid=,ppid=,command= | awk -v ppid="$DAEMON_PID" '$2 == ppid {print}'
    CHILD_COUNT_AFTER_RESTART=$(ps -ax -o pid=,ppid=,command= | awk -v ppid="$DAEMON_PID" '$2 == ppid {count++} END {print count+0}')
    echo "Actual: child count after restart = $CHILD_COUNT_AFTER_RESTART"
    echo

    echo "Test 3: Replace config with one process and send SIGHUP"
    echo "Expected: myinit reloads config, terminates old children and starts only 1 child."
    cp "$CONFIG_ONE" "$CONFIG"
    kill -HUP "$DAEMON_PID"
    sleep 2

    ps -ax -o pid=,ppid=,command= | awk -v ppid="$DAEMON_PID" '$2 == ppid {print}'
    CHILD_COUNT_AFTER_HUP=$(ps -ax -o pid=,ppid=,command= | awk -v ppid="$DAEMON_PID" '$2 == ppid {count++} END {print count+0}')
    echo "Actual: child count after SIGHUP = $CHILD_COUNT_AFTER_HUP"
    echo

    echo "Test 4: Check log"
    echo "Expected: log contains start of 3 processes, exit and restart of process 2, termination of 3 processes, start of 1 process."
    cat "$LOGFILE"
    echo

    echo "Summary checks:"
    echo "Expected: first count = 3, second count = 3, third count = 1."
    echo "Actual: first=$CHILD_COUNT second=$CHILD_COUNT_AFTER_RESTART third=$CHILD_COUNT_AFTER_HUP"
} > "$RESULT"

echo "Done. See $RESULT"