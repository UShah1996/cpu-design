/**
 * ============================================================
 * CMPE-220 Software CPU — ISA Definition
 * ============================================================
 *
 * INSTRUCTION FORMAT (16-bit word):
 *
 *  Bit 15..12  Bit 11..8   Bit 7..6    Bit 5..0
 *  [opcode(4)] [dest reg(4)] [mode(2)] [src/imm(6)]
 *
 *  mode 00 = register       src = register index (0-15)
 *  mode 01 = immediate      src = 6-bit literal  (0-63)
 *  mode 10 = direct memory  src = 6-bit address  (word addr)
 *  mode 11 = reg-indirect   src = register index holding address
 *
 * REGISTERS: R0-R15 (16 general-purpose 16-bit registers)
 *   R0  = accumulator (result default)
 *   R13 = stack pointer (SP)
 *   R14 = link register (LR) — holds return address
 *   R15 = program counter (PC)
 *
 * MEMORY MAP:
 *   0x0000 - 0x00FF  : ROM / boot vector
 *   0x0100 - 0x07FF  : Program code
 *   0x0800 - 0x0EFF  : Data / heap
 *   0x0F00 - 0x0FBF  : Stack (grows downward)
 *   0x0FC0 - 0x0FFF  : Memory-mapped I/O
 *     0x0FC0          : STDOUT (write a char)
 *     0x0FC1          : STDIN  (read a char)
 *     0x0FC2          : TIMER  (clock ticks)
 *
 * FLAGS (in FLAGS register):
 *   Bit 0 = ZF  Zero Flag       (result == 0)
 *   Bit 1 = CF  Carry Flag      (unsigned overflow)
 *   Bit 2 = SF  Sign Flag       (result < 0, MSB set)
 *   Bit 3 = OF  Overflow Flag   (signed overflow)
 * ============================================================
 */

#ifndef ISA_H
#define ISA_H

#include <cstdint>

// ── Word and address types ────────────────────────────────────
using Word    = uint16_t;   // 16-bit data word
using SWord   = int16_t;    // signed word
using Address = uint16_t;   // memory address

// ── Memory map constants ──────────────────────────────────────
static constexpr Address ROM_START   = 0x0000;
static constexpr Address CODE_START  = 0x0100;
static constexpr Address DATA_START  = 0x0800;
static constexpr Address STACK_BASE  = 0x0FBF; // SP starts here
static constexpr Address IO_BASE     = 0x0FC0;
static constexpr Address MEM_SIZE    = 0x1000; // 4096 words = 8KB

// ── Memory-mapped I/O addresses ──────────────────────────────
static constexpr Address IO_STDOUT   = 0x0FC0;
static constexpr Address IO_STDIN    = 0x0FC1;
static constexpr Address IO_TIMER    = 0x0FC2;

// ── Register indices ──────────────────────────────────────────
static constexpr int REG_COUNT = 16;
static constexpr int R0   =  0;   // accumulator
static constexpr int SP   = 13;   // stack pointer
static constexpr int LR   = 14;   // link register (return address)
static constexpr int PC   = 15;   // program counter

// ── Flag bit positions ────────────────────────────────────────
static constexpr Word FLAG_ZF = (1 << 0);  // zero
static constexpr Word FLAG_CF = (1 << 1);  // carry
static constexpr Word FLAG_SF = (1 << 2);  // sign
static constexpr Word FLAG_OF = (1 << 3);  // overflow

// ── Addressing modes (2 bits) ─────────────────────────────────
enum class Mode : uint8_t {
    REG      = 0b00,  // source is a register
    IMM      = 0b01,  // source is immediate value
    DIRECT   = 0b10,  // source is direct memory address
    INDIRECT = 0b11,  // source is address stored in register
};

// ── Opcodes (4 bits = 16 instructions) ────────────────────────
enum class Opcode : uint8_t {
    ADD   = 0x0,  // dest = dest + src
    SUB   = 0x1,  // dest = dest - src
    AND   = 0x2,  // dest = dest & src
    OR    = 0x3,  // dest = dest | src
    NOT   = 0x4,  // dest = ~dest (src ignored)
    SHL   = 0x5,  // dest = dest << src (logical left)
    SHR   = 0x6,  // dest = dest >> src (logical right, unsigned)
    SAR   = 0x7,  // dest = dest >> src (arithmetic right, signed)
    LOAD  = 0x8,  // dest = memory[src] or dest = immediate
    STORE = 0x9,  // memory[src_addr] = dest
    JMP   = 0xA,  // PC = src (unconditional jump)
    BEQ   = 0xB,  // if ZF: PC = src
    BNE   = 0xC,  // if !ZF: PC = src
    BLT   = 0xD,  // if SF != OF: PC = src (signed less than)
    CALL  = 0xE,  // LR = PC+1, PC = src (function call)
    RET   = 0xF,  // PC = LR  (return from function)
};

// ── Instruction encoding/decoding helpers ─────────────────────

// Encode a full instruction into a 16-bit word
inline Word encode(Opcode op, int dest, Mode mode, int src) {
    return ((static_cast<Word>(op)   & 0xF)  << 12)
         | ((static_cast<Word>(dest) & 0xF)  <<  8)
         | ((static_cast<Word>(mode) & 0x3)  <<  6)
         | ( static_cast<Word>(src)  & 0x3F);
}

// Extract fields from a 16-bit instruction word
inline Opcode  decode_opcode(Word instr) { return static_cast<Opcode>((instr >> 12) & 0xF); }
inline int     decode_dest  (Word instr) { return (instr >>  8) & 0xF; }
inline Mode    decode_mode  (Word instr) { return static_cast<Mode>((instr >> 6) & 0x3); }
inline int     decode_src   (Word instr) { return  instr        & 0x3F; }

// Convert opcode to human-readable string (for disassembly)
inline const char* opcode_name(Opcode op) {
    switch(op) {
        case Opcode::ADD:   return "ADD";
        case Opcode::SUB:   return "SUB";
        case Opcode::AND:   return "AND";
        case Opcode::OR:    return "OR";
        case Opcode::NOT:   return "NOT";
        case Opcode::SHL:   return "SHL";
        case Opcode::SHR:   return "SHR";
        case Opcode::SAR:   return "SAR";
        case Opcode::LOAD:  return "LOAD";
        case Opcode::STORE: return "STORE";
        case Opcode::JMP:   return "JMP";
        case Opcode::BEQ:   return "BEQ";
        case Opcode::BNE:   return "BNE";
        case Opcode::BLT:   return "BLT";
        case Opcode::CALL:  return "CALL";
        case Opcode::RET:   return "RET";
        default:            return "???";
    }
}

#endif // ISA_H
