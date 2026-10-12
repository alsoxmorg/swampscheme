# Optional SDL3 and Nuklear support

Build the graphics-enabled interpreter with:

```sh
make WITH_SDL3=1
```

The default build has no SDL3 or Nuklear dependency. `WITH_SDL3` enables both
together; there is intentionally no SDL3-without-Nuklear build.

## Drawing

The `sdl3` primitive owns one window and renderer:

```scheme
(sdl3 "init" 800 600 "Swamp window")
(sdl3 "clear" 20 25 30 255)
(sdl3 "color" 220 80 60 255)
(sdl3 "fill-rect" 20 20 160 90)
(sdl3 "rect" 200 20 160 90)
(sdl3 "line" 0 0 800 600)
(sdl3 "pixel" 400 300)
(sdl3 "present")
(sdl3 "shutdown")
```

Coordinates and dimensions are numeric Scheme values. Colors are RGBA bytes.

Timing and window control:

```scheme
(sdl3 "ticks")
(sdl3 "delay" 16)
(sdl3 "window-size")
(sdl3 "set-window-size" 1024 768)
(sdl3 "fullscreen" #t)
(sdl3 "fullscreen" #f)
```

`ticks` returns milliseconds since SDL startup and `delay` sleeps for the
requested number of milliseconds.
`texture-load` uses the bundled `stb_image.h`, supports its usual PNG/JPEG/BMP
and other image formats, and returns an opaque texture value. Use it with
`blit texture x y` or `blit texture x y width height`; `texture-size` returns
`(width height)`. Call `texture-destroy` when it is no longer needed.

## Events and Nuklear

Poll SDL events as flat property lists. Events are also forwarded to Nuklear automatically:

```scheme
(sdl3 "begin-frame")
(let loop ()
  (let ((event (sdl3 "event")))
    (if event
        (if (= (plist-ref event 'type) SDL_EVENT_QUIT) #f (loop))
        #t)))
(nuklear "begin-frame")
(when (nuklear "begin" "Hello" 40 40 300 180)
  (nuklear "row" 30 1)
  (nuklear "label" "Hello from Scheme")
  (nuklear "row" 30 1)
  (when (nuklear "button" "Close") (display "clicked"))
  (nuklear "end"))
(nuklear "render")
(sdl3 "present")
```

`nuklear "render"` ends the active frame after widgets have been emitted.
`nuklear "begin-frame"` is the input-finalization step between SDL event
polling and widget construction. Call `sdl3 "shutdown"` on exit; the runtime
also performs this cleanup at process exit.

Event values include `type`, `window`, `key`, `scancode`, `mod`, `repeat`,
`x`, `y`, `button`, and `text`. Use `plist-ref` and interpret fields according
to the event type. `SDL_EVENT_WINDOW_RESIZED` is exported for resize handling;
query the resulting drawable size with `window-size`.

SDL key constants and held-key queries are available in SDL builds:

```scheme
(sdl3 "key-down?" SDLK_LEFT)
(sdl3 "key-down?" SDLK_SPACE)
(sdl3 "clipboard-set" "copied text")
(define pasted (sdl3 "clipboard-get"))
(sdl3 "text-input" #t)
```

Exported constants include `SDLK_ESCAPE`, `SDLK_RETURN`, `SDLK_UP`, `SDLK_DOWN`, `SDLK_LEFT`,
`SDLK_RIGHT`, `SDLK_BACKSPACE`, `SDLK_DELETE`, `SDLK_TAB`, `SDLK_HOME`,
`SDLK_END`, `SDLK_W`, `SDLK_A`, `SDLK_S`, `SDLK_D`, `SDLK_C`, `SDLK_V`,
`SDLK_X`, `SDLK_Z`, and `SDLK_SPACE`. Modifier masks include
`SDL_KMOD_CTRL`, `SDL_KMOD_SHIFT`, and `SDL_KMOD_ALT`.

`text-input` starts or stops SDL text input and is intended for editor widgets.
`clipboard-set` and `clipboard-get` exchange UTF-8 text with the system
clipboard.

Nuklear font baking is enabled. Load a TrueType font before constructing
widgets with `(nuklear "font-load" "font.ttf" 18)`. The font atlas uses the
`stb_truetype` implementation bundled by Nuklear.

### Audio

The SDL build includes a small WAV playback API backed by SDL3 audio streams:

```scheme
(sdl3 "audio-init")
(define pellet (sdl3 "sound-load" "assets/pellet.wav"))
(sdl3 "sound-play" pellet)
(sdl3 "sound-destroy" pellet)
(sdl3 "audio-shutdown")
```

`load-sound` and `play-sound` are aliases. Audio initialization may fail on a
machine without a usable audio device.

An end-to-end example is in
[`scripts/sdl3_demo.scm`](scripts/sdl3_demo.scm); an optional image path
exercises `stb_image` loading.
