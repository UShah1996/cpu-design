/*
 * =============================================================================
 * CMPE-220 Software CPU — CPU Header
 * =============================================================================
 *
 * Declares the CPU struct and the public API for the emulator.
 * The implementation (Fetch/Decode/Execute pipeline, ALU, Control Unit)
 * is in cpu.c.
 *
 * REGISTERS:
 *   R0  – R12  : 16-bit general-purpose registers
 *   R13 / SP   : Stack Pointer — points to the top of the hardware stack.
 *                Decremented by CALL, incremented by RET.
 *   R14 / LR   : Link Register — holds the return address after a CALL.
 *                Saved to the stack so nested calls work correctly.
 *   R15 / PC   : Program Counter — address of the next instruction to fetch.
 *                Starts at CODE_START (0x0100).
 *
 * FLAGS REGISTER:
 *   Bit 0 = ZF  (Zero Flag)     — set when an ALU result is 0
 *   Bit 1 = CF  (Carry Flag)    — set on unsigned overflow or borrow
 *   Bit 2 = SF  (Sign Flag)     — set when result has bit 15 = 1 (negative)
 *   Bit 3 = OF  (Overflow Flag) — set on signed overflow
 *
 * CALL / RET CONVENTION:
 *   CALL target:  SP--, mem[SP] = LR,  LR = PC,  PC = target
 *   RET:          PC = LR,  LR = mem[SP],  SP++
 *   The stack grows downward from STACK_BASE (0x0FBF).
 *
 * =============================================================================
 */

#ifndef CPU_H
#define CPU_H

#include "../isa/isa.h"
#include "memory.h"
#include <stdint.h>

/*
 * CPU — complete state of the processor at any point in time.
 *
 * In real hardware, each Word is a bank of 16 flip-flops.
 * Here we use a C struct to model the same state.
 */
typedef struct {
    Word     R[REG_COUNT];   /* R0–R12: general purpose; R13=SP, R14=LR, R15=PC */
    Word     FLAGS;          /* ZF | CF | SF | OF (see flag masks in isa.h)      */
    Memory*  mem;            /* pointer to the attached memory subsystem          */
    uint64_t cycle_count;    /* total clock cycles executed (1 per fetch + exec)  */
    uint64_t instr_count;    /* total instructions retired                        */
    int      halted;         /* 1 = CPU has stopped (self-loop or error)         */
    int      trace_on;       /* 1 = print each instruction as it executes        */
} CPU;

/* ── Public API ──────────────────────────────────────────────────────────── */

/*
 * cpu_init — power-on reset.
 *   Sets all registers to 0, PC = CODE_START, SP = STACK_BASE.
 *   Must be called before cpu_step() or cpu_run().
 */
void cpu_init         (CPU* cpu, Memory* mem);

/*
 * cpu_reset — soft reset (keeps memory attached, optionally keeps trace flag).
 *   Equivalent to power-cycling the CPU without changing RAM.
 */
void cpu_reset        (CPU* cpu);

/*
 * cpu_step — execute exactly one instruction (Fetch → Decode → Execute).
 *   Does nothing if cpu->halted is set.
 */
void cpu_step         (CPU* cpu);

/*
 * cpu_run — execute up to max_cycles instructions.
 *   Stops early if the CPU halts (PC doesn't change after an instruction).
 */
void cpu_run          (CPU* cpu, uint64_t max_cycles);

/*
 * cpu_dump_registers — print a formatted table of all register values
 *   plus the FLAGS register and cycle/instruction counts.
 */
void cpu_dump_registers(const CPU* cpu);

#endif /* CPU_H */
