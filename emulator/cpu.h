/*
 * ============================================================
 * CMPE-220 Software CPU — CPU Header (C99)
 * ============================================================
 * FIXED: Replaced C++ class with C struct + function pointers.
 *        cpu.c contains the implementation.
 * ============================================================
 */

#ifndef CPU_H
#define CPU_H

#include "../isa/isa.h"
#include "memory.h"
#include <stdint.h>

/* ── CPU state structure ─────────────────────────────────────
 * All state needed to describe the CPU at any point in time.
 * In real hardware each Word is a bank of 16 flip-flops.
 */
typedef struct {
    Word     R[REG_COUNT];   /* 16 general-purpose registers   */
    Word     FLAGS;          /* ZF | CF | SF | OF              */
    Memory*  mem;            /* pointer to attached memory     */
    uint64_t cycle_count;    /* total clock cycles executed    */
    uint64_t instr_count;    /* total instructions executed    */
    int      halted;         /* 1 = CPU has halted             */
    int      trace_on;       /* 1 = print each instruction     */
} CPU;

/* ── Public API ──────────────────────────────────────────────*/
void cpu_init         (CPU* cpu, Memory* mem);
void cpu_reset        (CPU* cpu);
void cpu_step         (CPU* cpu);
void cpu_run          (CPU* cpu, uint64_t max_cycles);
void cpu_dump_registers(const CPU* cpu);

#endif /* CPU_H */
