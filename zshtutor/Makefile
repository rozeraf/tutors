
CC      = clang
CFLAGS  = -O2 -Wall -Wextra
TARGET  = zshtutor
SRC     = zshtutor.c
PREFIX  = /usr/local

all: build

build: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all build install uninstall clean
