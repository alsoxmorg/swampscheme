(import (math))

; add-two is exported directly, plus-two is exported through rename.
; hidden-name is deliberately not imported.
(display (list (add-two 3) (plus-two 5)))
(newline)
