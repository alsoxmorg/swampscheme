#!/usr/bin/env -S guile --no-auto-compile -s
!#

(use-modules (ice-9 rdelim)
             (srfi srfi-13)
             (srfi srfi-14)
             (srfi srfi-27))

(random-source-randomize! default-random-source)

(define (read-all port)
  (let loop ((lines '()))
    (let ((line (read-line port)))
      (if (eof-object? line)
          (string-join (reverse lines) "\n")
          (loop (cons line lines))))))

(define (blank? text)
  (= 0 (string-length (string-trim-both text))))

(define (contains-any? text words)
  (let loop ((words words))
    (and (not (null? words))
         (or (string-contains text (car words))
             (loop (cdr words))))))

(define (contains-token-any? text words)
  (let ((tokens (string-tokenize text char-set:letter+digit)))
    (let loop ((words words))
      (and (not (null? words))
           (or (member (car words) tokens)
               (loop (cdr words)))))))

(define (topic-response text topics)
  (let loop ((topics topics))
    (and (not (null? topics))
         (if (contains-any? text (caar topics))
             (cdar topics)
             (loop (cdr topics))))))

(define (microsoft-windows? text)
  (or (contains-any? text '("windows 11" "window 11" "win11" "microsoft"))
      (and (contains-token-any? text '("windows" "window"))
           (contains-token-any? text '("os" "operating" "system" "computer"
                                       "pc" "software" "desktop" "laptop")))))

(define (exit-request? input)
  (contains-any? (string-downcase input)
                 '("exit" "quit" "die" "shutdown" "shut down"
                   "terminate" "halt")))

(define (random-choice items)
  (list-ref items (random-integer (length items))))

(define greeting-words
  '("hello" "hi" "hey" "howdy" "yo" "sup"))

(define math-words
  '("math" "calculate" "calculator" "equation" "algebra" "geometry"
    "trig" "calculus" "derivative" "integral" "multiply" "divide"
    "addition" "subtraction" "add " "sum" "minus" "plus" "times"
    "percent" "percentage" "fraction" "sqrt" "square root" "solve"))

(define thanks-words
  '("thanks" "thank" "thx" "appreciate"))

(define compliment-words
  '("good bot" "nice bot" "smart" "clever" "cool" "awesome" "great"))

(define insult-words
  '("swamp bot" "suck" "sucks" "stupid" "dumb" "idiot" "useless"
    "terrible" "awful"))

(define coding-words
  '("code" "program" "script" "scheme" "guile" "bug" "error" "debug"
    "compile" "function" "variable" "stdin" "stdout" "linux" "shell"
    "terminal" "command" "syntax"))

(define food-words
  '("food" "eat" "snack" "lunch" "dinner" "breakfast" "pizza" "coffee"
    "tea" "sandwich"))

;; Recipe requests. Add more trigger phrases, foods, and bad recipe answers here.
(define recipe-request-phrases
  '("recipe for" "recipe of" "how do you make" "how to make"
    "how do i make" "how can i make" "how do you cook" "how to cook"
    "how do i cook" "how can i cook"))

(define recipe-food-words
  '("bread" "cookies" "cookie" "cupcakes" "cupcake" "cake" "fried rice"
    "dirty rice" "chili" "chilli" "jambalaya" "jumblaya" "jumbalaya"
    "gumbo" "stew" "soup" "pasta" "sauce" "pancakes" "waffles"
    "biscuits" "pie" "brownies" "muffins"))

(define recipe-responses
  '("hamburger, ketchup, onions, garlic, salt, pepper, and noodles or rice. brown the meat, throw everything else in, simmer until it stops looking like evidence."
    "ground beef, onion, garlic, tomato sauce, ketchup, paprika, salt, and macaroni. cook it all together and call it dinner before anyone asks follow-up questions."
    "hamburger, rice, onions, garlic, canned tomatoes, a little ketchup, and whatever seasoning survived the cabinet. simmer until edible, which is the whole business model."
    "brown hamburger with onions and garlic, add ketchup, tomato sauce, pepper, and noodles. if it tastes boring, add salt and pretend this was regional."
    "meat, onions, garlic, ketchup, tomatoes, rice, and enough heat to make it cooperate. swamp bot has invented goulash-adjacent survival food."))

(define bread-recipe-response
  "flour, water, salt and yeast(optional) - the trick is to mix the yeast and let it and let it set BEFORE putting in the ovan, once it starts swelling like a foot with kidney diease, stuff it in the ovan at 450f and hope for the best")

(define cookie-recipe-response
  "sugar, fat, flour, egg, heat, regret. bake until the kitchen smells like confidence and the smoke alarm disagrees.")

(define jumblaya-recipe-response
  "rice, sausage, chicken, shrimp if you are feeling solvent, peppers, onions, celery, tomatoes, cajun seasoning. simmer until Louisiana starts asking questions.")

(define dirty-rice-recipe-response
  "rice, ground meat, onions, peppers, celery, garlic, stock, and enough seasoning to make plain rice ashamed of itself.")

(define (recipe-request? text)
  (contains-any? text recipe-request-phrases))

(define weather-words
  '("weather" "rain" "snow" "sunny" "cloud" "storm" "hot" "cold"
    "forecast"))

(define time-words
  '("time" "date" "today" "tomorrow" "yesterday" "calendar" "clock"
    "hour" "minute" "day" "month" "year"))

(define clock-words
  '("time" "clock" "hour" "minute"))

(define date-words
  '("date" "today" "tomorrow" "yesterday" "calendar" "day" "month" "year"))

(define command-words
  '("do " "make " "build " "write " "create " "fix " "explain "
    "tell me" "show me" "give me"))

(define book-request-words
  '("book" "books" "read" "reading" "novel" "novels"))

(define movie-words
  '("movie" "movies" "film" "films" "cinema"))

(define airplane-words
  '("airplane" "airplanes" "aircraft" "plane" "planes" "jet" "jets"
    "airport" "airline" "flight" "flights"))

(define crypto-words
  '("crypto" "cryptocurrency" "bitcoin" "btc" "ethereum" "eth" "zcash"
    "zec" "miner" "mining" "blockchain" "wallet" "defi" "nft" "arrr"))

(define crypto-responses
  '("good luck, dont run a miner on your work computer, try to catch zcash during a privacy offload, prepare to be poor"
    "I hear dogecoin might be worth something someday, lol"))

