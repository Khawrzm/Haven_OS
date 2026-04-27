# kernel/Makefile — build the bare-metal RV32IMA kernel
#
# Produces:
#   kernel.elf  — ELF for inspection (objdump, readelf, gdb)
#   kernel.bin  — flat binary loaded by the emulator at RAM_BASE
#
# Toolchain: riscv64-unknown-elf-gcc / riscv32-unknown-elf-gcc / clang --target=riscv32
# We auto-detect a working cross-compiler.

# ── Toolchain detection ─────────────────────────────────────────────
CROSS := $(shell \
    for t in riscv32-unknown-elf- riscv64-unknown-elf- riscv32-elf- riscv64-elf- riscv-none-elf-; do \
        command -v $${t}gcc >/dev/null 2>&1 && { echo $$t; break; }; \
    done)

ifeq ($(CROSS),)
    # Fall back to clang
    HAS_CLANG := $(shell command -v clang >/dev/null 2>&1 && echo yes)
    ifeq ($(HAS_CLANG),yes)
        CC      := clang --target=riscv32 -march=rv32ima -mabi=ilp32 -mno-relax
        LD      := ld.lld
        OBJCOPY := llvm-objcopy
        OBJDUMP := llvm-objdump
    else
        $(error No RISC-V toolchain found. Install riscv32-unknown-elf-gcc, \
        riscv64-unknown-elf-gcc, or clang+lld. See ../docs/TOOLCHAIN.md)
    endif
else
    CC      := $(CROSS)gcc
    LD      := $(CROSS)ld
    OBJCOPY := $(CROSS)objcopy
    OBJDUMP := $(CROSS)objdump
endif

# ── Flags ──────────────────────────────────────────────────────────
CFLAGS := \
    -march=rv32ima -mabi=ilp32 \
    -ffreestanding -nostdlib -nostartfiles \
    -fno-builtin -fno-pic -mno-relax \
    -O2 -g \
    -Wall -Wextra -Werror -Wstrict-prototypes \
    -Wmissing-prototypes -Wcast-align -Wshadow

LDFLAGS := -T link.ld -nostdlib -static --gc-sections

# When using GCC's gcc-as-linker driver, use -Wl, prefix
ifneq ($(findstring clang,$(CC)),clang)
    LINK_VIA_DRIVER := 1
endif

# ── Targets ────────────────────────────────────────────────────────
.PHONY: all clean disasm install-data help

all: kernel.bin kernel.elf

kernel.elf: kernel.o link.ld
ifdef LINK_VIA_DRIVER
	$(CC) $(CFLAGS) -T link.ld -Wl,--gc-sections -o $@ kernel.o
else
	$(LD) $(LDFLAGS) -o $@ kernel.o
endif
	@echo "[kernel] linked $@ ($$(stat -c%s $@) bytes)"

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c -o $@ $<

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@
	@echo "[kernel] $@ $$(stat -c%s $@) bytes"

# Embed kernel.bin as a C array for the host emulator
install-data: kernel.bin
	python3 ../scripts/bin2c.py kernel.bin ../src/kernel_data.c kernel_bin
	@echo "[kernel] embedded in ../src/kernel_data.c"

disasm: kernel.elf
	$(OBJDUMP) -d $< | less

clean:
	rm -f kernel.o kernel.elf kernel.bin

help:
	@echo "kernel/ targets:"
	@echo "  all            build kernel.elf + kernel.bin"
	@echo "  install-data   regenerate ../src/kernel_data.c from kernel.bin"
	@echo "  disasm         disassemble kernel.elf"
	@echo "  clean          remove build artifacts"
	@echo ""
	@echo "Detected toolchain: $(CC)"
