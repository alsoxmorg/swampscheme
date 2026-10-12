; Rogue Arena -- a tiny turn-based roguelike for swampscheme's termbox bridge.
;
; Run with:
;   ./swampscheme scripts/roguearena.scm
; Guile
;   guile scripts/
; Move with arrows or hjkl.  Stand on > or < and press that key to change
; floors.  Press i for inventory, or d followed by 1-9 to drop an item.
; q/Esc quits.

(define (tb cmd . args)
  (apply termbox (cons cmd args)))

(define WIDTH 20)
(define HEIGHT 20)
(define MAP-SIZE (* WIDTH HEIGHT))
(define STATUS-Y 20)
(define HELP-Y 21)

(define dungeon (make-vector MAP-SIZE "."))
(define entities '())
(define items '())
(define inventory '())
(define portals '())
(define level 0)
(define turn 0)
(define message "Find the stairs.  The dungeon is not empty.")
(define running #t)
(define inventory-open? #f)

(define actor
  (make-record 'actor
    '((glyph . "@") (x . 10) (y . 10) (kind . player)
      (hp . 12) (max-hp . 12) (alive . #t)
      (weapon . 0) (weapon-name . "bare hands")
      (armor . 0) (armor-name . "ordinary clothes"))))

(define (tile-index x y)
  (+ x (* y WIDTH)))

(define (tile-at x y)
  (vector-ref dungeon (tile-index x y)))

(define (set-tile! x y tile)
  (vector-set! dungeon (tile-index x y) tile))

(define (actor-at field)
  (record-ref actor field))

(define (actor-set! field value)
  (record-set! actor field value))

(define (entity-at x y)
  (let loop ((items entities))
    (cond
      ((null? items) #f)
      ((and (record-ref (car items) 'alive)
            (= (record-ref (car items) 'x) x)
            (= (record-ref (car items) 'y) y))
       (car items))
      (else (loop (cdr items))))))

(define (inside? x y)
  (and (>= x 0) (< x WIDTH) (>= y 0) (< y HEIGHT)))

(define (wall? x y)
  (or (not (inside? x y)) (equal? (tile-at x y) "#")))

(define (walkable? x y)
  (and (inside? x y)
       (not (wall? x y))
       (not (entity-at x y))))

(define (item-at x y)
  (let loop ((things items))
    (cond
      ((null? things) #f)
      ((and (= (record-ref (car things) 'x) x)
            (= (record-ref (car things) 'y) y))
       (car things))
      (else (loop (cdr things))))))

(define (add-entity! glyph kind x y hp . weapon)
  (set! entities
        (cons (make-record 'actor
                           (list (cons 'glyph glyph) (cons 'x x) (cons 'y y)
                                 (cons 'kind kind) (cons 'hp hp)
                                 (cons 'max-hp hp) (cons 'alive #t)
                                 (cons 'weapon (if (null? weapon) 0 (car weapon)))))
              entities)))

(define (add-item! kind name glyph x y power identified effect)
  (set! items
        (cons (make-record 'item
                           (list (cons 'kind kind) (cons 'name name)
                                 (cons 'glyph glyph) (cons 'x x) (cons 'y y)
                                 (cons 'power power)
                                 (cons 'identified identified)
                                 (cons 'effect effect)))
              items)))

(define (remove-item! target things)
  (cond
    ((null? things) '())
    ((eq? (car things) target) (cdr things))
    (else (cons (car things) (remove-item! target (cdr things))))))

(define (pickup-at-player!)
  (let ((found (item-at (actor-at 'x) (actor-at 'y))))
    (when found
      (set! items (remove-item! found items))
      (set! inventory (cons found inventory))
      (set! message (string-append "Picked up "
                                   (record-ref found 'name) ".")))))

(define (clear-dungeon!)
  (let fill ((i 0))
    (when (< i MAP-SIZE)
      (vector-set! dungeon i ".")
      (fill (+ i 1)))))

(define (line-wall! x1 y1 x2 y2)
  (if (= y1 y2)
      (let loop ((x (min x1 x2)))
        (when (<= x (max x1 x2))
          (set-tile! x y1 "#")
          (loop (+ x 1))))
      (let loop ((y (min y1 y2)))
        (when (<= y (max y1 y2))
          (set-tile! x1 y "#")
          (loop (+ y 1))))))

(define (scatter! tile count seed)
  ; A deterministic pattern keeps the arena readable while every floor still
  ; gets a little different terrain.
  (let loop ((i 0))
    (when (< i count)
      (let ((x (+ 1 (modulo (+ (* i 7) seed) 18)))
            (y (+ 1 (modulo (+ (* i 11) seed) 18))))
        (when (equal? (tile-at x y) ".")
          (set-tile! x y tile)))
      (loop (+ i 1)))))

(define (depth)
  (abs level))

(define (weapon-name-and-power)
  (let ((d (depth)))
    (cond
      ((<= d 1)
       (list-ref '("stick" "baton" "cane" "staff" "bone knife")
                 (random-integer 5)))
      ((<= d 3)
       (list-ref '("rusty knife" "rusty hatchet" "iron club"
                   "iron spear" "bronze dagger")
                 (random-integer 5)))
      ((<= d 6)
       (list-ref '("bronze sword" "iron mace" "iron sword"
                   "steel knife" "tempered spear")
                 (random-integer 5)))
      (else
       (list-ref '("steel sword" "steel axe" "silvered blade"
                   "black iron mace" "runed spear")
                 (random-integer 5))))))

(define (weapon-power name)
  (cond
    ((or (equal? name "stick") (equal? name "baton")
         (equal? name "cane") (equal? name "bone knife")) 1)
    ((or (equal? name "staff") (equal? name "rusty knife")
         (equal? name "rusty hatchet") (equal? name "iron club")) 2)
    ((or (equal? name "iron spear") (equal? name "bronze dagger")
         (equal? name "bronze sword") (equal? name "iron mace")) 3)
    ((or (equal? name "iron sword") (equal? name "steel knife")
         (equal? name "tempered spear") (equal? name "steel axe")) 4)
    (else 5)))

(define (armor-name-and-power)
  (let ((d (depth)))
    (cond
      ((<= d 1) (list-ref '("robe" "tunic" "padded jacket") (random-integer 3)))
      ((<= d 3) (list-ref '("thick robe" "leather vest" "studded jerkin")
                           (random-integer 3)))
      ((<= d 6) (list-ref '("chain shirt" "bronze cuirass" "iron coat")
                           (random-integer 3)))
      (else (list-ref '("iron mail" "steel cuirass" "runed armor")
                      (random-integer 3))))))

(define (armor-power name)
  (cond
    ((or (equal? name "robe") (equal? name "tunic")) 1)
    ((equal? name "padded jacket") 2)
    ((or (equal? name "thick robe") (equal? name "leather vest")) 2)
    ((equal? name "studded jerkin") 3)
    ((or (equal? name "chain shirt") (equal? name "bronze cuirass")) 4)
    (else 5)))

(define (place-item! kind name glyph power identified effect)
  (let place-loop ((attempt 0))
    (if (>= attempt 100)
        #f
        (let ((x (+ 1 (random-integer 18)))
              (y (+ 1 (random-integer 18))))
          (if (and (equal? (tile-at x y) ".")
                   (not (entity-at x y))
                   (not (item-at x y)))
              (begin (add-item! kind name glyph x y power identified effect) #t)
              (place-loop (+ attempt 1)))))))

(define (place-entity-at! glyph kind x y hp weapon)
  (if (and (equal? (tile-at x y) ".")
           (not (entity-at x y))
           (not (item-at x y)))
      (begin (add-entity! glyph kind x y hp weapon) #t)
      #f))

(define (place-random-entity! glyph kind hp weapon)
  (let place-loop ((attempt 0))
    (if (>= attempt 100)
        #f
        (let ((x (+ 1 (random-integer 18)))
              (y (+ 1 (random-integer 18))))
          (if (place-entity-at! glyph kind x y hp weapon)
              #t
              (place-loop (+ attempt 1)))))))

(define (survivor-count)
  (cond
    ((= (depth) 0) 4)
    ((= (depth) 1) 3)
    ((= (depth) 2) 2)
    ((= (depth) 3) 1)
    (else 0)))

(define (scatter-survivors!)
  (let loop ((n 0))
    (when (< n (survivor-count))
      (if (= (random-integer 2) 0)
          (place-random-entity! "@" 'human 4 0)
          (place-random-entity! "e" 'elf 4 0))
      (loop (+ n 1))))
  ; Stairs are crossroads: occasionally somebody arrives after you.
  (when (and (> (depth) 0) (= (random-integer 3) 0))
    (if (= (random-integer 2) 0)
        (place-random-entity! "@" 'human 4 0)
        (place-random-entity! "e" 'elf 4 0))))

(define (add-portal!)
  (let place-loop ((attempt 0))
    (when (< attempt 100)
      (let ((x (+ 1 (random-integer 18)))
            (y (+ 1 (random-integer 18))))
        (if (and (equal? (tile-at x y) ".")
                 (not (entity-at x y))
                 (not (item-at x y)))
            (begin
              (set-tile! x y "P")
              (set! portals (cons (cons x y) portals)))
            (place-loop (+ attempt 1)))))))

(define (portal-monster!)
  (let ((d (depth)))
    (cond
      ((and (>= d 9) (= (random-integer 3) 0))
       (list "D" 'demon 14 4))
      ((and (>= d 7) (= (random-integer 4) 0))
       (list "T" 'minotaur 20 5))
      ((and (>= d 5) (= (random-integer 2) 0))
       (list "w" 'spider 7 2))
      ((>= d 4) (list "m" 'slime 6 1))
      (else #f))))

(define (portal-turn!)
  (let loop ((gates portals))
    (unless (null? gates)
      (when (= (random-integer 12) 0)
        (let ((monster (portal-monster!))
              (gate (car gates)))
          (when monster
            (let ((dx (list-ref '(-1 0 1) (random-integer 3)))
                  (dy (list-ref '(-1 0 1) (random-integer 3))))
              (when (place-entity-at! (list-ref monster 0)
                                      (list-ref monster 1)
                                      (+ (car gate) dx) (+ (cdr gate) dy)
                                      (list-ref monster 2)
                                      (list-ref monster 3))
                (set! message "Something crawls out of a dangerous portal."))))))
      (loop (cdr gates)))))

(define (scatter-items!)
  (set! items '())
  (let weapons ((n 0))
    (when (< n 12)
      (let* ((name (weapon-name-and-power))
             (power (weapon-power name)))
        (place-item! 'weapon name ")" power #t #f))
      (weapons (+ n 1))))
  (let armor ((n 0))
    (when (< n 6)
      (let* ((name (armor-name-and-power))
             (power (armor-power name)))
        (place-item! 'armor name "[" power #t #f))
      (armor (+ n 1))))
  (let potions ((n 0))
    (when (< n 8)
      (place-item! 'potion "unidentified potion" "!" 0 #f
                   (list-ref '(heal harm haste toughen confuse) (random-integer 5)))
      (potions (+ n 1))))
  (let bandages ((n 0))
    (when (< n 5)
      (place-item! 'bandage "bandage" "+" 4 #t 'heal)
      (bandages (+ n 1)))))

(define (build-floor! new-level)
  (set! level new-level)
  (set! entities '())
  (set! items '())
  (set! portals '())
  (clear-dungeon!)
  ; Border and a few chunky interior walls make a compact arena.
  (let border ((i 0))
    (when (< i WIDTH)
      (set-tile! i 0 "#")
      (set-tile! i (- HEIGHT 1) "#")
      (set-tile! 0 i "#")
      (set-tile! (- WIDTH 1) i "#")
      (border (+ i 1))))
  (line-wall! 4 3 4 8)
  (line-wall! 4 8 8 8)
  (line-wall! 12 3 16 3)
  (line-wall! 16 3 16 7)
  (line-wall! 11 11 16 11)
  (line-wall! 11 11 11 16)
  (line-wall! 3 14 8 14)
  (line-wall! 8 14 8 17)
  (scatter! "," 26 (+ 3 (* 17 level)))
  (scatter! "+" 18 (+ 9 (* 13 level)))
  (scatter! "~" 10 (+ 4 (* 19 level)))
  (set-tile! 2 2 ">")
  (set-tile! 17 17 "<")
  ; The player begins near the center on floor zero, or by the stair used.
  (let ((px (if (= new-level 0) 10 (if (> new-level 0) 17 2)))
        (py (if (= new-level 0) 10 (if (> new-level 0) 17 2))))
    (actor-set! 'x px)
    (actor-set! 'y py))
  ; A few friendly-to-nobody humans and elves, plus hostile denizens.
  (add-entity! "g" 'goblin 6 2 3
               (if (= (random-integer 2) 0) (random-integer 3) 0))
  (add-entity! "g" 'goblin 14 5 3
               (if (= (random-integer 2) 0) (random-integer 3) 0))
  (add-entity! "o" 'orc 14 15 5
               (if (= (random-integer 2) 0) (+ 1 (random-integer 4)) 0))
  (add-entity! "s" 'skeleton 6 16 3)
  (add-entity! "z" 'zombie 17 8 4)
  (when (>= (depth) 6)
    (place-random-entity! "w" 'spider 7 2))
  (when (>= (depth) 8)
    (place-random-entity! "T" 'minotaur 20 5))
  (scatter-survivors!)
  (when (>= (depth) 4)
    (let portals-to-add ((n 0))
      (when (< n (if (>= (depth) 7) 2 1))
        (add-portal!)
        (portals-to-add (+ n 1)))))
  (scatter-items!))

(define (hostile? thing)
  (member (record-ref thing 'kind)
          '(goblin orc skeleton zombie spider slime minotaur demon)))

(define (damage! target amount)
  (let ((hp (- (record-ref target 'hp) amount)))
    (record-set! target 'hp hp)
    (when (<= hp 0)
      (record-set! target 'alive #f))))

(define (combat! target)
  (if (hostile? target)
      (begin
        (damage! target (+ 2 (actor-at 'weapon)))
        (if (record-ref target 'alive)
            (set! message (string-append "You hit the "
                                         (symbol->string (record-ref target 'kind))
                                         "."))
            (set! message "The denizen falls.")))
      (set! message "That survivor will not join you.")))

(define (try-player-move! dx dy)
  (let ((nx (+ (actor-at 'x) dx))
        (ny (+ (actor-at 'y) dy)))
    (cond
      ((wall? nx ny) (set! message "A wall blocks your way."))
      ((entity-at nx ny) (combat! (entity-at nx ny)))
      (else
       (actor-set! 'x nx)
       (actor-set! 'y ny)
       (set! message "")
       (pickup-at-player!)))))

(define (distance x1 y1 x2 y2)
  (+ (abs (- x1 x2)) (abs (- y1 y2))))

(define (try-entity-move! thing dx dy)
  (let ((nx (+ (record-ref thing 'x) dx))
        (ny (+ (record-ref thing 'y) dy)))
    (when (and (walkable? nx ny)
               (not (and (= nx (actor-at 'x))
                         (= ny (actor-at 'y)))))
      (record-set! thing 'x nx)
      (record-set! thing 'y ny))))

(define (adjacent-hostile x y)
  (let loop ((spots (list (cons (+ x 1) y) (cons (- x 1) y)
                          (cons x (+ y 1)) (cons x (- y 1)))))
    (if (null? spots)
        #f
        (let ((target (entity-at (car (car spots)) (cdr (car spots)))))
          (if (and target (hostile? target))
              target
              (loop (cdr spots)))))))

(define (step-toward! thing dx dy)
  (if (not (= dx 0))
      (try-entity-move! thing dx 0)
      (try-entity-move! thing 0 dy)))

(define (entity-turn! thing)
  (when (record-ref thing 'alive)
    (let* ((x (record-ref thing 'x))
           (y (record-ref thing 'y))
           (px (actor-at 'x))
           (py (actor-at 'y))
           (dx (if (< px x) -1 (if (> px x) 1 0)))
           (dy (if (< py y) -1 (if (> py y) 1 0))))
      (if (and (hostile? thing) (= (distance x y px py) 1))
          (begin
            (damage! actor (max 1 (- (+ 1 (record-ref thing 'weapon))
                                     (actor-at 'armor))))
            (set! message (if (> (actor-at 'hp) 0)
                              "A denizen claws you."
                              "You have fallen.  Press q to leave.")))
          (let ((nearby (adjacent-hostile x y)))
            (if nearby
                (begin
                  (damage! nearby 1)
                  (set! message "A survivor strikes at a denizen."))
                (if (and (hostile? thing) (<= (distance x y px py) 8))
                    (step-toward! thing dx dy)
                    (when (= (random-integer 3) 0)
                      (try-entity-move! thing
                                        (list-ref '(-1 0 1) (random-integer 3))
                                        (list-ref '(-1 0 1) (random-integer 3)))))))))))

(define (clean-dead! items)
  (cond
    ((null? items) '())
    ((record-ref (car items) 'alive)
     (cons (car items) (clean-dead! (cdr items))))
    (else (clean-dead! (cdr items)))))

(define (advance-turn!)
  (set! turn (+ turn 1))
  (let loop ((items entities))
    (unless (null? items)
      (entity-turn! (car items))
      (loop (cdr items))))
  (portal-turn!)
  (set! entities (clean-dead! entities)))

(define (tile-color tile)
  (cond
    ((equal? tile "#") (bitwise-ior TB_WHITE TB_BOLD))
    ((equal? tile "~") TB_BLUE)
    ((equal? tile "+") TB_YELLOW)
    ((equal? tile ",") TB_GREEN)
    ((equal? tile "P") (bitwise-ior TB_MAGENTA TB_BOLD))
    ((or (equal? tile ">") (equal? tile "<"))
     (bitwise-ior TB_CYAN TB_BOLD))
    (else TB_WHITE)))

(define (entity-color thing)
  (cond
    ((eq? (record-ref thing 'kind) 'human) TB_WHITE)
    ((eq? (record-ref thing 'kind) 'elf) (bitwise-ior TB_YELLOW TB_BOLD))
    ((eq? (record-ref thing 'kind) 'minotaur) (bitwise-ior TB_RED TB_BOLD))
    ((eq? (record-ref thing 'kind) 'demon) (bitwise-ior TB_MAGENTA TB_BOLD))
    ((eq? (record-ref thing 'kind) 'slime) TB_CYAN)
    (else (bitwise-ior TB_GREEN TB_BOLD))))

(define (item-color thing)
  (cond
    ((eq? (record-ref thing 'kind) 'weapon) TB_YELLOW)
    ((eq? (record-ref thing 'kind) 'armor) TB_CYAN)
    ((eq? (record-ref thing 'kind) 'potion) TB_MAGENTA)
    (else TB_WHITE)))

(define (draw-ground-items!)
  (let loop ((things items))
    (unless (null? things)
      (tb 'set-cell (record-ref (car things) 'x) (record-ref (car things) 'y)
          (record-ref (car things) 'glyph) (item-color (car things)) TB_DEFAULT)
      (loop (cdr things)))))

(define (draw!)
  (tb tb_clear)
  (let rows ((y 0))
    (when (< y HEIGHT)
      (let cols ((x 0))
        (when (< x WIDTH)
          (let ((tile (tile-at x y)))
            (tb 'set-cell x y tile (tile-color tile) TB_DEFAULT))
          (cols (+ x 1))))
      (rows (+ y 1))))
  (draw-ground-items!)
  (let draw-entities ((items entities))
    (unless (null? items)
      (let ((thing (car items)))
        (when (record-ref thing 'alive)
          (tb 'set-cell (record-ref thing 'x) (record-ref thing 'y)
              (record-ref thing 'glyph) (entity-color thing) TB_DEFAULT)))
      (draw-entities (cdr items))))
  (tb 'set-cell (actor-at 'x) (actor-at 'y) "@"
      (bitwise-ior TB_CYAN TB_BOLD) TB_DEFAULT)
  ;; The 20-row map fits terminals that are at least 22 rows tall.  Smaller
  ;; terminals must not send print-at outside the screen: termbox rejects
  ;; y=20 on a 20-row terminal, which used to abort the Guile port.
  (when (< STATUS-Y (tb 'height))
    (tb 'print-at 0 STATUS-Y TB_WHITE TB_DEFAULT
        (string-append "floor " (number->string level)
                       "  hp " (number->string (max 0 (actor-at 'hp)))
                       "/" (number->string (actor-at 'max-hp))
                       "  turn " (number->string turn) "  " message)))
  (when (< HELP-Y (tb 'height))
    (tb 'print-at 0 HELP-Y TB_CYAN TB_DEFAULT
        "hjkl/arrows move  >/< stairs  i inventory  d# drop  q quit"))
  (tb tb_present))

(define (inventory-use! index)
  (if (>= index (length inventory))
      (set! message "There is no item in that slot.")
      (let ((thing (list-ref inventory index)))
        (set! inventory (remove-item! thing inventory))
        (cond
          ((eq? (record-ref thing 'kind) 'weapon)
           (actor-set! 'weapon (record-ref thing 'power))
           (actor-set! 'weapon-name (record-ref thing 'name))
           (set! message (string-append "Equipped " (record-ref thing 'name) ".")))
          ((eq? (record-ref thing 'kind) 'armor)
           (actor-set! 'armor (record-ref thing 'power))
           (actor-set! 'armor-name (record-ref thing 'name))
           (set! message (string-append "Wearing " (record-ref thing 'name) ".")))
          ((eq? (record-ref thing 'kind) 'bandage)
           (actor-set! 'hp (min (actor-at 'max-hp) (+ (actor-at 'hp) 4)))
           (set! message "You bind your wounds."))
          (else
           (cond
             ((eq? (record-ref thing 'effect) 'heal)
              (actor-set! 'hp (min (actor-at 'max-hp) (+ (actor-at 'hp) 7)))
              (set! message "The potion restores your health."))
             ((eq? (record-ref thing 'effect) 'harm)
              (damage! actor 3)
              (set! message "The potion burns!"))
             ((eq? (record-ref thing 'effect) 'toughen)
              (actor-set! 'armor (+ (actor-at 'armor) 2))
              (set! message "Your skin hardens like bark."))
             ((eq? (record-ref thing 'effect) 'haste)
              (set! message "Your limbs buzz with uncanny speed."))
             (else (set! message "The potion tastes like moonlight."))))))))

(define (inventory-drop! index)
  (if (>= index (length inventory))
      (begin
        (set! message "There is no item in that slot.")
        #f)
      (let ((thing (list-ref inventory index)))
        (set! inventory (remove-item! thing inventory))
        (record-set! thing 'x (actor-at 'x))
        (record-set! thing 'y (actor-at 'y))
        (set! items (cons thing items))
        (set! message (string-append "Dropped "
                                     (record-ref thing 'name) "."))
        #t)))

(define (draw-inventory! . drop-mode)
  (let ((dropping (not (null? drop-mode))))
  (tb tb_clear)
  (tb 'print-at 1 1 (bitwise-ior TB_CYAN TB_BOLD) TB_DEFAULT
      (if dropping
          "DROP ITEM -- press 1-9 to drop, d or Esc to cancel"
          "INVENTORY -- press 1-9 to use, i or Esc to close"))
  (tb 'print-at 1 3 TB_WHITE TB_DEFAULT
      (string-append "weapon: " (actor-at 'weapon-name)
                     "  attack +" (number->string (actor-at 'weapon))))
  (tb 'print-at 1 4 TB_WHITE TB_DEFAULT
      (string-append "armor:  " (actor-at 'armor-name)
                     "  AC +" (number->string (actor-at 'armor))))
  (let loop ((index 0))
    (when (< index (length inventory))
      (let ((thing (list-ref inventory index)))
        (tb 'print-at 1 (+ 6 index) (item-color thing) TB_DEFAULT
            (string-append (number->string (+ index 1)) ") "
                           (record-ref thing 'name)
                           (if (eq? (record-ref thing 'kind) 'weapon)
                               (string-append "  attack +"
                                              (number->string (record-ref thing 'power)))
                               (if (eq? (record-ref thing 'kind) 'armor)
                                   (string-append "  AC +"
                                                  (number->string (record-ref thing 'power)))
                                   ""))))
      (loop (+ index 1))))
  (when (null? inventory)
    (tb 'print-at 1 6 TB_WHITE TB_DEFAULT "Your pack is empty."))
  (tb tb_present))))

(define (inventory-loop)
  (draw-inventory!)
  (let ((ev (tb tb_poll_event)))
    (cond
      ((or (= (plist-ref ev 'key 0) TB_KEY_ESC)
           (= (plist-ref ev 'ch 0) 105)) #t)
      ((or (= (plist-ref ev 'key 0) TB_KEY_CTRL_C)
           (= (plist-ref ev 'ch 0) 113)) (set! running #f))
      ((and (>= (plist-ref ev 'ch 0) 49)
            (<= (plist-ref ev 'ch 0) 57))
       (inventory-use! (- (plist-ref ev 'ch 0) 49))
       (inventory-loop))
      (else (inventory-loop)))))

(define (drop-loop)
  (draw-inventory! #t)
  (let ((ev (tb tb_poll_event)))
    (cond
      ((or (= (plist-ref ev 'key 0) TB_KEY_ESC)
           (= (plist-ref ev 'ch 0) 100)) #t)
      ((or (= (plist-ref ev 'key 0) TB_KEY_CTRL_C)
           (= (plist-ref ev 'ch 0) 113)) (set! running #f))
      ((and (>= (plist-ref ev 'ch 0) 49)
            (<= (plist-ref ev 'ch 0) 57))
       (when (inventory-drop! (- (plist-ref ev 'ch 0) 49))
         (advance-turn!)))
      (else (drop-loop)))))

(define (activate-stairs! direction)
  (let ((tile (tile-at (actor-at 'x) (actor-at 'y))))
    (cond
      ((and (= direction 1) (equal? tile ">"))
       (build-floor! (+ level 1))
       (set! message "You climb upward.  The air grows thin.")
       #t)
      ((and (= direction -1) (equal? tile "<"))
       (build-floor! (- level 1))
       (set! message "You descend.  Something below is awake.")
       #t)
      (else
       (set! message (if (= direction 1)
                         "You are not standing on an up stair."
                         "You are not standing on a down stair."))
       #f))))

(define (quit-event? ev)
  (or (= (plist-ref ev 'key 0) TB_KEY_ESC)
      (= (plist-ref ev 'key 0) TB_KEY_CTRL_C)
      (= (plist-ref ev 'ch 0) 113)))

(define (move-event ev)
  (let ((key (plist-ref ev 'key 0))
        (ch (plist-ref ev 'ch 0)))
    (cond
      ((or (= key TB_KEY_ARROW_UP) (= ch 107)) '(0 . -1))
      ((or (= key TB_KEY_ARROW_DOWN) (= ch 106)) '(0 . 1))
      ((or (= key TB_KEY_ARROW_LEFT) (= ch 104)) '(-1 . 0))
      ((or (= key TB_KEY_ARROW_RIGHT) (= ch 108)) '(1 . 0))
      (else #f))))

(build-floor! 0)
(tb tb_init)
(tb 'hide-cursor)
(tb 'output-mode TB_OUTPUT_256)
(let loop ()
  (when running
    (draw!)
    (let ((ev (tb tb_poll_event)))
      (unless (quit-event? ev)
        (cond
          ((= (plist-ref ev 'ch 0) 105) (inventory-loop))
          ((= (plist-ref ev 'ch 0) 100) (drop-loop))
          ((= (plist-ref ev 'ch 0) 62)
           (when (activate-stairs! 1) (advance-turn!)))
          ((= (plist-ref ev 'ch 0) 60)
           (when (activate-stairs! -1) (advance-turn!)))
          ((and (= (plist-ref ev 'type 0) TB_EVENT_KEY)
                (move-event ev))
           (let ((delta (move-event ev)))
             (try-player-move! (car delta) (cdr delta))
             (when (> (actor-at 'hp) 0) (advance-turn!))))
          (else (set! message "Use hjkl, arrows, >, <, i, or d#.")))
        (when (> (actor-at 'hp) 0) (loop))))))
(tb tb_shutdown)
