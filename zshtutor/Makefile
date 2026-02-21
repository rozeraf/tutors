
CC      = gcc
CFLAGS  = -O2 -Wall -Wextra
TARGET  = zshtutor
SRC     = zshtutor.c
PREFIX  = /usr/local

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o ~/.local/bin/$(TARGET) $(SRC)

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
