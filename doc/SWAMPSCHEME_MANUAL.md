# The Swamp Scheme Reference Manual

Swamp Scheme is a small, practical Scheme-family interpreter written in C99.
It has the familiar shape of Scheme—S-expressions, lexical scope, closures,
lists, vectors, quoting, higher-order procedures, and tail-position loops—but
it is not a complete implementation of R7RS-small.

This manual describes the behavior implemented by this repository. It is both
a tutorial for learning Scheme in the Swamp dialect and a reference for the
runtime extensions used by terminal programs, editors, bots, file tools, and
SDL3 applications.

The short version:

> Swamp Scheme is a compact Scheme for scripts, terminal interfaces, small
> tools, bots, editors, and experimental SDL3 programs.

## Contents

1. Getting started
2. Running programs
3. The Swamp Scheme model
4. Reading source
5. Evaluation and special forms
6. Writing procedures
7. Lists, pairs, alists, and property lists
8. Vectors
9. Records
10. Hash tables
11. Gap buffers
12. Numbers and mathematics
13. Strings and character-like values
14. Input, output, and ports
15. Files and load paths
16. Processes and system integration
17. Errors and recovery
18. Randomness and time
19. Garbage collection
20. Termbox terminal applications
21. SDL3, stb_image, and Nuklear
22. Building and distributing programs
23. R7RS compatibility
24. Patterns and complete examples
25. Troubleshooting
26. Quick reference

---

## 1. Getting started

### 1.1 Build the command-line interpreter

From the project directory:

~~~sh
make
~~~

This produces swampscheme with the core language and termbox2 support. It does
not require SDL3. This is the right build for command-line tools, file
processors, bots, and terminal applications.

### 1.2 Build the SDL3/Nuklear interpreter

SDL3 and Nuklear are one optional feature. Enable them together:

~~~sh
make clean
make WITH_SDL3=1
~~~

The SDL-enabled build requires SDL3 development headers and libraries visible
through pkg-config or the system linker:

~~~sh
pkg-config --modversion sdl3
pkg-config --cflags --libs sdl3
~~~

If the binary was built without WITH_SDL3=1, the names sdl3 and nuklear do not
exist. Running an SDL program with that binary produces:

~~~text
swampscheme: unbound variable: sdl3
~~~

That is a build-mode problem, not a Scheme syntax problem.

### 1.3 Evaluate one expression

~~~sh
./swampscheme -e '(+ 2 3)'
~~~

Output:

~~~text
5
~~~

The -e form requires exactly one expression argument:

~~~sh
./swampscheme -e '(string-append "swamp" "scheme")'
~~~

### 1.4 Run a source file

~~~sh
./swampscheme program.scm
~~~

Multiple files are evaluated in the same top-level environment, in
command-line order:

~~~sh
./swampscheme library.scm application.scm
~~~

### 1.5 Use the REPL

Run without a file on a terminal:

~~~sh
./swampscheme
~~~

The prompt is:

~~~text
swampscheme>
~~~

The REPL accepts multi-line expressions and bracketed terminal paste. It waits
until parentheses and strings are complete before evaluating pasted source.
On non-terminal input, Swamp Scheme reads the stream as a program.

### 1.6 A first program

~~~scheme
(define (greet name)
  (display "Hello, ")
  (display name)
  (display "!")
  (newline))

(greet "from the swamp")
~~~

Save this as hello.scm and run:

~~~sh
./swampscheme hello.scm
~~~

### 1.7 Test the interpreter

~~~sh
make test
make test WITH_SDL3=1
~~~

The second command runs the normal regression suite against the SDL-enabled
binary.

---

## 2. Running programs

| Invocation | Meaning |
|---|---|
| ./swampscheme | Interactive REPL on a terminal; otherwise read stdin |
| ./swampscheme file.scm | Evaluate one or more files |
| ./swampscheme -e 'expression' | Evaluate one expression |

Arguments are available through command-line:

~~~scheme
(display (command-line))
(newline)
~~~

For:

~~~sh
./swampscheme app.scm image.png --debug
~~~

the list is conceptually:

~~~scheme
("./swampscheme" "app.scm" "image.png" "--debug")
~~~

### 2.1 Exit status

~~~scheme
(exit 7)
~~~

The numeric argument becomes the process exit status. quit exits with status
zero. An unhandled interpreter error exits with status one.

### 2.2 Relative source paths

When a file evaluates load or import, the requested path is resolved relative
to the file currently being evaluated. This makes a script directory
relocatable:

~~~text
project/
  main.scm
  lib/
    util.scm
~~~

main.scm can say:

~~~scheme
(load "lib/util.scm")
~~~

Loaded source shares the current environment. This is source inclusion, not a
separate module namespace.

---

## 3. The Swamp Scheme model

### 3.1 Everything is an expression

~~~scheme
42
(+ 10 32)
(if (> score 50) "pass" "try again")
~~~

Several expressions can be grouped with begin:

~~~scheme
(begin
  (display "working")
  (newline)
  (+ 20 22))
~~~

The value of begin is the value of its last expression. An empty begin returns
the empty list.

### 3.2 Values

The main value categories are:

- empty list: ()
- booleans: #t and #f
- numbers, represented internally as C double values
- strings and symbols
- pairs and lists
- vectors
- procedures and closures
- ports
- records
- gap buffers
- hash tables
- opaque SDL texture handles in SDL-enabled builds
- end-of-file object, printed as #<eof>

### 3.3 Truth

Only #f is false. The empty list, zero, empty strings, and all other values
are true:

~~~scheme
(if '() "the empty list is true" "false")
~~~

### 3.4 Numbers

All numbers use the interpreter's floating-point representation. They are not
separated into exact integers, rationals, and inexact numbers as in a full
Scheme numeric tower.

~~~scheme
(+ 1 2.5)
(number->string 12)
~~~

APIs that require dimensions, indexes, or counts convert numeric values to C
integers. Pass integers to those APIs.

### 3.5 Lexical scope

lambda, let, and define create lexical bindings. Closures remember the
environment in which they were created:

~~~scheme
(define (make-counter start)
  (let ((n start))
    (lambda ()
      (set! n (+ n 1))
      n)))

(define counter (make-counter 10))
(counter)
(counter)
~~~

### 3.6 Mutation

Mutation is available through set!, mutable vectors, records, hash tables, and
gap buffers. Functional code using returned values and recursion is often
easier to debug, while mutation is useful for editors and applications.

### 3.7 Equality

eq? and eqv? use the interpreter's identity/simple-value comparison. Use
equal? for structural comparison:

~~~scheme
(equal? '(a) '(a))
~~~

---

## 4. Reading source

### 4.1 Lists and calls

Parentheses form a list. In operator position, a list is normally a procedure
call:

~~~scheme
(display "hello")
(+ 1 2 3)
~~~

### 4.2 Comments

A semicolon starts a comment through the end of the line:

~~~scheme
(define answer 42) ; the answer
~~~

### 4.3 Symbols

Symbols name variables and procedures:

~~~scheme
(define pond "water")
'pond
'(pond frog reed)
~~~

The apostrophe makes the symbol or list data instead of evaluating it.

### 4.4 Strings

Strings use double quotes. The reader recognizes escaped quotes, backslashes,
newlines, and tabs in the ordinary printed representation:

~~~scheme
(display "line one\nline two")
(display "say \"hello\"")
~~~

Strings are replaced through set!; there is no character-level string mutation
procedure.

### 4.5 Booleans

Both spellings are accepted:

~~~scheme
#t #true
#f #false
~~~

### 4.6 Quote and quasiquote

The apostrophe is shorthand for quote:

~~~scheme
'name
(quote name)
~~~

Backquote creates a template. Comma evaluates a portion, and comma-at splices
a proper list:

~~~scheme
(define who "frog")
(quasiquote (hello (unquote who)))
(quasiquote (numbers 1 2 (unquote-splicing (list 3 4))))
~~~

The reader also accepts the short backquote/comma spellings.

### 4.7 Vectors

Literal vector syntax is:

~~~scheme
#(red green blue)
~~~

Use vector, make-vector, and conversion procedures for explicit construction.

---

## 5. Evaluation and special forms

Special forms control evaluation; their operands are not all evaluated like
ordinary procedure arguments.

### quote

~~~scheme
(quote datum)
'datum
~~~

Returns datum without evaluating it.

### quasiquote

~~~scheme
(quasiquote template)
~~~

Evaluates unquote and unquote-splicing portions at the active template depth.

### if

~~~scheme
(if test consequent alternative)
~~~

The alternative is required. Only the selected branch is evaluated.

### begin

~~~scheme
(begin expression ...)
~~~

Evaluate left to right and return the last value.

### define

Variable definition:

~~~scheme
(define answer 42)
~~~

Procedure shorthand:

~~~scheme
(define (square x) (* x x))
~~~

### set!

~~~scheme
(set! variable expression)
~~~

The variable must already be bound.

### lambda

~~~scheme
(lambda (parameters ...) body ...)
~~~

Fixed parameters:

~~~scheme
(lambda (x y) (+ x y))
~~~

Rest parameters:

~~~scheme
(lambda (first . rest) (cons first rest))
~~~

The rest parameter receives a proper list.

### let

Bindings are evaluated in the surrounding environment:

~~~scheme
(let ((x 10) (y 20))
  (+ x y))
~~~

Named let is available for loops:

~~~scheme
(let loop ((n 5) (total 0))
  (if (= n 0)
      total
      (loop (- n 1) (+ total n))))
~~~

### let*

Bindings are sequential:

~~~scheme
(let* ((x 10)
       (y (+ x 5)))
  y)
~~~

### letrec

All names are created before initializers run:

~~~scheme
(letrec ((even? (lambda (n) (if (= n 0) #t (odd? (- n 1)))))
         (odd?  (lambda (n) (if (= n 0) #f (even? (- n 1))))))
  (even? 12))
~~~

### when and unless

~~~scheme
(when test body ...)
(unless test body ...)
~~~

They return the empty list when the body is skipped.

### and and or

and and or short-circuit:

~~~scheme
(and (file-exists? "config") (display "loaded"))
(or (hash-ref options 'name #f) "anonymous")
~~~

No-operand and returns #t. No-operand or returns #f.

### cond

~~~scheme
(cond
  ((< n 0) "negative")
  ((= n 0) "zero")
  (else "positive"))
~~~

The => clause is supported:

~~~scheme
(cond
  ((string->number text) => number->string)
  (else "not a number"))
~~~

### try

Swamp error recovery takes two expressions:

~~~scheme
(try
  (car '())
  (begin
    (display "recovered")
    (last-error)))
~~~

If the first expression raises an interpreter error, the second runs.

### use-modules

use-modules is accepted as a compatibility no-op. It does not load a library
or create a module namespace.

---

## 6. Writing procedures

### 6.1 Ordinary procedures

~~~scheme
(define (greet name)
  (string-append "hello, " name))

(greet "world")
~~~

### 6.2 Variadic procedures

~~~scheme
(define (join-with-space first . rest)
  (string-join (cons first rest) " "))
~~~

### 6.3 Higher-order procedures

~~~scheme
(define (twice f x)
  (f (f x)))

(twice (lambda (n) (+ n 1)) 40)
~~~

map, for-each, and apply are the primary higher-order tools.

### 6.4 Tail-position loops

Ordinary closure calls in tail position are evaluated iteratively:

~~~scheme
(define (countdown n)
  (if (= n 0)
      (display "lift off\n")
      (begin
        (display n)
        (newline)
        (countdown (- n 1)))))
~~~

The project has not audited every path as a complete formal R7RS proper-tail
recursion guarantee. Use named-let or tail-recursive closure loops for large
iterations.

### 6.5 apply

~~~scheme
(apply + '(1 2 3 4))
~~~

The final argument must be a proper list.

---

## 7. Lists, pairs, alists, and property lists

### 7.1 Constructors and selectors

| Procedure | Description |
|---|---|
| cons | Construct a pair |
| car, cdr | Select pair components |
| list | Construct a proper list |
| null?, pair?, list? | Collection predicates |
| length | Length of a proper list |
| reverse | Reversed proper list |
| append | Append proper lists |
| list-ref | Zero-based access |
| map | Map over one or more lists |
| for-each | Apply for side effects |
| caar through cadddr | Common composed selectors |

~~~scheme
(cons 'a 'b)
(list 1 2 3)
(car '(one two))
(cdr '(one two))
~~~

append, length, map, and related procedures require proper lists.

### 7.2 Membership

| Procedure | Comparison |
|---|---|
| memq | Simple/identity comparison |
| memv | Same current comparison family as memq |
| member | Structural equal? comparison |

Successful membership returns the matching tail:

~~~scheme
(member "b" '("a" "b" "c"))
~~~

### 7.3 Association lists

| Procedure | Comparison |
|---|---|
| assq | Simple/identity comparison |
| assv | Same current comparison family as assq |
| assoc | Structural comparison |

~~~scheme
(define settings '((color . "green") (size . 12)))
(cdr (assoc 'color settings))
~~~

### 7.4 Property lists

Swamp events use flat property lists:

~~~scheme
'(type 1 key 42 x 10 y 20)
(plist-ref event 'type)
~~~

plist-ref returns the item after the matching key. Missing keys return #f.

---

## 8. Vectors

Vectors are mutable indexed collections:

~~~scheme
(define v (vector 10 20 30))
(vector-ref v 1)
(vector-set! v 1 99)
(vector->list v)
~~~

| Procedure | Description |
|---|---|
| vector | Construct from values |
| make-vector | Construct with optional fill value |
| vector? | Predicate |
| vector-length | Number of elements |
| vector-ref | Read an element |
| vector-set! | Mutate an element |
| vector-fill! | Fill all elements |
| list->vector | Convert a proper list |
| vector->list | Convert to a proper list |

Indexes are zero-based and checked.

---

## 9. Records

Records are mutable named field containers. They are a Swamp extension, not
R7RS define-record-type records.

~~~scheme
(define frog
  (make-record 'animal
               '((name . "Kermit")
                 (legs . 4))))

(record-ref frog 'name)
(record-set! frog 'legs 4)
(record-fields frog)
~~~

Bare fields begin with #f:

~~~scheme
(make-record 'point '(x y))
~~~

| Procedure | Description |
|---|---|
| record? | Predicate |
| make-record | Construct from type and field list |
| record-type | Return type as a symbol |
| record-fields | Return field names |
| record-ref | Read a field |
| record-set! | Mutate a field |

Unknown fields are errors. Records print as #<record type>.

---

## 10. Hash tables

Swamp hash tables are mutable string-keyed maps. Keys may be symbols or
strings and are normalized by textual name.

~~~scheme
(define h (make-hash-table))
(hash-set! h 'name "swamp")
(hash-set! h "answer" 42)
(hash-ref h 'name)
(hash-ref h 'missing "default")
~~~

| Procedure | Description |
|---|---|
| make-hash-table | Create an empty table |
| hash-table? | Predicate |
| hash-set! | Insert or replace |
| hash-ref | Read, with optional default |
| hash-exists? | Test membership |
| hash-delete! | Delete a key |
| hash-keys | List keys as strings |
| hash-count | Number of entries |

Hash values can be any Scheme value. Keys must be symbols or strings.

---

## 11. Gap buffers

Gap buffers are mutable text structures designed for editor-like workloads.
They keep a movable insertion point while making local edits efficient.

~~~scheme
(define text (make-gap-buffer "hello world"))
(gap-buffer-set-point! text 5)
(gap-buffer-insert! text ", swamp")
(gap-buffer->string text)
~~~

insert! inserts at the current point and advances it. delete! removes a
half-open range [start,end), independent of the current point.

| Procedure | Description |
|---|---|
| make-gap-buffer | Create from a string |
| gap-buffer? | Predicate |
| gap-buffer-length | Length |
| gap-buffer-point | Current insertion point |
| gap-buffer-set-point! | Move insertion point |
| gap-buffer->string | Return complete text |
| gap-buffer-ref | Read one character-like string |
| gap-buffer-substring | Return [start,end) |
| gap-buffer-insert! | Insert at point |
| gap-buffer-delete! | Delete [start,end) |
| gap-buffer-line-count | Count lines |
| gap-buffer-line-number-at | Zero-based line number |
| gap-buffer-line-start | Start offset of a line |
| gap-buffer-line-start-from | Start of containing line |
| gap-buffer-line-end-from | End of containing line |
| gap-buffer-find-next | Next matching offset or #f |
| gap-buffer-find-prev | Previous matching offset or #f |

Offsets are zero-based. The implementation is byte-oriented even when text
contains multibyte UTF-8, so editor code should use one offset convention.

---

## 12. Numbers and mathematics

### 12.1 Arithmetic

| Procedure | Description |
|---|---|
| + | Sum; zero arguments produce 0 |
| - | Subtraction; one argument negates |
| * | Product; zero arguments produce 1 |
| / | Division |
| abs | Absolute value |
| min, max | Minimum or maximum |
| modulo | Sign-adjusted integer modulo |
| remainder | C-style integer remainder |

Division and modulo by zero are errors. modulo and remainder convert operands
to C long values.

### 12.2 Rounding and transcendental functions

~~~scheme
(floor 3.9)
(ceiling 3.1)
(round 3.5)
(truncate -3.9)
(sqrt 9)
(expt 2 10)
(sin angle)
(cos angle)
(tan angle)
(atan y)
(atan y x)
~~~

Trigonometric functions use radians. One-argument atan is arctangent;
two-argument atan is atan2.

### 12.3 Comparisons and predicates

~~~scheme
(= a b ...)
(< a b ...)
(> a b ...)
(<= a b ...)
(>= a b ...)
(zero? n)
(positive? n)
(negative? n)
(odd? n)
(even? n)
~~~

Comparisons accept two or more values and compare pairwise. Fewer than two
values returns #t.

### 12.4 Bitwise operations

~~~scheme
(bitwise-ior a b ...)
(bitwise-and a b ...)
~~~

These are especially useful for termbox flags and modes.

---

## 13. Strings and character-like values

Characters are represented by one-character strings rather than a separate
character type.

### 13.1 String procedures

| Procedure | Description |
|---|---|
| string? | Predicate |
| string-length | Length |
| make-string | Construct with optional fill |
| string-append | Concatenate |
| substring | Return [start,end) |
| string-ref | Read one character-like string |
| string-downcase | Lowercase conversion |
| string-trim-both | Trim C whitespace |
| string-contains | Byte offset or #f |
| string-join | Join a list of strings |
| string-tokenize | Alphanumeric tokenization |
| string->number | Parse or return #f |
| number->string | Format a number |

string-tokenize's optional second argument is accepted for compatibility but
is not a configurable delimiter set; tokens are runs of alphanumeric bytes.

### 13.2 String comparisons

~~~scheme
(string=? a b ...)
(string<? a b ...)
(string>? a b ...)
(string<=? a b ...)
(string>=? a b ...)
~~~

Comparisons use C strcmp ordering.

### 13.3 Character-like procedures

~~~scheme
(char? value)
(char=? a b ...)
(char<? a b ...)
(char>? a b ...)
(char<=? a b ...)
(char>=? a b ...)
(char-alphabetic? ch)
(char-numeric? ch)
(char-whitespace? ch)
(char-upper-case? ch)
(char-lower-case? ch)
(char-upcase ch)
(char-downcase ch)
(char->integer ch)
(integer->char codepoint)
~~~

The predicate accepts strings with byte length one. Unicode conversion uses
termbox's UTF-8 conversion routines; classification and case conversion are
byte/locale-oriented.

---

## 14. Input, output, and ports

### 14.1 Standard output

~~~scheme
(display "text")
(display value)
(newline)
(write value)
(write-char "A")
(flush-output-port)
~~~

display writes strings without quotes and prints other values readably. write
prints strings with quotes and escapes. Both accept an optional output port.

### 14.2 Standard input

~~~scheme
(read)
(read-char)
(peek-char)
(read-line)
~~~

Each accepts an optional input port. End of input returns the EOF object, tested
with eof-object?.

### 14.3 Ports

| Procedure | Description |
|---|---|
| current-input-port | Standard input |
| current-output-port | Standard output |
| open-input-file | Open input |
| open-output-file | Open output, truncating/creating |
| close-port | Close an owned port |
| close-input-port | Alias |
| close-output-port | Alias |
| port? | Predicate |
| input-port? | Predicate |
| output-port? | Predicate |
| flush-output-port | Flush output |

Ports opened by open-input-file and open-output-file own their C FILE*. The
standard ports are not closed by user code.

### 14.4 Whole-file helpers

~~~scheme
(define source (read-file "input.txt"))
(write-file "output.txt" "new contents\n")
~~~

read-file returns a string. write-file returns #t on success.

---

## 15. Files and load paths

### 15.1 Loading source

~~~scheme
(load "library.scm")
(import "library.scm")
(load-relative "library.scm")
~~~

import is a source-loading alias for load. It does not create a namespace.
load-relative currently follows the same resolution behavior and exists to
express intent.

### 15.2 Load-path introspection

~~~scheme
(current-load-directory)
(resolve-load-path "child.scm")
~~~

current-load-directory returns the directory of the currently evaluated file,
or "." at top level.

### 15.3 Filesystem procedures

| Procedure | Description |
|---|---|
| file-exists? | Test existence |
| file-directory? | Test directory |
| file-regular? | Test regular file |
| file-size | Byte size |
| file-mtime | Modification time in Unix seconds |
| directory-files | Sorted names excluding . and .. |
| make-directory | Create a directory |
| delete-file | Remove a file |
| delete-directory | Remove a directory |
| remove-directory | Alias |
| rename-file | Rename a path |

Filesystem errors are fatal unless wrapped in try.

---

## 16. Processes and system integration

### 16.1 Captured subprocess output

~~~scheme
(system "printf" "hello")
~~~

system takes a command and separate arguments, executes directly, captures
standard output, and returns it as a string. A nonzero exit status is an
interpreter error.

### 16.2 Status-aware subprocesses

~~~scheme
(define result (system/status "printf" "swamp"))
(plist-ref result 'ok)
(plist-ref result 'status)
(plist-ref result 'output)
~~~

system/status returns a property list containing ok, status, and output instead
of raising merely because the child failed.

### 16.3 Other integration helpers

~~~scheme
(input-terminal?)
(command-line)
(cpu-temp)
(eztemplate "-c99")
~~~

cpu-temp invokes scripts/cputemp.sh and is environment-specific. eztemplate
invokes the repository's whitelisted template helper.

---

## 17. Errors and recovery

### 17.1 Normal errors

Examples:

~~~text
swampscheme: expected number
swampscheme: car arity/type
swampscheme: unbound variable: name
~~~

At top level an unhandled error ends the process with status one. In the REPL,
the error is reported and the prompt continues.

### 17.2 Raising and recovering

~~~scheme
(error "bad configuration")

(define result
  (try
    (car '())
    (list 'failed (last-error))))
~~~

last-error returns the most recent message as a string. try takes exactly two
expressions: a protected expression and a fallback expression.

### 17.3 Error boundaries

try handles interpreter and native-operation errors that reach the interpreter.
It does not turn C-level crashes, invalid external memory, operating-system
kills, or process termination into Scheme exceptions.

---

## 18. Randomness and time

### 18.1 Random numbers

~~~scheme
(random-source-randomize!)
(random-integer 10)
(random-real)
~~~

random-integer returns 0 through bound-1, or 0 when the bound is not positive.

### 18.2 Time

~~~scheme
(current-time)
(localtime value)
(strftime "%Y-%m-%d" (current-time))
~~~

current-time returns Unix seconds. strftime takes a format and numeric time.
localtime currently returns its input as a compatibility placeholder; it does
not produce a structured time record.

---

## 19. Garbage collection

Swamp Scheme uses a manual mark-and-sweep collector:

~~~scheme
(gc)
(gc-stats)
~~~

gc returns the number of values reclaimed. gc-stats returns heap information in
the runtime's property-list-like format.

Keep live Scheme values reachable through bindings, lists, vectors, records,
hash tables, or closures. Native resources such as ports and SDL textures have
their own lifecycle rules; call explicit close/destroy procedures.

---

## 20. Termbox terminal applications

The termbox bridge is available in the ordinary build through:

~~~scheme
(termbox command ...)
~~~

The command may be a symbol or a string alias:

~~~scheme
(termbox 'clear)
(termbox "tb_clear")
~~~

A convenient wrapper:

~~~scheme
(define (tb command . args)
  (apply termbox (cons command args)))
~~~

### 20.1 Lifecycle

~~~scheme
(tb 'init)
(tb 'clear)
(tb 'present)
(tb 'shutdown)
~~~

Termbox draws into a back buffer. Most changes become visible after present.
Always shut it down so the terminal is restored.

### 20.2 Drawing

~~~scheme
(tb 'goto x y)
(tb 'print text)
(tb 'print-at x y fg bg text)
(tb 'set-cell x y character fg bg)
(tb 'extend-cell x y character)
(tb 'cell-at x y)
~~~

Coordinates are zero-based with (0,0) at the top-left. set-cell accepts a
one-character string or Unicode codepoint number. print-at returns cells
printed.

### 20.3 Cursor and dimensions

~~~scheme
(tb 'width)
(tb 'height)
(tb 'set-cursor x y)
(tb 'hide-cursor)
~~~

### 20.4 Colors and attributes

Constants include:

~~~scheme
TB_DEFAULT TB_BLACK TB_RED TB_GREEN TB_YELLOW
TB_BLUE TB_MAGENTA TB_CYAN TB_WHITE
TB_BOLD TB_UNDERLINE TB_REVERSE TB_ITALIC
~~~

Combine flags:

~~~scheme
(define bright-green (bitwise-ior TB_GREEN TB_BOLD))
(tb 'set-cell 5 5 "@" bright-green TB_DEFAULT)
~~~

Commands:

~~~scheme
(tb 'fg attribute)
(tb 'bg attribute)
(tb 'attrs foreground background)
(tb 'set-clear-attrs foreground background)
~~~

Color names may be symbols or strings.

### 20.5 Modes and events

~~~scheme
(tb 'input-mode (bitwise-ior TB_INPUT_ESC TB_INPUT_MOUSE))
(tb 'output-mode TB_OUTPUT_256)
(define event (tb 'poll-event))
(define event-or-false (tb 'peek-event 100))
~~~

Event constants include TB_EVENT_KEY, TB_EVENT_RESIZE, TB_EVENT_MOUSE,
TB_MOD_ALT, TB_MOD_CTRL, TB_MOD_SHIFT, and TB_MOD_MOTION. Key constants
include escape, enter, space, backspace, tab, control keys, arrows, paging,
function keys, and mouse buttons.

An event is a flat property list with fields such as type, key, ch, mod, w, h,
x, and y:

~~~scheme
(define event (tb 'poll-event))
(cond
  ((= (plist-ref event 'type) TB_EVENT_RESIZE)
   (display "resized"))
  ((= (plist-ref event 'type) TB_EVENT_KEY)
   (display (plist-ref event 'key))))
~~~

### 20.6 Event-loop skeleton

~~~scheme
(define (tb command . args)
  (apply termbox (cons command args)))

(tb tb_init)
(tb 'input-mode (bitwise-ior TB_INPUT_ESC TB_INPUT_MOUSE))
(let loop ()
  (tb 'clear)
  (tb 'print-at 2 2 TB_GREEN TB_DEFAULT "Press Escape to quit")
  (tb 'present)
  (let ((event (tb 'poll-event)))
    (unless (and (= (plist-ref event 'type) TB_EVENT_KEY)
                 (= (plist-ref event 'key) TB_KEY_ESC))
      (loop))))
(tb tb_shutdown)
~~~

See TERMBOX.md and scripts/tui_demo.scm for the longer guide and example.

---

## 21. SDL3, stb_image, and Nuklear

SDL3 support is optional and automatically includes the Nuklear immediate-mode
GUI bridge. Enable it with WITH_SDL3=1.

### 21.1 Initialization

~~~scheme
(sdl3 "init" 800 600 "My Swamp Window")
(sdl3 "shutdown")
~~~

init takes width, height, and an optional title. Calling init again while a
window exists returns true. Cleanup is also attempted at process exit.

### 21.2 Drawing primitives

~~~scheme
(sdl3 "clear" r g b a)
(sdl3 "color" r g b a)
(sdl3 "set-color" r g b a)
(sdl3 "pixel" x y)
(sdl3 "line" x1 y1 x2 y2)
(sdl3 "rect" x y width height)
(sdl3 "fill-rect" x y width height)
(sdl3 "present")
~~~

Colors are RGBA byte values. Coordinates and dimensions are numeric.

### 21.3 Images and textures

texture-load calls the bundled stb_image.h loader and supports its configured
image formats, including common PNG, JPEG, BMP, TGA, GIF, PSD, HDR, and PNM
cases.

~~~scheme
(define logo (sdl3 "texture-load" "assets/logo.png"))
(display (sdl3 "texture-size" logo))
(sdl3 "blit" logo 20 20)
(sdl3 "blit" logo 300 20 128 128)
(sdl3 "texture-destroy" logo)
~~~

Texture values are opaque. Do not treat them as numbers or records. Explicitly
destroy them when finished.

### 21.4 SDL events

Frame input begins with:

~~~scheme
(sdl3 "begin-frame")
~~~

Poll until empty:

~~~scheme
(let loop ()
  (let ((event (sdl3 "event")))
    (when event
      (if (= (plist-ref event 'type) SDL_EVENT_QUIT)
          (set! done #t)
          (loop)))))
~~~

event and poll-event are aliases. Events are forwarded to Nuklear when a
context exists. The returned property list contains type, window, key, x, y,
button, and text. Fields not meaningful for the event type have default/union
values; branch on type first.

Exported constants include SDL_EVENT_QUIT, SDL_EVENT_KEY_DOWN,
SDL_EVENT_KEY_UP, SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN,
SDL_EVENT_MOUSE_BUTTON_UP, and SDL_EVENT_TEXT_INPUT.

### 21.5 Nuklear frame lifecycle

Recommended order:

1. sdl3 begin-frame
2. Poll SDL events until empty.
3. nuklear begin-frame to finalize input.
4. Emit widgets.
5. nuklear render.
6. sdl3 present.

Current widget API:

~~~scheme
(nuklear "begin" title x y width height)
(nuklear "end")
(nuklear "row" height columns)
(nuklear "label" text)
(nuklear "button" text)
(nuklear "render")
~~~

begin returns true when visible and active. button returns true when activated
during the current frame.

### 21.6 Fonts

~~~scheme
(nuklear "font-load" "/usr/share/fonts/Adwaita/AdwaitaMono-Bold.ttf" 18)
~~~

Nuklear font baking uses its embedded stb_truetype implementation. The
repository also carries stb_truetype.h for future direct text-rendering
features; it should not be separately compiled into the current interpreter
because Nuklear already provides the implementation in this build.

### 21.7 Complete SDL frame loop

~~~scheme
(define running #t)
(sdl3 "init" 800 500 "Swamp SDL")
(nuklear "font-load" "assets/regular.ttf" 16)

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
    (nuklear "label" "A Scheme-controlled window")
    (nuklear "row" 30 1)
    (when (nuklear "button" "Quit")
      (set! running #f))
    (nuklear "end"))
  (nuklear "render")
  (sdl3 "present")
  (when running (loop)))
(sdl3 "shutdown")
~~~

See scripts/sdl3_demo.scm for rectangle drawing and optional image loading.

---

## 22. Building and distributing programs

The Makefile recognizes:

~~~sh
make CC=clang
make CFLAGS='-std=c99 -O0 -g -Wall -Wextra -pedantic'
make WITH_SDL3=1
~~~

The ordinary build contains vendored termbox2. The SDL build additionally
links SDL3. Nuklear and stb_image/stb_truetype are in the source tree. A
machine running the SDL binary still needs the SDL3 runtime shared library.

Because both variants use the same output path, clean when switching:

~~~sh
make clean && make
make clean && make WITH_SDL3=1
~~~

Prefer source-relative load paths and do not assume the working directory is
the script directory.

---

## 23. R7RS compatibility

Swamp Scheme resembles a compact R5RS/R7RS-style language but is not a
conformance implementation.

Implemented or approximately implemented:

- S-expression reader for lists, vectors, symbols, booleans, numbers, strings,
  quote, and quasiquote
- lexical environments and closures
- common arithmetic, comparisons, pairs, lists, strings, vectors, ports, and
  filesystem procedures
- define, set!, lambda, if, begin, let, let*, letrec, when, unless, and, or,
  cond, and the Swamp try form
- map, for-each, apply, read, write, display, and basic port operations

Important differences:

- no hygienic macro system; define-syntax and syntax-rules are unavailable;
- no full R7RS library system; import is a source-loading alias for load;
- `cond-expand` supports implementation selection for `swampscheme` plus
  compound `and`/`or`/`not` requirements, but does not provide library tests;
- numbers are C doubles;
- characters are one-character strings;
- no bytevectors or advanced port modes;
- records are Swamp records, not define-record-type records;
- exceptions use try, error, and last-error;
- use-modules is a no-op compatibility form;
- the reader does not implement every R7RS lexical feature;
- practical tail loops exist, but this is not a complete audited proper-tail
  recursion guarantee;
- localtime is currently a compatibility placeholder.

See R7RS.md for the shorter project status summary.

---

## 24. Patterns and complete examples

### 24.1 Command-line filter

~~~scheme
(define (process line)
  (let ((clean (string-trim-both line)))
    (unless (string=? clean "")
      (display (string-downcase clean))
      (newline))))

(let loop ()
  (let ((line (read-line)))
    (unless (eof-object? line)
      (process line)
      (loop))))
~~~

Run with:

~~~sh
printf 'Hello\nSWAMP\n' | ./swampscheme filter.scm
~~~

### 24.2 Configuration record

~~~scheme
(define config
  (make-record 'config
               '((host . "localhost")
                 (port . 8080)
                 (debug . #f))))

(define (config-ref key)
  (record-ref config key))

(when (config-ref 'debug)
  (display "debug enabled\n"))
~~~

### 24.3 Mutable hash-backed work queue

~~~scheme
(define queue (make-hash-table))
(hash-set! queue 'first '(one two three))
(hash-set! queue 'second '(four five))

(for-each
 (lambda (key)
   (display key)
   (display ": ")
   (write (hash-ref queue key))
   (newline))
 (hash-keys queue))
~~~

### 24.4 Text editor operation

~~~scheme
(define buffer (make-gap-buffer "one two three"))
(define start (gap-buffer-find-next buffer "two" 0))
(when start
  (gap-buffer-delete! buffer start (+ start 3))
  (gap-buffer-set-point! buffer start)
  (gap-buffer-insert! buffer "swamp"))
(display (gap-buffer->string buffer))
(newline)
~~~

### 24.5 Safe file fallback

~~~scheme
(define (read-or-default path fallback)
  (try
    (read-file path)
    (begin
      (display "could not read ")
      (display path)
      (display ": ")
      (display (last-error))
      (newline)
      fallback)))

(define settings (read-or-default "settings.txt" "defaults"))
~~~

### 24.6 Termbox status screen

~~~scheme
(define (tb command . args)
  (apply termbox (cons command args)))

(tb tb_init)
(tb 'clear)
(tb 'hide-cursor)
(tb 'print-at 2 1 (bitwise-ior TB_CYAN TB_BOLD) TB_DEFAULT
    "Swamp status")
(tb 'print-at 2 3 TB_WHITE TB_DEFAULT
    (string-append "terminal: "
                   (number->string (tb 'width))
                   "x"
                   (number->string (tb 'height))))
(tb 'present)
(tb 'poll-event)
(tb tb_shutdown)
~~~

### 24.7 SDL/Nuklear application

~~~scheme
(define running #t)
(sdl3 "init" 640 400 "Swamp application")
(nuklear "font-load" "assets/regular.ttf" 16)

(let loop ()
  (sdl3 "clear" 25 30 40 255)
  (sdl3 "begin-frame")
  (let ((event (sdl3 "event")))
    (when event
      (if (= (plist-ref event 'type) SDL_EVENT_QUIT)
          (set! running #f)
          #f)))
  (nuklear "begin-frame")
  (when (nuklear "begin" "Swamp" 20 20 280 160)
    (nuklear "row" 24 1)
    (nuklear "label" "controlled by Scheme")
    (nuklear "row" 24 1)
    (when (nuklear "button" "Quit")
      (set! running #f))
    (nuklear "end"))
  (nuklear "render")
  (sdl3 "present")
  (when running (loop)))
(sdl3 "shutdown")
~~~

---

## 25. Troubleshooting

### unbound variable: sdl3

The binary was built without SDL support:

~~~sh
make clean
make WITH_SDL3=1
~~~

### SDL initialization or window creation fails

Check SDL3 installation and display access. On headless Linux, a configured
SDL video driver or virtual display may be needed. A dummy-driver smoke test:

~~~sh
SDL_VIDEODRIVER=dummy ./swampscheme -e \
  '(begin (sdl3 "init" 320 240) (sdl3 "clear" 0 0 0 255) (sdl3 "present") (sdl3 "shutdown"))'
~~~

### pkg-config cannot find SDL3

Install the SDL3 development package, not just the runtime package:

~~~sh
pkg-config --cflags --libs sdl3
~~~

### Terminal corruption after a termbox program

Call tb_shutdown on every normal exit path. The runtime also cleans up at
process exit, but explicit shutdown is best practice.

### expected number, expected string, or arity

Native primitives validate arguments strictly. Check both count and type.
Numeric-looking strings remain strings until string->number is used.

### unbound variable after loading a helper

load evaluates in the current environment; it does not create a module. Check:

~~~scheme
(display (resolve-load-path "helper.scm"))
(newline)
~~~

### Image path works on one machine but not another

stb_image is compiled into the SDL-enabled interpreter, but paths, permissions,
format, and working directory still matter. Use an absolute path while
diagnosing, and check current-load-directory for script-relative paths.

### REPL output is noisy for a terminal program

The REPL prints evaluated results. File execution does not print every result.
In termbox mode, the runtime suppresses some empty-list display noise to keep
the terminal UI usable.

### Missing R7RS feature

Check R7RS.md and this manual. Common replacements are load for libraries,
try for guard, one-character strings for standard characters, and local helper
procedures instead of macros.

---

## 26. Quick reference

### Special forms

~~~text
quote quasiquote if begin define set! lambda
let let* letrec when unless and or cond try use-modules
~~~

### Core procedures

~~~text
+ - * / abs min max floor ceiling round truncate sqrt expt sin cos tan atan
modulo remainder zero? positive? negative? odd? even? = < > <= >=
bitwise-ior bitwise-and
cons car cdr list caar cdar cadr cddr caddr cadddr
map for-each append length reverse list-ref
memq memv member assq assv assoc plist-ref apply
~~~

### Predicates and equality

~~~text
null? pair? list? symbol? string? number? boolean? vector? eof-object?
not eq? eqv? equal?
~~~

### Strings and characters

~~~text
string-length make-string string-append substring string-ref
string-downcase string-trim-both string-contains string-join string-tokenize
string=? string<? string>? string<=? string>=?
char? char=? char<? char>? char<=? char>=?
char-alphabetic? char-numeric? char-whitespace?
char-upper-case? char-lower-case? char-upcase char-downcase
integer->char char->integer number->string string->number
~~~

### Collections

~~~text
vector make-vector vector? vector-length vector-ref vector-set! vector-fill!
list->vector vector->list
make-record record? record-type record-fields record-ref record-set!
make-hash-table hash-table? hash-set! hash-ref hash-exists? hash-delete!
hash-keys hash-count
make-gap-buffer gap-buffer? gap-buffer-length gap-buffer-point
gap-buffer-set-point! gap-buffer->string gap-buffer-ref gap-buffer-substring
gap-buffer-insert! gap-buffer-delete! gap-buffer-line-count
gap-buffer-line-number-at gap-buffer-line-start gap-buffer-line-start-from
gap-buffer-line-end-from gap-buffer-find-next gap-buffer-find-prev
~~~

### I/O and filesystem

~~~text
display newline write read read-char peek-char write-char read-line
current-input-port current-output-port open-input-file open-output-file
close-port close-input-port close-output-port flush-output-port
port? input-port? output-port?
read-file write-file load import load-relative
current-load-directory resolve-load-path
directory-files file-exists? file-directory? file-regular? file-size file-mtime
make-directory delete-file delete-directory remove-directory rename-file
~~~

### Runtime and operating system

~~~text
system system/status command-line input-terminal? cpu-temp eztemplate
error last-error try gc gc-stats quit exit
random-source-randomize! random-integer random-real
current-time localtime strftime
~~~

### Optional UI bridges

~~~text
termbox termbox-demo
sdl3 nuklear
~~~

For command-level details, see TERMBOX.md and SDL3.md.

---

## Closing note

Swamp Scheme is intentionally small enough that the interpreter, examples, and
manual can be read together. When a feature is missing, the project often
favors a direct, understandable extension over a large abstraction. Make the
language useful, keep the runtime visible, and leave room for the next
experiment.
