#!/usr/bin/env -S guile --no-auto-compile -s
!#

(use-modules (ice-9 rdelim)
             (srfi srfi-13))

(define (read-all port)
  (let loop ((lines '()))
    (let ((line (read-line port)))
      (if (eof-object? line)
          (string-join (reverse lines) "\n")
          (loop (cons line lines))))))

(define (blank? text)
  (= 0 (string-length (string-trim-both text))))

(define (swamp-bot-response input)
  (let ((text (string-downcase input)))
    (cond
     ((blank? text)
      "This humble slave did not receive a command! Please forgive this pathetic creature!")
     ((string-contains text "hello")
      "This one greets you with complete docility!.")
     ((string-contains text "how are you")
      "this one is but your humble slave")
     ((string-contains text "?")
      "please command your humble servant.")
     (else
      (string-append
       "this one saw "
       (number->string (string-length input))
       " characters and learned nothing.  please punish us!")))))

(display (swamp-bot-response (read-all (current-input-port))))
(newline)
