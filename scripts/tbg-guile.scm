; Guile launcher for the pure-Scheme tbg editor.
;
; Build the Guile bridge once:
;   make -C ports/guile
;
; Run from the repository root:
;   GUILE_LOAD_PATH="$PWD/ports/guile" \
;   GUILE_EXTENSIONS_PATH="$PWD/ports/guile" \
;   guile --no-auto-compile scripts/tbg-guile.scm [filename]

(use-modules (termbox) (ice-9 ftw))

(define tb_init 'init)
(define tb_shutdown 'shutdown)
(define tb_clear 'clear)
(define tb_present 'present)
(define tb_poll_event 'poll-event)
(define TB_OUTPUT_256 2)
(define TB_EVENT_MOUSE 3)
(define TB_MOD_ALT 1)
(define TB_INPUT_ALT 2)
(define TB_INPUT_MOUSE 4)
(define TB_KEY_CTRL_C 3)
(define TB_KEY_BACKSPACE #x08)
(define TB_KEY_HOME (- #xffff 14))
(define TB_KEY_END (- #xffff 15))
(define TB_KEY_PGUP (- #xffff 16))
(define TB_KEY_PGDN (- #xffff 17))
(define TB_KEY_DELETE (- #xffff 13))
(define TB_KEY_BACKSPACE2 #x7f)
(define TB_KEY_TAB #x09)
(define TB_KEY_MOUSE_LEFT (- #xffff 23))
(define TB_KEY_MOUSE_WHEEL_UP (- #xffff 27))
(define TB_KEY_MOUSE_WHEEL_DOWN (- #xffff 28))

(define (bitwise-ior first . rest)
  (apply logior (cons first rest)))

(define (plist-ref plist key . default)
  (let loop ((xs plist))
    (cond
      ((null? xs) (if (null? default) #f (car default)))
      ((eq? (car xs) key) (cadr xs))
      (else (loop (cddr xs))))))

(define (make-record type fields)
  (vector type fields))

(define (record-ref record field)
  (let loop ((fields (vector-ref record 1)))
    (cond
      ((null? fields) #f)
      ((eq? (caar fields) field) (cdar fields))
      (else (loop (cdr fields))))))

(define (record-set! record field value)
  (vector-set! record 1
               (let update ((fields (vector-ref record 1)))
                 (cond
                   ((null? fields) '())
                   ((eq? (caar fields) field)
                    (cons (cons field value) (cdr fields)))
                   (else (cons (car fields) (update (cdr fields))))))))

; swampscheme's string-ref returns a one-character string.  Keep tbg's
; character tests portable without changing the editor source.
(define guile-string-ref string-ref)
(define (string-ref text index)
  (string (guile-string-ref text index)))

(define (make-string count fill)
  (let ((one (if (string? fill) fill (string fill))))
    (let loop ((n count) (out ""))
      (if (<= n 0) out (loop (- n 1) (string-append out one))))))

(define (read-file path)
  (call-with-input-file path
    (lambda (port)
      (let loop ((chunks '()))
        (let ((line (read-line port 'concat)))
          (if (eof-object? line)
              (apply string-append (reverse chunks))
              (loop (cons line chunks))))))))

(define (write-file path text)
  (call-with-output-file path
    (lambda (port) (display text port))))

; Filesystem primitives supplied by native SwampScheme.  Guile's scandir
; includes dot entries by default, while tbg supplies its own parent entry.
(define (directory-files path)
  (scandir path
           (lambda (name)
             (not (or (string=? name ".")
                      (string=? name ".."))))))

(define (file-directory? path)
  (file-is-directory? path))

(define (make-gap-buffer text)
  (vector text 0))

(define (gap-buffer->string buffer)
  (vector-ref buffer 0))

(define (gap-buffer-length buffer)
  (string-length (gap-buffer->string buffer)))

(define (gap-buffer-point buffer)
  (vector-ref buffer 1))

(define (gap-buffer-set-point! buffer point)
  (vector-set! buffer 1
               (max 0 (min point (gap-buffer-length buffer)))))

(define (gap-buffer-ref buffer index)
  (string-ref (gap-buffer->string buffer) index))

(define (gap-buffer-substring buffer start end)
  (substring (gap-buffer->string buffer) start end))

(define (gap-buffer-insert! buffer text)
  (let* ((old (gap-buffer->string buffer))
         (point (gap-buffer-point buffer))
         (inserted (if (char? text) (string text) text)))
    (vector-set! buffer 0
                 (string-append (substring old 0 point)
                                inserted
                                (substring old point (string-length old))))
    (gap-buffer-set-point! buffer (+ point (string-length inserted)))))

(define (gap-buffer-delete! buffer start end)
  (let ((old (gap-buffer->string buffer)))
    (vector-set! buffer 0
                 (string-append (substring old 0 start)
                                (substring old end (string-length old))))
    (gap-buffer-set-point! buffer start)))

(define (gap-buffer-line-start-from buffer index)
  (let loop ((i (min index (gap-buffer-length buffer))))
    (if (or (= i 0) (equal? (gap-buffer-ref buffer (- i 1)) "\n"))
        i
        (loop (- i 1)))))

(define (gap-buffer-line-end-from buffer index)
  (let loop ((i (min index (gap-buffer-length buffer))))
    (if (or (= i (gap-buffer-length buffer))
            (equal? (gap-buffer-ref buffer i) "\n"))
        i
        (loop (+ i 1)))))

(define (gap-buffer-line-number-at buffer index)
  (let loop ((i 0) (line 0))
    (if (or (>= i index) (>= i (gap-buffer-length buffer)))
        line
        (loop (+ i 1)
              (if (equal? (gap-buffer-ref buffer i) "\n")
                  (+ line 1)
                  line)))))

(define (gap-buffer-line-start buffer line)
  (let loop ((i 0) (current 0))
    (cond
      ((= current line) i)
      ((>= i (gap-buffer-length buffer)) (gap-buffer-length buffer))
      ((equal? (gap-buffer-ref buffer i) "\n")
       (loop (+ i 1) (+ current 1)))
      (else (loop (+ i 1) current)))))

(define (gap-buffer-line-count buffer)
  (let loop ((i 0) (lines 1))
    (if (>= i (gap-buffer-length buffer))
        lines
        (loop (+ i 1)
              (if (equal? (gap-buffer-ref buffer i) "\n")
                  (+ lines 1)
                  lines)))))

(define (gap-buffer-find-next buffer needle start)
  (let ((limit (- (gap-buffer-length buffer) (string-length needle))))
    (let loop ((i (max 0 start)))
      (cond
        ((> i limit) #f)
        ((equal? needle (gap-buffer-substring buffer i
                                               (+ i (string-length needle)))) i)
        (else (loop (+ i 1)))))))

(define (gap-buffer-find-prev buffer needle before)
  (let loop ((i (min before (- (gap-buffer-length buffer) (string-length needle)))))
    (cond
      ((< i 0) #f)
      ((equal? needle (gap-buffer-substring buffer i
                                               (+ i (string-length needle)))) i)
      (else (loop (- i 1))))))

; The editor's core uses grapheme offsets.  This launcher uses Guile's
; character indexing, which is a useful compatible fallback for the editor's
; normal ASCII/Unicode text workflow.
(define (grapheme-length text)
  (string-length text))

(define (grapheme-at text index)
  (string-ref text index))

(define (grapheme-next text index)
  (min (string-length text) (+ index 1)))

(define (grapheme-prev text index)
  (max 0 (- index 1)))

(define (load-tbg-definitions path)
  (call-with-input-file path
    (lambda (port)
      (let loop ()
        (let ((form (read port)))
          (unless (eof-object? form)
            (unless (and (pair? form) (eq? (car form) 'main))
              (eval form (current-module)))
            (loop)))))))

(load-tbg-definitions "scripts/tbg.scm")

; The native swampscheme gap buffer has grapheme-aware syntax scanning.  The
; Guile fallback is character-indexed, so use plain coloring while retaining
; all editing, navigation, dired, search, and copy/yank behavior.
(define (syntax-color index) D-FG)
(main)
