CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2 -D_POSIX_C_SOURCE=200809L

TARGETS = myinit testproc

all: $(TARGETS)

myinit: myinit.c
	$(CC) $(CFLAGS) -o myinit myinit.c

testproc: testproc.c
	$(CC) $(CFLAGS) -o testproc testproc.c

clean:
	rm -f $(TARGETS)
	rm -rf testenv
	rm -f result.txt
	rm -f /tmp/myinit.log /tmp/myinit.pid

rebuild: clean all

.PHONY: all clean rebuild