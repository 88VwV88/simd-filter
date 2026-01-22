# ANSI color codes
RESET := \033[0m
RED   := \033[31m
GREEN := \033[32m

pcolor = $($(1))$(2)$(RESET)

CC  := gcc
CXX := g++

WARNINGS := -W -Wall -Wextra -Wconversion

CCFLAGS  := -DGLEW_STATIC $(WARNINGS) -std=c23 -ggdb -O0
CXXFLAGS := -DGLEW_STATIC $(WARNINGS) -std=c++23 -ggdb -O0

TARGET := simd-filter
LDFLAGS := -fsanitize=undefined -fsanitize=address -lboost_program_options -march=native

.PHONY: all test clean

SRC := $(wildcard src/*.c*)
OBJ := $(patsubst src/%, out/%.o, $(SRC))

HEADERS := $(wildcard src/*.h) $(wildcard src/*.hpp)
PCH := $(patsubst src/%, src/%.gch, $(HEADERS))

src/%.h.gch: src/%.h
	@printf "$(GREEN)COMPILING$(RESET) $@\n"
	@$(CC) $(CCFLAGS)   -o $@ $<

src/%.hpp.gch: src/%.hpp
	@printf "$(GREEN)COMPILING$(RESET) $@\n"
	@$(CXX) $(CXXFLAGS) -o $@ $<

out/%.cpp.o: src/%.cpp
	@printf "$(GREEN)COMPILING$(RESET) $@\n"
	@$(CXX) $(CXXFLAGS) -c -o $@ $< $(LDFLAGS)

out/%.c.o: src/%.c
	@printf "$(GREEN)COMPILING$(RESET) $@\n"
	@$(CC) $(CCFLAGS)   -c -o $@ $< $(LDFLAGS)

all:
	@mkdir -p out
	@printf "$(GREEN) BUILDING$(RESET) $(TARGET)\n"
	@$(MAKE) --no-print-directory $(PCH) $(OBJ)
	@$(CXX) $(OBJ) -o $(TARGET) $(LDFLAGS)

test: all
	@printf "$(GREEN)  RUNNING$(RESET) $(TARGET)\n"
	@./$(TARGET)

clean:
	@printf "$(RED)CLEANING BUILD FILES$(RESET)\n"
	rm -rf out/* $(TARGET) src/*.gch
