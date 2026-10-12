# CHICKEN Termbox egg

A native CHICKEN egg exposing the vendored Termbox2 backend with Swamp Scheme's
command interface. The module is `termbox`; `tb` is an alias for `termbox`.
Uses CHICKEN core modules only, with no egg dependencies. Tested on CHICKEN
6.0.0 on Linux; the source uses the CHICKEN 5/6 FFI, but CHICKEN 5 has not
been tested. A C compiler and a Unix terminal are required.

## Build and run locally

From the repository root:

```sh
make -C ports/chicken check
make -C ports/chicken demo
```

The demo loads `scripts/tui_demo.scm` unchanged, inside a cleanup wrapper.
`check` runs nonterminal tests and real pseudo-terminal tests, including the
existing TUI demo. The latter require Python 3 on Unix.

The default tools here are `chicken-csc` and `chicken-csi`. On installations
using the usual names, pass `CSC=csc CSI=csi` to make.

## Install as an egg

```sh
cd ports/chicken
chicken-install -n  # verify packaging without installation
chicken-install     # install into your configured CHICKEN repository
```

Then programs can use:

```scheme
(import scheme (chicken base) (chicken bitwise) termbox)

(call-with-termbox
  (lambda ()
    (tb 'clear)
    (tb 'print-at 2 1 (bitwise-ior TB_GREEN TB_BOLD) TB_DEFAULT "Hello")
    (tb 'present)
    (tb 'poll-event)))
```

An installed module works in the interpreter and compiled programs. For a local
build, run from this directory with `chicken-csi -I . -s program.scm` or compile
with `chicken-csc -I . program.scm`. The local shared library must be available
in the module search path when running the resulting executable.

## Compatibility

Supported commands: `init`, `shutdown`, `clear`, `present`, `width`, `height`,
`setcolor`/`fg`, `setbg`/`bg`, `attrs`, `goto`/`move`, `set-cursor`/`cursor`,
`hide-cursor`, `set-cell`/`cell`, `extend-cell`, `print-at`, `print`,
`set-clear-attrs`/`clear-attrs`, `input-mode`/`set-input-mode`,
`output-mode`/`set-output-mode`, `poll-event`/`event`, `peek-event`, `send`,
`last-errno`, and `cell-at`. Commands accept symbols or strings. The symbols
`tb_init`, `tb_shutdown`, `tb_clear`, `tb_present`, and `tb_poll_event` are
exported for the existing command style `(tb tb_clear)`.

The module exports key, color, style, event, modifier, and mode constants from
the vendored header. Attributes accept integers or named symbols/strings.
Characters accept codepoints, Scheme characters, or UTF-8 strings. Coordinates
are zero based. Events are property lists:

```scheme
(type 1 key 0 ch 113 mod 0 w 0 h 0 x 0 y 0)
```

`plist-ref` is exported with an optional default, matching the existing TUI.
`peek-event` takes milliseconds and returns `#f` on timeout. Errors raise Scheme
exceptions. Terminal operations initialize automatically; use
`call-with-termbox` to guarantee shutdown on normal return or exception. The
terminal state is shared, so avoid nested sessions or concurrent callers.

This ports the terminal interface, not every Swamp Scheme primitive. Programs
using SDL3, Nuklear, gap buffers, or other interpreter-specific functions need
additional bindings. Scheme reader and library differences may also need
adaptation. The TUI demo is verified; other programs have not been verified.

`termbox2.h` is a copy of the repository header so the egg can build independently.
When updating the backend, refresh this copy and run `make check`. Its original
MIT license is preserved in the header.
