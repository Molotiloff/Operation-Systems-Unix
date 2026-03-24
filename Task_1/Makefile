CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2
TARGET = myprogram
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2 -D_POSIX_C_SOURCE=200809L

all: $(TARGET)

$(TARGET): myprogram.c
	$(CC) $(CFLAGS) -o $(TARGET) myprogram.c

clean:
	rm -f $(TARGET) A B C D A.gz B.gz result.txt

rebuild: clean all

.PHONY: all clean rebuild