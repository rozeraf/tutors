
CC      = clang
CFLAGS  = -O2 -Wall -Wextra
TARGET  = zshtutor
SRC     = content.c ../core/tutor.c
HEADERS = ../core/tutor.h
PREFIX  = /usr/local

all: build

build: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) -std=c11 -I../core -o $(TARGET) $(SRC)

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all build install uninstall clean
