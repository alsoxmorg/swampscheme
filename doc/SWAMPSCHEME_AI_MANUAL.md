# Swamp Scheme AI Coding Manual

## Purpose

This is an operational supplement for AI coding agents that must write usable
Swamp Scheme. It is intentionally constraint-heavy. Use it as a contract,
checklist, and pattern library.

The target agent may be Codex, Claude Code, an IDE assistant, a batch generator,
or a future Swamp-specific tool. The agent should use this manual to:

1. identify the interpreter build mode;
2. generate syntax the current reader accepts;
3. call only procedures present in the selected binary;
4. respect Swamp-specific values and errors;
5. produce complete runnable programs rather than pseudocode;
6. verify the result with the smallest useful execution test.

The human reference is [SWAMPSCHEME_MANUAL.md](SWAMPSCHEME_MANUAL.md). This
document is the engineering supplement.

## 1. Agent operating contract

When asked to write Swamp Scheme:

1. Assume this repository's interpreter, not Racket, Guile, Chicken, Chez, or
   generic R7RS.
2. Inspect the repository README, this manual, and the relevant subsystem
   manual before inventing an API.
3. Determine whether the request is CLI-only, termbox, or SDL3.
4. If SDL3 is required, build with WITH_SDL3=1. A default build does not
   provide SDL3 or Nuklear bindings.
5. Prefer existing Swamp primitives over shell commands or guessed libraries.
6. Use the exact argument shapes documented here.
7. Do not introduce R7RS libraries, syntax-rules, define-syntax, guard,
   define-record-type, bytevectors, standard character literals, or unsupported
   modules unless the user explicitly asks for an extension.
   `cond-expand` is available for selecting the `swampscheme` branch, but it
   is not a library loader and does not make R7RS libraries available.
8. Keep resource lifecycle explicit: close ports, shut down termbox/SDL, and
   destroy textures.
9. Test generated code with the target build before presenting it.
10. If a feature is unavailable, state the limitation instead of emitting
    plausible-looking pseudocode.

| Capability | Required build |
|---|---|
| Pure computation, files, subprocesses | ordinary build |
| Terminal UI | ordinary build; termbox is compiled in |
| SDL drawing, images, Nuklear | WITH_SDL3=1 build |

## 2. Repository reconnaissance

Before generating a nontrivial program, inspect:

~~~sh
pwd
rg --files
sed -n '1,180p' README.md
sed -n '1,220p' SWAMPSCHEME_AI_MANUAL.md
~~~

For terminal work:

~~~sh
sed -n '1,220p' TERMBOX.md
~~~

For SDL work:

~~~sh
sed -n '1,220p' SDL3.md
rg -n 'WITH_SDL3|b_sdl3|b_nuklear' swampscheme.c Makefile
~~~

Before using an unfamiliar name, search the environment registration:

~~~sh
rg -n 'add\\(e, "name"|env_def\\(e, "name"' swampscheme.c
~~~

The implementation is authoritative when a manual and implementation disagree.
Do not infer an API from another Scheme.

## 3. Build-mode protocol

### Ordinary build

~~~sh
make clean
make
~~~

This produces a CLI/termbox binary without SDL3/Nuklear names.

### SDL build

~~~sh
make clean
make WITH_SDL3=1
~~~

The SDL build requires SDL3 development headers and libraries:

~~~sh
pkg-config --modversion sdl3
pkg-config --cflags --libs sdl3
~~~

The repository supplies nuklear.h, nuklear_sdl3_renderer.h, stb_image.h, and
stb_truetype.h. SDL3 itself remains an external dependency.

### Build-mode failure signature

If execution produces:

~~~text
swampscheme: unbound variable: sdl3
~~~

the binary was compiled without WITH_SDL3=1. Do not modify the Scheme source
first. Rebuild:

~~~sh
make clean && make WITH_SDL3=1
~~~

Both variants output the same ./swampscheme path. Clean when switching:

~~~sh
make clean && make
make clean && make WITH_SDL3=1
~~~

An agent must report which build it tested.

## 4. Safe language subset

### Special forms

~~~text
quote quasiquote if begin define set! lambda
let let* letrec when unless and or cond try use-modules
~~~

