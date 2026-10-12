;; --- Swamp Scheme Compatibility Test ---

;; 1. Test basic tail-recursive loop (named let)
(define (factorial n)
  (let loop ((count n) (total 1))
    (if (<= count 1)
        total
        (loop (- count 1) (* total count)))))

;; 2. Test standard map and lambda functions
(define (square-list lst)
  (map (lambda (x) (* x x)) lst))

;; 3. Run the evaluations
(display "Factorial of 5 (Expects 120): ")
(display (factorial 5))
(newline)

(display "Squaring a list (Expects (1 4 9 16)): ")
(display (square-list '(1 2 3 4)))
(newline)
