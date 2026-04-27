/*
 * riscv.c — HAVEN RISC-V RV32IMA Emulator
 * Full implementation from scratch.
 * Dragon403 × Niyah · Riyadh 2026
 *
 * Implements: RV32I base + M (multiply/divide) + A (atomics)
 * 47 base instructions + 8 M-ext + 11 A-ext
 */

#include "riscv.h"
#include <stdlib.h>
#include <string.h>

/* ── Helpers ── */
static inline int32_t sign_extend(uint32_t val, int bits) {
    uint32_t mask = 1U << (bits - 1);
    return (int32_t)((val ^ mask) - mask);
}

/* ── Memory Access ── */
static uint32_t mem_load(RV32CPU *cpu, uint32_t addr, int size) {
    /* UART */
    if (addr >= UART_BASE && addr < UART_BASE + UART_SIZE) {
        uint32_t off = addr - UART_BASE;
        if (off == UART_RHR) {
            uint8_t ch = cpu->uart_rbr;
            cpu->uart_rx_ready = 0;
            return ch;
        }
        if (off == UART_LSR) {
            return (cpu->uart_rx_ready ? UART_LSR_RX : 0) | UART_LSR_TX;
        }
        return 0;
    }
    
    /* CLINT — timer */
    if (addr >= CLINT_BASE && addr < CLINT_BASE + CLINT_SIZE) {
        if (addr == CLINT_BASE + 0xBFF8) return (uint32_t)(cpu->cycle_count);
        if (addr == CLINT_BASE + 0xBFFC) return (uint32_t)(cpu->cycle_count >> 32);
        return 0;
    }
    
    /* RAM */
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE) {
        uint32_t off = addr - RAM_BASE;
        if (off + (uint32_t)size > RAM_SIZE) return 0;
        switch (size) {
            case 1: return cpu->ram[off];
            case 2: return cpu->ram[off] | ((uint32_t)cpu->ram[off+1] << 8);
            case 4: return cpu->ram[off] | ((uint32_t)cpu->ram[off+1] << 8) |
                           ((uint32_t)cpu->ram[off+2] << 16) | ((uint32_t)cpu->ram[off+3] << 24);
        }
    }
    
    return 0;
}

static void mem_store(RV32CPU *cpu, uint32_t addr, uint32_t val, int size) {
    /* UART TX */
    if (addr >= UART_BASE && addr < UART_BASE + UART_SIZE) {
        uint32_t off = addr - UART_BASE;
        if (off == UART_THR) {
            uart_output((uint8_t)(val & 0xFF));
        }
        return;
    }
    
    /* RAM */
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE) {
        uint32_t off = addr - RAM_BASE;
        if (off + (uint32_t)size > RAM_SIZE) return;
        switch (size) {
            case 1:
                cpu->ram[off] = (uint8_t)val;
                break;
            case 2:
                cpu->ram[off]   = (uint8_t)(val);
                cpu->ram[off+1] = (uint8_t)(val >> 8);
                break;
            case 4:
                cpu->ram[off]   = (uint8_t)(val);
                cpu->ram[off+1] = (uint8_t)(val >> 8);
                cpu->ram[off+2] = (uint8_t)(val >> 16);
                cpu->ram[off+3] = (uint8_t)(val >> 24);
                break;
        }
    }
}

/* ── CSR Access ── */
static uint32_t csr_read(RV32CPU *cpu, uint32_t addr) {
    switch (addr) {
        case CSR_MSTATUS:  return cpu->mstatus;
        case CSR_MIE:      return cpu->mie;
        case CSR_MTVEC:    return cpu->mtvec;
        case CSR_MEPC:     return cpu->mepc;
        case CSR_MCAUSE:   return cpu->mcause;
        case CSR_MTVAL:    return cpu->mtval;
        case CSR_MIP:      return cpu->mip;
        case CSR_MHARTID:  return 0;
        case 0xC00:        return (uint32_t)cpu->cycle_count;  /* cycle */
        case 0xC80:        return (uint32_t)(cpu->cycle_count >> 32); /* cycleh */
        default:           return 0;
    }
}

