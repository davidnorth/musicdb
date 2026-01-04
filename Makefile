CC = clang
CFLAGS = -Wall -Wextra -std=c11 -g
LDFLAGS = $(shell pkg-config --libs libavformat libavcodec libavutil)
INCLUDES = $(shell pkg-config --cflags libavformat libavcodec libavutil)

TARGET = musicdb_gen

all: $(TARGET)

$(TARGET): generator.c musicdb_format.h
	$(CC) $(CFLAGS) $(INCLUDES) generator.c -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
