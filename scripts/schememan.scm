; Schememan: a four-level, keyboard-driven SDL3 maze game.
;
; Build:
;   make clean && make WITH_SDL3=1
;
; Run:
;   ./swampscheme scripts/schememan.scm
;
; Controls: arrows or WASD.  Escape or closing the window quits.
; This is deliberately implemented with the current documented SDL API:
; rectangles, events, Nuklear labels/buttons, and no hidden C game logic.

(define TILE 24)
(define ORIGIN-X 64)
(define ORIGIN-Y 34)
(define WIDTH 20)
(define HEIGHT 15)

; Use the bridge's exported SDL3 key constants instead of numeric literals.
(define KEY-ESC SDLK_ESCAPE)
(define KEY-W SDLK_W)
(define KEY-A SDLK_A)
(define KEY-S SDLK_S)
(define KEY-D SDLK_D)
(define KEY-LEFT SDLK_LEFT)
(define KEY-RIGHT SDLK_RIGHT)
(define KEY-DOWN SDLK_DOWN)
(define KEY-UP SDLK_UP)

(define levels
  (list
   (list
    "####################"
    "#..................#"
    "#.####..####..####.#"
    "#..................#"
    "#.##.##########.##.#"
    "#....#........#....#"
    "####.#.######.#.####"
    "    .#...  ...#.    "
    "####.#.######.#.####"
    "#....#........#....#"
    "#.##.##########.##.#"
    "#..................#"
    "#.####..####..####.#"
    "#..................#"
    "####################")
   (list
    "####################"
    "#..................#"
    "#.####......####...#"
    "#....#......#......#"
    "###..#.####.#..####"
    "#....#....#.#......#"
    "#.######..#.######.#"
    "#................. ."
    "#.######.#..######.#"
    "#......#.#....#....#"
    "####...#.####.#..###"
    "#......#......#....#"
    "#...####......####.#"
    "#..................#"
    "####################")
   (list
    "####################"
    "#..................#"
    "#.##.######.######.#"
    "#....#....#.#......#"
    "####.#.##.#.#.######"
    "#....#....#.#......#"
    "#.#########.######.#"
    "#................. #"
    "#.######.#########.#"
    "#......#.#.........#"
    "######.#.#.########"
    "#......#.#.........#"
    "#.######.#########.#"
    "#..................#"
    "####################")
   (list
    "####################"
    "#..................#"
    "#.####.######.####.#"
    "#....#........#....#"
    "###..#.######.#..###"
    "#....#...##...#....#"
    "#.##.###.##.###.##.#"
    "#................. ."
    "#.##.###.##.###.##.#"
    "#....#...##...#....#"
    "###..#.######.#..###"
    "#....#........#....#"
    "#.####.######.####.#"
    "#..................#"
    "####################")))

