CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags libvirt)
LDFLAGS = $(shell pkg-config --libs libvirt)
TARGET = vmwatch

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c $(LDFLAGS)

clean:
	rm -f $(TARGET)

install:
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install
