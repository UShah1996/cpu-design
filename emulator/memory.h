/**
 * ============================================================
 * CMPE-220 Software CPU — Memory Subsystem
 * ============================================================
 * Implements:
 *   - 4096-word (8KB) flat address space
 *   - Memory-mapped I/O at 0x0FC0-0x0FFF
 *   - Load from binary image
 *   - Hex dump utility
 * ============================================================
 */

#ifndef MEMORY_H
#define MEMORY_H

#include "../isa/isa.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

class Memory {
public:
    // ── Construction ─────────────────────────────────────────
    Memory() { ram.fill(0); }

    // ── Read a word from address ──────────────────────────────
    Word read(Address addr) const {
        if (addr >= MEM_SIZE)
            throw std::out_of_range("Memory read out of bounds: " + std::to_string(addr));

        // Memory-mapped I/O — read side
        if (addr == IO_TIMER) {
            return timer_ticks;          // current timer value
        }
        if (addr == IO_STDIN) {
            int c = getchar();           // block for keyboard input
            return (c == EOF) ? 0 : static_cast<Word>(c);
        }

        return ram[addr];
    }

    // ── Write a word to address ───────────────────────────────
    void write(Address addr, Word value) {
        if (addr >= MEM_SIZE)
            throw std::out_of_range("Memory write out of bounds: " + std::to_string(addr));

        // Memory-mapped I/O — write side
        if (addr == IO_STDOUT) {
            putchar(static_cast<char>(value & 0xFF));  // print character
            fflush(stdout);
            return;
        }
        if (addr == IO_TIMER) {
            timer_ticks = value;         // reset timer
            return;
        }

        ram[addr] = value;
    }

    // ── Load a program (array of words) into memory ───────────
    void load(Address start, const Word* program, size_t count) {
        for (size_t i = 0; i < count; i++) {
            if (start + i >= MEM_SIZE)
                throw std::out_of_range("Program too large for memory");
            ram[start + i] = program[i];
        }
    }

    // ── Tick the timer (called each CPU cycle) ────────────────
    void tick() { timer_ticks++; }

    // ── Hex dump of a memory region ───────────────────────────
    void dump(Address start, Address end) const {
        printf("\n╔══════════════════════════════════════════════╗\n");
        printf("║           MEMORY DUMP  [%04X - %04X]         ║\n", start, end);
        printf("╠═════════╦══════╦══════╦══════╦══════╦══════╣\n");
        printf("║  ADDR   ║  +0  ║  +1  ║  +2  ║  +3  ║ ASCII║\n");
        printf("╠═════════╬══════╬══════╬══════╬══════╬══════╣\n");

        for (Address a = start; a <= end && a < MEM_SIZE; a += 4) {
            printf("║  %04X   ║", a);
            char ascii[5] = {0};
            for (int i = 0; i < 4 && (a+i) < MEM_SIZE && (a+i) <= end; i++) {
                Word w = ram[a+i];
                printf(" %04X║", w);
                char c = static_cast<char>(w & 0xFF);
                ascii[i] = (c >= 32 && c < 127) ? c : '.';
            }
            printf(" %-4s ║\n", ascii);
        }
        printf("╚═════════╩══════╩══════╩══════╩══════╩══════╝\n\n");
    }

    // ── Full memory dump (non-zero regions only) ──────────────
    void dump_nonzero() const {
        printf("\n=== Non-zero memory contents ===\n");
        for (Address a = 0; a < MEM_SIZE; a++) {
            if (ram[a] != 0) {
                printf("  [%04X] = %04X  (%d)\n", a, ram[a], (SWord)ram[a]);
            }
        }
        printf("================================\n");
    }

    // ── Direct access (for assembler to write) ────────────────
    Word& operator[](Address addr) { return ram[addr]; }
    const Word& operator[](Address addr) const { return ram[addr]; }

private:
    std::array<Word, MEM_SIZE> ram;
    Word timer_ticks = 0;
};

#endif // MEMORY_H