static void csr_write(RV32CPU *cpu, uint32_t addr, uint32_t val) {
    switch (addr) {
        case CSR_MSTATUS:  cpu->mstatus = val; break;
        case CSR_MIE:      cpu->mie = val; break;
        case CSR_MTVEC:    cpu->mtvec = val; break;
        case CSR_MEPC:     cpu->mepc = val; break;
        case CSR_MCAUSE:   cpu->mcause = val; break;
        case CSR_MTVAL:    cpu->mtval = val; break;
        case CSR_MIP:      cpu->mip = val; break;
    }
}

/* ── Exception Handling ── */
static void trap(RV32CPU *cpu, uint32_t cause, uint32_t tval) {
    cpu->mepc = cpu->pc;
    cpu->mcause = cause;
    cpu->mtval = tval;
    /* Save and clear MIE in MSTATUS */
    cpu->mstatus = (cpu->mstatus & ~(1 << 7)) | (((cpu->mstatus >> 3) & 1) << 7);
    cpu->mstatus &= ~(1 << 3);
    cpu->pc = cpu->mtvec;
}

/* ── Instruction Decode & Execute ── */
int rv32_step(RV32CPU *cpu) {
    if (cpu->halted) return -1;
    
    uint32_t inst = mem_load(cpu, cpu->pc, 4);
    uint32_t opcode = inst & 0x7F;
    uint32_t rd     = (inst >> 7) & 0x1F;
    uint32_t funct3 = (inst >> 12) & 0x7;
    uint32_t rs1    = (inst >> 15) & 0x1F;
    uint32_t rs2    = (inst >> 20) & 0x1F;
    uint32_t funct7 = (inst >> 25) & 0x7F;
    
    int32_t imm_i = sign_extend((inst >> 20), 12);
    int32_t imm_s = sign_extend(((inst >> 25) << 5) | ((inst >> 7) & 0x1F), 12);
    int32_t imm_b = sign_extend(
        (((inst >> 31) & 1) << 12) | (((inst >> 7) & 1) << 11) |
        (((inst >> 25) & 0x3F) << 5) | (((inst >> 8) & 0xF) << 1), 13);
    int32_t imm_u = (int32_t)(inst & 0xFFFFF000U);
    int32_t imm_j = sign_extend(
        (((inst >> 31) & 1) << 20) | (((inst >> 12) & 0xFF) << 12) |
        (((inst >> 20) & 1) << 11) | (((inst >> 21) & 0x3FF) << 1), 21);
    
    uint32_t next_pc = cpu->pc + 4;
    
    switch (opcode) {
        
    /* ═══ LUI ═══ */
    case 0x37:
        if (rd) cpu->x[rd] = (uint32_t)imm_u;
        break;
    
    /* ═══ AUIPC ═══ */
    case 0x17:
        if (rd) cpu->x[rd] = cpu->pc + (uint32_t)imm_u;
        break;
    
    /* ═══ JAL ═══ */
    case 0x6F:
        if (rd) cpu->x[rd] = next_pc;
        next_pc = cpu->pc + (uint32_t)imm_j;
        break;
    
    /* ═══ JALR ═══ */
    case 0x67:
        {
            uint32_t target = (cpu->x[rs1] + (uint32_t)imm_i) & ~1U;
            if (rd) cpu->x[rd] = next_pc;
            next_pc = target;
        }
        break;
    
    /* ═══ BRANCH ═══ */
    case 0x63:
        {
            int take = 0;
            switch (funct3) {
                case 0: take = (cpu->x[rs1] == cpu->x[rs2]); break; /* BEQ */
                case 1: take = (cpu->x[rs1] != cpu->x[rs2]); break; /* BNE */
                case 4: take = ((int32_t)cpu->x[rs1] < (int32_t)cpu->x[rs2]); break; /* BLT */
                case 5: take = ((int32_t)cpu->x[rs1] >= (int32_t)cpu->x[rs2]); break; /* BGE */
                case 6: take = (cpu->x[rs1] < cpu->x[rs2]); break; /* BLTU */
                case 7: take = (cpu->x[rs1] >= cpu->x[rs2]); break; /* BGEU */
                default: trap(cpu, EXC_ILLEGAL_INST, inst); goto done;
            }
            if (take) next_pc = cpu->pc + (uint32_t)imm_b;
        }
        break;
    
    /* ═══ LOAD ═══ */
    case 0x03:
        {
            uint32_t addr = cpu->x[rs1] + (uint32_t)imm_i;
            uint32_t val = 0;
            switch (funct3) {
                case 0: val = (uint32_t)sign_extend(mem_load(cpu, addr, 1), 8); break;  /* LB */
                case 1: val = (uint32_t)sign_extend(mem_load(cpu, addr, 2), 16); break; /* LH */
                case 2: val = mem_load(cpu, addr, 4); break;                             /* LW */
                case 4: val = mem_load(cpu, addr, 1); break;                             /* LBU */
                case 5: val = mem_load(cpu, addr, 2); break;                             /* LHU */
                default: trap(cpu, EXC_ILLEGAL_INST, inst); goto done;
            }
            if (rd) cpu->x[rd] = val;
        }
        break;
    
    /* ═══ STORE ═══ */
    case 0x23:
        {
            uint32_t addr = cpu->x[rs1] + (uint32_t)imm_s;
            switch (funct3) {
                case 0: mem_store(cpu, addr, cpu->x[rs2], 1); break; /* SB */
                case 1: mem_store(cpu, addr, cpu->x[rs2], 2); break; /* SH */
                case 2: mem_store(cpu, addr, cpu->x[rs2], 4); break; /* SW */
                default: trap(cpu, EXC_ILLEGAL_INST, inst); goto done;
            }
        }
        break;
    
    /* ═══ ALU Immediate ═══ */
    case 0x13:
        {
            uint32_t val = 0;
            uint32_t src = cpu->x[rs1];
            uint32_t shamt = rs2; /* shift amount = imm[4:0] */
            switch (funct3) {
                case 0: val = src + (uint32_t)imm_i; break;                      /* ADDI */
                case 1: val = src << shamt; break;                                 /* SLLI */
                case 2: val = ((int32_t)src < imm_i) ? 1 : 0; break;             /* SLTI */
                case 3: val = (src < (uint32_t)imm_i) ? 1 : 0; break;            /* SLTIU */
                case 4: val = src ^ (uint32_t)imm_i; break;                      /* XORI */
                case 5:
                    if (funct7 & 0x20) val = (uint32_t)((int32_t)src >> shamt);   /* SRAI */
                    else val = src >> shamt;                                        /* SRLI */
                    break;
                case 6: val = src | (uint32_t)imm_i; break;                      /* ORI */
                case 7: val = src & (uint32_t)imm_i; break;                      /* ANDI */
            }
            if (rd) cpu->x[rd] = val;
        }
        break;
    
    /* ═══ ALU Register ═══ */
    case 0x33:
        {
            uint32_t val = 0;
            uint32_t a = cpu->x[rs1], b = cpu->x[rs2];
            
            if (funct7 == 0x01) {
                /* ── M Extension (Multiply/Divide) ── */
                switch (funct3) {
                    case 0: val = (uint32_t)((int32_t)a * (int32_t)b); break;     /* MUL */
                    case 1: val = (uint32_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b >> 32); break; /* MULH */
                    case 2: val = (uint32_t)((int64_t)(int32_t)a * (uint64_t)b >> 32); break; /* MULHSU */
                    case 3: val = (uint32_t)((uint64_t)a * (uint64_t)b >> 32); break; /* MULHU */
                    case 4: /* DIV */
                        if (b == 0) val = 0xFFFFFFFF;
                        else if (a == 0x80000000 && b == 0xFFFFFFFF) val = a;
                        else val = (uint32_t)((int32_t)a / (int32_t)b);
                        break;
                    case 5: /* DIVU */
                        val = b ? a / b : 0xFFFFFFFF;
                        break;
                    case 6: /* REM */
                        if (b == 0) val = a;
                        else if (a == 0x80000000 && b == 0xFFFFFFFF) val = 0;
                        else val = (uint32_t)((int32_t)a % (int32_t)b);
                        break;
                    case 7: /* REMU */
                        val = b ? a % b : a;
                        break;
                }
            } else {
                /* ── Base I ── */
                switch (funct3) {
                    case 0:
                        val = (funct7 & 0x20) ? a - b : a + b; /* ADD/SUB */
                        break;
                    case 1: val = a << (b & 0x1F); break;           /* SLL */
                    case 2: val = ((int32_t)a < (int32_t)b) ? 1 : 0; break; /* SLT */
                    case 3: val = (a < b) ? 1 : 0; break;           /* SLTU */
                    case 4: val = a ^ b; break;                       /* XOR */
                    case 5:
                        if (funct7 & 0x20) val = (uint32_t)((int32_t)a >> (b & 0x1F)); /* SRA */
                        else val = a >> (b & 0x1F);                                     /* SRL */
                        break;
                    case 6: val = a | b; break;                       /* OR */
                    case 7: val = a & b; break;                       /* AND */
                }
            }
            if (rd) cpu->x[rd] = val;
        }
        break;
    
    /* ═══ SYSTEM ═══ */
    case 0x73:
        {
            uint32_t csr_addr = (inst >> 20) & 0xFFF;
            
            if (funct3 == 0) {
                /* ECALL / EBREAK / MRET / WFI */
                switch (csr_addr) {
                    case 0x000: /* ECALL */
                        trap(cpu, EXC_ECALL_M, 0);
                        goto done;
                    case 0x001: /* EBREAK */
                        /* In bare-metal we treat EBREAK as a graceful halt:
                         * the kernel's `exit` command issues it explicitly.
                         * The host (CLI or browser) detects cpu->halted and
                         * stops cleanly instead of spinning forever. */
                        cpu->halted = 1;
                        goto done;
                    case 0x302: /* MRET */
                        next_pc = cpu->mepc;
                        /* Restore MIE from MPIE */
                        cpu->mstatus = (cpu->mstatus & ~(1 << 3)) | (((cpu->mstatus >> 7) & 1) << 3);
                        cpu->mstatus |= (1 << 7);
                        break;
                    case 0x105: /* WFI — just nop */
                        break;
                    default:
                        trap(cpu, EXC_ILLEGAL_INST, inst);
                        goto done;
                }
            } else {
                /* CSR instructions */
                uint32_t old_val = csr_read(cpu, csr_addr);
                uint32_t new_val = old_val;
                uint32_t src = (funct3 & 0x4) ? rs1 : cpu->x[rs1]; /* CSRRWI/CSRRSI/CSRRCI use zimm */
                
                switch (funct3 & 0x3) {
                    case 1: new_val = src; break;              /* CSRRW */
                    case 2: new_val = old_val | src; break;    /* CSRRS */
                    case 3: new_val = old_val & ~src; break;   /* CSRRC */
                }
                csr_write(cpu, csr_addr, new_val);
                if (rd) cpu->x[rd] = old_val;
            }
        }
        break;
    
    /* ═══ FENCE ═══ */
    case 0x0F:
        /* NOP for single-core */
        break;
    
    /* ═══ AMO (Atomic) ═══ */
    case 0x2F:
        {
            uint32_t addr = cpu->x[rs1];
            uint32_t funct5 = funct7 >> 2;
            
            switch (funct5) {
                case 0x02: /* LR.W */
                    if (rd) cpu->x[rd] = mem_load(cpu, addr, 4);
                    cpu->reservation_addr = addr;
                    cpu->reservation_valid = 1;
                    break;
                case 0x03: /* SC.W */
                    if (cpu->reservation_valid && cpu->reservation_addr == addr) {
                        mem_store(cpu, addr, cpu->x[rs2], 4);
                        if (rd) cpu->x[rd] = 0; /* success */
                    } else {
                        if (rd) cpu->x[rd] = 1; /* failure */
                    }
                    cpu->reservation_valid = 0;
                    break;
                case 0x01: { /* AMOSWAP.W */
                    uint32_t old = mem_load(cpu, addr, 4);
                    mem_store(cpu, addr, cpu->x[rs2], 4);
                    if (rd) cpu->x[rd] = old;
                    break;
                }
                case 0x00: { /* AMOADD.W */
                    uint32_t old = mem_load(cpu, addr, 4);
                    mem_store(cpu, addr, old + cpu->x[rs2], 4);
                    if (rd) cpu->x[rd] = old;
                    break;
                }
                case 0x04: { /* AMOXOR.W */
                    uint32_t old = mem_load(cpu, addr, 4);
                    mem_store(cpu, addr, old ^ cpu->x[rs2], 4);
                    if (rd) cpu->x[rd] = old;
                    break;
                }
                case 0x08: { /* AMOAND.W */
                    uint32_t old = mem_load(cpu, addr, 4);
                    mem_store(cpu, addr, old & cpu->x[rs2], 4);
                    if (rd) cpu->x[rd] = old;
                    break;
                }
                case 0x0C: { /* AMOOR.W */
                    uint32_t old = mem_load(cpu, addr, 4);
                    mem_store(cpu, addr, old | cpu->x[rs2], 4);
                    if (rd) cpu->x[rd] = old;
                    break;
                }
                case 0x10: { /* AMOMIN.W */
                    uint32_t old = mem_load(cpu, addr, 4);
                    mem_store(cpu, addr, ((int32_t)old < (int32_t)cpu->x[rs2]) ? old : cpu->x[rs2], 4);
                    if (rd) cpu->x[rd] = old;
                    break;
                }
                case 0x14: { /* AMOMAX.W */
                    uint32_t old = mem_load(cpu, addr, 4);
                    mem_store(cpu, addr, ((int32_t)old > (int32_t)cpu->x[rs2]) ? old : cpu->x[rs2], 4);
                    if (rd) cpu->x[rd] = old;
                    break;
                }
                case 0x18: { /* AMOMINU.W */
                    uint32_t old = mem_load(cpu, addr, 4);
                    mem_store(cpu, addr, (old < cpu->x[rs2]) ? old : cpu->x[rs2], 4);
                    if (rd) cpu->x[rd] = old;
                    break;
                }
                case 0x1C: { /* AMOMAXU.W */
                    uint32_t old = mem_load(cpu, addr, 4);
                    mem_store(cpu, addr, (old > cpu->x[rs2]) ? old : cpu->x[rs2], 4);
                    if (rd) cpu->x[rd] = old;
                    break;
                }
                default:
                    trap(cpu, EXC_ILLEGAL_INST, inst);
                    goto done;
            }
        }
        break;
    
    default:
        trap(cpu, EXC_ILLEGAL_INST, inst);
        goto done;
    }
    
    cpu->pc = next_pc;
done:
    cpu->x[0] = 0; /* x0 is always zero */
    cpu->cycle_count++;
    return 0;
}

