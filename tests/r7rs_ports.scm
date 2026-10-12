; Regression coverage for standard file callback procedures.
(call-with-output-file "/tmp/swampscheme-r7rs-port-test"
  (lambda (port) (display "port-output" port)))
(display
 (list
  (call-with-input-file "/tmp/swampscheme-r7rs-port-test"
    (lambda (port) (read-line port)))
  (begin
    (with-output-to-file "/tmp/swampscheme-r7rs-current-output"
      (lambda () (display "current-output")))
    (read-file "/tmp/swampscheme-r7rs-current-output"))))
(newline)
