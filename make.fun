#!/bin/make -f

# -s    tells it to not echo the commands that are being executed
# -j16  tells it to run up to 16 jobs in parallel when it can
MAKEFLAGS += -s -j16

# Makefile reminder since I'll forget it again soon:
#
#   Variables are set at parse-time, unless $(eval) is used to set them.
#   Commands are expanded at run-time.
#
#   Special ugly syntax:
#       $@  is the target
#       $^  are the dependencies
#       $<  is the first dependency (and this is relevant because the .d files add all the headers)
#       %   is a wildcard and can be used for matching
#
#   This makefile does its best to avoid any implicit rules.

start_time := $(shell date +%s%N)
all: run_tree/fun
	$(if $(compiled) \
	,   echo "Compilation took $(shell expr "(" $(shell date +%s%N) - $(start_time) ")" / 1000000) ms" \
	,   echo "Nothing to be done")

archiver      := ar 
c_compiler    := gcc
cpp_compiler  := g++


# Building fun objects
compile_flags := -MD -g -O0 -mrdrnd -maes -pthread
c_flags       := $(compile_flags) -std=c11
cpp_flags     := $(compile_flags) -std=c++20
link_flags    := 

sources := $(wildcard src_fun/*.cpp)          \
           src_common/common.cpp              \
           src_common/integer.cpp             \
           src_common/crypto.cpp              \
           src_common/debug.cpp               \
		   src_common/libraries/lk_region.cpp
objects := $(addprefix obj/fun/, $(addsuffix .o, $(sources)))

obj/fun/%.cpp.o: %.cpp
	echo "[$(cpp_compiler)] $<"
	mkdir -p $(dir $@)
	$(cpp_compiler) $(cpp_flags) -c $< -o $@

obj/fun/%.c.o: %.c
	echo "[$(c_compiler)] $<"
	mkdir -p $(dir $@)
	$(c_compiler) $(c_flags) -c $< -o $@



run_tree/fun: $(objects)
	echo "[$(cpp_compiler)] $@"
	mkdir -p $(dir $@)
	$(cpp_compiler) $^ -o $@ $(link_flags)
	$(eval compiled := true)


lines:
	wc -l src_fun/*.cpp src_fun/*.inl src_fun/*.h

clean:
	rm -rf obj/fun
	rm -f  run_tree/fun
	echo "Removed all binaries"

.PHONY: all lines clean

-include $(objects:.o=.d)