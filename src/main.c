/*
 * main.c — Emscripten bridge
 * Connects HAVEN RISC-V emulator to the browser
 */

#include <emscripten.h>
#include <emscripten/html5.h>
#include "riscv.h"
#include <string.h>

static RV32CPU *cpu = NULL;

/* Output buffer for JS to read */
#define OUT_BUF_SIZE 4096
static char output_buf[OUT_BUF_SIZE];
static int output_pos = 0;

/* Called by riscv.c when UART TX happens */
void uart_output(uint8_t ch) {
    if (output_pos < OUT_BUF_SIZE - 1) {
        output_buf[output_pos++] = (char)ch;
    }
}

/* JS calls this to get output */
EMSCRIPTEN_KEEPALIVE
const char* get_output(void) {
    output_buf[output_pos] = 0;
    return output_buf;
}

EMSCRIPTEN_KEEPALIVE
void clear_output(void) {
    output_pos = 0;
    output_buf[0] = 0;
}

EMSCRIPTEN_KEEPALIVE
int get_output_len(void) {
    return output_pos;
}

/* JS calls this to send keyboard input */
EMSCRIPTEN_KEEPALIVE
void send_key(int ch) {
    if (cpu) {
        rv32_uart_input(cpu, (uint8_t)ch);
    }
}

/* Run a batch of cycles */
EMSCRIPTEN_KEEPALIVE
void run_cycles(int n) {
    if (cpu && !cpu->halted) {
        rv32_run(cpu, n);
    }
}

EMSCRIPTEN_KEEPALIVE
int is_halted(void) {
    return cpu ? cpu->halted : 1;
}

EMSCRIPTEN_KEEPALIVE
uint64_t get_cycle_count(void) {
    return cpu ? cpu->cycle_count : 0;
}

/* External: kernel binary embedded */
extern const unsigned char kernel_bin[];
extern const unsigned int kernel_bin_len;

EMSCRIPTEN_KEEPALIVE
void init_system(void) {
    cpu = rv32_create();
    if (!cpu) return;
    
    rv32_reset(cpu, RAM_BASE);
    rv32_load_binary(cpu, kernel_bin, kernel_bin_len, RAM_BASE);
}
