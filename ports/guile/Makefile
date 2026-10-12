CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -fPIC
GUILE_CFLAGS := $(shell pkg-config --cflags guile-3.0)
GUILE_LIBS := $(shell pkg-config --libs guile-3.0)

all: libswamp-termbox.so

libswamp-termbox.so: termbox.c ../../termbox2.h
	$(CC) $(CFLAGS) $(GUILE_CFLAGS) -shared -o $@ termbox.c $(GUILE_LIBS)

clean:
	rm -f libswamp-termbox.so

.PHONY: all clean