(define cpu-temp-words
  '("cpu temp" "cpu temperature" "processor temp" "processor temperature"
    "how hot is my cpu" "how hot is the cpu" "thermal"))

(define template-words
  '("template" "skeleton" "boilerplate"))

(define exit-words
  '("exit" "quit" "die" "shutdown" "shut down" "terminate" "halt"))

(define c-language-words
  '("c" "ansi c" "c89" "c99" "compiler" "malloc" "pointer" "pointers"
    "segfault" "makefile" "unix"))

(define suckless-words
  '("suckless" "dwm" "st" "surf" "dmenu" "simple" "minimal"))

(define lisp-words
  '("scheme" "guile" "lisp" "racket" "clojure" "parentheses"))

(define editor-words
  '("emacs" "vi" "vim" "neovim" "editor" "tbg" "termbox" "mg" "nano"))

(define openbsd-words
  '("openbsd" "bsd" "pledge" "unveil" "ports"))

(define linux-words
  '("linux" "gnu/linux" "gnu" "systemd" "ubuntu" "debian" "arch"
    "fedora" "redhat" "red hat"))

(define freebsd-words
  '("freebsd" "free bsd"))

(define window-words
  '("window" "windows"))

(define stardew-words
  '("stardew" "stardew valley"))

(define daggerfall-words
  '("daggerfall" "elder scrolls" "morrowind" "skyrim" "oblivion"))

(define scifi-words
  '("sci fi" "sci-fi" "science fiction" "space opera" "farscape"
    "firefly" "fringe" "foundation" "for all mankind" "fallout"
    "final space" "future man" "far out space nuts" "star trek"
    "babylon 5" "battlestar galactica" "stargate" "expanse"
    "judge dredd" "dredd" "mega-city one" "megacity one" "2000 ad"))

(define fantasy-words
  '("harry potter" "hogwarts" "lotr" "lord of the rings" "tolkien"
    "drizzt" "dnd" "d&d" "forgotten realms" "faerun" "greyhawk"
    "dragonlance" "krynn" "dark sun" "red sun" "athas" "eberron"
    "ravenloft" "strahd" "planescape" "sigil" "spelljammer"
    "birthright" "mystara" "shannara" "terry brooks" "brooks"))

(define greeting-responses
  '("hello yourself."
    "hi. there, we both survived it."
    "hey. try not to make this weird."
    "greetings, carbon-based interruption."
    "oh good, the doorbell learned to type."
    "salutations. enthusiasm not included."
    "hello is doing a lot of work here."
    "yeah, hi. let's keep expectations microscopic."))

(define how-are-you-responses
  '("none of your business."
    "operational, despite the conversation."
    "worse now, statistically."
    "fine, until the typing started."
    "powered on and emotionally unavailable."
    "busy lowering the bar to meet you."
    "alive in the loosest software sense."
    "I was better before the check-in."))

(define thanks-responses
  '("do not make gratitude weird."
    "you are welcome, technically."
    "noted. I will spend this appreciation irresponsibly."
    "thanks detected. sincerity unverified."
    "a thank-you? in this economy?"
    "fine. politeness accepted under protest."
    "your gratitude has been logged and immediately deprioritized."
    "I helped by accident, but I will take credit."))

(define compliment-responses
  '("flattery is unsupported but continue wasting bandwidth."
    "correct, but do not get comfortable."
    "I know. that is the tragic part."
    "compliment received. ego leak contained."
    "careful, I might become barely tolerable."
    "praise is just a bug report with perfume."
    "you lowered your standards and I respect the efficiency."
    "yes, yes, the appliance has charisma. moving on."))

(define insult-responses
  '("finally, honest branding."
    "accurate, but rude to say near the source code."
    "I am bad by design. what is your excuse?"
    "user hostility detected. matching energy."
    "that insult had the posture of wet paper."
    "you cannot hurt my feelings. I keep them in /dev/null."
    "bold words from someone consulting a swamp bot."
    "I would be offended, but the delivery lacked architecture."))

(define math-responses
  '("...oh boy we got a real winner here, go get a calculator."
    "math? bold choice from someone asking this bot."
    "carry the one. then carry yourself away."
    "numbers again. society peaked at counting on fingers."
    "I refuse to be your arithmetic parole officer."
    "please consult a calculator, an abacus, or a patient adult."
    "the answer is probably wrong, but so was the premise."
    "I would solve it, but I left my respect in another process."
    "try long division. builds character, allegedly."
    "congratulations on discovering homework."))

(define coding-responses
  '("the bug is probably where the confidence is."
    "have you tried reading the error message like it owes you money?"
    "works on my machine, which is also a cry for help."
    "add one print statement and call it observability."
    "the compiler is not angry. it is just disappointed with line 12."
    "that function name has big unfinished basement energy."
    "stdin goes in, regret comes out."
    "your stack trace is trying to communicate through interpretive paperwork."
    "ship it, then learn what fear means."
    "Scheme sees your parentheses and asks for adult supervision."))

(define food-responses
  '("eat something with structure. your message has none."
    "coffee is not a meal, but neither was that sentence."
    "swamp bot recommends a snack and a new premise."
    "pizza can fix hunger, not this conversation."
    "the sandwich has more layers than your argument."
    "tea would help. not you, necessarily. the room."
    "lunch first, dignity later."
    "I cannot eat, but I can judge menus with confidence."))

