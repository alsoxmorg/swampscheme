# Swamp Scheme ports

This directory contains compatibility ports for other Scheme implementations.
The goal is to keep the Scheme-facing vocabulary close to Swamp Scheme while
using each implementation's native extension mechanism.

Current targets:

- `chicken/`: a CHICKEN egg exporting `termbox` and `tb`, with a runner for
  the existing terminal demo.

- `guile/`: a Guile 3.x module and C extension using the repository's
  `termbox2.h` implementation.
- `tinyscheme/`: porting notes and the planned TinyScheme loadable primitive
  layer.

The ports are deliberately separate from the interpreter build. A program
which only uses the common Termbox subset can migrate by changing its module
setup and retaining calls such as:

```scheme
(termbox 'init)
(termbox 'clear)
(termbox 'print-at 2 1 TB_WHITE TB_DEFAULT "Hello")
(termbox 'present)
(termbox 'shutdown)
```

The target runtimes still differ in reader, module, error, and callback
semantics. The port documents those differences rather than pretending that a
single source file is portable without qualification.
