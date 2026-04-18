CC = gcc
CFLAGS = -Wall -Wextra -O2 -I./include -g
LDFLAGS = -lbpf -lelf -lz -lpthread -lxdp

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)
TARGET = byPassRT

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)

.PHONY: all clean