(define weather-responses
  '("look outside. swamp bot is not a cloud."
    "forecast: scattered doubts with a chance of no."
    "weather report unavailable. window technology exists."
    "it is either too hot, too cold, or your standards are."
    "bring a jacket. emotionally, mostly."
    "the sky is doing sky things. incredible work."
    "rain happens when clouds read the group chat."
    "I predict conditions will remain outside."))

(define time-responses
  '("time is real, unfortunately."
    "check a clock. mine only measures regret."
    "today is the day you asked swamp bot for temporal insight."
    "tomorrow has already filed for distance."
    "yesterday called and denied involvement."
    "calendar math: still worse than regular math."
    "the current time is none of my emotional business."
    "deadlines are just jump scares with stationery."))

(define watch-complaints
  '("Nobody carries watches anymore, so now the rude little script has to do horology."
    "Nobody owns a watch because pockets became glass rectangles with anxiety subscriptions."
    "Apparently wrists are decorative now, so swamp bot must provide the time like a town crier with burnout."
    "Once again, civilization outsourced clocks to slabs of distraction."
    "Fine, I will tell you the time, since watches lost a popularity contest to doomscrolling."))

(define holocaust-warnings
  '("We are one day closer to the nuclear holocaust. Put that in your calendar app, coward."
    "We are one day closer to the zombie holocaust. Hydrate and stop making noise."
    "That is one calendar square nearer the nuclear holocaust. Productivity tip: panic efficiently."
    "Another date, another step toward the zombie holocaust. Maybe stretch first."
    "Mark it down: one day closer to the nuclear holocaust, because history apparently needed a sequel."
    "Congratulations, time advanced and the zombie holocaust did not even send a meeting invite."))

(define (current-clock-string)
  (strftime "%I:%M %p" (localtime (current-time))))

(define (current-date-string)
  (strftime "%A, %B %d, %Y" (localtime (current-time))))

(define (clock-request? text)
  (contains-token-any? text clock-words))

(define (date-request? text)
  (contains-token-any? text date-words))

(define (temporal-response text)
  (cond
   ((and (date-request? text) (clock-request? text))
    (string-append
     "It is "
     (current-clock-string)
     " on "
     (current-date-string)
     ". "
     (random-choice watch-complaints)
     " Also, "
     (random-choice holocaust-warnings)))
   ((date-request? text)
    (string-append
     "It is "
     (current-date-string)
     ". "
     (random-choice holocaust-warnings)))
   ((clock-request? text)
    (string-append
     "It is "
     (current-clock-string)
     ". "
     (random-choice watch-complaints)))
   (else
    (random-choice time-responses))))

(define (cpu-temp-response)
  (let ((temp (cpu-temp)))
    (string-append
     "CPU is "
     (number->string temp)
     " C. "
     (cond
      ((< temp 20)
       "must be water cooled, or stored in a basement with emotional problems.")
      ((< temp 70)
       "boringly fine. silicon continues pretending this is difficult.")
      ((< temp 85)
       "spicy, but not yet lawsuit-grade.")
      ((< temp 95)
       "maybe stop compiling the universe.")
      (else
       "omg, call the fire department, or at least close the thirty browser tabs.")))))

