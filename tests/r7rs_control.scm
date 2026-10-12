; Regression coverage for R7RS case and do.
(display
 (list
  (case 2
    ((1) 'wrong)
    ((2 3) 'matched)
    (else 'wrong))
  (case 8
    ((8) => (lambda (value) (+ value 1)))
    (else 0))
  (let ((sum 0))
    (do ((i 0 (+ i 1)))
        ((= i 5) sum)
      (set! sum (+ sum i))))))
(newline)
