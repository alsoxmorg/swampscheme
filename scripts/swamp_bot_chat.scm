#!/usr/bin/env -S guile --no-auto-compile -s
!#

(import "swamp_bot_lib.scm")

(define (maybe-prompt)
  (when (input-terminal?)
    (display "you> ")))

(define (chat-loop)
  (maybe-prompt)
  (let ((line (read-line (current-input-port))))
    (if (eof-object? line)
        '()
        (begin
          (display "swamp_bot> ")
          (display (swamp-bot-response line))
          (newline)
          (if (exit-request? line)
              (exit 0)
              (chat-loop))))))

(chat-loop)
