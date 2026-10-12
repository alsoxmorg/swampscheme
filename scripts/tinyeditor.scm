; TinyEditor: a small SDL3/Nuklear notepad-style editor.
;
; Build and run from the repository root:
;   make clean && make WITH_SDL3=1
;   ./swampscheme scripts/tinyeditor.scm
;
; File operations use tinyeditor.txt in the current working directory.
; Keyboard: arrows/home/end, Backspace/Delete, Enter, Tab, Ctrl-A/C/X/V/Z.

(define WINDOW-W 780)
(define WINDOW-H 700)
(define filename "tinyeditor.txt")
(define buffer (make-gap-buffer ""))
(define mark #f)
(define undo-stack '())
(define soft-wrap #t)
(define running #t)
(define status "New document")
(define open-menu #f)

(define (selected-range)
  (if mark
      (list (min mark (gap-buffer-point buffer))
            (max mark (gap-buffer-point buffer)))
      #f))

(define (selection-start)
  (car (selected-range)))

(define (selection-end)
  (cadr (selected-range)))

(define (push-undo!)
  (set! undo-stack
        (cons (list (gap-buffer->string buffer) (gap-buffer-point buffer))
              undo-stack)))

(define (restore-state! state)
  (set! buffer (make-gap-buffer (car state)))
  (gap-buffer-set-point! buffer (cadr state)))

(define (undo!)
  (if (null? undo-stack)
      (set! status "Nothing to undo")
      (begin
        (restore-state! (car undo-stack))
        (set! undo-stack (cdr undo-stack))
        (set! mark #f)
        (set! status "Undo"))))

(define (delete-selection!)
  (when mark
    (let ((start (selection-start))
          (end (selection-end)))
      (push-undo!)
      (gap-buffer-delete! buffer start end)
      (gap-buffer-set-point! buffer start)
      (set! mark #f))))

(define (insert-text! text)
  (unless (string=? text "")
    (if mark (delete-selection!) (push-undo!))
    (gap-buffer-insert! buffer text)
    (set! status "Modified")))

(define (backspace!)
  (if mark
      (delete-selection!)
      (when (> (gap-buffer-point buffer) 0)
        (let ((p (gap-buffer-point buffer)))
          (push-undo!)
          (gap-buffer-delete! buffer (- p 1) p)
          (gap-buffer-set-point! buffer (- p 1))
          (set! status "Modified")))))

(define (delete-forward!)
  (if mark
      (delete-selection!)
      (let ((p (gap-buffer-point buffer)))
        (when (< p (gap-buffer-length buffer))
          (push-undo!)
          (gap-buffer-delete! buffer p (+ p 1))
          (set! status "Modified")))))

(define (move-point! new-point shift?)
  (when shift?
    (unless mark (set! mark (gap-buffer-point buffer))))
  (gap-buffer-set-point! buffer new-point)
  (unless shift? (set! mark #f)))

(define (move-horizontal! delta shift?)
  (move-point! (+ (gap-buffer-point buffer) delta) shift?))

(define (move-line-edge! which shift?)
  (let ((p (gap-buffer-point buffer)))
    (move-point!
     (if (= which 0)
         (gap-buffer-line-start-from buffer p)
         (gap-buffer-line-end-from buffer p))
     shift?)))

(define (copy-selection!)
  (if mark
      (begin
        (sdl3 "clipboard-set"
              (gap-buffer-substring buffer (selection-start) (selection-end)))
        (set! status "Copied"))
      (set! status "No selection")))

(define (cut-selection!)
  (if mark
      (begin (copy-selection!) (delete-selection!))
      (set! status "No selection")))

(define (paste!)
  (insert-text! (sdl3 "clipboard-get")))

(define (select-all!)
  (set! mark 0)
  (gap-buffer-set-point! buffer (gap-buffer-length buffer))
  (set! status "Selected all"))

(define (new-file!)
  (set! buffer (make-gap-buffer ""))
  (set! mark #f)
  (set! undo-stack '())
  (set! status "New document"))

(define (save-file!)
  (write-file filename (gap-buffer->string buffer))
  (set! status (string-append "Saved " filename)))

(define (open-file!)
  (let ((text (try (read-file filename) #f)))
    (if text
        (begin
          (set! buffer (make-gap-buffer text))
          (set! mark #f)
          (set! undo-stack '())
          (set! status (string-append "Opened " filename)))
        (set! status (string-append "No file: " filename)))))

(define (ctrl? mod)
  (not (= 0 (bitwise-and mod SDL_KMOD_CTRL))))

(define (shift? mod)
  (not (= 0 (bitwise-and mod SDL_KMOD_SHIFT))))

(define (handle-key! event)
  (let ((key (plist-ref event 'key))
        (mod (plist-ref event 'mod)))
    (cond
      ((and (ctrl? mod) (= key SDLK_A)) (select-all!))
      ((and (ctrl? mod) (= key SDLK_C)) (copy-selection!))
      ((and (ctrl? mod) (= key SDLK_X)) (cut-selection!))
      ((and (ctrl? mod) (= key SDLK_V)) (paste!))
      ((and (ctrl? mod) (= key SDLK_Z)) (undo!))
      ((= key SDLK_LEFT) (move-horizontal! -1 (shift? mod)))
      ((= key SDLK_RIGHT) (move-horizontal! 1 (shift? mod)))
      ((= key SDLK_HOME) (move-line-edge! 0 (shift? mod)))
      ((= key SDLK_END) (move-line-edge! 1 (shift? mod)))
      ((= key SDLK_BACKSPACE) (backspace!))
      ((= key SDLK_DELETE) (delete-forward!))
      ((= key SDLK_RETURN) (insert-text! "\n"))
      ((= key SDLK_TAB) (insert-text! "    "))
      (else #f))))

(define (handle-event! event)
  (cond
    ((= (plist-ref event 'type) SDL_EVENT_QUIT)
     (set! running #f))
    ((= (plist-ref event 'type) SDL_EVENT_TEXT_INPUT)
     (insert-text! (plist-ref event 'text)))
    ((= (plist-ref event 'type) SDL_EVENT_KEY_DOWN)
     (if (= (plist-ref event 'key) SDLK_ESCAPE)
         (set! running #f)
         (handle-key! event)))
    (else #f)))

(define (drain-events!)
  (let ((event (sdl3 "event")))
    (when event
      (handle-event! event)
      (drain-events!))))

(define (draw-editor-line! line-number)
  (let* ((start (gap-buffer-line-start buffer line-number))
         (end (gap-buffer-line-end-from buffer start))
         (text (gap-buffer-substring buffer start end)))
    (if (= (string-length text) 0)
        (begin (nuklear "row" 22 1) (nuklear "label" " "))
        (let chunks ((offset 0))
          (when (< offset (string-length text))
            (let ((amount (if soft-wrap
                              (min 76 (- (string-length text) offset))
                              (- (string-length text) offset))))
              (nuklear "row" 22 1)
              (nuklear "label" (substring text offset (+ offset amount)))
              (chunks (+ offset amount))))))))

(define (draw-ui!)
  (nuklear "begin-frame")
  (when (nuklear "begin" "TinyEditor" 10 10 760 670)
    (nuklear "row" 28 4)
    (when (nuklear "button" "File")
      (set! open-menu (if (eq? open-menu 'file) #f 'file)))
    (when (nuklear "button" "Edit")
      (set! open-menu (if (eq? open-menu 'edit) #f 'edit)))
    (when (nuklear "button" "Options")
      (set! open-menu (if (eq? open-menu 'options) #f 'options)))
    (when (nuklear "button" "Help")
      (set! open-menu (if (eq? open-menu 'help) #f 'help)))
    (nuklear "label" (string-append filename " | " status))
    (let lines ((i 0) (total (gap-buffer-line-count buffer)))
      (when (< i total)
        (draw-editor-line! i)
        (lines (+ i 1) total)))
    (nuklear "end"))

  (when (eq? open-menu 'file)
    (when (nuklear "begin" "File" 10 42 180 180)
      (nuklear "row" 28 1)
      (when (nuklear "button" "New") (new-file!) (set! open-menu #f))
      (nuklear "row" 28 1)
      (when (nuklear "button" "Open") (open-file!) (set! open-menu #f))
      (nuklear "row" 28 1)
      (when (nuklear "button" "Save") (save-file!) (set! open-menu #f))
      (nuklear "row" 28 1)
      (when (nuklear "button" "Exit") (set! running #f) (set! open-menu #f))
      (nuklear "end")))

  (when (eq? open-menu 'edit)
    (when (nuklear "begin" "Edit" 105 42 190 210)
      (nuklear "row" 28 1)
      (when (nuklear "button" "Undo") (undo!) (set! open-menu #f))
      (nuklear "row" 28 1)
      (when (nuklear "button" "Copy") (copy-selection!) (set! open-menu #f))
      (nuklear "row" 28 1)
      (when (nuklear "button" "Paste") (paste!) (set! open-menu #f))
      (nuklear "row" 28 1)
      (when (nuklear "button" "Select All") (select-all!) (set! open-menu #f))
      (nuklear "end")))

  (when (eq? open-menu 'options)
    (when (nuklear "begin" "Options" 200 42 190 110)
      (nuklear "row" 28 1)
      (when (nuklear "button" (if soft-wrap "Soft Wrap: On" "Soft Wrap: Off"))
        (set! soft-wrap (not soft-wrap)) (set! open-menu #f))
      (nuklear "end")))

  (when (eq? open-menu 'help)
    (when (nuklear "begin" "Help" 300 42 260 110)
      (nuklear "row" 28 1)
      (when (nuklear "button" "About")
        (set! status "TinyEditor - Swamp Scheme SDL3 sample")
        (set! open-menu #f))
      (nuklear "end")))
  (nuklear "render"))

(sdl3 "init" WINDOW-W WINDOW-H "TinyEditor")
(sdl3 "text-input" #t)
(let ((initial (try (read-file filename) #f)))
  (when initial (set! buffer (make-gap-buffer initial))))

(let loop ()
  (sdl3 "clear" 18 22 32 255)
  (sdl3 "begin-frame")
  (drain-events!)
  (draw-ui!)
  (sdl3 "present")
  (when running
    (sdl3 "delay" 16)
    (loop)))

(sdl3 "text-input" #f)
(sdl3 "shutdown")
