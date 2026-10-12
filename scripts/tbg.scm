(define (tb cmd . args)
  (apply termbox (cons cmd args)))

(define D-FG TB_WHITE)
(define D-BG TB_DEFAULT)
(define CURSOR-BG TB_GREEN)
(define STATUS-FG (bitwise-ior TB_WHITE TB_BOLD))
(define STATUS-BG 237)
(define MINI-BG TB_CYAN)
(define TAB-WIDTH 3)

(define CTRL-A 1)
(define CTRL-C 3)
(define CTRL-D 4)
(define CTRL-E 5)
(define CTRL-F 6)
(define CTRL-G 7)
(define CTRL-K 11)
(define CTRL-L 12)
(define CTRL-R 18)
(define CTRL-S 19)
(define CTRL-W 23)
(define CTRL-X 24)
(define CTRL-Y 25)
(define ENTER 13)

(define editor
  (make-record 'editor
    '((filename . "tbg.txt")
      (buffer . #f)
      (point . 0)
      (scroll-line . 0)
      (scroll-x . 0)
      (preferred-col . 0)
      (modified . #f)
      (status . "C-x C-s save  C-x C-f find-file  C-x C-c quit  C-s search")
      (kill-ring . "")
      (search-term . "")
      (mark-active . #f)
      (mark-pos . 0)
      (mode . text)
      (syntax-mode . text)
      (dired-dir . "."))))

(define (state field) (record-ref editor field))
(define (set-state! field value) (record-set! editor field value))

(define (filename) (state 'filename))
(define (set-filename! value) (set-state! 'filename value))
(define (buffer) (state 'buffer))
(define (set-buffer! value) (set-state! 'buffer value))
(define (point) (state 'point))
(define (set-point! value) (set-state! 'point value))
(define (scroll-line) (state 'scroll-line))
(define (set-scroll-line! value) (set-state! 'scroll-line value))
(define (scroll-x) (state 'scroll-x))
(define (set-scroll-x! value) (set-state! 'scroll-x value))
(define (preferred-col) (state 'preferred-col))
(define (set-preferred-col! value) (set-state! 'preferred-col value))
(define (modified) (state 'modified))
(define (set-modified! value) (set-state! 'modified value))
(define (status) (state 'status))
(define (set-status! value) (set-state! 'status value))
(define (kill-ring) (state 'kill-ring))
(define (set-kill-ring! value) (set-state! 'kill-ring value))
(define (search-term) (state 'search-term))
(define (set-search-term! value) (set-state! 'search-term value))
(define (mark-active) (state 'mark-active))
(define (set-mark-active! value) (set-state! 'mark-active value))
(define (mark-pos) (state 'mark-pos))
(define (set-mark-pos! value) (set-state! 'mark-pos value))
(define (mode) (state 'mode))
(define (set-mode! value) (set-state! 'mode value))
(define (dired-dir) (state 'dired-dir))
(define (set-dired-dir! value) (set-state! 'dired-dir value))
(define (text-mode?) (eq? (mode) 'text))
(define (syntax-mode) (state 'syntax-mode))
(define (set-syntax-mode! value) (set-state! 'syntax-mode value))

;; Syntax highlighting follows the small, deliberately forgiving lexer in
;; tbg.c.  Colors are attributes rather than hard-coded terminal escapes, so
;; the same source works with termbox's normal and 256-color output modes.
(define syntax-keywords
  '((c
     ("auto" "break" "case" "char" "const" "continue" "default"
      "do" "double" "else" "enum" "extern" "float" "for" "goto"
      "if" "inline" "int" "long" "register" "restrict" "return"
      "short" "signed" "sizeof" "static" "struct" "switch" "typedef"
      "union" "unsigned" "void" "volatile" "while"))
    (python
     ("False" "None" "True" "and" "as" "assert" "async" "await"
      "break" "class" "continue" "def" "del" "elif" "else" "except"
      "finally" "for" "from" "global" "if" "import" "in" "is"
      "lambda" "nonlocal" "not" "or" "pass" "raise" "return" "try"
      "while" "with" "yield"))
    (lisp
     ("and" "begin" "case" "cond" "define" "defmacro" "defun" "do"
      "else" "if" "lambda" "let" "let*" "letrec" "nil" "or" "progn"
      "quote" "set!" "setq" "t"))
    (sh
     ("if" "then" "else" "elif" "fi" "for" "while" "until" "do"
      "done" "case" "esac" "in" "function" "return" "local" "export"
      "unset" "shift" "set" "echo" "printf" "read" "source" "cd" "pwd"
      "true" "false" "test" "break" "continue" "mkdir" "rm" "mv" "cp"
      "touch" "chmod" "grep" "sed" "awk" "cut" "sort" "find" "cat"))
    (lua
     ("and" "break" "do" "else" "elseif" "end" "false" "for" "function"
      "goto" "if" "in" "local" "nil" "not" "or" "repeat" "return"
      "then" "true" "until" "while"))))

(define (syntax-keywords-for m)
  (let loop ((xs syntax-keywords))
    (cond
      ((null? xs) '())
      ((eq? (car (car xs)) m) (car (cdr (car xs))))
      (else (loop (cdr xs))))))

(define (syntax-color-for m)
  (cond
    ((eq? m 'c) TB_CYAN)
    ((eq? m 'python) TB_YELLOW)
    ((eq? m 'lisp) TB_GREEN)
    ((eq? m 'sh) TB_RED)
    ((eq? m 'lua) TB_CYAN)
    (else D-FG)))

(define (syntax-comment-mode? m)
  (or (eq? m 'c) (eq? m 'python) (eq? m 'sh) (eq? m 'lua)
      (eq? m 'lisp)))

(define (syntax-prefix-at? idx prefix)
  (and (<= (+ idx (string-length prefix)) (buflen))
       (equal? prefix (gap-buffer-substring (buffer) idx
                                             (+ idx (string-length prefix))))))

(define (syntax-word-char? c)
  (or (member c '("0" "1" "2" "3" "4" "5" "6" "7" "8" "9"))
      (member c '("A" "B" "C" "D" "E" "F" "G" "H" "I" "J"
                  "K" "L" "M" "N" "O" "P" "Q" "R" "S" "T" "U"
                  "V" "W" "X" "Y" "Z"))
      (member c '("a" "b" "c" "d" "e" "f" "g" "h" "i" "j"
                  "k" "l" "m" "n" "o" "p" "q" "r" "s" "t" "u"
                  "v" "w" "x" "y" "z"))
      (equal? c "_")))

(define (syntax-boundary? idx)
  (or (= idx 0)
      (not (syntax-word-char? (ch (- idx 1))))))

(define (syntax-after-boundary? idx word)
  (or (= (+ idx (string-length word)) (buflen))
      (not (syntax-word-char? (ch (+ idx (string-length word)))))))

(define (syntax-keyword-covering? idx word)
  ;; The renderer visits one cell at a time.  Check possible keyword starts
  ;; behind the current cell so every character in "int" or "return" gets
  ;; the keyword color, not just the first character.
  (let loop ((start (max 0 (- idx (- (string-length word) 1)))))
    (cond
      ((> start idx) #f)
      ((and (syntax-prefix-at? start word)
            (syntax-boundary? start)
            (syntax-after-boundary? start word)
            (< idx (+ start (string-length word)))) #t)
      (else (loop (+ start 1))))))

(define (syntax-keyword-at? idx words)
  (cond
    ((null? words) #f)
    ((syntax-keyword-covering? idx (car words)) #t)
    (else (syntax-keyword-at? idx (cdr words)))))

;; Return the lexical state immediately before IDX: 0 normal, 1 block
;; comment, 2 string, 3 line comment.  Scanning from the beginning keeps highlighting correct
;; when the viewport starts in the middle of a multi-line C comment.
(define (syntax-state-before idx)
  (let loop ((i 0) (state 0) (quote #f))
    (cond
      ((>= i idx) state)
      ((newline? (ch i)) (loop (+ i 1) (if (= state 2) 0 state) #f))
      ((= state 1)
       (if (and (equal? (ch i) "*")
                (< (+ i 1) idx)
                (equal? (ch (+ i 1)) "/"))
           (loop (+ i 2) 0 #f)
           (loop (+ i 1) 1 #f)))
      ((= state 2)
       (if (and (equal? (ch i) quote)
                (or (= i 0) (not (equal? (ch (- i 1)) "\\"))))
           (loop (+ i 1) 0 #f)
           (loop (+ i 1) 2 quote)))
      ((and (eq? (syntax-mode) 'c) (syntax-prefix-at? i "/*"))
       (loop (+ i 2) 1 #f))
      ((and (eq? (syntax-mode) 'c) (syntax-prefix-at? i "//"))
       (let skip ((j i))
         (cond
           ((>= j idx) (loop j 3 #f))
           ((newline? (ch j)) (loop j 0 #f))
           (else (skip (+ j 1))))))
      ((and (eq? (syntax-mode) 'lua) (syntax-prefix-at? i "--"))
       (let skip ((j i))
         (cond
           ((>= j idx) (loop j 3 #f))
           ((newline? (ch j)) (loop j 0 #f))
           (else (skip (+ j 1))))))
      ((and (or (eq? (syntax-mode) 'python)
                (eq? (syntax-mode) 'sh))
            (equal? (ch i) "#"))
       (let skip ((j i))
         (cond
           ((>= j idx) (loop j 3 #f))
           ((newline? (ch j)) (loop j 0 #f))
           (else (skip (+ j 1))))))
      ((and (eq? (syntax-mode) 'lisp) (equal? (ch i) ";"))
       (let skip ((j i))
         (cond
           ((>= j idx) (loop j 3 #f))
           ((newline? (ch j)) (loop j 0 #f))
           (else (skip (+ j 1))))))
      ((equal? (ch i) "\"") (loop (+ i 1) 2 "\""))
      (else (loop (+ i 1) 0 #f)))))

(define (syntax-color idx)
  (let ((m (syntax-mode))
        (state (syntax-state-before idx)))
    (cond
      ((= state 1) TB_MAGENTA)
      ((= state 2) TB_YELLOW)
      ((= state 3) TB_MAGENTA)
      ((and (eq? m 'c) (or (syntax-prefix-at? idx "/*")
                           (syntax-prefix-at? idx "//"))) TB_MAGENTA)
      ((and (eq? m 'lua) (syntax-prefix-at? idx "--")) TB_MAGENTA)
      ((and (or (eq? m 'python) (eq? m 'sh))
            (equal? (ch idx) "#")) TB_MAGENTA)
      ((and (eq? m 'lisp) (equal? (ch idx) ";")) TB_MAGENTA)
      ((equal? (ch idx) "\"") TB_YELLOW)
      ((syntax-keyword-at? idx (syntax-keywords-for m))
       (syntax-color-for m))
      (else D-FG))))

(define (detect-syntax-mode path)
  (cond
    ((or (equal? path "Makefile") (equal? path "makefile")
         (equal? path "GNUmakefile")) 'c)
    ((or (string-suffix? ".c" path) (string-suffix? ".h" path)) 'c)
    ((string-suffix? ".py" path) 'python)
    ((or (string-suffix? ".scm" path) (string-suffix? ".ss" path)
         (string-suffix? ".lisp" path) (string-suffix? ".el" path)) 'lisp)
    ((or (string-suffix? ".sh" path) (string-suffix? ".bash" path)
         (string-suffix? ".zsh" path)) 'sh)
    ((string-suffix? ".lua" path) 'lua)
    (else 'text)))

(define mx-commands
  '("delete-char"
    "describe-bindings"
    "dired"
    "find-file"
    "goto-line"
    "help"
    "help-commands"
    "help-keys"
    "isearch-backward"
    "isearch-forward"
    "kill-region"
    "kill-ring-save"
    "kill-line"
    "quit"
    "redraw-screen"
    "save-buffer"
    "text-mode"
    "c-mode"
    "python-mode"
    "lisp-mode"
    "sh-mode"
    "lua-mode"
    "write-file"
    "yank"))

(define (min a b) (if (< a b) a b))
(define (max a b) (if (> a b) a b))
(define (clamp n lo hi) (max lo (min n hi)))
(define (buffer-string) (gap-buffer->string (buffer)))
(define (buflen) (gap-buffer-length (buffer)))
(define (ch i) (gap-buffer-ref (buffer) i))
(define (newline? s) (equal? s "\n"))
(define (tab? s) (equal? s "\t"))
(define (space n) (make-string (max 0 n) " "))

(define (safe-substring s a b)
  (let ((n (string-length s)))
    (substring s (clamp a 0 n) (clamp b 0 n))))

(define (pad s width)
  (safe-substring (string-append s (space width)) 0 width))

(define (set-status s)
  (set-status! s))

(define (open-buffer path)
  (set-filename! path)
  (if (file-exists? path)
      (begin
        (set-buffer! (make-gap-buffer (read-file path)))
        (set-status (string-append "Opened " path)))
      (begin
        (set-buffer! (make-gap-buffer ""))
        (set-status (string-append "New file " path))))
  (set-point! 0)
  (set-scroll-line! 0)
  (set-scroll-x! 0)
  (set-preferred-col! 0)
  (set-mark-active! #f)
  (set-mark-pos! 0)
  (set-mode! 'text)
  (set-syntax-mode! (detect-syntax-mode path))
  (set-modified! #f))

(define (string-suffix? suffix s)
  (let ((n (string-length suffix))
        (m (string-length s)))
    (and (<= n m)
         (equal? suffix (substring s (- m n) m)))))

(define (path-join dir name)
  (cond
    ((equal? dir "/") (string-append "/" name))
    ((string-suffix? "/" dir) (string-append dir name))
    (else (string-append dir "/" name))))

(define (parent-dir path)
  (let loop ((i (- (string-length path) 1)) (last #f))
    (cond
      ((< i 0) ".")
      ((and (> i 0) (equal? (string-ref path i) "/"))
       (if (= i 0) "/" (substring path 0 i)))
      (else (loop (- i 1) last)))))

(define (dired-entry-line dir name)
  (let ((full (path-join dir name)))
    (string-append
      (if (file-directory? full) "d " "- ")
      name
      (if (file-directory? full) "/" "")
      "\n")))

(define (dired-listing dir)
  (let loop ((xs (directory-files dir)) (out "../\n"))
    (if (null? xs)
        out
        (loop (cdr xs) (string-append out (dired-entry-line dir (car xs)))))))

(define (open-dired dir)
  (set-filename! (string-append "dired: " dir))
  (set-buffer! (make-gap-buffer (dired-listing dir)))
  (set-point! 0)
  (set-scroll-line! 0)
  (set-scroll-x! 0)
  (set-preferred-col! 0)
  (set-mark-active! #f)
  (set-mark-pos! 0)
  (set-modified! #f)
  (set-mode! 'dired)
  (set-syntax-mode! 'text)
  (set-dired-dir! dir)
  (set-status (string-append "Dired " dir)))

(define (line-start-from i)
  (gap-buffer-line-start-from (buffer) i))

(define (line-end-from i)
  (gap-buffer-line-end-from (buffer) i))

(define (current-line-text)
  (gap-buffer-substring (buffer) (line-start-from (point)) (line-end-from (point))))

(define (line-number-at i)
  (gap-buffer-line-number-at (buffer) i))

(define (line-start line)
  (gap-buffer-line-start (buffer) line))

(define (line-count)
  (gap-buffer-line-count (buffer)))

(define (cursor-col)
  (- (point) (line-start-from (point))))

(define (set-point-from-line-col line col)
  (let* ((start (line-start (clamp line 0 (- (line-count) 1))))
         (end (line-end-from start)))
    (set-point! (+ start (clamp col 0 (- end start))))))

(define (set-point-from-index idx)
  (set-point! (clamp idx 0 (buflen)))
  (set-preferred-col! (cursor-col)))

(define (insert-text s)
  (set-mark-active! #f)
  (gap-buffer-set-point! (buffer) (point))
  (gap-buffer-insert! (buffer) s)
  (set-point! (+ (point) (string-length s)))
  (set-preferred-col! (cursor-col))
  (set-modified! #t))

(define (delete-range start end)
  (when (< start end)
    (gap-buffer-delete! (buffer) start end)
    (set-point! start)
    (set-preferred-col! (cursor-col))
    (set-modified! #t)))

(define (delete-backward)
  (when (> (point) 0)
    (set-mark-active! #f)
    (delete-range (- (point) 1) (point))))

(define (delete-forward)
  (when (< (point) (buflen))
    (set-mark-active! #f)
    (delete-range (point) (+ (point) 1))))

(define (region-lo)
  (if (< (mark-pos) (point)) (mark-pos) (point)))

(define (region-hi)
  (if (> (mark-pos) (point)) (mark-pos) (point)))

(define (region-active?)
  (and (mark-active) (< (region-lo) (region-hi))))

(define (copy-region)
  (if (region-active?)
      (let ((lo (region-lo))
            (hi (region-hi)))
        (set-kill-ring! (gap-buffer-substring (buffer) lo hi))
        (set-mark-active! #f)
        (set-status "Copied"))
      (set-status "No region selected")))

(define (kill-region)
  (if (region-active?)
      (let ((lo (region-lo))
            (hi (region-hi)))
        (set-kill-ring! (gap-buffer-substring (buffer) lo hi))
        (set-mark-active! #f)
        (delete-range lo hi)
        (set-status "Cut"))
      (set-status "No region selected")))

(define (kill-line)
  (set-mark-active! #f)
  (let* ((start (point))
         (end (line-end-from (point))))
    (if (= start end)
        (when (< end (buflen))
          (set-kill-ring! "\n")
          (delete-range end (+ end 1)))
        (begin
          (set-kill-ring! (gap-buffer-substring (buffer) start end))
          (delete-range start end))))
  (set-status "Killed line"))

(define (yank)
  (set-mark-active! #f)
  (if (> (string-length (kill-ring)) 0)
      (begin
        (insert-text (kill-ring))
        (set-status "Yanked"))
      (set-status "Kill ring is empty")))

(define (move-left)
  (when (> (point) 0) (set-point! (- (point) 1)))
  (set-preferred-col! (cursor-col)))

(define (move-right)
  (when (< (point) (buflen)) (set-point! (+ (point) 1)))
  (set-preferred-col! (cursor-col)))

(define (move-up)
  (let ((line (line-number-at (point))))
    (when (> line 0)
      (set-point-from-line-col (- line 1) (preferred-col)))))

(define (move-down)
  (let ((line (line-number-at (point))))
    (when (< line (- (line-count) 1))
      (set-point-from-line-col (+ line 1) (preferred-col)))))

(define (go-bol)
  (set-point! (line-start-from (point)))
  (set-preferred-col! 0))

(define (go-eol)
  (set-point! (line-end-from (point)))
  (set-preferred-col! (cursor-col)))

(define (go-buffer-start)
  (set-point-from-index 0))

(define (go-buffer-end)
  (set-point-from-index (buflen)))

(define (strip-trailing-slash s)
  (if (and (> (string-length s) 1) (string-suffix? "/" s))
      (substring s 0 (- (string-length s) 1))
      s))

(define (dired-selected-name)
  (let ((line (current-line-text)))
    (cond
      ((equal? line "../") "..")
      ((< (string-length line) 3) #f)
      (else (strip-trailing-slash (substring line 2 (string-length line)))))))

(define (dired-open-selected)
  (let ((name (dired-selected-name)))
    (if name
        (let ((path (if (equal? name "..")
                        (parent-dir (dired-dir))
                        (path-join (dired-dir) name))))
          (if (file-directory? path)
              (open-dired path)
              (open-buffer path)))
        (set-status "No dired entry"))))

(define (page-up)
  (let ((step (max 1 (- (tb 'height) 2))))
    (set-scroll-line! (max 0 (- (scroll-line) step)))
    (set-point-from-line-col (scroll-line) (preferred-col))))

(define (page-down)
  (let ((step (max 1 (- (tb 'height) 2))))
    (set-scroll-line! (min (- (line-count) 1) (+ (scroll-line) step)))
    (set-point-from-line-col (scroll-line) (preferred-col))))

(define (ensure-scroll)
  (let* ((h (tb 'height))
         (w (tb 'width))
         (edit-h (max 1 (- h 2)))
         (line (line-number-at (point)))
         (col (cursor-col)))
    (when (< line (scroll-line))
      (set-scroll-line! line))
    (when (>= line (+ (scroll-line) edit-h))
      (set-scroll-line! (+ 1 (- line edit-h))))
    (when (< col (scroll-x))
      (set-scroll-x! col))
    (when (>= col (+ (scroll-x) w))
      (set-scroll-x! (+ 1 (- col w))))))

(define (line-fragment line width)
  (let* ((start (line-start line))
         (end (line-end-from start))
         (from (+ start (scroll-x)))
         (to (min end (+ from width))))
    (if (< from end) (gap-buffer-substring (buffer) from to) "")))

(define (display-char c)
  (if (tab? c) " " c))

(define (draw-line-char screen-x screen-y idx)
  (if (and (region-active?) (>= idx (region-lo)) (< idx (region-hi)))
      (tb 'set-cell screen-x screen-y (display-char (ch idx)) TB_WHITE TB_BLUE)
      (tb 'set-cell screen-x screen-y (display-char (ch idx))
          (syntax-color idx) D-BG)))

(define (draw-line screen-y line width)
  (let* ((start (line-start line))
         (end (line-end-from start))
         (from (+ start (scroll-x))))
    (tb 'print-at 0 screen-y D-FG D-BG (space width))
    (let loop ((x 0) (idx from))
      (when (and (< x width) (< idx end))
        (draw-line-char x screen-y idx)
        (loop (+ x 1) (+ idx 1))))))

(define (draw-lines edit-h width)
  (let loop ((y 0))
    (when (< y edit-h)
      (draw-line y (+ (scroll-line) y) width)
      (loop (+ y 1)))))

(define (status-left)
  (string-append
    " [1/1] "
    (if (modified) "** " "")
    (filename)
    " "))

(define (status-right)
  (string-append
    " Ln "
    (number->string (+ 1 (line-number-at (point))))
    "/"
    (number->string (line-count))
    ", Col "
    (number->string (+ 1 (cursor-col)))
    " "))

(define (draw-status width y)
  (let* ((left (status-left))
         (right (status-right))
         (gap (- width (string-length left) (string-length right))))
    (tb 'print-at 0 y STATUS-FG STATUS-BG
        (pad (string-append left (space gap) right) width))))

(define (draw-message width y)
  (tb 'print-at 0 y D-FG D-BG (pad (status) width)))

(define (cursor-char)
  (if (< (point) (buflen))
      (let ((c (ch (point))))
        (if (or (newline? c) (tab? c)) "_" c))
      "_"))

(define (draw)
  (ensure-scroll)
  (let* ((w (tb 'width))
         (h (tb 'height))
         (edit-h (max 1 (- h 2)))
         (line (line-number-at (point)))
         (x (clamp (- (cursor-col) (scroll-x)) 0 (- w 1)))
         (y (clamp (- line (scroll-line)) 0 (- edit-h 1))))
    (tb 'set-clear-attrs D-FG D-BG)
    (tb tb_clear)
    (draw-lines edit-h w)
    (draw-status w edit-h)
    (draw-message w (+ edit-h 1))
    (tb 'set-cell x y (cursor-char) D-FG CURSOR-BG)
    (tb 'hide-cursor)
    (tb tb_present)))

(define (event-key ev) (plist-ref ev 'key 0))
(define (event-ch ev) (plist-ref ev 'ch 0))
(define (event-type ev) (plist-ref ev 'type 0))
(define (event-mod ev) (plist-ref ev 'mod 0))
(define (event-x ev) (plist-ref ev 'x 0))
(define (event-y ev) (plist-ref ev 'y 0))

(define (save-buffer)
  (write-file (filename) (buffer-string))
  (set-modified! #f)
  (set-status (string-append "Saved " (filename))))

(define (find-next-from needle start)
  (gap-buffer-find-next (buffer) needle start))

(define (find-prev-before needle before)
  (gap-buffer-find-prev (buffer) needle before))

(define (search-forward needle)
  (if (= (string-length needle) 0)
      (set-status "Empty search")
      (let* ((from (min (buflen) (+ (point) 1)))
             (hit (find-next-from needle from)))
        (if hit
            (begin
              (set-search-term! needle)
              (set-point-from-index hit)
              (set-status (string-append "Search: " needle)))
            (let ((wrapped (find-next-from needle 0)))
              (if wrapped
                  (begin
                    (set-search-term! needle)
                    (set-point-from-index wrapped)
                    (set-status (string-append "Search wrapped: " needle)))
                  (set-status (string-append "No match for " needle))))))))

(define (search-backward needle)
  (if (= (string-length needle) 0)
      (set-status "Empty search")
      (let ((hit (find-prev-before needle (point))))
        (if hit
            (begin
              (set-search-term! needle)
              (set-point-from-index hit)
              (set-status (string-append "Search backward: " needle)))
            (let ((wrapped (find-prev-before needle (+ (buflen) 1))))
              (if wrapped
                  (begin
                    (set-search-term! needle)
                    (set-point-from-index wrapped)
                    (set-status (string-append "Search backward wrapped: " needle)))
                  (set-status (string-append "No match for " needle))))))))

(define (string-prefix? prefix s)
  (let ((n (string-length prefix)))
    (and (<= n (string-length s))
         (equal? prefix (substring s 0 n)))))

(define (blank? s)
  (= (string-length (string-trim-both s)) 0))

(define (command-exact? input name)
  (equal? (string-trim-both input) name))

(define (command-arg input name)
  (let ((cmd (string-trim-both input))
        (n (string-length name)))
    (if (and (< n (string-length cmd))
             (equal? (substring cmd 0 n) name)
             (or (equal? (substring cmd n (+ n 1)) " ")
                 (equal? (substring cmd n (+ n 1)) "\t")))
        (string-trim-both (safe-substring cmd n (string-length cmd)))
        #f)))

(define (count-prefix-matches prefix commands)
  (let loop ((xs commands) (n 0))
    (cond
      ((null? xs) n)
      ((string-prefix? prefix (car xs)) (loop (cdr xs) (+ n 1)))
      (else (loop (cdr xs) n)))))

(define (first-prefix-match prefix commands)
  (cond
    ((null? commands) #f)
    ((string-prefix? prefix (car commands)) (car commands))
    (else (first-prefix-match prefix (cdr commands)))))

(define (complete-command input)
  (let* ((prefix (string-trim-both input))
         (matches (count-prefix-matches prefix mx-commands)))
    (cond
      ((= matches 1) (first-prefix-match prefix mx-commands))
      ((> matches 1) (begin (set-status "Ambiguous M-x command") input))
      (else (begin (set-status "No M-x completion") input)))))

(define (mini-read-command)
  (let loop ((input ""))
    (let* ((w (tb 'width))
           (h (tb 'height))
           (prompt "M-x "))
      (draw)
      (tb 'print-at 0 (- h 1) TB_BLACK MINI-BG
          (pad (string-append prompt input) w))
      (tb 'set-cell (clamp (string-length (string-append prompt input)) 0 (- w 1))
                    (- h 1) " " TB_BLACK TB_YELLOW)
      (tb tb_present)
      (let ((ev (tb tb_poll_event)))
        (cond
          ((or (= (event-key ev) CTRL-G) (= (event-key ev) TB_KEY_ESC)) #f)
          ((or (= (event-key ev) TB_KEY_ENTER) (= (event-key ev) ENTER)) input)
          ((= (event-key ev) TB_KEY_TAB) (loop (complete-command input)))
          ((or (= (event-key ev) TB_KEY_BACKSPACE) (= (event-key ev) TB_KEY_BACKSPACE2))
           (loop (safe-substring input 0 (- (string-length input) 1))))
          ((and (>= (event-ch ev) 32) (< (event-ch ev) 127))
           (loop (string-append input (integer->char (event-ch ev)))))
          (else (loop input)))))))

(define (digit-value c)
  (cond
    ((equal? c "0") 0)
    ((equal? c "1") 1)
    ((equal? c "2") 2)
    ((equal? c "3") 3)
    ((equal? c "4") 4)
    ((equal? c "5") 5)
    ((equal? c "6") 6)
    ((equal? c "7") 7)
    ((equal? c "8") 8)
    ((equal? c "9") 9)
    (else #f)))

(define (parse-number s)
  (let loop ((i 0) (n 0))
    (if (= i (string-length s))
        (if (= i 0) #f n)
        (let ((digit (digit-value (string-ref s i))))
          (if digit
              (loop (+ i 1) (+ (* n 10) digit))
              #f)))))

(define (show-help)
  (set-status
    "M-x: save-buffer write-file PATH find-file PATH goto-line N isearch-forward isearch-backward kill-line kill-region kill-ring-save delete-char yank quit"))

(define (write-buffer-as path)
  (if (blank? path)
      (set-status "write-file: no path given")
      (begin
        (set-filename! path)
        (set-syntax-mode! (detect-syntax-mode path))
        (save-buffer))))

(define (goto-line-command arg)
  (let ((line (parse-number arg)))
    (if line
        (begin
          (set-point-from-line-col (- (max 1 line) 1) 0)
          (set-status (string-append "Line " (number->string (max 1 line)))))
        (set-status "goto-line: expected a number"))))

(define (execute-command input)
  (let* ((cmd (string-trim-both input))
         (find-arg (command-arg cmd "find-file"))
         (write-arg (command-arg cmd "write-file"))
         (goto-arg (command-arg cmd "goto-line")))
    (cond
      ((blank? cmd) (set-status "Quit") #f)
      ((or (command-exact? cmd "help")
           (command-exact? cmd "help-commands")
           (command-exact? cmd "describe-bindings")
           (command-exact? cmd "help-keys"))
       (show-help) #f)
      ((command-exact? cmd "save-buffer") (save-buffer) #f)
      ((command-exact? cmd "kill-line") (kill-line) #f)
      ((command-exact? cmd "kill-ring-save") (copy-region) #f)
      ((command-exact? cmd "kill-region") (kill-region) #f)
      ((command-exact? cmd "delete-char") (delete-forward) #f)
      ((command-exact? cmd "yank") (yank) #f)
      ((command-exact? cmd "redraw-screen") (set-status "Redraw") #f)
      ((command-exact? cmd "isearch-forward")
       (let ((needle (mini-read "Search: " (search-term))))
         (if needle (search-forward needle) (set-status "Quit")))
       #f)
      ((command-exact? cmd "isearch-backward")
       (let ((needle (mini-read "Search backward: " (search-term))))
         (if needle (search-backward needle) (set-status "Quit")))
       #f)
      ((command-exact? cmd "quit") #t)
      ((command-exact? cmd "dired")
       (let ((dir (mini-read "Dired: " ".")))
         (if dir (open-dired dir) (set-status "Quit")))
       #f)
      ((and find-arg (not (blank? find-arg))) (open-buffer find-arg) #f)
      ((command-exact? cmd "find-file") (find-file) #f)
      ((and write-arg (not (blank? write-arg))) (write-buffer-as write-arg) #f)
      ((command-exact? cmd "write-file")
       (let ((path (mini-read "Write file: " (filename))))
         (if path (write-buffer-as path) (set-status "Quit")))
       #f)
      ((and goto-arg (not (blank? goto-arg))) (goto-line-command goto-arg) #f)
      ((command-exact? cmd "goto-line")
       (let ((line (mini-read "Goto line: " "")))
         (if line (goto-line-command line) (set-status "Quit")))
       #f)
      ((command-exact? cmd "text-mode")
       (set-syntax-mode! 'text) (set-status "Text-mode") #f)
      ((command-exact? cmd "c-mode")
       (set-syntax-mode! 'c) (set-status "C-mode") #f)
      ((command-exact? cmd "python-mode")
       (set-syntax-mode! 'python) (set-status "Python-mode") #f)
      ((command-exact? cmd "lisp-mode")
       (set-syntax-mode! 'lisp) (set-status "Lisp-mode") #f)
      ((command-exact? cmd "sh-mode")
       (set-syntax-mode! 'sh) (set-status "Sh-mode") #f)
      ((command-exact? cmd "lua-mode")
       (set-syntax-mode! 'lua) (set-status "Lua-mode") #f)
      ((or (command-exact? cmd "dired-jump")
           (command-exact? cmd "soft-wrap-mode")
           (command-exact? cmd "autocomplete")
           (command-exact? cmd "kill-buffer")
           (command-exact? cmd "menu-bar-mode")
           (command-exact? cmd "evil-mode")
           (command-exact? cmd "roff-mode")
           (command-exact? cmd "makefile-mode"))
       (set-status (string-append cmd " is not available in tbg.scm yet")) #f)
      (else (set-status (string-append "Unknown command: " cmd)) #f))))

(define (mx)
  (let ((cmd (mini-read-command)))
    (if cmd
        (execute-command cmd)
        (begin (set-status "Quit") #f))))

(define (mini-read prompt initial)
  (let loop ((input initial))
    (let* ((w (tb 'width))
           (h (tb 'height)))
      (draw)
      (tb 'print-at 0 (- h 1) TB_BLACK MINI-BG
          (pad (string-append prompt input) w))
      (tb 'set-cell (clamp (string-length (string-append prompt input)) 0 (- w 1))
                    (- h 1) " " TB_BLACK TB_YELLOW)
      (tb tb_present)
      (let ((ev (tb tb_poll_event)))
        (cond
          ((or (= (event-key ev) CTRL-G) (= (event-key ev) TB_KEY_ESC)) #f)
          ((or (= (event-key ev) TB_KEY_ENTER) (= (event-key ev) ENTER)) input)
          ((or (= (event-key ev) TB_KEY_BACKSPACE) (= (event-key ev) TB_KEY_BACKSPACE2))
           (loop (safe-substring input 0 (- (string-length input) 1))))
          ((and (>= (event-ch ev) 32) (< (event-ch ev) 127))
           (loop (string-append input (integer->char (event-ch ev)))))
          (else (loop input)))))))

(define (find-file)
  (let ((path (mini-read "Find file: " "")))
    (if path
        (open-buffer path)
        (set-status "Quit"))))

(define (mouse-point x y)
  (set-point-from-line-col (+ (scroll-line) y) (+ (scroll-x) x)))

(define (handle-mouse ev)
  (cond
    ((= (event-key ev) TB_KEY_MOUSE_LEFT)
     (mouse-point (event-x ev) (event-y ev)))
    ((= (event-key ev) TB_KEY_MOUSE_WHEEL_UP)
     (set-scroll-line! (max 0 (- (scroll-line) 3))))
    ((= (event-key ev) TB_KEY_MOUSE_WHEEL_DOWN)
     (set-scroll-line! (min (- (line-count) 1) (+ (scroll-line) 3))))
    (else #f)))

(define (handle-key ev)
  (let ((key (event-key ev))
        (code (event-ch ev)))
    (cond
      ((= key TB_KEY_ARROW_LEFT) (move-left))
      ((= key TB_KEY_ARROW_RIGHT) (move-right))
      ((= key TB_KEY_ARROW_UP) (move-up))
      ((= key TB_KEY_ARROW_DOWN) (move-down))
      ((or (= key TB_KEY_HOME) (= key CTRL-A)) (go-bol))
      ((or (= key TB_KEY_END) (= key CTRL-E)) (go-eol))
      ((= key TB_KEY_PGUP) (page-up))
      ((= key TB_KEY_PGDN) (page-down))
      ((or (= key TB_KEY_BACKSPACE) (= key TB_KEY_BACKSPACE2)) (if (text-mode?) (delete-backward) (set-status "Dired is read-only")))
      ((= key TB_KEY_DELETE) (if (text-mode?) (delete-forward) (set-status "Dired is read-only")))
      ((= key CTRL-D) (if (text-mode?) (delete-forward) (set-status "Dired is read-only")))
      ((= key CTRL-K) (if (text-mode?) (kill-line) (set-status "Dired is read-only")))
      ((= key CTRL-W) (if (text-mode?) (kill-region) (set-status "Dired is read-only")))
      ((= key CTRL-Y) (if (text-mode?) (yank) (set-status "Dired is read-only")))
      ((= key CTRL-L) (set-status "Redraw"))
      ((= key CTRL-S)
       (let ((needle (mini-read "Search: " (search-term))))
         (if needle (search-forward needle) (set-status "Quit"))))
      ((= key CTRL-R)
       (let ((needle (mini-read "Search backward: " (search-term))))
         (if needle (search-backward needle) (set-status "Quit"))))
      ((or (= key TB_KEY_ENTER) (= key ENTER))
       (if (eq? (mode) 'dired) (dired-open-selected) (insert-text "\n")))
      ((= key TB_KEY_TAB) (if (text-mode?) (insert-text (space TAB-WIDTH)) (set-status "Dired is read-only")))
      ((and (>= code 32) (< code 127)) (if (text-mode?) (insert-text (integer->char code)) (set-status "Dired is read-only")))
      (else #f))))

(define (handle-ctrl-x ev)
  (cond
    ((= (event-key ev) CTRL-C) #t)
    ((= (event-key ev) CTRL-S) (begin (save-buffer) #f))
    ((= (event-key ev) CTRL-F) (begin (find-file) #f))
    ((= (event-ch ev) 115) (begin (save-buffer) #f))
    ((= (event-ch ev) 102) (begin (find-file) #f))
    ((= (event-ch ev) 60) (begin (go-buffer-start) #f))
    ((= (event-ch ev) 62) (begin (go-buffer-end) #f))
    (else (begin (set-status "Unknown C-x command") #f))))

(define (loop)
  (draw)
  (let ((ev (tb tb_poll_event)))
    (cond
      ((= (event-key ev) TB_KEY_ESC) #t)
      ((= (event-key ev) CTRL-C) #t)
      ((= (event-key ev) CTRL-X)
       (begin
         (set-status "C-x")
         (draw)
         (if (handle-ctrl-x (tb tb_poll_event)) #t (loop))))
      ((and (= (event-mod ev) TB_MOD_ALT) (= (event-ch ev) 120))
       (if (mx) #t (loop)))
      ((and (= (event-mod ev) TB_MOD_ALT) (= (event-ch ev) 119))
       (begin (copy-region) (loop)))
      ((and (= (event-mod ev) TB_MOD_ALT) (= (event-ch ev) 60))
       (begin (go-buffer-start) (loop)))
      ((and (= (event-mod ev) TB_MOD_ALT) (= (event-ch ev) 62))
       (begin (go-buffer-end) (loop)))
      ((and (= (event-key ev) 0) (= (event-ch ev) 0))
       (begin
         (set-mark-pos! (point))
         (set-mark-active! #t)
         (set-status "Mark set")
         (loop)))
      ((= (event-type ev) TB_EVENT_MOUSE)
       (begin (handle-mouse ev) (loop)))
      (else (handle-key ev) (loop)))))

(define (main)
  (let ((args (command-line)))
    (if (> (length args) 2)
        (open-buffer (list-ref args 2))
        (open-buffer (filename))))
  (tb tb_init)
  (tb 'input-mode (bitwise-ior TB_INPUT_ALT TB_INPUT_MOUSE))
  (tb 'output-mode TB_OUTPUT_256)
  (loop)
  (tb tb_shutdown))

(main)
