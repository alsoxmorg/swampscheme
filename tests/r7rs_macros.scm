; Practical syntax-rules coverage: literals, body ..., and recursive use.
(define-syntax my-when
  (syntax-rules ()
    ((_ test body ...)
     (if test (begin body ...)))))

(define-syntax my-unless
  (syntax-rules ()
    ((_ test body ...)
     (if test #f (begin body ...)))))

(define-syntax my-or
  (syntax-rules ()
    ((_ a) a)
    ((_ a b ...) (if a a (my-or b ...)))))

(define-syntax choose
  (syntax-rules (otherwise)
    ((_ otherwise fallback) fallback)
    ((_ test yes no) (if test yes no))))

(define total 0)
(my-when #t (set! total (+ total 1)) (set! total (+ total 2)))
(my-unless #f (set! total (+ total 4)))
(display (list total (my-or #f #f 42) (choose otherwise 99) (choose #t 7 8)))
(newline)
