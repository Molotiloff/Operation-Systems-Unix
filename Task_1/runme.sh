#!/bin/bash
set -euo pipefail

RESULT="result.txt"

rm -f A B C D A.gz B.gz "$RESULT"

make

./create_test_a.sh

stat_line() {
    local f="$1"

    if stat --version >/dev/null 2>&1; then
        # GNU stat (Linux)
        stat --printf="%n: size=%s bytes, allocated_blocks=%b, block_size=%B\n" "$f"
    else
        # BSD stat (macOS)
        stat -f "%N: size=%z bytes, allocated_blocks=%b, block_size=%k" "$f"
    fi
}

gzip_keep() {
    local f="$1"

    rm -f "${f}.gz"
    gzip -c "$f" > "${f}.gz"
}

{
    echo "=== UNIX OS homework: sparse file test report ==="
    echo

    echo "Test 1: Create file A"
    echo "Expected: file A exists, size = 4*1024*1024 + 1 = 4194305 bytes; bytes 0, 10000 and last are 0x01."
    stat_line A
    echo

    echo "Test 2: Copy A -> B using ./myprogram A B"
    echo "Expected: B content must be identical to A; B should usually occupy fewer disk blocks than A if holes are created."
    ./myprogram A B
    if cmp -s A B; then
        echo "Actual: cmp(A, B) = identical"
    else
        echo "Actual: cmp(A, B) = DIFFERENT"
    fi
    stat_line B
    echo

    echo "Test 3: Compress A and B with gzip"
    echo "Expected: A.gz and B.gz must be created."
    gzip_keep A
    gzip_keep B
    stat_line A.gz
    stat_line B.gz
    echo

    echo "Test 4: Restore B.gz to stdout and save through program into C"
    echo "Command: gzip -cd B.gz | ./myprogram C"
    echo "Expected: C content must be identical to A."
    gzip -cd B.gz | ./myprogram C
    if cmp -s A C; then
        echo "Actual: cmp(A, C) = identical"
    else
        echo "Actual: cmp(A, C) = DIFFERENT"
    fi
    stat_line C
    echo

    echo "Test 5: Copy A -> D using block size 100"
    echo "Command: ./myprogram -b 100 A D"
    echo "Expected: D content must be identical to A."
    ./myprogram -b 100 A D
    if cmp -s A D; then
        echo "Actual: cmp(A, D) = identical"
    else
        echo "Actual: cmp(A, D) = DIFFERENT"
    fi
    stat_line D
    echo

    echo "Test 6: Summary stat for all required files"
    echo "Expected: logical sizes are preserved; sparse files may have fewer allocated blocks."
    for f in A A.gz B B.gz C D; do
        stat_line "$f"
    done
    echo

    echo "Test 7: Hex checks at important offsets"
    echo "Expected: byte at offsets 0, 10000, 4194304 equals 01 for A/B/C/D."
    for f in A B C D; do
        b0=$(od -An -tx1 -j 0 -N 1 "$f" | tr -d ' \n')
        b1=$(od -An -tx1 -j 10000 -N 1 "$f" | tr -d ' \n')
        b2=$(od -An -tx1 -j 4194304 -N 1 "$f" | tr -d ' \n')
        echo "Actual: $f -> offset0=$b0 offset10000=$b1 offset4194304=$b2"
    done
} > "$RESULT"

echo "Done. See $RESULT"