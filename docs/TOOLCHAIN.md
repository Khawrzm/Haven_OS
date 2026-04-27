# RISC-V Toolchain Setup

You only need this if you want to **rebuild `kernel.bin`** from
`kernel/kernel.c`. The repo ships with a pre-built kernel embedded in
`src/kernel_data.c`, so you can run HAVEN OS without any RISC-V tools.

`kernel/Makefile` auto-detects the toolchain. Any of the following work.

---

## Option 1 — GCC (recommended)

### macOS (Homebrew)

```bash
brew tap riscv-software-src/riscv
brew install riscv-tools
```

This gives you `riscv64-unknown-elf-gcc`, which can target RV32 via
`-march=rv32ima -mabi=ilp32`.

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install gcc-riscv64-unknown-elf
```

### Arch / Manjaro

```bash
sudo pacman -S riscv64-elf-gcc riscv64-elf-binutils
```

### Build from source

```bash
git clone https://github.com/riscv-collab/riscv-gnu-toolchain.git
cd riscv-gnu-toolchain
./configure --prefix=/opt/riscv --enable-multilib
make -j$(nproc)
export PATH=/opt/riscv/bin:$PATH
```

---

## Option 2 — Clang + LLD

If you already have a recent Clang (≥13) and LLD installed, you have a
RV32 cross-compiler — Clang is multi-target by default.

### macOS

```bash
brew install llvm
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

### Debian / Ubuntu

```bash
sudo apt install clang lld llvm
```

The Makefile invokes Clang with `--target=riscv32 -march=rv32ima -mabi=ilp32`
and links via `ld.lld`.

---

## Verifying the toolchain

```bash
cd kernel
make
```

Expected output:

```
[kernel] linked kernel.elf (XXXX bytes)
[kernel] kernel.bin XXXX bytes
```

If `make` complains "No RISC-V toolchain found", check that one of these
is in your `PATH`:

- `riscv32-unknown-elf-gcc`
- `riscv64-unknown-elf-gcc`
- `riscv32-elf-gcc`
- `riscv64-elf-gcc`
- `riscv-none-elf-gcc`
- `clang` (with `lld`)

---

## Updating the embedded kernel

After rebuilding `kernel.bin`, regenerate the C array consumed by the
host:

```bash
make embed      # runs scripts/bin2c.py
make            # rebuild ./haven with the new kernel
make test       # confirm 19/19 still pass
```
