CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2
LDLIBS ?= -lm

# `make WITH_SDL3=1` enables the optional SDL3/Nuklear graphics bridge.
ifeq ($(WITH_SDL3),1)
CFLAGS += -DWITH_SDL3 $(shell pkg-config --cflags sdl3 2>/dev/null)
LDLIBS += $(shell pkg-config --libs sdl3 2>/dev/null || echo -lSDL3)
endif

all: swampscheme

swampscheme: swampscheme.c
	$(CC) $(CFLAGS) -o $@ swampscheme.c $(LDLIBS)

test: swampscheme
	./swampscheme tests/smoke.scm
	test "$$(./swampscheme tests/r7rs_easy.scm)" = '((2 3) (4 5) "bxxe" (2 3) (1 2 3 4) "_cde__" (0 7 8 0) "hello" #t #t #t 3 2 2 1 6 60 #t #f #f)'
	printf '%s\n' '(display (+ 2 3))' '(newline)' | ./swampscheme
	printf '%s\n' '(quit)' | ./swampscheme
	printf '%s\n' '(exit (+ 3 4))' | ./swampscheme; test "$$?" -eq 7
	test "$$(./swampscheme -e '(+ 8 9)')" = 17
	test "$$(./swampscheme -e '(begin (display "side") (+ 1 2))')" = side3
	./swampscheme -e missing-variable 2>/dev/null; test "$$?" -eq 1
	test "$$(./swampscheme tests/import_main.scm)" = 42
	test -n "$$(printf '%s\n' 'hello swamp bot' | ./swampscheme scripts/swamp_bot.scm)"
	printf '%s\n' 'what kind of books should i read' | ./swampscheme scripts/swamp_bot.scm | grep -Eiq 'read|book|classic|history|novel|manual|famous|weird|sentences'
	printf '%s\n' 'what kind of movies do you like?' | ./swampscheme scripts/swamp_bot.scm | grep -Eiq 'movie|film|noir|sci-fi|watch|genre'
	printf '%s\n' 'what are airplanes?' | ./swampscheme scripts/swamp_bot.scm | grep -Eiq 'airplane|plane|wing|fly'
	printf '%s\n' 'quit' | ./swampscheme scripts/swamp_bot.scm; test "$$?" -eq 86
	printf '%s\n' 'hello swamp bot' 'quit' | tests/swamp_bot_chat.sh | grep -q '^swamp_bot> '
	test "$$(printf '%s\n' '(display (system "printf" "swamp"))' | ./swampscheme)" = swamp

ports-guile:
	$(MAKE) -C ports/guile

clean:
	rm -f swampscheme

.PHONY: all test clean ports-guile
