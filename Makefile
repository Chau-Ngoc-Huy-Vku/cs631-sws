CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g
TARGET = sws
SOURCES = src/main.c src/http.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -Iinclude -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET)

.PHONY: all clean