### Reader syntax

The reader accepts:

- lists and dotted pairs;
- vectors written as #( ... );
- numbers, strings, symbols;
- #t, #true, #f, and #false;
- apostrophe quote;
- backquote quasiquote;
- comma unquote;
- comma-at unquote-splicing;
- semicolon comments.

### Forms not to assume

Do not emit these as implemented:

~~~text
define-syntax syntax-rules define-library
guard raise with-exception-handler
define-record-type
call-with-values values delay force case do
bytevectors parameters
~~~

### Three-argument if

Swamp if requires an explicit else:

~~~scheme
(if condition then-value else-value)
~~~

For a side-effect-only branch use when or unless:

~~~scheme
(when condition (do-work))
~~~

### Function definitions

~~~scheme
(define (add a b)
  (+ a b))

(define (collect first . rest)
  (cons first rest))
~~~

### Binding rules

Use let for parallel initialization:

~~~scheme
(let ((a 10) (b 20))
  (+ a b))
~~~

Use let* when later initializers depend on earlier bindings:

~~~scheme
(let* ((path "settings.txt")
       (exists (file-exists? path)))
  exists)
~~~

Use letrec for mutually recursive local procedures.

## 5. Value and type discipline

| Value | Do not confuse it with |
|---|---|
| number 42 | string "42" |
| symbol name | string "name" |
| one-character string "A" | a standard Scheme character |
| empty list () | false #f |
| property list | alist |
| vector | list |
| record | hash table |
| opaque SDL texture | number or record |
| EOF object | #f |

Only #f is false. The empty list, zero, and empty strings are true.

Use one-character strings for characters:

~~~scheme
(char? "A")
(char->integer "A")
(integer->char 65)
~~~

Do not generate standard character syntax such as #\\a.

All numbers are C double values. APIs needing indexes, dimensions, flags, or
counts convert to C integers. Generate integral values for those positions.

## 6. Canonical program skeletons

### CLI script

~~~scheme
(define (main args)
  (display "hello from Swamp Scheme")
  (newline)
  0)

(exit (main (command-line)))
~~~

### CLI filter

~~~scheme
(let loop ()
  (let ((line (read-line)))
    (unless (eof-object? line)
      (display line)
      (newline)
      (loop))))
~~~

### Safe file read

~~~scheme
(define (read-or-default path fallback)
  (try
    (read-file path)
    (begin
      (display "read failed: ")
      (display (last-error))
      (newline)
      fallback)))
~~~

### Sibling helper

~~~scheme
(load "lib/helpers.scm")
~~~

load/import paths are resolved relative to the source file being evaluated.
They are not module imports and share the current environment.

### Termbox application

~~~scheme
(define (tb command . args)
  (apply termbox (cons command args)))

