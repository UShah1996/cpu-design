/*
 * ============================================================
 * CMPE-220 Software CPU — ISA Definition (C99)
 * ============================================================
 * FIXED: Replaced C++ headers (cstdint, using, constexpr,
 *        enum class) with pure C99 equivalents.
 *
 * INSTRUCTION FORMAT (16-bit word):
 *  Bit: 15..12   11..8    7..6    5..0
 *       opcode   dest     mode    src/imm
 *
 *  mode 00 = REG      register index
 *  mode 01 = IMM      literal value 0-63
 *  mode 10 = DIRECT   memory address 0-63
 *  mode 11 = INDIRECT address stored in register
 *
 * MEMORY MAP:
 *   0x0000-0x000F  Constant pool (address literals)
 *   0x0100-0x07FF  Code segment  (PC starts at 0x0100)
 *   0x0800-0x0EFF  Data segment
 *   0x0F00-0x0FBF  Stack (grows downward, SP=0x0FBF)
 *   0x0FC0         IO_STDOUT
 *   0x0FC1         IO_STDIN
 *   0x0FC2         IO_TIMER
 *
 * FLAGS:
 *   Bit 0 = ZF  Zero
 *   Bit 1 = CF  Carry (unsigned overflow)
 *   Bit 2 = SF  Sign  (result negative)
 *   Bit 3 = OF  Overflow (signed overflow)
 * ============================================================
 */

#ifndef ISA_H
#define ISA_H

/* C99: use stdint.h not cstdint */
#include <stdint.h>
#include <stddef.h>

/* ── Basic types (C99 style, not C++ 'using') ────────────────*/
typedef uint16_t Word;     /* 16-bit unsigned — basic data unit */
typedef int16_t  SWord;    /* 16-bit signed   — for arithmetic  */
typedef uint16_t Address;  /* memory address                    */

/* ── Memory map (C #define, not C++ constexpr) ──────────────*/
#define ROM_START    ((Address)0x0000)
#define CODE_START   ((Address)0x0100)
#define DATA_START   ((Address)0x0800)
#define STACK_BASE   ((Address)0x0FBF)
#define IO_BASE      ((Address)0x0FC0)
#define MEM_SIZE     ((Address)0x1000)   /* 4096 words = 8KB */

/* ── Memory-mapped I/O ───────────────────────────────────────*/
#define IO_STDOUT    ((Address)0x0FC0)
#define IO_STDIN     ((Address)0x0FC1)
#define IO_TIMER     ((Address)0x0FC2)

/* ── Register indices ────────────────────────────────────────*/
#define REG_COUNT    16
#define REG_R0       0    /* accumulator                       */
#define REG_SP       13   /* stack pointer                     */
#define REG_LR       14   /* link register (return address)    */
#define REG_PC       15   /* program counter                   */

/* ── Flag masks ─────────────────────────────────────────────*/
#define FLAG_ZF  ((Word)(1 << 0))
#define FLAG_CF  ((Word)(1 << 1))
#define FLAG_SF  ((Word)(1 << 2))
#define FLAG_OF  ((Word)(1 << 3))

/* ── Addressing modes (C enum, not 'enum class') ────────────*/
typedef enum {
    MODE_REG      = 0,   /* 00 — source is a register        */
    MODE_IMM      = 1,   /* 01 — source is immediate literal */
    MODE_DIRECT   = 2,   /* 10 — source is memory address    */
    MODE_INDIRECT = 3    /* 11 — register holds the address  */
} AddrMode;

/* ── Opcodes (4 bits = 16 instructions) ──────────────────────*/
typedef enum {
    OP_ADD   = 0x0,   /* dest = dest + src                   */
    OP_SUB   = 0x1,   /* dest = dest - src                   */
    OP_AND   = 0x2,   /* dest = dest & src                   */
    OP_OR    = 0x3,   /* dest = dest | src                   */
    OP_NOT   = 0x4,   /* dest = ~dest                        */
    OP_SHL   = 0x5,   /* dest = dest << src  (logical left)  */
    OP_SHR   = 0x6,   /* dest = dest >> src  (logical right) */
    OP_SAR   = 0x7,   /* dest = dest >> src  (arithmetic)    */
    OP_LOAD  = 0x8,   /* dest = src                          */
    OP_STORE = 0x9,   /* mem[addr] = dest                    */
    OP_JMP   = 0xA,   /* PC = src                            */
    OP_BEQ   = 0xB,   /* if ZF: PC = src                     */
    OP_BNE   = 0xC,   /* if !ZF: PC = src                    */
    OP_BLT   = 0xD,   /* if SF!=OF: PC = src                 */
    OP_CALL  = 0xE,   /* LR=PC, PC=src                       */
    OP_RET   = 0xF    /* PC=LR                               */
} Opcode;

/*
 * ── Instruction encoding ─────────────────────────────────────
 *
 * Pack four fields into a 16-bit word:
 *   opcode → bits 15:12  (shift << 12)
 *   dest   → bits 11:8   (shift <<  8)
 *   mode   → bits  7:6   (shift <<  6)
 *   src    → bits  5:0   (no shift)
 *
 * Masks: 0xF=4-bit, 0x3=2-bit, 0x3F=6-bit
 */
static inline Word encode_instr(Opcode op, int dest, AddrMode mode, int src) {
    return (Word)(
        (((Word)(op)   & 0xF)  << 12) |
        (((Word)(dest) & 0xF)  <<  8) |
        (((Word)(mode) & 0x3)  <<  6) |
         ((Word)(src)  & 0x3F)
    );
}

/* Extract fields from an encoded instruction */
static inline Opcode   decode_opcode(Word w) { return (Opcode)  ((w >> 12) & 0xF);  }
static inline int      decode_dest  (Word w) { return (int)     ((w >>  8) & 0xF);  }
static inline AddrMode decode_mode  (Word w) { return (AddrMode)((w >>  6) & 0x3);  }
static inline int      decode_src   (Word w) { return (int)      (w        & 0x3F); }

/* Human-readable name (for trace / disassembly) */
static inline const char* opcode_name(Opcode op) {
    switch(op) {
        case OP_ADD:   return "ADD";
        case OP_SUB:   return "SUB";
        case OP_AND:   return "AND";
        case OP_OR:    return "OR";
        case OP_NOT:   return "NOT";
        case OP_SHL:   return "SHL";
        case OP_SHR:   return "SHR";
        case OP_SAR:   return "SAR";
        case OP_LOAD:  return "LOAD";
        case OP_STORE: return "STORE";
        case OP_JMP:   return "JMP";
        case OP_BEQ:   return "BEQ";
        case OP_BNE:   return "BNE";
        case OP_BLT:   return "BLT";
        case OP_CALL:  return "CALL";
        case OP_RET:   return "RET";
        default:       return "???";
    }
}

#endif /* ISA_H */
