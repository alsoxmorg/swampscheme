; Guile launcher for Rogue Arena.
;
; Build the Guile bridge once:
;   make -C ports/guile
;
; Run from the repository root:
;   GUILE_LOAD_PATH="$PWD/ports/guile" \
;   GUILE_EXTENSIONS_PATH="$PWD/ports/guile" \
;   guile scripts/roguearena-guile.scm

(use-modules (termbox))

; Names used by the swampscheme bridge are aliases for command symbols in
; the Guile bridge.
(define tb_init 'init)
(define tb_shutdown 'shutdown)
(define tb_clear 'clear)
(define tb_present 'present)
(define tb_poll_event 'poll-event)
(define TB_OUTPUT_256 2)
(define TB_EVENT_KEY 1)
(define TB_KEY_CTRL_C 3)

(define (bitwise-ior first . rest)
  (apply logior (cons first rest)))

(define (plist-ref plist key . default)
  (let loop ((xs plist))
    (cond
      ((null? xs) (if (null? default) #f (car default)))
      ((eq? (car xs) key) (cadr xs))
      (else (loop (cddr xs))))))

; A small vector-backed record compatible with the fields used by the shared
; arena.  Replacing the field alist on mutation avoids relying on mutable
; pairs, so this works across Guile and swampscheme.
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

(define (random-integer bound)
  (if (> bound 0) (random bound) 0))

(load "roguearena.scm")
