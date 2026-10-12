; Regression coverage for R7RS define-record-type.
(define-record-type <point>
  (make-point x y)
  point?
  (x point-x point-x-set!)
  (y point-y))
(define p (make-point 3 4))
(point-x-set! p 9)
(display (list (point? p)
               (point? (make-record 'other '()))
               (point-x p)
               (point-y p)
               (record-type p)))
(newline)
