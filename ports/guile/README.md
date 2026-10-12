# Guile Termbox2 port

This port exposes the repository's `termbox2.h` implementation to Guile 3.x
as the `(termbox)` module. It uses a small Guile foreign extension rather than
reimplementing Termbox in Scheme.

Build and run the hello-world example from the repository root:

```sh
make -C ports/guile
GUILE_LOAD_PATH="$PWD/ports/guile" \
GUILE_EXTENSIONS_PATH="$PWD/ports/guile" \
guile ports/guile/hello.scm
```

The extension is built from the same `termbox2.h` used by `swampscheme.c`, so
the terminal backend and low-level behavior are shared. Guile's documented
`load-extension` mechanism loads the compiled `.so` into the `(termbox)`
module.

The initial port covers initialization, shutdown, clearing, presentation,
colors, cursor movement, cells, text, dimensions, input/output modes, and
event polling. More advanced helpers can be added without changing the module
boundary.
