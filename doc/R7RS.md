# swampscheme R7RS Status

`swampscheme` is a compact C99 Scheme interpreter scaffold. It is not yet a
complete R7RS-small implementation.

Implemented:

- S-expression reader for lists, vectors, symbols, booleans, numbers, strings,
  quote, and quasiquote syntax
- Lexical environments
- Closures with fixed arity and variadic rest parameter syntax
- Special forms: `quote`, `if`, `begin`, `define`, `set!`, `lambda`, `let`,
  `let*`, `letrec`, `when`, `unless`, `and`, `or`, `cond`
- Basic `cond-expand` with `swampscheme`, `guile`, `tinyscheme`, `else`, and
  compound `and`/`or`/`not` feature requirements
- Core procedures for arithmetic, comparison, pairs, predicates, strings,
  whole-file text I/O, basic file ports, output, `apply`, `load`, and `import`
- Swamp mutable records through `make-record`, `record?`, `record-type`,
  `record-fields`, `record-ref`, and `record-set!`
- Swamp mutable gap buffers through `make-gap-buffer`, `gap-buffer?`,
  `gap-buffer-length`, `gap-buffer-point`, `gap-buffer-set-point!`,
  `gap-buffer->string`, `gap-buffer-ref`, `gap-buffer-substring`,
  `gap-buffer-insert!`, and `gap-buffer-delete!`
- Swamp mutable hash tables through `make-hash-table`, `hash-table?`,
  `hash-set!`, `hash-ref`, `hash-exists?`, `hash-delete!`, `hash-keys`, and
  `hash-count`
- Script-file execution and an interactive REPL
- Tail-position closure calls are evaluated without growing the C stack for
  ordinary loops and recursive functions
- Recoverable errors through Swamp extensions: `try`, `error`, and `last-error`
- Swamp extensions: captured `(system "command" "arg" ...)`, whitelisted helper
  procedures for swamp_bot, and explicit termbox2 demos
- Compatibility procedures needed by `swamp_bot.scm`, including `read-line`,
  `eof-object?`, `string-contains`, `string-tokenize`, `string-join`,
  `string-trim-both`, `substring`, `string-ref`, `integer->char`, `char->integer`,
  `string->number`, string comparisons, `for-each`, alist helpers, list helpers,
  mutable vectors, random choice support, command-line access, file checks, and time formatting
- Character compatibility over one-character strings: `char?`, character
  comparisons, predicates, `char-upcase`, and `char-downcase`
- Additional numeric helpers: `floor`, `ceiling`, `round`, `truncate`, `sqrt`,
  `expt`, `sin`, `cos`, `tan`, `atan`, and `random-real`
- Basic ports: `current-input-port`, `current-output-port`, `open-input-file`,
  `open-output-file`, `close-port`, `read`, `write`, `read-char`, `peek-char`,
  `write-char`, `display`, `newline`, and `flush-output-port`
- Filesystem helpers: `directory-files`, `file-exists?`, `file-directory?`,
  `file-regular?`, `file-size`, `file-mtime`, `make-directory`,
  `delete-file`, `delete-directory`, `remove-directory`, and `rename-file`
- Manual mark-and-sweep garbage collection through Swamp extensions `gc` and
  `gc-stats`
- Load-path helpers: `current-load-directory`, `resolve-load-path`, and
  `load-relative`
- Additional low-risk R7RS procedures: `boolean=?`, `list-tail`, `list-copy`,
  `string-copy`, `string-fill!`, `string-ci=?`, `string-ci<?`, `string-ci>?`,
  `string-ci<=?`, `string-ci>=?`, `char-ci=?`, `char-ci<?`, `char-ci>?`,
  `char-ci<=?`, `char-ci>=?`, `symbol->string`, `string->symbol`,
  `string-copy!`, `vector-copy`, `vector-copy!`, `vector-append`, `gcd`, `lcm`,
  `finite?`, `infinite?`, `nan?`, `exp`, `log`, `asin`, and `acos`

Major R7RS gaps:

- No hygienic macro system: `define-syntax`, `syntax-rules`
- No full R7RS library system. `import` is a Swamp source include alias for
  `load`, and evaluates the imported file in the current environment.
- Incomplete numeric tower; numbers are represented as C doubles
- Incomplete character, bytevector, exception, and advanced port support
- Several standard procedure families remain incomplete, including string and
  vector slice-copy variants, bytevector operations, and exactness-aware
  numeric conversions
- Records are a Swamp extension, not yet R7RS `define-record-type`
- Proper tail-call coverage is partial; ordinary tail-position closure calls are
  iterative, but this is not yet a full audited Scheme proper-tail-recursion
  guarantee across every feature
- Garbage collection is manual; automatic collection thresholds and broader
  native temporary root auditing are still pending
- No full R7RS lexical syntax, datum labels, or comments beyond `;`
- No conformance test suite yet

This gives the project a C99 base to iterate from. Full R7RS-small compliance
should be tracked feature by feature against the standard.
