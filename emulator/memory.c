/*
 * ============================================================
 * CMPE-220 Software CPU — Memory Subsystem Implementation
 * ============================================================
 */

#include "memory.h"
#include <stdio.h>    /* printf, putchar, getchar, fprintf */
#include <string.h>   /* memset */
#include <stdlib.h>   /* exit */

/* ── Initialize memory to all zeros ──────────────────────────
 * In C++ this would be a constructor.
 * In C we use an explicit init function.
 */
void mem_init(Memory* m) {
    memset(m->ram, 0, sizeof(m->ram));   /* zero every byte */
    m->timer_ticks = 0;
}

/* ── Read a word from address ─────────────────────────────────
 *
 * This is called every time the CPU fetches an instruction OR
 * executes a LOAD instruction. The memory bus read operation.
 *
 * For normal addresses: return ram[addr]
 * For I/O addresses:    interact with the device instead
 */
Word mem_read(Memory* m, Address addr) {
    if (addr >= MEM_SIZE) {
        fprintf(stderr, "[MEMORY] ERROR: read out of bounds at 0x%04X\n", addr);
        exit(1);
    }

    /* --- Memory-mapped I/O read side --- */
    if (addr == IO_TIMER) {
        return m->timer_ticks;          /* return current timer value */
    }
    if (addr == IO_STDIN) {
        int c = getchar();              /* block waiting for keyboard */
        return (c == EOF) ? 0 : (Word)c;
    }

    /* --- Normal RAM read --- */
    return m->ram[addr];
}

/* ── Write a word to address ──────────────────────────────────
 *
 * Called by STORE instructions and by the assembler loading code.
 * For I/O addresses, writing triggers device action instead of
 * modifying RAM.
 */
void mem_write(Memory* m, Address addr, Word value) {
    if (addr >= MEM_SIZE) {
        fprintf(stderr, "[MEMORY] ERROR: write out of bounds at 0x%04X\n", addr);
        exit(1);
    }

    /* --- Memory-mapped I/O write side --- */
    if (addr == IO_STDOUT) {
        putchar((char)(value & 0xFF));  /* print low byte as ASCII */
        fflush(stdout);
        return;                         /* don't write to RAM */
    }
    if (addr == IO_TIMER) {
        m->timer_ticks = value;         /* reset timer */
        return;
    }

    /* --- Normal RAM write --- */
    m->ram[addr] = value;
}

/* ── Advance the timer (called each CPU cycle) ───────────────*/
void mem_tick(Memory* m) {
    m->timer_ticks++;
}

/* ── Load a program array into memory ────────────────────────
 *
 * 'program' is an array of Words (encoded instructions).
 * 'count'   is how many words to copy.
 * 'start'   is the memory address to load them at.
 *
 * This is what a bootloader does — copies code into RAM.
 */
void mem_load_program(Memory* m, Address start, const Word* program, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (start + i >= MEM_SIZE) {
            fprintf(stderr, "[MEMORY] ERROR: program too large\n");
            exit(1);
        }
        m->ram[start + i] = program[i];
    }
}

/* ── Hex dump of a memory region ─────────────────────────────
 *
 * Prints a formatted table showing addresses and their contents.
 * Very useful for debugging — lets you "see inside" the CPU's memory.
 * This is what a "memory dump" means in your project spec.
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

/* ── Dump only non-zero memory (useful for seeing results) ───*/
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
