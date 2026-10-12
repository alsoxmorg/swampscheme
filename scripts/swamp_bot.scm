#!/usr/bin/env -S guile --no-auto-compile -s
!#

(import "swamp_bot_lib.scm")

(let ((input (read-all (current-input-port))))
  (display (swamp-bot-response input))
  (newline)
  (when (exit-request? input)
    (exit 86)))
