CSC ?= chicken-csc
CSI ?= chicken-csi

all: termbox.so termbox.import.scm

termbox.so termbox.import.scm &: termbox.scm termbox2.h
	$(CSC) -s -J -o termbox.so termbox.scm

check: all
	$(CSI) -I . -s tests/smoke.scm
	CSI=$(CSI) python3 tests/terminal.py

demo: all
	$(CSI) -I . -s run-tui.scm

.PHONY: all check demo