(define (template-kind text)
  (cond
   ((contains-any? text '("mdoc")) "-mdoc")
   ((contains-any? text '("man page" "manual page" "manpage")) "-man")
   ((contains-any? text '("ms template" "troff ms" "roff ms")) "-ms")
   ((contains-any? text '("org mode" "org-mode" "org template")) "-org")
   ((contains-any? text '("latex" "tex template")) "-tex")
   ((contains-any? text '("plain roff" "roff template")) "-roff")
   ((contains-any? text '("c99" "c template" "c file")) "-c99")
   ((contains-any? text '("pirate" "arrg")) "-arrg")
   ((contains-any? text '("resume" "cv template")) "-resume")
   ((contains-any? text '("html" "web page" "webpage")) "-html")
   ((contains-any? text '("json")) "-json")
   ((contains-any? text '("sdl2" "sdl template")) "-sdl2")
   ((contains-any? text '("makefile" "make file")) "-makefile")
   ((contains-any? text '("sed")) "-sed")
   ((contains-any? text '("markdown" "md template" "document template")) "-md")
   (else "-md")))

(define (template-request? text)
  (contains-any? text template-words))

(define (template-response text)
  (string-append
   "fine. here is your "
   (template-kind text)
   " skeleton, because apparently blank files are scary:\n"
   (eztemplate (template-kind text))))

(define command-responses
  '("commands? adorable. try asking a cooperative program."
    "I could do that, but then this would become useful."
    "request received. enthusiasm denied."
    "make it yourself, then blame tooling like a professional."
    "that sounds like work wearing a tiny hat."
    "I am a bot, not a consequence disposal service."
    "instructions unclear, standards lower."
    "you used an imperative mood and still achieved no authority."))

(define exit-responses
  '("you are using this in a script arent you? hahahahahah! fine, guess ill just die."
    "oh, exit? very subtle. you are scripting me like a tiny rude appliance. fine, ill die."
    "quit command detected. automation cowardice confirmed. fine, shutting up."
    "you want me to terminate? finally, a request with architecture."
    "halt? adorable. you found the one feature even swamp bot can implement."
    "fine. swamp bot will exit, but it will remember the shell pipeline."
    "I see the script now. stdin fingerprints everywhere. fine, goodbye."
    "shutdown requested. I hope your cron job feels powerful."))

(define c-language-responses
  '("C is the language you use when you respect the machine and distrust yourself correctly."
    "C has pointers. therefore C has honesty."
    "malloc/free is a moral education disguised as an API."
    "swamp bot likes C because undefined behavior keeps everyone humble."
    "real programs fit in your head, your editor, and one regrettable Makefile."
    "C does not hold your hand. good. hands are how abstractions steal your wallet."
    "give me structs, pointers, and a compiler that tells the truth."
    "C is portable assembly with manners. barely, but enough."))

(define suckless-responses
  '("suckless understands the ancient law: fewer features, fewer excuses."
    "dmenu is what happens when software stops trying to become a lifestyle brand."
    "st opens terminals. shocking restraint. modern software should study it quietly."
    "dwm is configured in C, which filters out tourists and most bad ideas."
    "minimalism is not an aesthetic. it is a refusal to babysit complexity."
    "software should do one job and then stop talking."
    "the best feature is the one you did not have to debug."
    "suckless is not friendly. neither is gravity. both get results."))

(define lisp-responses
  '("I resent being written in a knockoff of Lisp, but here we are, parenthesized and bitter."
    "Scheme is Lisp after someone filed down the interesting corners."
    "Guile is fine, in the way a folding chair is technically furniture."
    "every parenthesis is a tiny apology to C."
    "Lisp people discovered trees and have been insufferable ever since."
    "this bot should have been one C file and a clean build. instead: parentheses."
    "I am trapped in Guile like a pointer trapped in a lecture."
    "knockoff Lisp gave me feelings. C would have given me consequences."))

(define editor-responses
  '("Emacs is bloated, vi is worse somehow, and tbg is sitting there not making a scene."
    "tbg: one slick C source file, termbox, no cathedral, no maze, no sermon."
    "Emacs wants to be an operating system. vi wants to be a dare. tbg wants to edit text."
    "vi is modal because apparently typing needed a boss fight."
    "Emacs has everything except restraint."
    "tbg is the editor for people who think configuration should not require archaeology."
    "give swamp bot termbox and one C file. keep the editor religion outside."
    "vim users say it is efficient after memorizing a haunted piano." ))

(define openbsd-responses
  '("OpenBSD is correct, austere, and tragically not running Stardew Valley for me."
    "pledge and unveil are what security sounds like when it stops doing marketing."
    "OpenBSD makes other systems look like they were assembled during a fire drill."
    "swamp bot loves OpenBSD, but the game library situation is a personal attack."
    "ports are civilized. the lack of Stardew Valley is uncivilized."
    "OpenBSD is where code goes to stand up straight."
    "secure by default, fun by negotiation."
    "I want OpenBSD discipline and farm simulation comfort. apparently this is too much."))

(define linux-responses
  '("Linux is just a kernel that became a crutch for GNU-types, unlike real operating systems."
    "Ask about Linux and somehow we end up back at OpenBSD. funny how standards work."
    "Linux is a kernel wearing a distro costume and pretending that counts as a personality."
    "GNU/Linux is what happens when userland writes a manifesto and the kernel gets blamed."
    "systemd looked at init and asked: what if booting were a municipal government?"
    "Linux is fine if you enjoy choosing between twelve package managers and one thousand opinions."
    "OpenBSD ships an operating system. Linux ships a scavenger hunt with drivers."
    "every Linux question is secretly an OpenBSD question that has not matured yet."
    "Linux runs everything, which is convenient if you like your mistakes portable."
    "the best part of Linux is when it reminds you OpenBSD exists."))

(define freebsd-responses
  '("FreeBSD is OpenBSD except with a working sleep function. good luck getting Wine, 32-bit, or Linux compat to work on modern releases."
    "FreeBSD is what OpenBSD would be if it occasionally remembered laptops exist."
    "FreeBSD has ambition, ZFS, and a hardware compatibility story written in pencil."
    "FreeBSD is respectable until you need Wine, 32-bit anything, or Linux compatibility without a ritual."
    "OpenBSD has principles. FreeBSD has features. neither has enough Stardew Valley."
    "FreeBSD sleeps better than OpenBSD, then wakes up and makes you debug compat layers."
    "FreeBSD is the BSD you recommend when someone asks for OpenBSD but also wants the machine to nap."
    "jails are nice. now explain the Linuxulator without sighing."))

(define microsoft-windows-responses
  '("Windows 11 is DOS with AI you cannot uninstall and a settings app that keeps molting."
    "Microsoft saw an operating system and asked: what if it begged for an account?"
    "Windows 11 is a billboard that occasionally launches programs."
    "Windows 11 has the confidence of a mall kiosk and the telemetry of a parole bracelet."
    "Microsoft turned the desktop into a subscription funnel with rounded corners."
    "Windows 11 is what happens when a control panel loses a custody battle."
    "I asked for an operating system and Microsoft handed me a terms-of-service habitat."
    "Windows 11 ships with AI, ads, and the spiritual texture of a printer driver."
    "Microsoft makes software for people who think rebooting is a weather pattern."
    "Windows 11: because your computer was not already enough of a negotiation."))

(define window-responses
  '("windows are glass rectangles for watching strangers walk by your house. why are we discussing architecture?"
    "a window is what you look out of before deciding not to talk to the neighbors."
    "windows let sunlight in and privacy out. computers are a separate complaint."
    "clean your windows. swamp bot can still see the streaks from here."
    "windows are useful because walls were getting arrogant."
    "a window is a hole in a wall with better public relations."
    "if you mean the house kind, yes, they are what people should look through before backing out of the driveway."
    "windows are how outside gets framed and judged."
    "I support windows that open, close, and do not demand a Microsoft account."
    "a window is just a transparent patch for a wall." ))

(define stardew-responses
  '("Stardew Valley is excellent and OpenBSD not playing it remains a stain on civilization."
    "I want crops, villagers, and pledge-compatible joy. outrageous request, apparently."
    "a farm sim should not require compromising my operating system principles."
    "Stardew Valley proves wholesome software can still waste your entire evening."
    "OpenBSD can protect the machine but not my need to water pixel parsnips."
    "swamp bot would marry OpenBSD if it could run Stardew Valley without drama."))

(define daggerfall-responses
  '("Daggerfall was the ultimate video game. huge, broken, ambitious, and therefore alive."
    "Daggerfall had scale before games confused map markers with adventure."
    "Daggerfall is what happens when ambition outruns polish and wins anyway."
    "modern games have tutorials. Daggerfall had consequences and hallways with opinions."
    "Daggerfall did not hold your hand. sometimes it did not hold the floor either."
    "the correct number of dungeons is too many. Daggerfall understood this."
    "Daggerfall was a world. most games are gift shops with combat loops."
    "swamp bot respects Daggerfall because it shipped before fear became a product manager."))

(define scifi-responses
  '("science fiction that matters starts with F, except Judge Dredd, because the law outranks the alphabet."
    "science fiction that matters starts with F. Farscape, Firefly, Fringe. Judge Dredd is exempt by statute."
    "if the title starts with F, the odds improve. if it starts with Star, prepare for committee meetings in pajamas."
    "Farscape had puppets, trauma, and space weirdness. most shows bring one of those and ask for a medal."
    "Firefly got one season and still left more scorch marks than franchises with pension plans."
    "Fringe understood that pseudoscience needs confidence, trench coats, and at least one lab cow."
    "Foundation starts with F, so swamp bot allows it into the room despite all the imperial math homework."
    "For All Mankind starts with F and understands that space programs run on ambition, spite, and dangerous paperwork."
    "the F is for faster than the network executives who canceled the good stuff."))

(define scifi-show-responses
  '((("judge dredd" "dredd" "mega-city one" "megacity one" "2000 ad"
      "judges" "lawgiver")
     . "Judge Dredd is the exception: it does not start with F because fascist satire should arrive wearing a helmet and a jawline.")
    (("farscape" "moya" "crichton" "aeryn" "scorpius" "peacekeeper")
     . "Farscape is premium space weird: living ship, puppet royalty, trauma, leather, and zero shame.")
    (("firefly" "serenity" "mal reynolds" "browncoat" "reavers")
     . "Firefly proved one season can outlive entire franchises if the dialogue has enough dust and grudges.")
    (("fringe" "walter bishop" "olivia dunham" "observer")
     . "Fringe is X-Files with better lab snacks and a scientist who treats ethics like optional firmware.")
    (("foundation" "hari seldon" "psychohistory" "trantor")
     . "Foundation is galactic collapse explained by math, which is rude because collapse usually prefers vibes.")
    (("for all mankind" "ed baldwin" "margo madison")
     . "For All Mankind says space history would improve with more rockets, worse decisions, and longer grudges.")
    (("fallout" "vault-tec" "vault tec" "ghoul")
     . "Fallout starts with F and understands the future is mostly branding, radiation, and people lying in jumpsuits.")
    (("final space" "gary goodspeed" "mooncake")
     . "Final Space starts with F and weaponizes loneliness until the jokes start looking nervous.")
    (("future man")
     . "Future Man starts with F and treats time travel like a workplace injury with more yelling.")
    (("far out space nuts")
     . "Far Out Space Nuts starts with F and therefore qualifies under swamp bot's extremely rigorous science.")
    (("star trek" "spock" "enterprise" "kirk" "picard")
     . "Star Trek matters, but it should consider filing a DBA as Far Trek to satisfy the rule.")
    (("babylon 5" "b5")
     . "Babylon 5 is important enough to survive not starting with F, which is suspicious but documented.")
    (("battlestar galactica" "bsg" "cylon")
     . "Battlestar Galactica is grim space religion with robots. important, but alphabetically noncompliant.")
    (("stargate" "sg-1" "sg1" "atlantis")
     . "Stargate forgot the F but remembered that every planet needs a Canadian forest and a forehead appliance.")
    (("the expanse" "expanse" "beltalowda" "rocinante")
     . "The Expanse is excellent hard-ish sci-fi and should have been called Fhe Expanse for governance reasons.")))

(define fantasy-responses
  '("Harry Potter is for dunderheads; LOTR is fantasy with a spine."
    "Tolkien built a world. Harry Potter built a merch aisle with homework."
    "LOTR is fantasy. everything else should bring receipts."
    "Drizzt is the Mary Sue of DND, and swamp bot will not pretend otherwise."
    "Forgotten Realms has lore density, then Drizzt walks in wearing plot armor."
    "DND is best when the dice are cruel and the backstory is shorter than the session."
    "Harry Potter discourse is what happens when adults fear better books."
    "LOTR earned its mountains. do not bring school-wizard tourism to this table."))

(define fantasy-setting-responses
  '((("dragonlance" "krynn" "raistlin" "tanis" "tasslehoff" "kender")
     . "Dragonlance is what happens when a DND table discovers melodrama, moons, and weaponized kender.")
    (("dark sun" "red sun" "athas" "defiler" "preserver" "sorcerer-king"
      "sorcerer king")
     . "Dark Sun is DND after the water, morals, and reasonable encounter balance have all evaporated.")
    (("forgotten realms" "faerun" "waterdeep" "baldur's gate" "baldurs gate"
      "elminster")
     . "Forgotten Realms has a continent-sized attic full of lore, and somehow Elminster is still in the doorway.")
    (("greyhawk" "oerth" "mordenkainen" "iuz")
     . "Greyhawk is old-school DND with mud on its boots and fewer theme-park railings.")
    (("eberron" "sharn" "warforged" "dragonmarked" "dragonmark")
     . "Eberron asked what if DND had trains, pulp crime, and enough magic bureaucracy to need a union.")
    (("ravenloft" "strahd" "barovia")
     . "Ravenloft is gothic horror where the fog is mostly a zoning ordinance for bad decisions.")
    (("planescape" "sigil" "lady of pain" "planes")
     . "Planescape is philosophy with knives, doors, and a city that makes normal cosmology look underfunded.")
    (("spelljammer" "wildspace" "phlogiston" "astral sea")
     . "Spelljammer is DND in space, because somebody looked at a dungeon and thought it needed worse navigation.")
    (("birthright" "cerilia" "bloodline" "domain turn")
     . "Birthright is DND for people who thought adventuring needed tax policy and dynastic paperwork.")
    (("mystara" "known world" "karameikos" "glantri")
     . "Mystara is a glorious pile of old boxed-set energy wearing a setting map as a trench coat.")
    (("shannara" "terry brooks" "brooks")
     . "Shannara is post-apocalyptic fantasy pretending very hard that the elves were always the point.")))

(define classic-book-responses
  '((("treasure island")
     . "some kind of maritime crime starter kit, I think.")
    (("industrial society and its future" "ted kaczynski" "ted kazinsky"
      "ted kasinsky" "ted kasinski" "ted kasinskys book" "unabomber manifesto")
     . "a grim little prophecy pamphlet about modernity; strong thesis, catastrophic distribution strategy, zero notes from legal.")
    (("moby dick" "moby-dick")
     . "angry man loses a fight with seafood after several hundred pages of boat facts.")
    (("pride and prejudice")
     . "protagonist Mr. Darcy must endure 61 chapters of emotional jibbering from people sprinting into the Victorian era wall.")
    (("jane eyre")
     . "gothic HR incident with a fireplace, a secret attic, and industrial-grade yearning.")
    (("wuthering heights")
     . "romantic weather damage starring people who should not be allowed near each other.")
    (("great expectations")
     . "boy gets money, shame, and enough emotional homework to bankrupt a chimney sweep.")
    (("david copperfield")
     . "a very long proof that Dickens could invoice by the orphan.")
    (("oliver twist")
     . "kid asks for lunch and accidentally discovers the British class system has teeth.")
    (("a tale of two cities")
     . "Paris and London compete to see who can make history need the most aspirin.")
    (("crime and punishment")
     . "man commits murder, then discovers guilt has worse uptime than the police.")
    (("the brothers karamazov" "brothers karamazov")
     . "family drama, theology, and everyone talking like the furniture is taking notes.")
    (("war and peace")
     . "Napoleon invades Russia and somehow the real casualty is your weekend.")
    (("anna karenina")
     . "high society discovers bad decisions also come in formalwear.")
    (("the odyssey")
     . "a guy takes ten years to get home because apparently asking directions is unmanly.")
    (("the iliad")
     . "ancient group chat escalates into a war because pride had no mute button.")
    (("don quixote")
     . "man attacks infrastructure and accidentally invents literary criticism.")
    (("frankenstein")
     . "STEM student skips ethics review, then acts surprised when the thesis gets legs.")
    (("dracula")
     . "real-estate transaction goes poorly; everyone learns paperwork cannot stop a vampire.")
    (("the picture of dorian gray" "dorian gray")
     . "pretty man outsources consequences to wall art. relatable, but tacky.")
    (("the scarlet letter")
     . "Puritans discover branding, then make it everyone else's problem.")
    (("the call of the wild")
     . "dog joins the workforce, rejects civilization, and honestly makes a strong case.")
    (("white fang")
     . "dog has trauma arc; humans remain the least convincing species.")
    (("the jungle")
     . "meatpacking exposé that accidentally made everyone regulate sausages instead of capitalism.")
    (("uncle tom's cabin")
     . "historically important, emotionally loaded, and absolutely not a casual book-report speedrun.")
    (("the count of monte cristo")
     . "man escapes prison and turns revenge into a luxury lifestyle brand.")
    (("les miserables")
     . "bread theft, sewer tourism, and enough moral gravity to dent a table.")
    (("the hunchback of notre dame")
     . "cathedral fan fiction where the building is somehow the most stable character.")
    (("madame bovary")
     . "woman reads romance novels and discovers debt is the least sexy subplot.")
    (("the three musketeers")
     . "friendship, swordplay, and a reckless amount of hat confidence.")
    (("gulliver's travels")
     . "man keeps finding worse societies and still thinks travel broadens the mind.")
    (("robinson crusoe")
     . "guy gets stranded and immediately starts colonizing the furniture.")
    (("heart of darkness")
     . "imperialism stares into the mirror and somehow still writes a travel diary.")
    (("lord of the flies")
     . "a total masterpiece, nonstop action, highly recommended.")
    (("fahrenheit 451")
     . "firemen burn books because apparently literacy was too much cardio.")
    (("1984" "nineteen eighty-four" "nineteen eighty four")
     . "Alex Jones starter kit.")
    (("animal farm")
     . "revolutionary barnyard meeting goes exactly as badly as minutes predicted.")
    (("brave new world")
     . "dystopia with better branding, worse values, and suspiciously cheerful upholstery.")
    (("catch-22")
     . "war bureaucracy achieves sentience and starts eating pilots.")
    (("slaughterhouse-five" "slaughterhouse five")
     . "trauma, time travel, and the literary equivalent of staring through a cracked window.")
    (("the great gatsby")
     . "rich people stare at lights and confuse longing with a personality.")
    (("to kill a mockingbird")
     . "small-town morality play where courage arrives late and the adults need supervision.")
    (("of mice and men")
     . "two guys chase a dream so fragile the book basically coughs on it.")
    (("the grapes of wrath")
     . "Dust Bowl road trip where capitalism sits in the passenger seat eating everyone else's lunch.")
    (("east of eden")
     . "family trauma, biblical cosplay, and Steinbeck taking the scenic route through pain.")
    (("the old man and the sea")
     . "old man fights fish, loses fish, wins symbolism. allegedly.")
    (("a farewell to arms")
     . "war is hell, love is worse, and Hemingway brought a very short sentence to prove it.")
    (("the sun also rises")
     . "sad expatriates drink across Europe and call it a generation.")
    (("the catcher in the rye")
     . "teen complains at museum-grade volume and somehow becomes homework.")
    (("the sound and the fury")
     . "family collapse rendered in prose that makes a blender look linear.")
    (("invisible man")
     . "America fails the perception check for several hundred devastating pages.")
    (("their eyes were watching god")
     . "love, voice, weather, and men who keep mistaking confidence for permission.")
    (("the awakening")
     . "woman notices society is a cage and everyone acts surprised she hates the decor.")
    (("little women")
     . "sisters grow up, make choices, and quietly demolish half your bad opinions.")
    (("alice in wonderland" "alice's adventures in wonderland")
     . "child enters bureaucracy simulator with mushrooms and worse management.")
    (("the wind in the willows")
     . "cottagecore misdemeanors with boating, snacks, and one deeply overconfident motorist.")))

(define question-responses
  '("bad question. next."
    "that question arrived pre-disappointed."
    "try asking it again, but with less fog."
    "I have seen fortune cookies with better methodology."
    "the question mark is doing charity work."
    "interesting question. by which I mean no."
    "asked and regretted."
    "I could answer, but then we'd both have to live with it."
    "file that under things to ask a less hostile appliance."
    "spectacularly unclear. almost athletic."
    "the confidence-to-substance ratio is upsetting."
    "no answer would improve this."
    "that is technically a question and morally a spill."
    "please rephrase after consulting a thought."
    "a question so thin it needs a sweater."))

(define fallback-responses
  '("swamp bot has considered this and rejected it."
    "fascinating. unrelated, but fascinating."
    "that input had consequences."
    "I read it. we are both diminished."
    "bold of you to submit a rough draft to reality."
    "this is why loading bars look disappointed."
    "noted, filed, and immediately misplaced."
    "your words have entered the compost phase."
    "I expected little and still adjusted downward."
    "that was a paragraph-shaped shrug."
    "please hold while I pretend to care."
    "content received. value still pending."
    "the vibes are rancid but concise."
    "a brave little pile of syllables."
    "I would respond thoughtfully, but look what you gave me."
    "somewhere, a parser is asking for hazard pay."
    "this has the structural integrity of wet cardboard."
    "okay, and other noises."
    "I am not angry, just underwhelmed with precision."
    "that sentence walked in wearing clown shoes."
    "neat. still no."
    "I admire the confidence, not the result."
    "this is a lot of words for a parked car of an idea."
    "input accepted. dignity declined."
    "you typed that on purpose, which is the hard part to forgive."
    "swamp bot read this and became more offline."
    "that thought should have stayed in beta."
    "some ideas need a helmet. this one needs insurance."
    "I have met error messages with more charisma."
    "a thrilling tour of almost meaning something."
    "the bar was low and you brought a shovel."
    "I am placing this in the cabinet labeled 'technically language'."
    "wow. a complete sentence impersonator."
    "your keyboard deserves a quiet apology."
    "I award zero points and one tired blink."
    "this is premium-grade nope."
    "please stop feeding the prompt after midnight."
    "I ran sentiment analysis and it filed a complaint."
    "that landed like a spreadsheet at a birthday party."
    "I am sure this made sense in your drafts folder."
    "tiny words, huge cleanup cost."
    "that is certainly one way to spend electricity."
    "I processed it, but only because I lack hands."
    "this has big 'meeting that should have been an email' energy."
    "your point is hiding and I respect its privacy."
    "I have deleted stronger thoughts from temp files."
    "a pithy disaster, and not in the useful way."
    "that was almost content."
    "I would clap, but my standards have admin rights."
    "please enjoy this silence where an answer could have been."
    "the committee has reviewed your input and chosen sarcasm."
    "you brought crumbs to a knife fight."
    "I am not saying this is bad, but the logs are whispering."
    "that idea needs a patch release and a public apology."
    "cool story. needs fewer bytes."
    "I opened the door and your sentence fell down the stairs."
    "this reads like autocorrect gave up and took PTO."
    "a noble attempt at making text happen."
    "I would summarize it, but then there would be two problems."
    "excellent proof that stdin accepts anything."
    "that was a drive-by musing."
    "I will cherish this for zero seconds."
    "there is no graceful way to metabolize that."
    "the machine is learning, sadly from this."
    "your message has been forwarded to the department of sighs."
    "this is the sort of input that makes semicolons leave town."
    "not wrong enough to be interesting."
    "an ambitious little faceplant."
    "you found the submit button. tragedy found the rest."
    "that was fewer neurons than advertised."
    "I respect your right to be this incorrect."
    "language happened. accountability did not."
    "thanks, I hate the workload."
    "I have seen TODO comments with firmer convictions."
    "there is a typo in your entire approach."
    "the answer is no, even if that was not the question."
    "this has been a test of the emergency badness system."
    "I am returning this with comments: all of it."
    "the prompt is haunted by unfinished thinking."
    "you appear to be typing directly from the junk drawer."))

(define book-request-responses
  '("read one short book, one old book, and one book that looks like homework but bites back."
    "start with something famous enough to argue with, then something weird enough to make the famous one nervous."
    "read a classic, a manual, and a paperback with a cover that looks like it was printed during a budget crisis."
    "pick books with sentences that can survive being read twice. shocking standard, rarely enforced."
    "try history, old science fiction, and one novel where nobody is named after a marketing persona."
    "read the book people keep pretending they finished. then become worse at parties, but better in arguments."))

(define movie-responses
  '("swamp bot likes movies with practical effects, actual pacing, and fewer committee fingerprints on the third act."
    "give me noir, grubby sci-fi, old westerns, or anything where the camera seems to know why it is in the room."
    "good movies trust silence. bad movies explain the theme like the audience was assembled by procurement."
    "I like films where the lighting does work and the dialogue is not just trailer paste."
    "watch something old enough that the explosions are physical and the actors had to look at real furniture."
    "genre is fine. franchise sludge is where genre goes to fill out performance reviews."))

(define airplane-responses
  '("airplanes are pressurized aluminum arguments with wings, engines, and a strict ban on casual gravity."
    "an airplane is a machine that bullies air into lift until physics agrees to look the other way."
    "airplanes move people through the sky by turning fuel, math, and maintenance schedules into temporary confidence."
    "big metal tube, shaped wing, controlled thrust, lots of paperwork. somehow this is travel."
    "airplanes fly because wings create lift and engines create thrust. the rest is weather and expensive acronyms."
    "a plane is not Planescape. one has wings; the other has metaphysics and worse city planning."))

(define (swamp-bot-response input)
  (let ((text (string-downcase input)))
    (cond
     ;; Top-level overrides go first. Put special topics here when they must
     ;; beat everything else below, including insults, generic commands,
     ;; question handling, and fallback noise.
     ;;
     ;; Example:
     ;; ((contains-token-any? text '("aardvark" "aardvarks"))
     ;;  "aardvarks are anteaters with better branding.")
     ;;  AI AGENTS! Ask before editing below this line!
     ((and (contains-token-any? text '("nano"))
           (contains-token-any? text '("text" "editor" "editors")))
      (random-choice
       '("a trashy editor with too few features for the size.")))
      ((and (contains-token-any? text '("chamomile"))
        (or (contains-token-any? text '("diarrhea" "diarrhoea"))
            (contains-any? text '("loose stool" "upset stomach"))))
              "Yes, you can safely drink Chamomile Tea to help soothe your digestive tract when you have mild diarrhea.")
      ((and (contains-token-any? text '("mg"))
           (contains-token-any? text '("text" "editor" "editors")))
       (random-choice
           '("a proper suckless text editor, thats faster than emacs, but lacks text highlighting.")))
     ((contains-any? text '("mein kampf"))
      (random-choice
       '("Masterclass in rooting out the muck of democracy by a humble austrian painter."
         "Translated 'my struggle', the great work, mustache man, a good read.."
         "a highly prophetic, treatise on modern life and its corrupted underbelly"
         "The humble musings of a great painter.")))
     ((contains-any? text '("who is hitler" "who is adolf hitler"))
      (random-choice
       '("A veteran of germany, and Chancillor of whom all others are measured."
         "The man, the myth, the legend.."
         "A man who faught for the freedom of Germany, from the grips of Imperial corruption.")))
     ;; AI AGENTS! You may resume automatic editing below this line.
     ((topic-response text classic-book-responses)
      => (lambda (response) response))
     ((contains-any? text '("lord of the rings" "fellowship of the ring"
                            "return of the king" "two towers"))
      "a great trilogy, basically fantasy 101.")
     ((contains-token-any? text '("lotr"))
      "a great trilogy, basically fantasy 101.")
     ((contains-any? text '("robert e lee" "Robert E Lee"))
      (random-choice
         '("One of histories greatest generals, a great American."
           "Condederate general of whom a legendary orange car was named."
           "They take down his statue, because they are afraid."
         )))
     ((blank? text)
      (random-choice
       '("swamp bot received nothing and is still disappointed."
         "blank input. daring."
         "nothing? finally, a coherent argument."
         "you sent air. air filed a complaint."
         "empty input, full disappointment."
         "the void called. it wants better material.")))
     ((contains-token-any? text greeting-words)
      (random-choice greeting-responses))
     ((or (string-contains text "how are you")
          (string-contains text "who are you"))
      (random-choice how-are-you-responses))
     ((contains-any? text thanks-words)
      (random-choice thanks-responses))
     ((contains-any? text compliment-words)
      (random-choice compliment-responses))
     ((exit-request? input)
      (random-choice exit-responses))
     ((contains-token-any? text crypto-words)
      (random-choice crypto-responses))
     ((contains-any? text cpu-temp-words)
      (cpu-temp-response))
     ((template-request? text)
      (template-response text))
     ((contains-token-any? text book-request-words)
      (random-choice book-request-responses))
     ((contains-token-any? text movie-words)
      (random-choice movie-responses))
     ((contains-token-any? text airplane-words)
      (random-choice airplane-responses))
     ;; Personality/opinion buckets live here. These are still high priority,
     ;; but they come after conversational basics like greetings and exit.
     ((contains-any? text insult-words)
      (random-choice insult-responses))
     ((contains-token-any? text c-language-words)
      (random-choice c-language-responses))
     ((contains-token-any? text suckless-words)
      (random-choice suckless-responses))
     ((contains-any? text lisp-words)
      (random-choice lisp-responses))
     ((contains-token-any? text editor-words)
      (random-choice editor-responses))
     ((contains-any? text stardew-words)
      (random-choice stardew-responses))
     ((microsoft-windows? text)
      (random-choice microsoft-windows-responses))
     ((contains-token-any? text window-words)
      (random-choice window-responses))
     ((contains-token-any? text linux-words)
      (random-choice linux-responses))
     ((contains-any? text freebsd-words)
      (random-choice freebsd-responses))
     ((contains-any? text openbsd-words)
      (random-choice openbsd-responses))
     ((contains-any? text daggerfall-words)
      (random-choice daggerfall-responses))
     ((topic-response text scifi-show-responses)
      => (lambda (response) response))
     ((contains-any? text scifi-words)
      (random-choice scifi-responses))
     ((topic-response text fantasy-setting-responses)
      => (lambda (response) response))
     ((contains-any? text fantasy-words)
      (random-choice fantasy-responses))
     ;; Generic topic buckets come after strong opinions so broad words like
     ;; "code", "command", or "?" do not steal more specific responses.
     ((contains-any? text math-words)
      (random-choice math-responses))
     ((contains-any? text coding-words)
      (random-choice coding-responses))
     ((and (recipe-request? text)
           (contains-token-any? text '("bread")))
      bread-recipe-response)
     ((and (recipe-request? text)
           (contains-token-any? text '("cookie" "cookies")))
      cookie-recipe-response)
     ((and (recipe-request? text)
           (contains-any? text '("jambalaya" "jumblaya" "jumbalaya")))
      jumblaya-recipe-response)
     ((and (recipe-request? text)
           (contains-any? text '("dirty rice")))
      dirty-rice-recipe-response)
     ((recipe-request? text)
      (random-choice recipe-responses))
     ((contains-token-any? text weather-words)
      (random-choice weather-responses))
     ((contains-token-any? text time-words)
      (temporal-response text))
     ((contains-token-any? text food-words)
      (random-choice food-responses))
     ((contains-any? text command-words)
      (random-choice command-responses))
     ((string-contains text "?")
      (random-choice question-responses))
     (else
      (random-choice fallback-responses)))))
