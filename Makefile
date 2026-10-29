CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g
TARGET = sws
SOURCES = src/main.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET)

.PHONY: all clean