/* ── Run N cycles ── */
void rv32_run(RV32CPU *cpu, int cycles) {
    for (int i = 0; i < cycles && !cpu->halted; i++) {
        rv32_step(cpu);
    }
}

/* ── Create/Destroy ── */
RV32CPU* rv32_create(void) {
    RV32CPU *cpu = (RV32CPU*)calloc(1, sizeof(RV32CPU));
    if (!cpu) return NULL;
    cpu->ram = (uint8_t*)calloc(1, RAM_SIZE);
    if (!cpu->ram) { free(cpu); return NULL; }
    return cpu;
}

void rv32_destroy(RV32CPU *cpu) {
    if (cpu) {
        free(cpu->ram);
        free(cpu);
    }
}

void rv32_reset(RV32CPU *cpu, uint32_t entry) {
    memset(cpu->x, 0, sizeof(cpu->x));
    cpu->pc = entry;
    cpu->halted = 0;
    cpu->cycle_count = 0;
    cpu->mstatus = 0;
    cpu->x[2] = RAM_BASE + RAM_SIZE - 16; /* SP = top of RAM */
}

void rv32_uart_input(RV32CPU *cpu, uint8_t ch) {
    cpu->uart_rbr = ch;
    cpu->uart_rx_ready = 1;
}

void rv32_load_binary(RV32CPU *cpu, const uint8_t *data, uint32_t size, uint32_t addr) {
    if (addr < RAM_BASE) return;
    uint32_t off = addr - RAM_BASE;
    if (off + size > RAM_SIZE) size = RAM_SIZE - off;
    memcpy(cpu->ram + off, data, size);
}
