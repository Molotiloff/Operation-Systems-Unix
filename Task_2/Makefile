CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2 -D_POSIX_C_SOURCE=200809L

TARGET = locker

all: $(TARGET)

$(TARGET): locker.c
	$(CC) $(CFLAGS) -o $(TARGET) locker.c

clean:
	rm -f $(TARGET) stats.txt *.lck

rebuild: clean all

.PHONY: all clean rebuild