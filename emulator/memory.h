/*
 * =============================================================================
 * CMPE-220 Software CPU — Memory Subsystem Header
 * =============================================================================
 *
 * This file declares the Memory struct and all memory-related functions.
 * The emulator models a flat 16-bit address space of 4096 words (8 KB).
 *
 * MEMORY MAP (see isa.h for full breakdown):
 *   0x0000–0x000F   Constant pool  (branch target literals)
 *   0x0100–0x07FF   Code segment   (assembled instructions)
 *   0x0800–0x0EFF   Data segment   (variables, arrays)
 *   0x0F00–0x0FBF   Stack          (grows downward)
 *   0x0FC0          IO_STDOUT      (write = print char to terminal)
 *   0x0FC1          IO_STDIN       (read  = read char from keyboard)
 *   0x0FC2          IO_TIMER       (read  = clock tick count)
 *
 * MEMORY-MAPPED I/O:
 *   Writing to IO_STDOUT causes mem_write() to call putchar() instead
 *   of storing to RAM. Reading from IO_STDIN calls getchar(). Reading
 *   IO_TIMER returns the internal tick counter. This models real
 *   hardware where peripheral registers live in the address space.
 *
 * =============================================================================
 */

#ifndef MEMORY_H
#define MEMORY_H

#include "../isa/isa.h"
#include <stddef.h>

/*
 * Memory — the entire address space of the CPU.
 *
 * ram[]        : 4096 16-bit words (index = word address).
 * timer_ticks  : hardware timer, incremented by mem_tick() each CPU cycle.
 */
typedef struct {
    Word ram[MEM_SIZE];    /* flat 4096-word address space                  */
    Word timer_ticks;      /* free-running timer, read via IO_TIMER (0x0FC2) */
} Memory;

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Zero all RAM and reset the timer. Must be called before first use. */
void mem_init        (Memory* m);

/* Read one word from addr. I/O addresses trigger device reads instead. */
Word mem_read        (Memory* m, Address addr);

/* Write one word to addr. I/O addresses trigger device writes instead. */
void mem_write       (Memory* m, Address addr, Word value);

/* Advance the hardware timer by 1. Called by the CPU on every fetch. */
void mem_tick        (Memory* m);

/* Bulk-copy 'count' words from program[] into RAM starting at 'start'. */
void mem_load_program(Memory* m, Address start, const Word* program, size_t count);

/* Print a formatted hex dump of addresses start..end (inclusive). */
void mem_dump        (const Memory* m, Address start, Address end);

/* Print only addresses that contain non-zero values (compact view). */
void mem_dump_nonzero(const Memory* m);

#endif /* MEMORY_H */
