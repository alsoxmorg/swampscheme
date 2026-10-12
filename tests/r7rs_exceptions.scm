(define handled
  (with-exception-handler
    (lambda (condition) (list 'caught condition))
    (lambda () (raise 'boom))))

(define guarded
  (guard (condition
           ((eq? condition 'boom) 'guarded)
           (else 'fallback))
    (raise 'boom)))

(define continuable
  (with-exception-handler
    (lambda (condition) (list 'continued condition))
    (lambda () (raise-continuable 'again))))

(define old-style (try (error "legacy-error") 'fallback))
(display (list handled guarded continuable old-style))
(newline)
