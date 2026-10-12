; Optional SDL3/Nuklear demo. Pass an image path as the second argument to
; exercise stb_image texture loading:
;   ./swampscheme scripts/sdl3_demo.scm image.png

(define argv (command-line))
(sdl3 "init" 800 500 "swampscheme SDL3")
(define texture
  (if (> (length argv) 2)
      (sdl3 "texture-load" (list-ref argv 2))
      #f))
(define done #f)

(let loop ()
  (sdl3 "clear" 18 24 32 255)
  (sdl3 "color" 80 190 150 255)
  (sdl3 "fill-rect" 40 40 220 120)
  (sdl3 "color" 240 190 80 255)
  (sdl3 "rect" 300 40 220 120)
  (sdl3 "line" 0 0 800 500)
  (if texture (sdl3 "blit" texture 580 40 160 120) #f)

  (sdl3 "begin-frame")
  (let events ()
    (let ((event (sdl3 "event")))
      (if event
          (begin
            (if (= (plist-ref event 'type) SDL_EVENT_QUIT)
                (set! done #t)
                #f)
            (events))
          #f)))
  (nuklear "begin-frame")
  (if (nuklear "begin" "Swamp Scheme" 40 220 360 210)
      (begin
        (nuklear "row" 30 1)
        (nuklear "label" "SDL3 + Nuklear + stb_image")
        (nuklear "row" 30 1)
        (if (nuklear "button" "Quit") (set! done #t) #f)
        (nuklear "end"))
      #f)
  (nuklear "render")
  (sdl3 "present")
  (if done #f (loop)))

(if texture (sdl3 "texture-destroy" texture) #f)
(sdl3 "shutdown")
