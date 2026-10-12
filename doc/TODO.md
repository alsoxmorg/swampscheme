# SwampScheme TODO

These are interpreter/runtime gaps exposed by porting `tbg.c` to
`scripts/tbg.scm`. The editor port should hard-stop on missing runtime
features instead of working around them.

## Runtime Foundations

- Cleaner module/library system beyond source-include `load`/`import`.
- Real character type and character predicates/comparisons.
- Automatic GC thresholds and broader native temporary root auditing.
- Macros after the core runtime is strong enough.

## Recently Added

- Mutable vectors and vector literal I/O.
- Basic file ports.
- Directory/stat primitives for `dired` and file pickers:
  `directory-files`, `file-directory?`, `file-regular?`, `file-size`,
  `file-mtime`, `make-directory`, `delete-file`, `delete-directory`,
  `remove-directory`, and `rename-file`.
- Mutable records for editor buffers, windows, modes, menus, sprites, and game
  state.
- Mutable gap buffer primitive for editor-scale text edits.
- `letrec` for local recursive helpers.
- More math for games: `floor`, `ceiling`, `round`, `truncate`, `sqrt`, `expt`,
  `sin`, `cos`, `tan`, `atan`, and `random-real`.
- Manual mark-and-sweep garbage collection through `(gc)` and `(gc-stats)`.
- Mutable hash tables for command dispatch, caches, symbol tables, and editor
  modes.
- Quasiquote/backquote with comma and comma-at.
- Character compatibility predicates/comparisons over one-character strings.
- Load-path helpers: `current-load-directory`, `resolve-load-path`, and
  `load-relative`.
- Tail-position closure calls for long-running loops.
- Recoverable `(try expr fallback)`, `(error ...)`, and `(last-error)`.
