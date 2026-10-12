# swampscheme

`swampscheme` is a compact C99 Scheme-family interpreter. It began life as
`bad_bot`, a snarky chatbot experiment, but the interpreter has become the main
project.

The project is intended to supersede `xenolisp` as the local Lisp/Scheme
interpreter.

## Build

```sh
make
```

## Run

```sh
./swampscheme
./swampscheme tests/smoke.scm
./swampscheme -e '(+ 2 3)'
```

Scheme files can reuse definitions from another file with `import` or `load`.
Both evaluate the target file in the current environment; relative paths are
resolved from the file doing the import:

```scheme
(import "helper.scm")
(display (helper-function 42))
```

The interactive REPL supports multi-line expressions and terminal bracketed
paste. You can paste a whole definition or several top-level expressions; the
REPL buffers the pasted source and evaluates it once the form is complete.

The old chatbot idea now lives as an example script:

```sh
./swampscheme scripts/swamp_bot.scm
./swamp_bot_chat.sh
```

`scripts/swamp_bot_lib.scm` contains the reusable response function,
`scripts/swamp_bot.scm` is the one-shot stdin entrypoint, and
`scripts/swamp_bot_chat.scm` is the pure Scheme chat loop.

See `R7RS.md` for the current language status and known gaps.

The comprehensive tutorial and reference is [SWAMPSCHEME_MANUAL.md](SWAMPSCHEME_MANUAL.md).

AI coding agents should also read [SWAMPSCHEME_AI_MANUAL.md](SWAMPSCHEME_AI_MANUAL.md),
which focuses on exact calling conventions, build-mode detection, canonical
program skeletons, lifecycle invariants, failure repair, and verification.

## Termbox TUI

`swampscheme` includes a termbox2 bridge for terminal apps:

```sh
./swampscheme scripts/tui_demo.scm
./swampscheme tbg.scm
./swampscheme tbg.scm notes.txt
./swampscheme shell.scm
```

Optional SDL3/Nuklear graphics support is enabled as one feature (SDL3 cannot
be enabled without Nuklear):

```sh
make WITH_SDL3=1
```

See [SDL3.md](SDL3.md) for the Scheme drawing, texture, event, and Nuklear
 APIs.

Cross-Scheme compatibility ports live under [ports](ports/README.md). The
Guile port currently exposes the repository's Termbox2 implementation through
`(use-modules (termbox))`; build it with `make ports-guile`.

The SDL build also includes a pure-Scheme falling-block test game:

```sh
./swampscheme scripts/swamptetris.scm
```

Use its Nuklear controls to test window resizing and audio initialization.

For a GUI editor example:

```sh
./swampscheme scripts/tinyeditor.scm
```

TinyEditor uses the built-in gap buffer and provides New/Open/Save/Exit,
Undo/Copy/Paste/Select All, soft wrapping, and About. File operations use
`tinyeditor.txt` in the current working directory.

`tbg.scm` is a small pure Scheme text editor inspired by `tbg.c`. It edits an
in-memory buffer, supports cursor movement, insertion, deletion, kill/yank,
search, mouse positioning/scrolling, `C-x C-s` save, `C-x C-f` find-file, and
`C-x C-c` quit. Without a filename it edits `tbg.txt`.

`shell.scm` is a small termbox command console. It accepts one command line at
a time, runs it through `/bin/sh -c`, captures stdout/stderr, and shows the
scrollable transcript. It is not a PTY-backed terminal emulator, so full-screen
interactive programs are still outside its scope.

The `(termbox command ...)` native supports:

- Lifecycle: `tb_init`, `tb_shutdown`, `tb_clear`, `tb_present`
- Drawing: `goto`, `print`, `print-at`, `set-cell`, `extend-cell`, `cell-at`
- Cursor: `set-cursor`, `hide-cursor`
- Colors/attrs: `fg`, `bg`, `attrs`, `set-clear-attrs`
- Modes: `input-mode`, `output-mode`
- Events: `poll-event`, `peek-event`
- Escape hatch: `send`, `last-errno`

General process helpers include `(system command arg ...)`, which returns
stdout and aborts on nonzero exit, and `(system/status command arg ...)`, which
returns a property list with `ok`, `status`, and `output` fields.

Events are property lists, so Scheme can inspect them with `plist-ref`:

```scheme
(define ev (termbox 'poll-event))
(plist-ref ev 'type)
(plist-ref ev 'key)
(plist-ref ev 'ch)
```

Colors, attributes, keys, event types, input modes, and output modes are exposed
as `TB_*` constants. Use `bitwise-ior` to combine flags, for example:

```scheme
(termbox 'input-mode (bitwise-ior TB_INPUT_ESC TB_INPUT_MOUSE))
(termbox 'set-cell 10 5 "@" (bitwise-ior TB_GREEN TB_BOLD) TB_DEFAULT)
```

See `TERMBOX.md` for the full command reference and event-loop examples.
