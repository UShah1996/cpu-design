/*
 * =============================================================================
 * CMPE-220 Software CPU — Memory Subsystem Implementation
 * =============================================================================
 *
 * This file implements the memory bus. All CPU reads and writes pass through
 * mem_read() / mem_write(), which intercept I/O addresses before touching RAM.
 *
 * The key insight is that I/O "registers" share the same address space as RAM.
 * Writing to 0x0FC0 (IO_STDOUT) prints a character — it doesn't store to RAM.
 * This is called Memory-Mapped I/O (MMIO) and is used in real CPUs.
 *
 * =============================================================================
 */

#include "memory.h"
#include <stdio.h>    /* printf, putchar, getchar, fprintf */
#include <string.h>   /* memset                            */
#include <stdlib.h>   /* exit                              */

/* ── mem_init ────────────────────────────────────────────────────────────────
 * Zero-initialize the entire memory array and reset the hardware timer.
 * Always call this before loading a program or running the CPU.
 */
void mem_init(Memory* m) {
    memset(m->ram, 0, sizeof(m->ram));   /* clear all 4096 words */
    m->timer_ticks = 0;                  /* reset hardware timer  */
}

/* ── mem_read ─────────────────────────────────────────────────────────────────
 * Read one 16-bit word from the given address.
 *
 * This is called:
 *   - Every time the CPU fetches an instruction (FETCH phase)
 *   - Every time a LOAD instruction executes with DIRECT or INDIRECT mode
 *   - Every time the Control Unit resolves a branch target from the pool
 *
 * Address routing:
 *   IO_TIMER (0x0FC2) → return current timer tick count
 *   IO_STDIN (0x0FC1) → block and read one character from the keyboard
 *   Anything else     → return ram[addr] (normal RAM access)
 */
Word mem_read(Memory* m, Address addr) {
    /* Guard: catch out-of-bounds accesses before they corrupt memory */
    if (addr >= MEM_SIZE) {
        fprintf(stderr, "[MEMORY] ERROR: read out-of-bounds at 0x%04X\n", addr);
        exit(1);
    }

    /* Memory-Mapped I/O — timer */
    if (addr == IO_TIMER) {
        return m->timer_ticks;          /* hardware counter value */
    }

    /* Memory-Mapped I/O — keyboard input */
    if (addr == IO_STDIN) {
        int c = getchar();              /* blocks until user presses Enter */
        return (c == EOF) ? 0 : (Word)c;
    }

    /* Normal RAM read */
    return m->ram[addr];
}

/* ── mem_write ────────────────────────────────────────────────────────────────
 * Write one 16-bit word to the given address.
 *
 * This is called by:
 *   - STORE instructions in the CPU
 *   - The assembler loader (mem_load_program, asm_apply_pool)
 *   - CALL instruction (saves LR to the stack)
 *
 * Address routing:
 *   IO_STDOUT (0x0FC0) → print low byte as an ASCII character, don't store
 *   IO_TIMER  (0x0FC2) → reset the timer to the given value, don't store
 *   Anything else      → store to ram[addr]
 */
void mem_write(Memory* m, Address addr, Word value) {
    if (addr >= MEM_SIZE) {
        fprintf(stderr, "[MEMORY] ERROR: write out-of-bounds at 0x%04X\n", addr);
        exit(1);
    }

    /* Memory-Mapped I/O — character output */
    if (addr == IO_STDOUT) {
        putchar((char)(value & 0xFF));  /* only the low byte is printed */
        fflush(stdout);
        return;                         /* don't also write to RAM */
    }

    /* Memory-Mapped I/O — timer reset */
    if (addr == IO_TIMER) {
        m->timer_ticks = value;
        return;
    }

    /* Normal RAM write */
    m->ram[addr] = value;
}

/* ── mem_tick ─────────────────────────────────────────────────────────────────
 * Advance the hardware timer by one tick.
 * Called by cpu_fetch() on every instruction fetch, so the timer tracks
 * real CPU cycles rather than wall-clock time.
 */
void mem_tick(Memory* m) {
    m->timer_ticks++;
}

/* ── mem_load_program ─────────────────────────────────────────────────────────
 * Bulk-copy an array of assembled words into RAM.
 *
 * 'start'   : first address to write (usually CODE_START = 0x0100)
 * 'program' : array of pre-encoded 16-bit instruction words
 * 'count'   : number of words to copy
 *
 * This models what a bootloader or ROM does: copy machine code into RAM
 * before handing control to the CPU (setting PC = start).
 */
void mem_load_program(Memory* m, Address start, const Word* program, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (start + i >= MEM_SIZE) {
            fprintf(stderr, "[MEMORY] ERROR: program too large to fit in RAM\n");
            exit(1);
        }
        m->ram[start + i] = program[i];
    }
}

/* ── mem_dump ─────────────────────────────────────────────────────────────────
 * Print a formatted hex table of addresses start..end (inclusive).
 *
 * Each row shows three consecutive addresses so the table fits an 80-char
 * terminal. This is the "memory dump" shown in the project demos — it lets
 * you verify that results were written to the correct locations.
 *
 * Example output:
 *   |  ADDR   |   +0    |   +1    |   +2    |
 *   |  0800   |  0000   |  0001   |  0001   |
 */
void mem_dump(const Memory* m, Address start, Address end) {
    Address a;
    printf("\n+=========================================+\n");
    printf("|  MEMORY DUMP  [%04X - %04X]            |\n", start, end);
    printf("+---------+---------+---------+---------+\n");
    printf("|  ADDR   |   +0    |   +1    |   +2    |\n");
    printf("+---------+---------+---------+---------+\n");

    for (a = start; a <= end && a < MEM_SIZE; a += 3) {
        int i;
        printf("|  %04X   |", a);
        for (i = 0; i < 3 && (a+i) <= end && (a+i) < MEM_SIZE; i++) {
            printf("  %04X   |", m->ram[a+i]);
        }
        printf("\n");
    }
    printf("+---------+---------+---------+---------+\n\n");
}

/* ── mem_dump_nonzero ─────────────────────────────────────────────────────────
 * Print a compact list of all non-zero memory locations.
 * Useful for a quick sanity check after running a program — if a location
 * that should have been written still shows zero, something went wrong.
 */
void mem_dump_nonzero(const Memory* m) {
    Address a;
    printf("\n=== Non-zero memory ===\n");
    for (a = 0; a < MEM_SIZE; a++) {
        if (m->ram[a] != 0) {
            printf("  [%04X] = %04X  (%d)\n", a, m->ram[a], (SWord)m->ram[a]);
        }
    }
    printf("=======================\n");
}
