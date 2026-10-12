(define-module (termbox)
  #:export (termbox
            TB_DEFAULT TB_BLACK TB_RED TB_GREEN TB_YELLOW TB_BLUE
            TB_MAGENTA TB_CYAN TB_WHITE TB_BOLD TB_UNDERLINE TB_REVERSE
            TB_ITALIC
            TB_KEY_ARROW_UP TB_KEY_ARROW_DOWN TB_KEY_ARROW_LEFT
            TB_KEY_ARROW_RIGHT TB_KEY_ENTER TB_KEY_ESC))

(load-extension "libswamp-termbox" "init_swamp_termbox")
