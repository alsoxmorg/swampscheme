#!/usr/bin/env bash

case "$1" in
    -mdoc)
echo "
.Dd $(date "+%B %d, %Y")
.Dt NEWFILE 1
.Os
.Sh NAME
.Nm newfile
.Nd short description here
.Sh SYNOPSIS
.Nm
.Op options
.Sh DESCRIPTION
Full description here.
.Sh SEE ALSO
.Xr othercommand 1"
        ;;

    -ms)
echo "
.TL
Title of Document
.AU
Author Name
.AI
Author's Institution
.AB
Abstract text here.
.AE
.SH
Your subheader
.LP
Body text begins here."
;;    
    -org)
echo "
#+TITLE: New Org Document
#+AUTHOR: Your Name
#+DATE: $(date "+%Y-%m-%d")
#+OPTIONS: toc:nil num:nil
#+STARTUP: showeverything

* Introduction
Write your introduction here.

** Subsection
Your sub-content goes here.

* Conclusion
Final thoughts here."
;;

    -man) #old school man format
echo "
.TH NEWFILE 1 \"$(date "+%B %d, %Y")\" \"1.0\" \"User Commands\"
.SH NAME
newfile \- short description here
.SH SYNOPSIS
.B newfile
[\fIOPTIONS\fR]
.SH DESCRIPTION
Full description here.
.SH OPTIONS
.TP
.B -h
Show help message.
.SH SEE ALSO
.BR othercommand (1)
.SH AUTHOR
Your Name"
        ;;

    -tex) # basic latex
echo "
\\documentclass{article}

\\title{Title of Document}
\\author{Your Name}
\\date{\\today}

\\begin{document}

\\maketitle

\\section{Introduction}
Write your introduction here.

\\section{Main Content}
Write your main content here.

\begin{quote}
  ``This is a sample quote''  
  \textit{Where you got the quote}
\end{quote}

\\section{Conclusion}
Write your conclusion here.

\\end{document}"
;;
    -roff) #basic roff (no macro packages)
echo "
.\\\" Roff document created on $(date "+%B %d, %Y")
.sp 2
.ce
Basic Roff Document
.sp
This is a plain roff file with no macro package.
.sp
Text can be wrapped normally. You can insert manual breaks with:
.br
and extra vertical space with:  \".sp 1\"
.sp 1"
;;
    -c99) #a C file
	echo "
/* $2 - Minimal C99 suckless style */

#include <stdio.h>
#include <stdlib.h>

/*This is a comment*/
char cvar; /*a character variable*/
int  ivar; /*an integer variable*/
void vvar; /*a void variable*/

int
main(void)
{
    puts(\"Hello, world!\");
    return EXIT_SUCCESS;
}
"
	;;
    -farbfeld) # a single pixel farbfeld
	echo -n "farbfeld"         # 8-byte magic
        printf '\x00\x00\x00\x01'  # width = 1
        printf '\x00\x00\x00\x01'  # height = 1
        printf '\xFF\xFF'          # Red = max
        printf '\x00\x00'          # Green = 0
        printf '\x00\x00'          # Blue = 0
        printf '\xFF\xFF'          # Alpha = max (opaque)
	;;
    -1fpirate) # a single pixel 1fpirate
	echo -n "1fpirate"
	printf '\x00\x00\x00\x01'  # width = 1
        printf '\x00\x00\x00\x01'  # height = 1
	printf '0'
	;;
    -md) # a markdown skeleton
    echo "
# Title of Document

_Author: Your Name_  
_Date: $(date "+%Y-%m-%d")_

---

## Introduction

Write your introduction here.

## Main Content
"
    ;;
    -arrg) # pirate markdown!
        echo "
! This be a Title

