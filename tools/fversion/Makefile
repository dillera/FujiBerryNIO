# fversion for Workbench 1.3+: same compiler flags as the driver's KS1.3
# tools (fujinet-nio-driver/amiga/Makefile), linked against nix13.
CC := m68k-amigaos-gcc
CFLAGS := -std=c99 -Wall -Wextra -Werror -O2 -mcpu=68000 -msoft-float
BUILD_DIR ?= build

$(BUILD_DIR)/fversion: fversion.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -mcrt=nix13 $< -lamiga -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: clean
