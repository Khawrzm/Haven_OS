/*
 * riscv.h — HAVEN RISC-V Emulator
 * RV32IMA — Integer + Multiply + Atomic
 * Built from scratch by Dragon403 × Niyah
 * Zero dependencies. Pure C99.
 */

#ifndef HAVEN_RISCV_H
#define HAVEN_RISCV_H

#include <stdint.h>
#include <stddef.h>

/* ── Memory Map ── */
#define RAM_BASE    0x80000000U
#define RAM_SIZE    (16 * 1024 * 1024)  /* 16 MB */
#define UART_BASE   0x10000000U
#define UART_SIZE   0x100
#define CLINT_BASE  0x02000000U
#define CLINT_SIZE  0x10000

/* ── UART Registers ── */
#define UART_RHR    0  /* Receive Holding Register */
#define UART_THR    0  /* Transmit Holding Register */
#define UART_LSR    5  /* Line Status Register */
#define UART_LSR_RX 0x01
#define UART_LSR_TX 0x20

/* ── CSR Addresses ── */
#define CSR_MSTATUS    0x300
#define CSR_MIE        0x304
#define CSR_MTVEC      0x305
#define CSR_MEPC       0x341
#define CSR_MCAUSE     0x342
#define CSR_MTVAL      0x343
#define CSR_MIP        0x344
#define CSR_MHARTID    0xF14

/* ── Exception Codes ── */
#define EXC_INST_MISALIGNED   0
#define EXC_ILLEGAL_INST      2
#define EXC_BREAKPOINT        3
#define EXC_LOAD_MISALIGNED   4
#define EXC_LOAD_FAULT        5
#define EXC_STORE_MISALIGNED  6
#define EXC_STORE_FAULT       7
#define EXC_ECALL_M           11

/* ── CPU State ── */
typedef struct {
    uint32_t x[32];       /* General purpose registers */
    uint32_t pc;          /* Program counter */
    
    /* CSRs */
    uint32_t mstatus;
    uint32_t mie;
    uint32_t mtvec;
    uint32_t mepc;
    uint32_t mcause;
    uint32_t mtval;
    uint32_t mip;
    
    /* Memory */
    uint8_t *ram;
    
    /* UART */
    uint8_t uart_rbr;      /* RX buffer */
    uint8_t uart_rx_ready;
    
    /* State */
    int halted;
    uint64_t cycle_count;
    
    /* Atomic reservation */
    uint32_t reservation_addr;
    int reservation_valid;
    
} RV32CPU;

/* ── API ── */
RV32CPU* rv32_create(void);
void     rv32_destroy(RV32CPU *cpu);
void     rv32_reset(RV32CPU *cpu, uint32_t entry);
int      rv32_step(RV32CPU *cpu);
void     rv32_run(RV32CPU *cpu, int cycles);
void     rv32_uart_input(RV32CPU *cpu, uint8_t ch);
void     rv32_load_binary(RV32CPU *cpu, const uint8_t *data, uint32_t size, uint32_t addr);

/* ── Callbacks (implemented in JS) ── */
extern void uart_output(uint8_t ch);

#endif /* HAVEN_RISCV_H */
