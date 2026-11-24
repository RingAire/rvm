# Standalone build for the RingEcho version manager.
# The mainline Makefile also builds this source for the combined installer;
# this file proves (and later ships) rvm without the compiler tree.

CONFIG ?= Release

ifeq ($(OS),Windows_NT)
    EXE := .exe
    PYTHON ?= python
    MKDIR_P := mkdir -p
    ifeq ($(origin CC),default)
        CC := gcc
    endif
else
    EXE :=
    PYTHON ?= python3
    MKDIR_P := mkdir -p
    ifeq ($(origin CC),default)
        CC := cc
    endif
endif

EXTRA_CFLAGS ?=
CFLAGS_COMMON := -std=c11 -Wall -Wextra -Wpedantic $(EXTRA_CFLAGS)
ifneq ($(OS),Windows_NT)
    CFLAGS_COMMON += -D_POSIX_C_SOURCE=200809L
endif
ifeq ($(CONFIG),Release)
    CFLAGS_PROFILE := -O2 -DNDEBUG
else ifeq ($(CONFIG),Debug)
    CFLAGS_PROFILE := -O0 -g3
else
    $(error unsupported CONFIG '$(CONFIG)'; use Release or Debug)
endif

RV_VERSION ?=
ifneq ($(RV_VERSION),)
    CFLAGS_PROFILE += -DRVM_VERSION=\"$(RV_VERSION)\"
endif

BUILD_DIR := target/$(CONFIG)
OBJ := $(BUILD_DIR)/main.o
BIN := $(BUILD_DIR)/rvm$(EXE)

.PHONY: all build test clean
all: build

build: $(BIN)

$(BIN): $(OBJ)
	@$(MKDIR_P) "$(dir $@)"
	$(CC) $(CFLAGS_COMMON) $(CFLAGS_PROFILE) -o "$@" $^

$(OBJ): src/main.c
	@$(MKDIR_P) "$(dir $@)"
	$(CC) $(CFLAGS_COMMON) $(CFLAGS_PROFILE) -c -o "$@" "$<"

test: $(BIN)
	RVM_TEST_ROOT="$(abspath target/test-tmp)" RVM_BIN="$(abspath $(BIN))" 		python -m unittest discover -s tests -v

clean:
	rm -rf target
