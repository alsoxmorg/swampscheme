# Using termbox2 from swampscheme

`swampscheme` exposes termbox2 through one native procedure:

```scheme
(termbox command ...)
```

`command` can be a symbol, such as `'clear`, or one of the built-in string
aliases, such as `tb_clear`. Most programs define a small helper:

```scheme
(define (tb cmd . args)
  (apply termbox (cons cmd args)))
```

Then call it like this:

```scheme
(tb tb_init)
(tb 'print-at 2 1 TB_WHITE TB_DEFAULT "hello")
(tb tb_present)
(tb tb_poll_event)
(tb tb_shutdown)
```

Always call `tb_shutdown` before the program exits normally. `swampscheme`
also shuts termbox down during `quit`, `exit`, fatal errors, and process exit.

## Minimal app

```scheme
(define (tb cmd . args)
  (apply termbox (cons cmd args)))

(tb tb_init)
(tb tb_clear)
(tb 'hide-cursor)
(tb 'print-at 2 1 (bitwise-ior TB_GREEN TB_BOLD) TB_DEFAULT
    "Press any key to close")
(tb tb_present)
(tb tb_poll_event)
(tb tb_shutdown)
```

Run it from a real terminal:

```sh
./swampscheme myapp.scm
```

For a larger example, run:

```sh
./swampscheme scripts/tui_demo.scm
```

## Coordinates

Termbox uses zero-based cell coordinates:

- `x = 0`, `y = 0` is the top-left cell.
- `x` increases to the right.
- `y` increases downward.
- `(tb 'width)` and `(tb 'height)` return the current terminal size.

Commands that write outside the current terminal bounds fail.

## Lifecycle

```scheme
(tb tb_init)
(tb tb_shutdown)
(tb tb_clear)
(tb tb_present)
```

- `tb_init`: initialize termbox.
- `tb_shutdown`: restore the terminal.
- `tb_clear`: clear the back buffer and reset swampscheme's print cursor.
- `tb_present`: draw the back buffer to the terminal.

Termbox draws into a back buffer. Most drawing commands do not become visible
until `tb_present`.

## Drawing

```scheme
(tb 'goto x y)
(tb 'print text)
(tb 'print-at x y fg bg text)
(tb 'set-cell x y ch fg bg)
(tb 'extend-cell x y ch)
(tb 'cell-at x y)
```

- `goto`: move swampscheme's text cursor for later `(display ...)`,
  `(newline)`, or `(tb 'print ...)`.
- `print`: write text at the current swampscheme text cursor.
- `print-at`: write text at `x`, `y` with explicit foreground/background
  attributes. It returns the number of cells printed.
- `set-cell`: write one character cell. `ch` can be a one-character string or
  a Unicode codepoint number.
- `extend-cell`: append a Unicode codepoint to an existing cell.
- `cell-at`: return a property list describing a back-buffer cell:
  `(ch codepoint fg attr bg attr)`.

Example:

```scheme
(tb 'set-cell 10 5 "@" (bitwise-ior TB_CYAN TB_BOLD) TB_DEFAULT)
(tb 'print-at 2 8 TB_WHITE TB_BLUE "status")
```

## Cursor

```scheme
(tb 'set-cursor x y)
(tb 'hide-cursor)
```

The cursor is separate from the swampscheme text cursor used by `goto` and
`print`.

## Colors and Attributes

Foreground and background values can be numeric `TB_*` constants, numeric
palette indexes, or these symbolic/string names:

```scheme
'default 'black 'red 'green 'yellow 'blue 'magenta 'cyan 'white
'bold 'underline 'reverse 'italic
```

The numeric constants are:

```scheme
TB_DEFAULT
TB_BLACK TB_RED TB_GREEN TB_YELLOW TB_BLUE TB_MAGENTA TB_CYAN TB_WHITE
TB_BOLD TB_UNDERLINE TB_REVERSE TB_ITALIC
```

Use `bitwise-ior` to combine a color with attributes:

```scheme
(define title-fg (bitwise-ior TB_WHITE TB_BOLD))
(tb 'print-at 2 1 title-fg TB_DEFAULT "Title")
```

Convenience commands set the current attributes used by `(display ...)`,
`(newline)`, and `(tb 'print ...)`:

```scheme
(tb 'fg TB_GREEN)
(tb 'bg TB_DEFAULT)
(tb 'attrs (bitwise-ior TB_YELLOW TB_BOLD) TB_DEFAULT)
(tb 'set-clear-attrs TB_DEFAULT TB_BLUE)
```

## Output Modes