!! This be a subheader Yaaar!
   ~ These be bullets, arrg
     \` These be other bullets! ha har
This is normal text.
{you can put off color text in here!}
| Turn | Yer | Tables |
:8 spanish ladies for single line comments ::
"
	;;
    -resume) # resume in roff
	echo "
.\" Resume created on $(date "+%B %d, %Y")
.sp 2
.ce
Your Name
.br
Your Address
.br
Your Email | Your Phone
.sp 2

.ce
\\fBObjective\\fR
.sp
Seeking a position as a [Your Position] where I can apply my skills and experience.
.sp 2

.ce
\\fBExperience\\fR
.sp
.TP
\\fBJob Title 1\\fR
Company Name, Location
Dates of Employment
.br
Description of responsibilities and achievements.

.TP
\\fBJob Title 2\\fR
Company Name, Location
Dates of Employment
.br
Description of responsibilities and achievements.

.sp 2
.ce
\\fBEducation\\fR
.sp
.TP
Degree, Major
School Name, Location
Graduation Year

.sp 2
.ce
\\fBSkills\\fR
.sp
- Skill 1
- Skill 2
- Skill 3

.sp 2
.ce
\\fBReferences\\fR
.sp
Available upon request.
"
	;;
    -html)
	echo "
<!DOCTYPE html>
<html lang=\"en\">
<head>
    <meta charset=\"UTF-8\" />
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />
    <title>New Document</title>
</head>
<body>
    <h1>Title of Document</h1>
    <p>Write your introduction here.</p>

    <h2>Main Content</h2>
    <p>Your main content goes here.</p>

    <h2>Conclusion</h2>
    <p>Final thoughts here.</p>
</body>
</html>"
	
	;;
    -json)
	echo "
{
    \"_comment\": \"This is a human-editable JSON template. Remove or ignore _comment fields.\",
    \"title\": \"Your document title\",
    \"author\": \"Your name\",
    \"date\": \"$(date "+%Y-%m-%d")\",
    \"sections\": [
        {
            \"heading\": \"Introduction\",
            \"content\": \"Write your introduction here.\"
        },
        {
            \"heading\": \"Main Content\",
            \"content\": \"Your main content goes here.\"
        },
        {
            \"heading\": \"Conclusion\",
            \"content\": \"Final thoughts here.\"
        }
    ]
}
"
	;;
    -yaml) # eww
	echo "eww gross dont use this format."
	;;
    -sdl2)
	echo "
/* ${2}.c - Minimal C99 SDL2 skeleton
 *
 * Build: make
 * Requires: SDL2 development libraries and pkg-config
 * try cc yourfile.c -lSDL2
 * if that doestn work, try cc yourfile.c -I(path to sdl) -L(path to libs) -lSDL2
 */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

int
main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, \"SDL_Init: %s\n\", SDL_GetError());
        return 1;
    }

    const int WIDTH = 800;
    const int HEIGHT = 600;

    SDL_Window *win = SDL_CreateWindow(
        \"SDL2 Window\",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT,
        0
    );
    if (!win) {
        fprintf(stderr, \"SDL_CreateWindow: %s\n\", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!ren) {
        fprintf(stderr, \"SDL_CreateRenderer: %s\n\", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    SDL_Event e;

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
            }
        }

        /* clear background */
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderClear(ren);

        /* draw centered rectangle */
        SDL_Rect rect = {
            (WIDTH - 200) / 2,
            (HEIGHT - 150) / 2,
            200,
            150
        };
        SDL_SetRenderDrawColor(ren, 200, 60, 60, 255);
        SDL_RenderFillRect(ren, &rect);

        SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}"	
	;;
    -makefile)
	echo "
# Simple Makefile skeleton
# Usage: change SRCS and NAME to your liking

NAME    = program
CC      = cc   # your compliler
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -O2 #extra flags
LDFLAGS =
SRCS    = main.c
OBJS    = \$(SRCS:.c=.o)

all: \$(NAME)

\$(NAME): \$(OBJS)
	\$(CC) \$(OBJS) \$(LDFLAGS) -o \$(NAME)

%.o: %.c
	\$(CC) \$(CFLAGS) -c \$< -o \$@

run: \$(NAME)
	./\$(NAME)

clean:
	rm -f \$(OBJS) \$(NAME)

.PHONY: all clean run
"
	;;
    -sed) 
	echo "lazy sed for string replacement"
	echo "sed -i 's/REPLACEME/WITHME/g' filename"
	;;
    *)
        echo "Usage: $0 -type > (file)"
	echo "Types are: -mdoc, -ms -man -org -tex -roff -c99 -farbfeld -1fpirate -md -arrg -resume"
        exit 1
        ;;
esac
