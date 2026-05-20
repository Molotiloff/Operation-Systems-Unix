CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2 -D_POSIX_C_SOURCE=200809L

TARGETS = brown_server brown_client test_client

all: $(TARGETS)

brown_server: brown_server.c
	$(CC) $(CFLAGS) -o brown_server brown_server.c

brown_client: brown_client.c
	$(CC) $(CFLAGS) -o brown_client brown_client.c

test_client: test_client.c
	$(CC) $(CFLAGS) -o test_client test_client.c

clean:
	rm -f $(TARGETS)
	rm -rf testenv
	rm -f /tmp/brown_server.log
	rm -f /tmp/brown_bot.sock
	rm -f result.txt
	rm -f stats_summary.txt

rebuild: clean all

.PHONY: all clean rebuild