```scheme
(tb 'output-mode mode)
```

Modes:

```scheme
TB_OUTPUT_CURRENT
TB_OUTPUT_NORMAL
TB_OUTPUT_256
TB_OUTPUT_216
TB_OUTPUT_GRAYSCALE
TB_OUTPUT_TRUECOLOR
```

`swampscheme` sets `TB_OUTPUT_256` during initialization. You can pass numeric
palette values directly as foreground/background attributes in 256-color mode.

## Input Modes

```scheme
(tb 'input-mode mode)
```

Modes:

```scheme
TB_INPUT_CURRENT
TB_INPUT_ESC
TB_INPUT_ALT
TB_INPUT_MOUSE
```

Input modes are bit flags. Enable mouse support like this:

```scheme
(tb 'input-mode (bitwise-ior TB_INPUT_ESC TB_INPUT_MOUSE))
```

## Events

```scheme
(tb tb_poll_event)
(tb 'peek-event timeout-ms)
```

- `poll-event`: wait forever for the next event.
- `peek-event`: wait up to `timeout-ms`; returns `#f` if no event arrives.

Events are property lists:

```scheme
(type event-type key key-code ch codepoint mod modifiers w width h height x mouse-x y mouse-y)
```

Read fields with `plist-ref`:

```scheme
(define ev (tb tb_poll_event))
(define type (plist-ref ev 'type))
(define key  (plist-ref ev 'key))
(define ch   (plist-ref ev 'ch))
(define x    (plist-ref ev 'x))
(define y    (plist-ref ev 'y))
```

Event types:

```scheme
TB_EVENT_KEY
TB_EVENT_RESIZE
TB_EVENT_MOUSE
```

Modifiers:

```scheme
TB_MOD_ALT
TB_MOD_CTRL
TB_MOD_SHIFT
TB_MOD_MOTION
```

Common keys:

```scheme
TB_KEY_ESC
TB_KEY_ENTER
TB_KEY_SPACE
TB_KEY_BACKSPACE
TB_KEY_BACKSPACE2
TB_KEY_TAB
TB_KEY_CTRL_C
TB_KEY_CTRL_D
TB_KEY_CTRL_Q
TB_KEY_ARROW_UP
TB_KEY_ARROW_DOWN
TB_KEY_ARROW_LEFT
TB_KEY_ARROW_RIGHT
TB_KEY_INSERT
TB_KEY_DELETE
TB_KEY_HOME
TB_KEY_END
TB_KEY_PGUP
TB_KEY_PGDN
TB_KEY_F1 ... TB_KEY_F12
TB_KEY_MOUSE_LEFT
TB_KEY_MOUSE_RIGHT
TB_KEY_MOUSE_MIDDLE
TB_KEY_MOUSE_RELEASE
TB_KEY_MOUSE_WHEEL_UP
TB_KEY_MOUSE_WHEEL_DOWN
```

Printable keyboard input usually arrives in the `ch` field as a Unicode
codepoint. For example, lowercase `q` is `113`.

## Event loop pattern

```scheme
(define (quit-event? ev)
  (or (= (plist-ref ev 'key 0) TB_KEY_ESC)
      (= (plist-ref ev 'key 0) TB_KEY_CTRL_C)
      (= (plist-ref ev 'ch 0) 113)))

(define (draw x)
  (tb tb_clear)
  (tb 'set-cell x 5 "@" (bitwise-ior TB_GREEN TB_BOLD) TB_DEFAULT)
  (tb tb_present))

(define (loop x)
  (draw x)
  (let ((ev (tb tb_poll_event)))
    (when (not (quit-event? ev))
      (loop
        (cond
          ((= (plist-ref ev 'key 0) TB_KEY_ARROW_LEFT) (- x 1))
          ((= (plist-ref ev 'key 0) TB_KEY_ARROW_RIGHT) (+ x 1))
          (else x))))))

(tb tb_init)
(tb 'hide-cursor)
(loop 10)
(tb tb_shutdown)
```

## Raw terminal output

```scheme
(tb 'send bytes)
(tb 'last-errno)
```

`send` writes raw bytes to the terminal. Use it only when termbox does not
already provide the control you need. `last-errno` returns termbox2's last
recorded error number.

## Helper procedures

The termbox bridge pairs well with these general swampscheme helpers:

```scheme
(bitwise-ior a b ...)
(bitwise-and a b ...)
(plist-ref plist key)
(plist-ref plist key default)
```

`plist-ref` is useful for event and cell property lists. It returns `#f` when
the key is missing unless a default value is supplied.
