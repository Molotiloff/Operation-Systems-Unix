#!/bin/bash
set -euo pipefail

FILE="A"
SIZE=$((4 * 1024 * 1024 + 1))
LAST_OFFSET=$((SIZE - 1))

rm -f "$FILE"

dd if=/dev/zero of="$FILE" bs=1048576 count=4 status=none

dd if=/dev/zero of="$FILE" bs=1 count=1 seek=$LAST_OFFSET conv=notrunc status=none

printf '\x01' | dd of="$FILE" bs=1 seek=0 conv=notrunc status=none
printf '\x01' | dd of="$FILE" bs=1 seek=10000 conv=notrunc status=none
printf '\x01' | dd of="$FILE" bs=1 seek=$LAST_OFFSET conv=notrunc status=none

echo "Created dense file $FILE with size $SIZE bytes"