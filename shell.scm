(define (tb cmd . args)
  (apply termbox (cons cmd args)))

(define FG TB_WHITE)
(define BG TB_DEFAULT)
(define PROMPT-FG (bitwise-ior TB_GREEN TB_BOLD))
(define STATUS-FG (bitwise-ior TB_WHITE TB_BOLD))
(define STATUS-BG 237)
(define INPUT-BG 235)
(define CURSOR-BG TB_YELLOW)

(define CTRL-A 1)
(define CTRL-C 3)
(define CTRL-D 4)
(define CTRL-E 5)
(define CTRL-L 12)
(define ENTER 13)

(define transcript "swampscheme shell\nType a command and press Enter.  Type exit or quit to leave.\n\n")
(define input "")
(define input-cursor 0)
(define scroll-line 0)
(define running #t)
(define screen-active #f)
(define status "ready")

(define (min a b) (if (< a b) a b))
(define (max a b) (if (> a b) a b))
(define (clamp n lo hi) (max lo (min n hi)))

(define (safe-substring s a b)
  (let ((n (string-length s)))
    (substring s (clamp a 0 n) (clamp b 0 n))))

(define (space n)
  (if (<= n 0) "" (string-append " " (space (- n 1)))))

(define (pad s width)
  (safe-substring (string-append s (space width)) 0 width))

(define (newline? s)
  (equal? s "\n"))

(define (blank? s)
  (= (string-length (string-trim-both s)) 0))

(define (line-count s)
  (let loop ((p 0) (lines 1))
    (if (= p (string-length s))
        lines
        (loop (+ p 1) (if (newline? (string-ref s p)) (+ lines 1) lines)))))

(define (line-start s line)
  (let loop ((p 0) (cur 0))
    (cond
      ((or (= cur line) (= p (string-length s))) p)
      ((newline? (string-ref s p)) (loop (+ p 1) (+ cur 1)))
      (else (loop (+ p 1) cur)))))

(define (line-end s start)
  (let loop ((p start))
    (if (or (= p (string-length s)) (newline? (string-ref s p)))
        p
        (loop (+ p 1)))))

(define (line-text s line)
  (let* ((start (line-start s line))
         (end (line-end s start)))
    (safe-substring s start end)))

(define (visible-input width)
  (let* ((prompt-width 2)
         (room (max 1 (- width prompt-width)))
         (start (if (>= input-cursor room) (+ 1 (- input-cursor room)) 0))
         (end (min (string-length input) (+ start room))))
    (safe-substring input start end)))

(define (input-view-start width)
  (let* ((prompt-width 2)
         (room (max 1 (- width prompt-width))))
    (if (>= input-cursor room) (+ 1 (- input-cursor room)) 0)))

(define (max-scroll edit-h)
  (max 0 (- (line-count transcript) edit-h)))

(define (scroll-to-bottom)
  (if screen-active
      (set! scroll-line (max-scroll (max 1 (- (tb 'height) 2))))
      (set! scroll-line 0)))

(define (append-transcript text)
  (set! transcript (string-append transcript text))
  (scroll-to-bottom))

(define (sanitize-output text)
  (let loop ((i 0) (out ""))
    (if (= i (string-length text))
        out
        (let ((c (string-ref text i)))
          (cond
            ((equal? c (integer->char 27))
             (loop (+ i 1) (string-append out "^[")))
            ((equal? c (integer->char 13))
             (loop (+ i 1) out))
            ((equal? c (integer->char 9))
             (loop (+ i 1) (string-append out "    ")))
            (else
             (loop (+ i 1) (string-append out c))))))))

(define (draw-lines width edit-h)
  (let loop ((y 0))
    (when (< y edit-h)
      (tb 'print-at 0 y FG BG (pad (line-text transcript (+ scroll-line y)) width))
      (loop (+ y 1)))))

(define (draw-status width y)
  (tb 'print-at 0 y STATUS-FG STATUS-BG
      (pad (string-append " swampscheme shell  " status) width)))

(define (draw-input width y)
  (let* ((shown (visible-input width))
         (start (input-view-start width))
         (cx (clamp (+ 2 (- input-cursor start)) 2 (- width 1)))
         (cursor-char (if (< input-cursor (string-length input))
                          (string-ref input input-cursor)
                          " ")))
    (tb 'print-at 0 y PROMPT-FG INPUT-BG "$ ")
    (tb 'print-at 2 y FG INPUT-BG (pad shown (- width 2)))
    (tb 'set-cell cx y cursor-char FG CURSOR-BG)))

(define (draw)
  (let* ((w (tb 'width))
         (h (tb 'height))
         (edit-h (max 1 (- h 2))))
    (set! scroll-line (clamp scroll-line 0 (max-scroll edit-h)))
    (tb 'set-clear-attrs FG BG)
    (tb tb_clear)
    (draw-lines w edit-h)
    (draw-status w edit-h)
    (draw-input w (+ edit-h 1))
    (tb 'hide-cursor)
    (tb tb_present)))

(define (restore-screen)
  (when screen-active
    (tb tb_shutdown)
    (tb tb_init)
    (tb 'input-mode TB_INPUT_ESC)
    (tb 'output-mode TB_OUTPUT_256)
    (tb 'hide-cursor)))

(define (event-key ev) (plist-ref ev 'key 0))
(define (event-ch ev) (plist-ref ev 'ch 0))
(define (event-type ev) (plist-ref ev 'type 0))

(define (insert-input s)
  (set! input
        (string-append
          (safe-substring input 0 input-cursor)
          s
          (safe-substring input input-cursor (string-length input))))
  (set! input-cursor (+ input-cursor (string-length s))))

(define (delete-backward)
  (when (> input-cursor 0)
    (set! input
          (string-append
            (safe-substring input 0 (- input-cursor 1))
            (safe-substring input input-cursor (string-length input))))
    (set! input-cursor (- input-cursor 1))))

(define (delete-forward)
  (when (< input-cursor (string-length input))
    (set! input
          (string-append
            (safe-substring input 0 input-cursor)
            (safe-substring input (+ input-cursor 1) (string-length input))))))

(define (reset-input)
  (set! input "")
  (set! input-cursor 0))

(define (run-command cmd)
  (let ((trimmed (string-trim-both cmd)))
    (cond
      ((blank? trimmed) #f)
      ((or (equal? trimmed "exit") (equal? trimmed "quit"))
       (set! running #f))
      ((equal? trimmed "clear")
       (set! transcript "")
       (set! scroll-line 0)
       (set! status "cleared"))
      (else
       (begin
         (append-transcript (string-append "$ " cmd "\n"))
         (set! status "running")
         (when screen-active (draw))
         (let* ((result (system/status "/bin/sh" "-c" cmd))
                (code (plist-ref result 'status 127))
                (out (sanitize-output (plist-ref result 'output ""))))
           (restore-screen)
           (append-transcript out)
           (when (or (= (string-length out) 0)
                     (not (newline? (string-ref out (- (string-length out) 1)))))
             (append-transcript "\n"))
           (when (not (= code 0))
             (append-transcript (string-append "[exit " (number->string code) "]\n")))
           (set! status (string-append "exit " (number->string code)))))))))

(define (submit)
  (let ((cmd input))
    (reset-input)
    (run-command cmd)))

(define (handle-key ev)
  (let ((key (event-key ev))
        (code (event-ch ev))
        (edit-h (max 1 (- (tb 'height) 2))))
    (cond
      ((or (= key TB_KEY_ESC) (= key CTRL-C) (= key CTRL-D))
       (set! running #f))
      ((or (= key TB_KEY_ENTER) (= key ENTER))
       (submit))
      ((or (= key TB_KEY_BACKSPACE) (= key TB_KEY_BACKSPACE2))
       (delete-backward))
      ((= key TB_KEY_DELETE)
       (delete-forward))
      ((or (= key TB_KEY_HOME) (= key CTRL-A))
       (set! input-cursor 0))
      ((or (= key TB_KEY_END) (= key CTRL-E))
       (set! input-cursor (string-length input)))
      ((= key TB_KEY_ARROW_LEFT)
       (when (> input-cursor 0) (set! input-cursor (- input-cursor 1))))
      ((= key TB_KEY_ARROW_RIGHT)
       (when (< input-cursor (string-length input)) (set! input-cursor (+ input-cursor 1))))
      ((= key TB_KEY_PGUP)
       (set! scroll-line (max 0 (- scroll-line edit-h))))
      ((= key TB_KEY_PGDN)
       (set! scroll-line (min (max-scroll edit-h) (+ scroll-line edit-h))))
      ((= key CTRL-L)
       (set! transcript "")
       (set! scroll-line 0)
       (set! status "cleared"))
      ((and (>= code 32) (< code 127))
       (insert-input (integer->char code)))
      (else #f))))

(define (loop)
  (when running
    (draw)
    (handle-key (tb tb_poll_event))
    (loop)))

(define (main)
  (tb tb_init)
  (set! screen-active #t)
  (tb 'input-mode TB_INPUT_ESC)
  (tb 'output-mode TB_OUTPUT_256)
  (scroll-to-bottom)
  (loop)
  (tb tb_shutdown))

(main)