(define level-index 0)
(define board (list-ref levels level-index))
(define eaten (make-hash-table))
(define score 0)
(define lives 3)
(define pac-x 1)
(define pac-y 1)
(define ghost-x (vector 18 17 10))
(define ghost-y (vector 1 13 7))
(define running #t)
(define won #f)
(define last-step 0)

(define (key-is? key a b)
  (or (= key a) (= key b)))

(define (tile-at x y)
  (if (or (< x 0) (>= x WIDTH) (< y 0) (>= y HEIGHT))
      "#"
      (string-ref (list-ref board y) x)))

(define (wall? x y)
  (string=? (tile-at x y) "#"))

(define (cell-key x y)
  (string-append (number->string level-index)
                 ":"
                 (number->string x)
                 ":"
                 (number->string y)))

(define (eaten? x y)
  (hash-exists? eaten (cell-key x y)))

(define (eat-pellet!)
  (when (and (string=? (tile-at pac-x pac-y) ".")
             (not (eaten? pac-x pac-y)))
    (hash-set! eaten (cell-key pac-x pac-y) #t)
    (set! score (+ score 10))))

(define (pellets-left)
  (let rows ((ys board) (y 0) (n 0))
    (if (null? ys)
        n
        (let cols ((x 0) (count n))
          (if (= x WIDTH)
              (rows (cdr ys) (+ y 1) count)
              (if (and (string=? (string-ref (car ys) x) ".")
                       (not (eaten? x y)))
                  (cols (+ x 1) (+ count 1))
                  (cols (+ x 1) count)))))))

(define (reset-positions!)
  (set! pac-x 1)
  (set! pac-y 1)
  (vector-set! ghost-x 0 18)
  (vector-set! ghost-y 0 1)
  (vector-set! ghost-x 1 17)
  (vector-set! ghost-y 1 13)
  (vector-set! ghost-x 2 10)
  (vector-set! ghost-y 2 7))

(define (load-level! n)
  (set! level-index n)
  (set! board (list-ref levels level-index))
  (set! eaten (make-hash-table))
  (reset-positions!))

(define (next-level!)
  (if (= level-index 3)
      (set! won #t)
      (begin
        (set! score (+ score 500))
        (load-level! (+ level-index 1)))))

(define (try-move! dx dy)
  (let ((nx (+ pac-x dx))
        (ny (+ pac-y dy)))
    (unless (wall? nx ny)
      (set! pac-x nx)
      (set! pac-y ny)
      (eat-pellet!)
      (when (= (pellets-left) 0)
        (next-level!)))))

(define (move-from-key! key)
  (cond
    ((key-is? key KEY-UP KEY-W) (try-move! 0 -1))
    ((key-is? key KEY-DOWN KEY-S) (try-move! 0 1))
    ((key-is? key KEY-LEFT KEY-A) (try-move! -1 0))
    ((key-is? key KEY-RIGHT KEY-D) (try-move! 1 0))
    (else #f)))

(define (move-from-held-key!)
  (cond
    ((or (sdl3 "key-down?" KEY-UP) (sdl3 "key-down?" KEY-W))
     (try-move! 0 -1))
    ((or (sdl3 "key-down?" KEY-DOWN) (sdl3 "key-down?" KEY-S))
     (try-move! 0 1))
    ((or (sdl3 "key-down?" KEY-LEFT) (sdl3 "key-down?" KEY-A))
     (try-move! -1 0))
    ((or (sdl3 "key-down?" KEY-RIGHT) (sdl3 "key-down?" KEY-D))
     (try-move! 1 0))
    (else #f)))

(define (ghost-can-move? x y dx dy)
  (not (wall? (+ x dx) (+ y dy))))

(define (move-ghost! index)
  (let ((x (vector-ref ghost-x index))
        (y (vector-ref ghost-y index))
        (dx (- pac-x (vector-ref ghost-x index)))
        (dy (- pac-y (vector-ref ghost-y index))))
    (if (> (abs dx) (abs dy))
        (if (and (not (= dx 0)) (ghost-can-move? x y (if (> dx 0) 1 -1) 0))
            (vector-set! ghost-x index (+ x (if (> dx 0) 1 -1)))
            (if (and (not (= dy 0)) (ghost-can-move? x y 0 (if (> dy 0) 1 -1)))
                (vector-set! ghost-y index (+ y (if (> dy 0) 1 -1)))
                #f))
        (if (and (not (= dy 0)) (ghost-can-move? x y 0 (if (> dy 0) 1 -1)))
            (vector-set! ghost-y index (+ y (if (> dy 0) 1 -1)))
            (if (and (not (= dx 0)) (ghost-can-move? x y (if (> dx 0) 1 -1) 0))
                (vector-set! ghost-x index (+ x (if (> dx 0) 1 -1)))
                #f)))))

(define (collision?)
  (or (and (= pac-x (vector-ref ghost-x 0)) (= pac-y (vector-ref ghost-y 0)))
      (and (= pac-x (vector-ref ghost-x 1)) (= pac-y (vector-ref ghost-y 1)))
      (and (= pac-x (vector-ref ghost-x 2)) (= pac-y (vector-ref ghost-y 2)))))

(define (resolve-collision!)
  (when (collision?)
    (set! lives (- lives 1))
    (reset-positions!)
    (when (= lives 0)
      (set! running #f))))

(define (draw-tile x y ch)
  (let ((sx (+ ORIGIN-X (* x TILE)))
        (sy (+ ORIGIN-Y (* y TILE))))
    (cond
      ((string=? ch "#")
       (sdl3 "color" 25 70 170 255)
       (sdl3 "fill-rect" sx sy (- TILE 1) (- TILE 1)))
      ((and (string=? ch ".") (not (eaten? x y)))
       (sdl3 "color" 245 220 80 255)
       (sdl3 "fill-rect" (+ sx 9) (+ sy 9) 6 6))
      (else #f))))

(define (draw-board)
  (let rows ((ys board) (y 0))
    (unless (null? ys)
      (let cols ((x 0))
        (unless (= x WIDTH)
          (draw-tile x y (string-ref (car ys) x))
          (cols (+ x 1))))
      (rows (cdr ys) (+ y 1)))))

(define (draw-actor x y r g b)
  (sdl3 "color" r g b 255)
  (sdl3 "fill-rect"
        (+ ORIGIN-X (* x TILE) 3)
        (+ ORIGIN-Y (* y TILE) 3)
        (- TILE 7)
        (- TILE 7)))

(define (draw-actors)
  (draw-actor pac-x pac-y 245 220 40)
  (draw-actor (vector-ref ghost-x 0) (vector-ref ghost-y 0) 230 50 55)
  (draw-actor (vector-ref ghost-x 1) (vector-ref ghost-y 1) 220 80 190)
  (draw-actor (vector-ref ghost-x 2) (vector-ref ghost-y 2) 60 210 230))

(define (draw-ui)
  (nuklear "begin-frame")
  (when (nuklear "begin" "SCHEMEMAN" 560 70 210 330)
    (nuklear "row" 28 1)
    (nuklear "label"
             (string-append "LEVEL "
                            (number->string (+ level-index 1))
                            " / 4"))
    (nuklear "row" 28 1)
    (nuklear "label" (string-append "SCORE " (number->string score)))
    (nuklear "row" 28 1)
    (nuklear "label" (string-append "LIVES " (number->string lives)))
    (nuklear "row" 28 1)
    (nuklear "label" (string-append "PELLETS "
                                    (number->string (pellets-left))))
    (nuklear "row" 34 1)
    (nuklear "label" "ARROWS / WASD")
    (nuklear "row" 34 1)
    (when (nuklear "button" "QUIT")
      (set! running #f))
    (nuklear "end"))
  (nuklear "render"))

(define (handle-event event)
  (cond
    ((= (plist-ref event 'type) SDL_EVENT_QUIT)
     (set! running #f))
    ((= (plist-ref event 'type) SDL_EVENT_KEY_DOWN)
     (when (= (plist-ref event 'key) KEY-ESC)
       (set! running #f)))
    (else #f)))

(define (drain-events)
  (let ((event (sdl3 "event")))
    (when event
      (handle-event event)
      (drain-events))))

(sdl3 "init" 800 500 "Schememan")
(set! last-step (sdl3 "ticks"))

(let game-loop ()
  (sdl3 "clear" 5 8 18 255)
  (sdl3 "begin-frame")
  (drain-events)
  (let ((now (sdl3 "ticks")))
    (when (>= (- now last-step) 120)
      (set! last-step now)
      (unless won
        (move-from-held-key!)
        (move-ghost! 0)
        (move-ghost! 1)
        (move-ghost! 2)
        (resolve-collision!))))
  (unless won
    (draw-board)
    (draw-actors))
  (draw-ui)
  (sdl3 "present")
  (if won
      (set! running #f)
      #f)
  (when running
    (sdl3 "delay" 16)
    (game-loop)))

(sdl3 "shutdown")
(if won
    (begin (display "SCHEMEMAN: FOUR LEVELS CLEARED\n")
           (display (string-append "FINAL SCORE " (number->string score) "\n")))
    (display (string-append "SCHEMEMAN: GAME OVER, SCORE "
                            (number->string score)
                            "\n")))