(tb tb_init)
(tb 'input-mode (bitwise-ior TB_INPUT_ESC TB_INPUT_MOUSE))
(tb 'clear)
(tb 'print-at 2 2 (bitwise-ior TB_GREEN TB_BOLD) TB_DEFAULT
    "Press Escape to quit")
(tb 'present)
(tb 'poll-event)
(tb tb_shutdown)
~~~

### SDL/Nuklear application

~~~scheme
(define running #t)
(sdl3 "init" 800 500 "Swamp application")

(let loop ()
  (sdl3 "clear" 18 24 32 255)
  (sdl3 "begin-frame")
  (let events ()
    (let ((event (sdl3 "event")))
      (when event
        (if (= (plist-ref event 'type) SDL_EVENT_QUIT)
            (set! running #f)
            (events)))))
  (nuklear "begin-frame")
  (when (nuklear "begin" "Swamp" 40 40 320 200)
    (nuklear "row" 30 1)
    (nuklear "label" "Hello from Scheme")
    (nuklear "row" 30 1)
    (when (nuklear "button" "Quit")
      (set! running #f))
    (nuklear "end"))
  (nuklear "render")
  (sdl3 "present")
  (when running (loop)))

(sdl3 "shutdown")
~~~

## 7. Core procedure inventory

### Arithmetic and predicates

~~~text
+ - * / abs min max
floor ceiling round truncate sqrt expt sin cos tan atan
modulo remainder zero? positive? negative? odd? even?
= < > <= >= bitwise-ior bitwise-and
~~~

### Lists and higher-order procedures

~~~text
cons car cdr list
caar cdar cadr cddr caddr cadddr
map for-each append length reverse list-ref
memq memv member assq assv assoc plist-ref apply
~~~

Use map to collect results, for-each for side effects, and apply when the final
argument is already a proper list.

### Predicates and equality

~~~text
null? pair? list? symbol? string? number? boolean? vector? eof-object?
not eq? eqv? equal?
~~~

### Vectors

~~~text
vector make-vector vector? vector-length vector-ref vector-set! vector-fill!
list->vector vector->list
~~~

### Records

~~~text
record? make-record record-type record-fields record-ref record-set!
~~~

Construction:

~~~scheme
(make-record 'type '((field . value) (other . value)))
(make-record 'type '(field other))
~~~

Bare fields start as #f. Unknown fields are errors.

### Hash tables

~~~text
make-hash-table hash-table? hash-set! hash-ref hash-exists?
hash-delete! hash-keys hash-count
~~~

Keys must be symbols or strings. Use the optional third hash-ref argument for a
default.

### Gap buffers

~~~text
make-gap-buffer gap-buffer? gap-buffer-length gap-buffer-point
gap-buffer-set-point! gap-buffer->string gap-buffer-ref
gap-buffer-substring gap-buffer-insert! gap-buffer-delete!
gap-buffer-line-count gap-buffer-line-number-at gap-buffer-line-start
gap-buffer-line-start-from gap-buffer-line-end-from
gap-buffer-find-next gap-buffer-find-prev
~~~

Ranges use [start,end). Offsets are byte-oriented in important paths.

### Strings and character-like values

~~~text
string-length make-string string-append substring string-ref
string-downcase string-trim-both string-contains string-join string-tokenize
string=? string<? string>? string<=? string>=?
char? char=? char<? char>? char<=? char>=?
char-alphabetic? char-numeric? char-whitespace?
char-upper-case? char-lower-case? char-upcase char-downcase
integer->char char->integer number->string string->number
~~~

### Ports and I/O

~~~text
display newline write read read-char peek-char write-char read-line
current-input-port current-output-port
open-input-file open-output-file
close-port close-input-port close-output-port flush-output-port
port? input-port? output-port?
~~~

### Files and loading

~~~text
read-file write-file load import load-relative
current-load-directory resolve-load-path
directory-files file-exists? file-directory? file-regular?
file-size file-mtime make-directory delete-file delete-directory
remove-directory rename-file
~~~

### Runtime integration

~~~text
system system/status command-line input-terminal? cpu-temp eztemplate
error last-error quit exit
random-source-randomize! random-integer random-real
current-time localtime strftime gc gc-stats
termbox termbox-demo
~~~

## 8. Termbox agent reference

### Dispatch

~~~scheme
(define (tb command . args)
  (apply termbox (cons command args)))

(tb 'clear)
(tb tb_clear)
~~~

Commands may be symbols or strings.

### Lifecycle

~~~text
init / tb_init
shutdown / tb_shutdown
clear / tb_clear
present / tb_present
~~~

### Drawing

~~~text
goto x y
move x y
print text
print-at x y fg bg text
set-cell x y character fg bg
cell x y character fg bg
extend-cell x y character
cell-at x y
width
height
~~~

The back buffer is not visible until present. set-cell accepts a one-character
string or a Unicode codepoint number.

### Styles

~~~text
setcolor / fg attribute
setbg / bg attribute
attrs foreground background
set-clear-attrs foreground background
~~~

Constants:

~~~text
TB_DEFAULT TB_BLACK TB_RED TB_GREEN TB_YELLOW TB_BLUE
TB_MAGENTA TB_CYAN TB_WHITE TB_BOLD TB_UNDERLINE TB_REVERSE TB_ITALIC
~~~

Combine flags using bitwise-ior.

### Input

~~~text
set-input-mode / input-mode mode
set-output-mode / output-mode mode
poll-event / event
peek-event timeout
send string
last-errno
~~~

Events are flat property lists. Use plist-ref, not assoc:

~~~scheme
(define event (tb 'poll-event))
(plist-ref event 'type)
(plist-ref event 'key)
~~~

### Termbox frame invariant

For each frame:

1. clear;
2. draw;
3. present;
4. poll or peek;
5. branch on event;
6. repeat or shutdown.

Never poll before initialization.

## 9. SDL3/Nuklear agent reference

### SDL commands

~~~text
init width height [title]
shutdown / destroy
clear r g b a
color r g b a
set-color r g b a
pixel x y
line x1 y1 x2 y2
rect x y width height
fill-rect x y width height
texture-load path
load-texture path
texture-size texture
texture-destroy texture
blit texture x y
blit texture x y width height
copy texture x y
ticks
delay milliseconds
window-size
set-window-size width height
fullscreen boolean
key-down? keycode
clipboard-set text
clipboard-get
text-input boolean
present
event / poll-event
begin-frame
audio-init
audio-shutdown
sound-load path
load-sound path
sound-play sound
play-sound sound
sound-destroy sound
~~~

### SDL lifecycle

Initialize once:

~~~scheme
(sdl3 "init" width height title)
~~~

Each frame:

~~~scheme
(sdl3 "clear" r g b a)
(sdl3 "begin-frame")
; drain events
; optional Nuklear frame
(sdl3 "present")
~~~

Shutdown:

~~~scheme
(sdl3 "shutdown")
~~~

### Texture invariant

The result of texture-load is opaque:

~~~scheme
(define texture (sdl3 "texture-load" path))
(define size (sdl3 "texture-size" texture))
(sdl3 "blit" texture 0 0)
(sdl3 "texture-destroy" texture)
~~~

Only texture-size, texture-destroy, blit, and copy accept the handle. Do not
perform arithmetic on it or serialize it.

Relative image paths are resolved from the process working directory, not
automatically from the Scheme source directory. Require an absolute path,
document the working directory, or derive a path with
current-load-directory.

Use ticks and delay for frame timing. Do not spawn an external sleep process
every frame. Use exported SDLK_* values rather than hardcoded numeric keycodes,
and use key-down? for held-key movement.

The WAV audio lifecycle is:

~~~text
audio-init -> sound-load -> sound-play -> sound-destroy -> audio-shutdown
~~~

Audio requires a usable host audio device. SDL_AUDIODRIVER=dummy is useful for
headless initialization tests.

### Event drain template

~~~scheme
(let events ()
  (let ((event (sdl3 "event")))
    (when event
      (if (= (plist-ref event 'type) SDL_EVENT_QUIT)
          (set! running #f)
          (events)))))
~~~

The queue should be drained every frame. Polling only once can leave a program
unresponsive under input load.

Exported key constants include SDLK_ESCAPE, SDLK_RETURN, SDLK_UP, SDLK_DOWN, SDLK_LEFT,
SDLK_RIGHT, SDLK_BACKSPACE, SDLK_DELETE, SDLK_TAB, SDLK_HOME, SDLK_END,
SDLK_W, SDLK_A, SDLK_S, SDLK_D, SDLK_C, SDLK_V, SDLK_X, SDLK_Z, and SDLK_SPACE.
Modifier masks include SDL_KMOD_CTRL, SDL_KMOD_SHIFT, and SDL_KMOD_ALT. Prefer these
bindings over hardcoded numeric SDL keycodes.

For text editors, call `(sdl3 "text-input" #t)` after initialization, insert
the `text` field from `SDL_EVENT_TEXT_INPUT`, and handle editing keys from
`SDL_EVENT_KEY_DOWN`. Use `clipboard-set`/`clipboard-get` for copy and paste.

SDL_EVENT_WINDOW_RESIZED is also exported. On resize, query the current
logical window dimensions with `(sdl3 "window-size")` rather than trusting
stale dimensions in Scheme state.

### Nuklear lifecycle

Required order:

1. sdl3 begin-frame;
2. drain sdl3 event;
3. nuklear begin-frame;
4. emit widgets;
5. nuklear render;
6. sdl3 present.

Current widgets:

~~~text
begin title x y width height
end
row height columns
label text
button text
render
font-load path pixel-size
begin-frame
~~~

begin and button return booleans.

### Fonts

~~~scheme
(nuklear "font-load" "/absolute/path/font.ttf" 18)
~~~

Nuklear font baking uses its embedded stb_truetype implementation. Do not
separately compile stb_truetype into the current interpreter; that would
duplicate the implementation already provided by Nuklear.

### Headless validation

~~~sh
SDL_VIDEODRIVER=dummy ./swampscheme test.scm
~~~

This validates initialization and renderer calls where the installed SDL driver
supports dummy video. It does not replace a visual test.

## 10. Error-driven repair protocol

Classify an error before editing source:

| Error | Likely cause | Repair |
|---|---|---|
| unbound variable: sdl3 | wrong build | rebuild WITH_SDL3=1 |
| expected number | string/symbol in numeric position | convert or fix literal |
| expected string | wrong value type | use number->string or correct binding |
| expected vector/list/pair | wrong collection shape | inspect constructor/predicate |
| arity | wrong argument count | compare exact command signature |
| unknown termbox command | typo/unsupported alias | inspect TERMBOX.md |
| unknown sdl3 command | typo/unsupported command | inspect SDL3.md |
| SDL initialization failure | missing runtime/display/driver | inspect SDL error |
| stb_image failure | path/format/corrupt image | use absolute path and verify |
| Nuklear failure | wrong frame/font lifecycle | follow frame invariant |
| terminal corruption | missing shutdown | add cleanup path |

Do not repair an unbound SDL name by defining a fake Scheme procedure. The
feature is compiled into the interpreter.

## 11. Anti-patterns

### Generic module syntax

Do not emit:

~~~scheme
(import (scheme base))
~~~

Use a string source path:

~~~scheme
(import "lib/helpers.scm")
~~~

### Two-argument if

Do not emit:

~~~scheme
(if condition (display "yes"))
~~~

Use when or an explicit empty-list fallback.

### Shell interpolation

Prefer:

~~~scheme
(system "printf" "%s\n" value)
~~~

over constructing one shell command string.

### Empty list as false

An empty directory returns (), which is true. Use null? when emptiness matters.

### assoc on property lists

Do not use assoc on flat event data:

~~~scheme
(plist-ref event 'type)
~~~

### Incomplete SDL frame

Do not emit widgets before Nuklear begin-frame, and do not present before
Nuklear render when GUI output is expected.

### Missing resource cleanup

~~~scheme
(define texture (sdl3 "texture-load" path))
; use texture
(sdl3 "texture-destroy" texture)
(sdl3 "shutdown")
~~~

### Unicode assumptions

String and gap-buffer offsets are byte-oriented in important paths. Do not
promise grapheme-aware editing.

## 12. Agent verification checklist

### Language

- Does every if have an else?
- Are all variables defined before use?
- Are values used in the correct type positions?
- Are characters one-character strings?
- Are only supported special forms used?
- Are recursive loops in tail position?
- Are proper lists passed to map, append, length, apply, and for-each?

### Files

- Are load paths correct?
- Is the process working directory assumption documented?
- Are opened ports closed?
- Are filesystem failures intentionally fatal or wrapped in try?

### Termbox

- Is termbox initialized before drawing or polling?
- Is input/output mode configured when needed?
- Is the back buffer presented?
- Is shutdown reachable?
- Are events read with plist-ref?

### SDL/Nuklear

- Was the binary built with WITH_SDL3=1?
- Is SDL initialized before use?
- Is the event queue drained?
- Is Nuklear input finalized before widgets?
- Is Nuklear rendered before present?
- Are image paths valid?
- Are textures destroyed?
- Is SDL shutdown reachable?

### Verification

- Was the requested build compiled?
- Was a syntax/load smoke test run?
- Was the relevant regression suite run?
- If GUI code was not visually tested, is that stated?

## 13. Minimal smoke tests

### Core

~~~sh
./swampscheme -e '(begin (display (+ 2 3)) (newline))'
~~~

Expected output is 5.

### Error recovery

~~~sh
./swampscheme -e '(display (try (car ()) (last-error)))'
~~~

Expected output contains an error message.

### SDL

~~~sh
SDL_VIDEODRIVER=dummy ./swampscheme -e \
  '(begin (sdl3 "init" 320 240 "test") \
          (sdl3 "clear" 0 0 0 255) \
          (sdl3 "present") \
          (sdl3 "shutdown"))'
~~~

### Texture

~~~scheme
(begin
  (sdl3 "init" 64 64 "texture-test")
  (define image (sdl3 "texture-load" "/absolute/path/image.png"))
  (display (sdl3 "texture-size" image))
  (sdl3 "blit" image 0 0)
  (sdl3 "texture-destroy" image)
  (sdl3 "shutdown"))
~~~

### Font

~~~scheme
(begin
  (sdl3 "init" 320 240 "font-test")
  (nuklear "font-load" "/absolute/path/font.ttf" 18)
  (sdl3 "shutdown"))
~~~

## 14. Communicating limitations

Use precise statements:

- This requires the SDL-enabled build: make clean && make WITH_SDL3=1.
- The texture loader uses stb_image and expects a path visible from the
  process working directory.
- Characters are one-character strings in this implementation.
- Swamp import is source inclusion, not R7RS module import.
- The code was syntax-tested, but visual SDL behavior requires a display.

Avoid vague statements such as “this should work in Scheme.” If uncertain,
inspect the source or run a probe.

## 15. Source-of-truth hierarchy

When evidence conflicts:

1. actual swampscheme.c implementation;
2. Makefile and compile flags;
3. SDL3.md and TERMBOX.md;
4. SWAMPSCHEME_MANUAL.md;
5. README.md and R7RS.md;
6. general Scheme knowledge;
7. speculation.

General Scheme knowledge is last because Swamp deliberately differs in several
important places.

## 16. AI handoff format

Every generated program handoff should include:

1. filename;
2. build command;
3. run command;
4. required external assets/packages;
5. lifecycle summary;
6. tests run and result;
7. known limitations, especially visual/GUI limitations.

Example:

~~~text
File: scripts/example.scm
Build: make clean && make WITH_SDL3=1
Run: SDL_VIDEODRIVER=dummy ./swampscheme scripts/example.scm
Dependencies: SDL3 development/runtime; optional PNG and TTF assets
Lifecycle: init -> begin-frame -> drain events -> Nuklear -> render ->
present -> texture-destroy -> shutdown
Verification: SDL compile and headless smoke test passed
Limitation: no real-window visual interaction test was run
~~~

### SDL integration test

`scripts/swamptetris.scm` is the canonical larger SDL smoke test. It is a
pure-Scheme falling-block game and exercises `ticks`, `delay`, exported
`SDLK_*` constants, `key-down?`, event draining, `SDL_EVENT_WINDOW_RESIZED`,
`window-size`, `set-window-size`, SDL rectangles/colors, Nuklear widgets, and
the audio init/shutdown path through buttons in the UI.

Run it with:

~~~sh
make clean && make WITH_SDL3=1
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./swampscheme scripts/swamptetris.scm
~~~

The dummy-driver run verifies startup and shutdown but cannot click the
Nuklear buttons or provide visual confirmation. On a real display, use
`RESIZE WINDOW` and `AUDIO INIT` in the side panel.

## 17. One-page machine summary

~~~text
Interpreter: ./swampscheme
Normal build: make
SDL build: make clean && make WITH_SDL3=1
Source run: ./swampscheme file.scm
Expression run: ./swampscheme -e '(...)'
Comments: semicolon to end of line
False: only #f
Characters: one-character strings
If: requires test, then, else
Modules: unavailable; import/load a string path into current environment
Errors: try protected fallback; error raises; last-error reports
Lists: proper lists required by map/append/length/apply
Events: flat property lists; use plist-ref
Termbox: init -> draw -> present -> poll -> shutdown
SDL: init -> clear/begin-frame -> drain events -> widgets/render -> present
Textures: opaque; texture-load -> blit -> texture-destroy
Fonts: nuklear font-load path size
Cleanup: close ports, termbox shutdown, texture destroy, SDL shutdown
~~~

The agent's job is not to write plausible Scheme. It is to write code this
interpreter can read, evaluate, and execute in the requested build mode.
