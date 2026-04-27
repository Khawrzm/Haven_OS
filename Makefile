# HAVEN OS — top-level build
#
# Targets:
#   make           build the native host CLI (`haven`)
#   make haven     same as above
#   make web       build web/haven.{js,wasm} via emscripten (needs `emcc`)
#   make kernel    build kernel/kernel.bin
#   make embed     rebuild kernel.bin AND regenerate src/kernel_data.c
#   make test      run the smoke suite (tests/run_smoke.sh)
#   make serve     start a local HTTP server for web/ on http://localhost:8080
#   make fmt       format C sources with clang-format
#   make clean     remove all build artifacts
#   make help      print this help
#
# The emulator itself (src/riscv.c + src/main.c + src/kernel_data.c) is pure
# C99 with zero dependencies beyond libc.

# ── Tooling ────────────────────────────────────────────────────────
CC      ?= cc
PYTHON  ?= python3
EMCC    ?= emcc
PORT    ?= 8080

CFLAGS_NATIVE := \
    -std=c99 -O2 -g \
    -Wall -Wextra -Werror -Wstrict-prototypes \
    -Wmissing-prototypes -Wcast-align -Wwrite-strings -Wshadow -pedantic

CFLAGS_WASM := \
    -std=c99 -O3 \
    -Wall -Wextra -Werror -Wstrict-prototypes \
    -Wmissing-prototypes -Wcast-align -Wwrite-strings -Wshadow -pedantic

# Emscripten exports — must match the function names called from web/index.html
EMCC_EXPORTS := \
    _init_system,_run_cycles,_send_key,_get_output,_get_output_len,\
    _clear_output,_is_halted,_get_cycle_count,_malloc,_free

EMCC_RUNTIME := \
    ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8

EMCC_FLAGS := \
    -s WASM=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME=createHavenModule \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=33554432 \
    -s ENVIRONMENT=web,worker \
    -s EXPORTED_FUNCTIONS='[$(EMCC_EXPORTS)]' \
    -s EXPORTED_RUNTIME_METHODS='[$(EMCC_RUNTIME)]'

# ── Sources ────────────────────────────────────────────────────────
EMU_SRCS    := src/riscv.c src/kernel_data.c
HOST_SRCS   := src/host_cli.c $(EMU_SRCS)
WEB_SRCS    := src/main.c $(EMU_SRCS)

# ── Targets ────────────────────────────────────────────────────────
.PHONY: all haven web kernel embed test serve fmt clean help

all: haven

haven: $(HOST_SRCS) src/riscv.h
	$(CC) $(CFLAGS_NATIVE) $(HOST_SRCS) -o haven
	@echo "[haven] built ./haven ($$(stat -c%s haven 2>/dev/null || stat -f%z haven) bytes)"

web: web/haven.js

web/haven.js: $(WEB_SRCS) src/riscv.h
	@command -v $(EMCC) >/dev/null 2>&1 || { \
	  echo "error: emcc not found. Install Emscripten: https://emscripten.org/docs/getting_started/downloads.html"; exit 1; }
	$(EMCC) $(CFLAGS_WASM) $(EMCC_FLAGS) $(WEB_SRCS) -o web/haven.js
	@echo "[haven] built web/haven.js + web/haven.wasm"

kernel:
	$(MAKE) -C kernel

embed:
	$(MAKE) -C kernel install-data

test: haven
	bash tests/run_smoke.sh

serve:
	@echo "[haven] serving web/ on http://localhost:$(PORT)/"
	@cd web && $(PYTHON) -m http.server $(PORT)

fmt:
	@command -v clang-format >/dev/null 2>&1 && \
	  clang-format -i src/*.c src/*.h kernel/*.c || \
	  echo "clang-format not installed — skipping"

clean:
	rm -f haven haven.exe web/haven.js web/haven.wasm web/haven.wasm.map
	$(MAKE) -C kernel clean 2>/dev/null || true

help:
	@awk 'BEGIN{FS=":.*##"} /^[a-zA-Z_-]+:.*##/ {printf "  %-12s %s\n",$$1,$$2}' $(MAKEFILE_LIST)
	@echo ""
	@echo "Common workflows:"
	@echo "  Native CLI:    make && ./haven"
	@echo "  Web (Wasm):    make web && make serve"
	@echo "  Refresh kern:  make kernel embed && make"
