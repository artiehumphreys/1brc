.PHONY: all clean

PYTHON := $(shell command -v python3)

ifneq ($(PYTHON),)
	$(error python3 not found)
endif

input.txt:
	$(PYTHON) 1brows.py

clean:
	rm -f input.txt
