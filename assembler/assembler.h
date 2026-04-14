/*
 * ============================================================
 * CMPE-220 Software CPU — Assembler Header (C99)
 * ============================================================
 * FIXED:
 *   1. Replaced C++ includes (vector, map, string) with C structs
 *   2. Fixed branch label encoding bug:
 *      Labels in CODE_START (0x0100+) have addresses > 63.
 *      The src field is only 6 bits (max 63).
 *      Fix: store label addresses in the constant pool (0x00-0x0F)
 *           and emit DIRECT mode pointing to the pool slot.
 *      This is handled transparently by asm_assemble().
 *
 * SYNTAX:
 *   LABEL:            define label at current address
 *   ADD  R0, R1       register mode
 *   LOAD R0, #5       immediate mode (# prefix)
 *   LOAD R0, [0x00]   direct memory  (pool slot)
 *   LOAD R0, [R1]     register indirect
 *   ; comment         ignored
 *   .data 0x1234      emit raw word
 *   .org  0x0100      set current address
 * ============================================================
 */

#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include "../isa/isa.h"
#include "memory.h"

/* ── Limits ─────────────────────────────────────────────────*/
#define MAX_LABELS        256
#define MAX_LABEL_LEN      64
#define MAX_PROGRAM      2048
#define MAX_LINE_LEN      256
#define MAX_TOKENS         16
#define MAX_SOURCE_LINES 1024

/* ── Label table ────────────────────────────────────────────*/
typedef struct {
    char    name[MAX_LABEL_LEN];
    Address addr;
} Label;

typedef struct {
    Label entries[MAX_LABELS];
    int   count;
} LabelTable;

/* ── Assembled program output ───────────────────────────────*/
typedef struct {
    Word       words[MAX_PROGRAM];
    int        count;
    Address    load_addr;
    LabelTable labels;
    int        ok;          /* 1=success 0=error */
    char       error[256];
} AsmProgram;

/* ── Public API ─────────────────────────────────────────────
 *
 * asm_assemble():
 *   Performs a two-pass assembly.
 *   BRANCH LABEL FIX: any branch/jump targeting a label whose
 *   address > 63 is automatically stored in the constant pool
 *   (ram[0x00..0x0F]) during Pass 1, and the instruction is
 *   encoded with DIRECT mode pointing to that pool slot.
 *   The caller must write the pool into memory BEFORE running:
 *     asm_load_into_memory(&prog, &mem);   // loads code
 *     asm_apply_pool(&prog, &mem);          // loads pool fixes
 *
 * asm_load_into_memory():
 *   Copies assembled words into RAM at load_addr.
 *
 * asm_apply_pool():
 *   Writes branch target addresses into constant pool slots.
 *   Must be called AFTER asm_load_into_memory().
 *
 * asm_print_listing():
 *   Pretty-prints the assembled listing with disassembly.
 */
AsmProgram asm_assemble        (const char* source, Address load_addr);
void       asm_load_into_memory(const AsmProgram* prog, Memory* mem);
void       asm_apply_pool      (const AsmProgram* prog, Memory* mem);
void       asm_print_listing   (const AsmProgram* prog);

/* ── Pool tracking (embedded in AsmProgram) ─────────────────
 * The assembler tracks which pool slots it allocated for
 * branch targets so asm_apply_pool() can write them.
 */
#define MAX_POOL_SLOTS 16

typedef struct {
    int  slot;    /* pool index (0-15)   */
    Word value;   /* address to store    */
} PoolEntry;

/* Extended program with pool info — returned by asm_assemble */
/* (pool_entries and pool_count are appended after AsmProgram) */
typedef struct {
    AsmProgram base;
    PoolEntry  pool_entries[MAX_POOL_SLOTS];
    int        pool_count;
} AsmProgramEx;

/* Use this version for full pool support */
AsmProgramEx asm_assemble_ex(const char* source, Address load_addr,
                              int pool_slots_used);

#endif /* ASSEMBLER_H */
