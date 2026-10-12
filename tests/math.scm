(define-library (math)
  (export add-two hidden-name (rename add-two plus-two))
  (import (scheme base))
  (begin
    (define (add-two x) (+ x 2))
    (define hidden-name 99)))
