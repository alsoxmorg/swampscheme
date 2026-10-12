# Swamp Scheme

Swamp Scheme is a small, practical Scheme-family interpreter written in C99.
It is a command-line Lisp for scripts, terminal tools, bots, editors, and
small experimental games and GUI programs.

The project started as `bad_bot`, a snarky chatbot experiment. The interpreter
is now the main project, and the bot is one of its examples. Swamp Scheme is
also intended to become the local Lisp/Scheme interpreter that supersedes
`xenolisp`.

## What it wants to be

Swamp Scheme aims to be a compact, hackable, batteries-included Scheme for
one-person tools and experiments:

- small enough to understand and modify;
- pleasant to use from a REPL or a shell script;
- capable of making useful terminal applications with Termbox2;
- able to grow into SDL3/Nuklear graphics programs when needed; and
- close enough to familiar Scheme to make ordinary Lisp techniques useful.

It is deliberately not presented as a finished or fully standards-compliant
Scheme. The implementation currently has a substantial Scheme core, but it
still has R7RS-small gaps, a manual garbage collector, an incomplete numeric
tower, and no hygienic macro system. See [the R7RS status](doc/R7RS.md) for
the exact boundary.

## Quick start

You need a C99 compiler and `make`.

```sh
make
./swampscheme
```

Evaluate one expression:

```sh
./swampscheme -e '(+ 2 3)'
# 5
```

Run a Scheme file, or several files in the same top-level environment:

```sh
./swampscheme hello.scm
./swampscheme library.scm application.scm
```

With no command-line file, Swamp Scheme starts its REPL on a terminal. When
input is redirected, it reads and evaluates the input as a program:

```sh
printf '%s\n' '(display (+ 20 22))' '(newline)' | ./swampscheme
```

The REPL supports multi-line expressions and bracketed terminal paste. Scheme
files can include other files with `load` or `import`; both evaluate the file
in the current environment, and relative paths are resolved from the file
doing the load.

## A first program

Save this as `hello.scm`:

```scheme
(define (greet name)
  (display "Hello, ")
  (display name)
  (display "!")
  (newline))

(greet "from the swamp")
```

Then run it:

```sh
./swampscheme hello.scm
```

The core language includes S-expressions, lexical scope, closures, lists,
vectors, quoting and quasiquoting, records, hash tables, gap buffers, file
ports, processes, and a growing set of Scheme-compatible procedures. Swamp
extensions provide practical facilities such as `try`, filesystem helpers,
manual `(gc)`, and command-line access.

## Terminal, graphics, and examples

Termbox2 support is included in the normal build. Examples include:

```sh
./swampscheme scripts/tui_demo.scm
./swampscheme scripts/tbg.scm
./swampscheme scripts/tinyeditor.scm
./swampscheme scripts/swampdired.scm
./swampscheme scripts/swamp_bot.scm
```

The repository also includes a pure-Scheme text editor, shell console,
falling-block game, rogue arena experiment, and chatbot libraries. They are
useful both as examples and as integration tests for the runtime.

For optional SDL3/Nuklear support, install SDL3 development files and build:

```sh
make clean
make WITH_SDL3=1
./swampscheme scripts/sdl3_demo.scm
```

SDL3 and Nuklear are enabled together. See [SDL3.md](doc/SDL3.md) for the
graphics, textures, audio, events, and Nuklear APIs.

## Development

Build the interpreter:

```sh
make
```

Run the regression suite:

```sh
make test
```

Build the Guile Termbox compatibility port:

```sh
make ports-guile
```

Clean the generated interpreter:

```sh
make clean
```

The interpreter is a single C99 translation unit, `swampscheme.c`, with
vendored headers for Termbox2 and the optional graphics stack. Scheme examples
and tests live in `scripts/` and `tests/`. Compatibility work for other Scheme
implementations lives in `ports/`.

## Documentation

- [Swamp Scheme Reference Manual](doc/SWAMPSCHEME_MANUAL.md) — tutorial,
  language reference, runtime extensions, and complete examples.
- [R7RS status](doc/R7RS.md) — implemented features and known gaps.
- [Termbox2 reference](doc/TERMBOX.md) — terminal applications and events.
- [SDL3 reference](doc/SDL3.md) — optional graphics support.
- [Ports](ports/README.md) — Guile, CHICKEN, and TinyScheme compatibility work.
- [AI/manual integration guide](doc/SWAMPSCHEME_AI_MANUAL.md) — conventions for
  agents and automated work on the repository.

## Project status

Swamp Scheme is an active, experimental interpreter. APIs and language details
may change as the runtime grows. The most useful way to understand its current
direction is to read the reference manual and run the examples: the project is
meant to be explored by building things with it.
