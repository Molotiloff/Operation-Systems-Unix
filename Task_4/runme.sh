#!/bin/bash
set -euo pipefail

MODE="${1:-debug}"

RESULT="result.txt"
SUMMARY="stats_summary.txt"
WORKDIR="$(pwd)/testenv"
CONFIG="$(pwd)/config"
SOCKET_PATH="/tmp/brown_bot.sock"
SERVER_LOG="/tmp/brown_server.log"
SERVER_PID=""

if [ "$MODE" = "full" ]; then
    echo "[FULL MODE]"
    CLIENTS=100
    NUMBERS=1000
    CLIENT_SERIES="1 10 25 50 75 100"
    DELAY_SERIES="0 0.2 0.4 0.6 0.8 1.0"
else
    echo "[DEBUG MODE]"
    CLIENTS=10
    NUMBERS=100
    CLIENT_SERIES="1 5 10"
    DELAY_SERIES="0 0.1 0.2"
fi

mkdir -p "$WORKDIR"
rm -f "$RESULT" "$SUMMARY" "$SERVER_LOG" "$SOCKET_PATH"

make

cat > "$CONFIG" <<EOF
$SOCKET_PATH
EOF

generate_numbers() {
    local outfile="$1"
    : > "$outfile"
    for i in $(seq 1 $((NUMBERS / 2))); do
        echo "$i" >> "$outfile"
        echo "-$i" >> "$outfile"
    done
}

start_server() {
    echo "Starting server..."
    ./brown_server "$CONFIG" &
    SERVER_PID=$!
    sleep 1

    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "ERROR: server failed to start"
        exit 1
    fi
}

stop_server() {
    if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$SOCKET_PATH"
}

wait_for_pids() {
    local pid
    for pid in "$@"; do
        wait "$pid" 2>/dev/null || true
    done
}

run_mass_test() {
    local delay="$1"
    local round="$2"
    local log_dir="$WORKDIR/delay_logs_${round}"
    local pids=()

    echo "Running mass test: round=$round clients=$CLIENTS delay=$delay" >&2

    rm -rf "$log_dir"
    mkdir -p "$log_dir"
    : > "$SERVER_LOG"

    for i in $(seq 1 "$CLIENTS"); do
        ./test_client "$CONFIG" "$WORKDIR/numbers.txt" "$delay" \
            "$log_dir/client_${i}.log" "$i" &
        pids+=("$!")
    done

    wait_for_pids "${pids[@]}"

    local final_state
    final_state=$(printf "0\n" | ./brown_client "$CONFIG" | tail -n 1 | tr -d '\r\n')
    echo "$final_state"
}

run_experiment() {
    local clients="$1"
    local delay="$2"
    local tag="c${clients}_d${delay//./_}"
    local log_dir="$WORKDIR/exp_${tag}"
    local pids=()

    echo "Running experiment: clients=$clients delay=$delay" >&2

    rm -rf "$log_dir"
    mkdir -p "$log_dir"
    : > "$SERVER_LOG"

    for i in $(seq 1 "$clients"); do
        ./test_client "$CONFIG" "$WORKDIR/numbers.txt" "$delay" \
            "$log_dir/client_${i}.log" "$i" &
        pids+=("$!")
    done

    wait_for_pids "${pids[@]}"

    {
        echo "experiment clients=$clients delay=$delay"
        python3 analyze.py "$SERVER_LOG" "$log_dir/client_*.log"
        echo
    } >> "$SUMMARY"
}

generate_numbers "$WORKDIR/numbers.txt"

trap 'stop_server' EXIT
start_server

{
    echo "=== UNIX OS coursework: brown bot test report ==="
    echo "Mode: $MODE"
    echo

    echo "Test 1: parallel clients"
    echo "Expected: final state = 0"
    FINAL_STATE=$(run_mass_test "0.05" "round1")
    echo "Actual: final state after round 1 = $FINAL_STATE"
    echo

    echo "Test 2: repeat without restart"
    echo "Expected: still works, final state = 0"
    FINAL_STATE2=$(run_mass_test "0.05" "round2")
    echo "Actual: final state after round 2 = $FINAL_STATE2"
    echo

    echo "Test 3: Performance experiments"
    echo "Expected: effective time ≈ slowest client"
    echo "Results saved to $SUMMARY"
    echo
} > "$RESULT"

: > "$SUMMARY"

for c in $CLIENT_SERIES; do
    for d in $DELAY_SERIES; do
        run_experiment "$c" "$d"
    done
done

{
    echo "Performance summary:"
    cat "$SUMMARY"
    echo
} >> "$RESULT"

echo "Done. See $RESULT and $SUMMARY"