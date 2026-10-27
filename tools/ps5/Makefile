# Builds the TTT2 PS5 smoke payload (M3) with the ps5-payload-sdk.
#
# Usage:
#   PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -f tools/ps5/Makefile all
#   PS5_HOST=<PS5 IP> PS5_PORT=9021 make -f tools/ps5/Makefile test
#
# The SDK must be installed once from
# .superpowers/m3-downloads/ps5-payload-sdk.zip (see plan M3, Task 2).

SELF_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := $(SELF_DIR)build/ttt2_smoke.elf

CFLAGS := -Wall -Werror -g

all: $(ELF)

$(ELF): $(SELF_DIR)smoke/main.c | $(SELF_DIR)build
	$(CC) $(CFLAGS) -o $@ $^

$(SELF_DIR)build:
	mkdir -p $(SELF_DIR)build

clean:
	rm -rf $(SELF_DIR)build

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^

.PHONY: all clean test
