#!/usr/bin/env swampscheme
;
; swampdired - a small, batteries-included, Termbox file manager.
;
; Keys:
;   arrows/j/k       move in the directory pane
;   Enter            enter a directory
;   Backspace        parent directory
;   Tab              cycle directory / search / listing focus
;   /                focus fuzzy search
;   g                focus directory input
;   p                preview selected file
;   q, Escape        quit
;
; The optional preview action uses an installed terminal image viewer when it
; can find one (viu, chafa, TerminalImageViewer, tiv, or catimg). Text files
; are previewed directly inside the right pane.

(define (tb cmd . args)
  (apply termbox (cons cmd args)))

(define (event-key ev) (plist-ref ev 'key 0))
(define (event-ch ev) (plist-ref ev 'ch 0))
(define (event-type ev) (plist-ref ev 'type 0))

(define (clamp n lo hi)
  (if (< n lo) lo (if (> n hi) hi n)))

(define (string-ref-text s i)
  (let ((x (string-ref s i)))
    (if (char? x) (string x) x)))

(define (string-suffix? suffix s)
  (let ((a (string-length suffix)) (b (string-length s)))
    (and (<= a b) (string=? suffix (substring s (- b a) b)))))

(define (path-join dir name)
  (cond
    ((string=? dir "/") (string-append "/" name))
    ((string-suffix? "/" dir) (string-append dir name))
    (else (string-append dir "/" name))))

(define (parent-dir path)
  (let loop ((i (- (string-length path) 1)))
    (cond
      ((< i 0) ".")
      ((and (> i 0) (string=? (string-ref-text path i) "/"))
       (if (= i 0) "/" (substring path 0 i)))
      (else (loop (- i 1))))))

(define (space n)
  (if (<= n 0) " "
      (let loop ((i n) (out ""))
        (if (<= i 0) out (loop (- i 1) (string-append out " "))))))

(define (clip text width)
  (if (<= width 0) ""
      (if (> (string-length text) width)
          (let ((limit (if (> width 3) (- width 3) width)))
            (let loop ((i 0) (out ""))
              (if (>= i limit)
                  (if (> width 3) (string-append out "...") out)
                  (loop (+ i 1)
                        (string-append out (string-ref-text text i))))))
          text)))

(define (fit text width)
  ; Leave a small safety margin for wide Unicode cells.  string-length counts
  ; codepoints, whereas termbox lays out terminal columns.
  (let ((short (clip text (max 0 (- width 2)))))
    (string-append short (space (- width (string-length short))))))

(define (contains-ci? needle haystack)
  (if (string=? needle "") #t
      (string-contains (string-downcase haystack) (string-downcase needle))))

(define (entry-names dir query)
  (let ((all (cons ".." (directory-files dir))))
    (filter (lambda (name) (contains-ci? query name)) all)))

(define (entry-path dir name)
  (if (string=? name "..") (parent-dir dir) (path-join dir name)))

(define (entry-label dir name)
  (let ((path (entry-path dir name)))
    (if (file-directory? path)
        (string-append "[DIR] " name "/")
        (string-append "      " name))))

(define path-buffer ".")

(define (line-at text wanted)
  ; string-ref is codepoint-based, while substring's indexes are byte-based
  ; in the native runtime.  Build the requested line from characters instead
  ; of using a codepoint index as a substring byte offset; otherwise a UTF-8
  ; line (for example the Unicode test in Makefile) can produce malformed
  ; text and make termbox reject the preview row.
  (let loop ((i 0) (line 0) (out ""))
    (cond
      ((= line wanted)
       (if (>= i (string-length text))
           out
           (let ((ch (string-ref-text text i)))
             (if (string=? ch "\n")
                 out
                 (loop (+ i 1) line (string-append out ch))))))
      ((>= i (string-length text)) "")
      (else
       (let ((ch (string-ref-text text i)))
         (loop (+ i 1)
               (if (string=? ch "\n") (+ line 1) line)
               (if (string=? ch "\n") "" out)))))))

(define (image-file? path)
  (or (string-suffix? ".png" (string-downcase path))
      (string-suffix? ".jpg" (string-downcase path))
      (string-suffix? ".jpeg" (string-downcase path))
      (string-suffix? ".gif" (string-downcase path))
      (string-suffix? ".webp" (string-downcase path))
      (string-suffix? ".bmp" (string-downcase path))))

(define (command-available? command)
  (plist-ref (system/status "sh" "-c"
                            (string-append "command -v " command " >/dev/null 2>&1"))
             'ok #f))

(define (image-viewer)
  (cond
    ((command-available? "viu") "viu")
    ((command-available? "chafa") "chafa")
    ((command-available? "TerminalImageViewer") "TerminalImageViewer")
    ((command-available? "tiv") "tiv")
    ((command-available? "catimg") "catimg")
    (else #f)))

(define preview-limit 262144)
(define force-preview #f)

(define (file-size path)
  (let* ((result (system/status "wc" "-c" path))
         (tokens (string-tokenize (plist-ref result 'output ""))))
    (if (and (plist-ref result 'ok #f) (not (null? tokens)))
        (try (string->number (car tokens)) 0)
        0)))

(define (preview-lines dir name height forced?)
  (let ((path (entry-path dir name)))
    (cond
      ((string=? name "..")
       (list "parent directory" "Press Enter to go up."))
      ((file-directory? path)
       (list "directory" path "Press Enter to open."))
      ((image-file? path)
       (list "image file" path "Press p for external image preview."))
      ((and (not forced?) (> (file-size path) preview-limit))
       (list "large file; preview skipped"
             (string-append (number->string preview-limit)
                            "+ bytes; press p to force preview")))
      (else
       ; Forced previews still only need the visible screenful.  Let head stop
       ; the read early so a huge file does not get loaded into the Scheme VM.
       (let ((text (if forced?
                      (plist-ref (system/status "head" "-n"
                                                (number->string height) path)
                                 'output "[cannot read this file]")
                      (try (read-file path) "[cannot read this file]"))))
         (let loop ((i 0) (out '()))
           (if (>= i height) (reverse out)
               (loop (+ i 1) (cons (line-at text i) out)))))))))

(define (draw-text x y width text fg bg)
  (tb 'print-at x y fg bg (fit text width)))

(define (draw dir query focus names selected offset status)
  (let* ((w (tb 'width))
         (h (tb 'height))
         (left (clamp (/ w 2) 28 (- w 24)))
         (right (- w left 1))
         (body (- h 6)))
    (tb tb_clear)
    (draw-text 0 0 w
               (string-append " swampdired  " dir)
               (bitwise-ior TB_BLACK TB_BOLD) TB_CYAN)
    (draw-text 0 1 left
               (string-append "Path: " (if (eq? focus 'path) ">" " ") path-buffer)
               TB_YELLOW TB_DEFAULT)
    (draw-text left 1 right
               (string-append "Search: " (if (eq? focus 'search) ">" " ") query)
               TB_YELLOW TB_DEFAULT)
    (tb 'print-at (- left 1) 2 TB_CYAN TB_DEFAULT "|")
    (draw-text 0 2 left " DIRECTORY" TB_GREEN TB_DEFAULT)
    (draw-text (+ left 1) 2 right " PREVIEW" TB_GREEN TB_DEFAULT)
    (let loop ((xs names) (i 0))
      (when (and (< i body) (< (+ offset i) (length names)))
        (let* ((name (list-ref names (+ offset i)))
               (active (= (+ offset i) selected))
               (fg (if active TB_BLACK TB_WHITE))
               (bg (if active TB_YELLOW TB_DEFAULT)))
          (draw-text 0 (+ 3 i) left (entry-label dir name) fg bg)
          (loop (cdr xs) (+ i 1)))))
    (let* ((chosen (if (null? names) ".." (list-ref names selected)))
           (preview (preview-lines dir chosen (- body 1) force-preview)))
      (let loop ((xs preview) (i 0))
        (when (and (< i (- body 1)) (not (null? xs)))
          (draw-text (+ left 1) (+ 3 i) right (car xs) TB_WHITE TB_DEFAULT)
          (loop (cdr xs) (+ i 1)))))
    (draw-text 0 (- h 2) w
               (string-append " " status
                              "  [Tab focus] [/ search] [g path] [p preview] [q quit]")
               TB_BLACK TB_GREEN)
    (tb 'present)))

(define (reinit)
  (tb tb_init)
  (tb 'hide-cursor)
  (tb 'input-mode (bitwise-ior TB_INPUT_ESC TB_INPUT_MOUSE))
  (tb 'output-mode TB_OUTPUT_256))

(define (external-preview dir name)
  (let ((viewer (image-viewer)))
    (if viewer
        (begin
          (tb tb_shutdown)
          (system/raw viewer (entry-path dir name))
          (reinit)
          "returned from image viewer")
        "no viewer found (try viu, chafa, tiv, or catimg)")))

(define (main-loop dir query focus names selected offset status)
  (let* ((names (entry-names dir query))
         (selected (clamp selected 0 (max 0 (- (length names) 1))))
         (offset (clamp offset 0 (max 0 (- selected 1)))))
    (draw dir query focus names selected offset status)
    (let ((ev (tb tb_poll_event)))
      (cond
        ((or (= (event-key ev) TB_KEY_ESC) (= (event-ch ev) 113))
         (tb tb_shutdown))
        ((= (event-key ev) TB_KEY_TAB)
         (main-loop dir query
                    (cond ((eq? focus 'path) 'search)
                          ((eq? focus 'search) 'list)
                          (else 'path))
                    names selected offset "focus changed"))
        ((= (event-ch ev) 47)
         (main-loop dir query 'search names selected offset "search input"))
        ((= (event-ch ev) 103)
         (begin
           (set! path-buffer dir)
           (main-loop dir query 'path names selected offset "directory input")))
        ((eq? focus 'path)
         (path-input dir query ev names selected offset))
        ((eq? focus 'search)
         (search-input dir query ev names selected offset))
        ((or (= (event-key ev) TB_KEY_ARROW_UP) (= (event-ch ev) 107))
         (begin
           (set! force-preview #f)
           (main-loop dir query focus names (- selected 1) offset "")))
        ((or (= (event-key ev) TB_KEY_ARROW_DOWN) (= (event-ch ev) 106))
         (begin
           (set! force-preview #f)
           (main-loop dir query focus names (+ selected 1) offset "")))
        ((= (event-key ev) TB_KEY_ENTER)
         (let ((path (entry-path dir (if (null? names) ".." (list-ref names selected)))))
           (if (file-directory? path)
               (begin
                 (set! force-preview #f)
                 (main-loop path "" 'list names 0 0 "entered directory"))
               (main-loop dir query focus names selected offset "not a directory"))))
        ((= (event-key ev) TB_KEY_BACKSPACE)
         (begin
           (set! force-preview #f)
           (main-loop (parent-dir dir) "" 'list names 0 0 "parent directory")))
        ((= (event-ch ev) 112)
         (let ((name (if (null? names) ".." (list-ref names selected))))
           (if (image-file? (entry-path dir name))
               (main-loop dir query focus names selected offset
                          (external-preview dir name))
               (begin
                 (set! force-preview #t)
                 (main-loop dir query focus names selected offset
                            "preview forced")))))
        (else (main-loop dir query focus names selected offset status))))))

(define (path-input dir query ev names selected offset)
  (let ((key (event-key ev)) (ch (event-ch ev)))
    (cond
      ((= key TB_KEY_ENTER)
       (if (file-directory? path-buffer)
           (begin
             (set! path-buffer (if (string=? path-buffer "") "." path-buffer))
             (main-loop path-buffer query 'list names 0 0 "path accepted"))
           (main-loop dir query 'path names selected offset "directory does not exist")))
      ((or (= key TB_KEY_BACKSPACE) (= key TB_KEY_BACKSPACE2))
       (set! path-buffer
             (if (> (string-length path-buffer) 0)
                 (substring path-buffer 0 (- (string-length path-buffer) 1))
                 path-buffer))
       (main-loop dir query 'path names selected offset "path input"))
      ((and (>= ch 32) (< ch 127))
       (set! path-buffer (string-append path-buffer (string (integer->char ch))))
       (main-loop dir query 'path names selected offset "path input"))
      (else (main-loop dir query 'path names selected offset "path input")))))

(define (search-input dir query ev names selected offset)
  (let ((key (event-key ev)) (ch (event-ch ev)))
    (cond
      ((= key TB_KEY_ENTER)
       (main-loop dir query 'list names 0 0 "search accepted"))
      ((or (= key TB_KEY_BACKSPACE) (= key TB_KEY_BACKSPACE2))
       (main-loop dir
                  (if (> (string-length query) 0)
                      (substring query 0 (- (string-length query) 1)) query)
                  'search names 0 0 "search input"))
      ((and (>= ch 32) (< ch 127))
       (main-loop dir (string-append query (string (integer->char ch)))
                  'search names 0 0 "search input"))
      (else (main-loop dir query 'search names selected offset "search input")))))

(reinit)
(main-loop "." "" 'list '() 0 0 "ready")
