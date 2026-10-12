(define (tb cmd . args)
  (apply termbox (cons cmd args)))

(define (put x y text)
  (tb 'goto x y)
  (tb 'print text))

(define (hline x y n ch)
  (let loop ((i 0))
    (when (< i n)
      (put (+ x i) y ch)
      (loop (+ i 1)))))

(define (vline x y n ch)
  (let loop ((i 0))
    (when (< i n)
      (put x (+ y i) ch)
      (loop (+ i 1)))))

(define (draw-box x y w h)
  (hline (+ x 1) y (- w 2) "-")
  (hline (+ x 1) (+ y h -1) (- w 2) "-")
  (vline x (+ y 1) (- h 2) "|")
  (vline (+ x w -1) (+ y 1) (- h 2) "|")
  (put x y "+")
  (put (+ x w -1) y "+")
  (put x (+ y h -1) "+")
  (put (+ x w -1) (+ y h -1) "+"))

(define (event-key ev)
  (plist-ref ev 'key 0))

(define (event-ch ev)
  (plist-ref ev 'ch 0))

(define (event-type ev)
  (plist-ref ev 'type 0))

(define (event-x ev)
  (plist-ref ev 'x 0))

(define (event-y ev)
  (plist-ref ev 'y 0))

(define (clamp n lo hi)
  (if (< n lo) lo (if (> n hi) hi n)))

(define (draw x y status)
  (tb tb_clear)
  (tb 'setcolor (bitwise-ior TB_CYAN TB_BOLD))
  (draw-box 2 1 64 15)
  (tb 'print-at 5 3 (bitwise-ior TB_WHITE TB_BOLD) TB_DEFAULT "swampscheme termbox TUI")
  (tb 'print-at 5 5 TB_WHITE TB_DEFAULT "Arrow keys move the marker. Mouse clicks jump it.")
  (tb 'print-at 5 6 TB_WHITE TB_DEFAULT "Press q, Esc, or Ctrl-C to close.")
  (tb 'print-at 5 8 TB_YELLOW TB_DEFAULT status)
  (tb 'set-cell x y "@" (bitwise-ior TB_GREEN TB_BOLD) TB_DEFAULT)
  (tb tb_present))

(define (status ev)
  (string-append
    "last event type="
    (number->string (event-type ev))
    " key="
    (number->string (event-key ev))
    " ch="
    (number->string (event-ch ev))
    " x="
    (number->string (event-x ev))
    " y="
    (number->string (event-y ev))))

(define (quit-event? ev)
  (or (= (event-key ev) TB_KEY_ESC)
      (= (event-key ev) TB_KEY_CTRL_C)
      (= (event-key ev) TB_KEY_CTRL_D)
      (= (event-ch ev) 113)))

(define (next-x ev x)
  (cond
    ((= (event-type ev) TB_EVENT_MOUSE) (clamp (event-x ev) 3 64))
    ((= (event-key ev) TB_KEY_ARROW_LEFT) (- x 1))
    ((= (event-key ev) TB_KEY_ARROW_RIGHT) (+ x 1))
    (else x)))

(define (next-y ev y)
  (cond
    ((= (event-type ev) TB_EVENT_MOUSE) (clamp (event-y ev) 2 14))
    ((= (event-key ev) TB_KEY_ARROW_UP) (- y 1))
    ((= (event-key ev) TB_KEY_ARROW_DOWN) (+ y 1))
    (else y)))

(define (main)
  (tb tb_init)
  (tb 'hide-cursor)
  (tb 'input-mode (bitwise-ior TB_INPUT_ESC TB_INPUT_MOUSE))
  (tb 'output-mode TB_OUTPUT_256)
  (let loop ((x 12) (y 11) (msg "waiting for input"))
    (draw x y msg)
    (let ((ev (tb tb_poll_event)))
      (when (not (quit-event? ev))
        (loop (clamp (next-x ev x) 3 64)
              (clamp (next-y ev y) 2 14)
              (status ev)))))
  (tb tb_shutdown))

(main)
