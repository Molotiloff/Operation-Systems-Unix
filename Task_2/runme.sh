#!/bin/bash
set -euo pipefail

rm -f stats.txt myfile.lck

make

echo "Starting 10 processes..."

for i in {1..10}; do
    ./locker myfile &
done

PIDS=$(jobs -p)

echo "Running for 5 minutes..."
sleep 300

echo "Stopping processes..."
kill -INT $PIDS

wait

echo
echo "=== RESULTS ==="
cat stats.txt