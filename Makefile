.PHONY: all run clean distclean

PYTHON ?= python3

CXX := c++
CXXFLAGS := -std=c++23 -Wall -Wextra -g -fno-omit-frame-pointer -O3 -march=native

SRC := 1brc.cpp
BIN := build/1brc

all: $(BIN)

$(BIN): $(SRC) | build
	$(CXX) $(CXXFLAGS) $< -o $@

build:
	mkdir -p $@

input.txt:
	$(PYTHON) 1brows.py

run: $(BIN) input.txt
	./$(BIN)

clean:
	rm -rf build output.txt

distclean: clean
	rm -f input.txt
