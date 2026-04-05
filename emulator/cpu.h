/**
 * ============================================================
 * CMPE-220 Software CPU — CPU Emulator
 * ============================================================
 * Implements the full CPU:
 *   - Registers (R0-R15, FLAGS)
 *   - ALU (all operations from ISA)
 *   - Control Unit (fetch-decode-execute loop)
 *   - Bus interface to memory
 *   - Single-step and full-run modes
 *   - Register and state dump
 * ============================================================
 */

#ifndef CPU_H
#define CPU_H

#include "../isa/isa.h"
#include "memory.h"
#include <array>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>

class CPU {
public:
    // ── CPU State ─────────────────────────────────────────────
    std::array<Word, REG_COUNT> R = {};  // R0-R15 (PC=R15, SP=R13, LR=R14)
    Word FLAGS = 0;                       // ZF CF SF OF
    Memory& mem;

    // Statistics
    uint64_t cycle_count  = 0;
    uint64_t instr_count  = 0;
    bool     halted       = false;
    bool     trace_on     = false;  // print each instruction if true

    // ── Constructor ───────────────────────────────────────────
    explicit CPU(Memory& memory) : mem(memory) {
        reset();
    }

    // ── Reset CPU to initial state ────────────────────────────
    void reset() {
        R.fill(0);
        FLAGS       = 0;
        cycle_count = 0;
        instr_count = 0;
        halted      = false;
        R[PC] = CODE_START;         // PC starts at 0x0100
        R[SP] = STACK_BASE;         // SP starts at top of stack
    }

    // ─────────────────────────────────────────────────────────
    // FETCH — retrieve next instruction from memory
    // Returns the 16-bit instruction word
    // ─────────────────────────────────────────────────────────
    Word fetch() {
        Word instr = mem.read(R[PC]);   // read from address in PC
        R[PC]++;                         // advance PC (like IP increment)
        cycle_count++;                   // each fetch = 1 cycle
        mem.tick();                      // advance timer
        return instr;
    }

    // ─────────────────────────────────────────────────────────
    // DECODE — extract fields from instruction word
    // ─────────────────────────────────────────────────────────
    struct DecodedInstr {
        Opcode op;
        int    dest;
        Mode   mode;
        int    src_field;  // raw 6-bit field from instruction
    };

    DecodedInstr decode(Word instr) {
        return {
            decode_opcode(instr),
            decode_dest(instr),
            decode_mode(instr),
            decode_src(instr)
        };
    }

    // ─────────────────────────────────────────────────────────
    // RESOLVE SOURCE — get the actual value based on mode
    // This is where addressing modes are implemented
    // ─────────────────────────────────────────────────────────
    Word resolve_src(Mode mode, int src_field) {
        switch(mode) {
            case Mode::REG:
                // Register mode: src_field is register index
                return R[src_field & 0xF];

            case Mode::IMM:
                // Immediate mode: src_field IS the value
                return static_cast<Word>(src_field);

            case Mode::DIRECT:
                // Direct memory: src_field is the address
                return mem.read(static_cast<Address>(src_field));

            case Mode::INDIRECT:
                // Register indirect: register holds the address
                return mem.read(R[src_field & 0xF]);
        }
        return 0;
    }

    // ─────────────────────────────────────────────────────────
    // ALU — all arithmetic and logic operations
    // Updates FLAGS register based on result
    // ─────────────────────────────────────────────────────────
    Word alu(Opcode op, Word a, Word b) {
        uint32_t result32 = 0;  // 32-bit for carry detection
        Word result = 0;

        switch(op) {
            case Opcode::ADD:
                result32 = (uint32_t)a + (uint32_t)b;
                result   = static_cast<Word>(result32);
                update_flags_add(a, b, result, result32);
                break;

            case Opcode::SUB:
                // Subtraction: a - b = a + (~b + 1)
                result32 = (uint32_t)a - (uint32_t)b;
                result   = static_cast<Word>(result32);
                update_flags_sub(a, b, result, result32);
                break;

            case Opcode::AND:
                result = a & b;
                update_flags_logical(result);
                break;

            case Opcode::OR:
                result = a | b;
                update_flags_logical(result);
                break;

            case Opcode::NOT:
                result = ~a;
                update_flags_logical(result);
                break;

            case Opcode::SHL:
                // Logical shift left — fills 0s on right
                if (b >= 16) { result = 0; FLAGS |= FLAG_CF; }
                else {
                    uint32_t r32 = (uint32_t)a << b;
                    result = static_cast<Word>(r32);
                    // CF = last bit shifted out
                    if (b > 0) set_flag(FLAG_CF, (a >> (16-b)) & 1);
                    else clear_flag(FLAG_CF);
                }
                update_flags_logical(result);
                break;

            case Opcode::SHR:
                // Logical shift right — fills 0s on left (unsigned)
                if (b >= 16) { result = 0; }
                else {
                    if (b > 0) set_flag(FLAG_CF, (a >> (b-1)) & 1);
                    result = a >> b;
                }
                update_flags_logical(result);
                break;

            case Opcode::SAR:
                // Arithmetic shift right — fills sign bit (signed)
                // Correct for negative numbers: -8 >> 1 = -4
                if (b >= 16) { result = (a & 0x8000) ? 0xFFFF : 0; }
                else {
                    if (b > 0) set_flag(FLAG_CF, (a >> (b-1)) & 1);
                    result = static_cast<Word>(
                        static_cast<SWord>(a) >> b
                    );
                }
                update_flags_logical(result);
                break;

            default:
                // Non-ALU instructions (LOAD, STORE, JMP, etc.)
                result = b;  // pass-through
                break;
        }
        return result;
    }

    // ─────────────────────────────────────────────────────────
    // EXECUTE — carry out the decoded instruction
    // This is where the Control Unit routes signals
    // ─────────────────────────────────────────────────────────
    void execute(const DecodedInstr& instr) {
        Word src_val = resolve_src(instr.mode, instr.src_field);
        Word dest_val = R[instr.dest];

        if (trace_on) print_trace(instr, src_val);

        switch(instr.op) {

            // ── Arithmetic / Logic (ALU operations) ───────────
            case Opcode::ADD:
            case Opcode::SUB:
            case Opcode::AND:
            case Opcode::OR:
            case Opcode::NOT:
            case Opcode::SHL:
            case Opcode::SHR:
            case Opcode::SAR:
                R[instr.dest] = alu(instr.op, dest_val, src_val);
                break;

            // ── LOAD — move data into register ────────────────
            // Handles: LOAD R0, #5 / LOAD R0, R1 / LOAD R0, [addr]
            case Opcode::LOAD:
                R[instr.dest] = src_val;
                update_flags_logical(src_val);
                break;

            // ── STORE — write register to memory ──────────────
            // STORE R0, addr  →  mem[addr] = R0
            case Opcode::STORE: {
                Address target;
                if (instr.mode == Mode::REG)
                    target = R[instr.src_field & 0xF];  // register holds address
                else
                    target = static_cast<Address>(instr.src_field);
                mem.write(target, R[instr.dest]);
                break;
            }

            // ── JMP — unconditional jump ───────────────────────
            case Opcode::JMP:
                R[PC] = src_val;
                break;

            // ── BEQ — branch if equal (ZF=1) ──────────────────
            case Opcode::BEQ:
                if (FLAGS & FLAG_ZF) R[PC] = src_val;
                break;

            // ── BNE — branch if not equal (ZF=0) ──────────────
            case Opcode::BNE:
                if (!(FLAGS & FLAG_ZF)) R[PC] = src_val;
                break;

            // ── BLT — branch if less than (signed) ────────────
            // SF != OF means signed underflow (negative result)
            case Opcode::BLT: {
                bool sf = (FLAGS & FLAG_SF) != 0;
                bool of = (FLAGS & FLAG_OF) != 0;
                if (sf != of) R[PC] = src_val;
                break;
            }

            // ── CALL — function call ───────────────────────────
            // Saves return address in LR, then jumps
            // Also pushes LR to stack to support nested calls
            case Opcode::CALL:
                // Push LR onto stack (support nested/recursive calls)
                R[SP]--;
                mem.write(R[SP], R[LR]);
                // Save return address (current PC, already incremented)
                R[LR] = R[PC];
                // Jump to function
                R[PC] = src_val;
                break;

            // ── RET — return from function ─────────────────────
            // Restores PC from LR, pops stack
            case Opcode::RET:
                R[PC] = R[LR];              // jump back to caller
                R[LR] = mem.read(R[SP]);    // restore saved LR
                R[SP]++;                     // pop stack
                break;
        }

        instr_count++;
        cycle_count++;  // execute = 1 more cycle (fetch was already 1)
    }

    // ─────────────────────────────────────────────────────────
    // STEP — execute exactly one instruction (fetch+decode+execute)
    // ─────────────────────────────────────────────────────────
    void step() {
        if (halted) return;
        Word       raw    = fetch();
        DecodedInstr instr = decode(raw);
        execute(instr);
    }

    // ─────────────────────────────────────────────────────────
    // RUN — execute until HALT (LOAD R15, R15 = self-loop)
    // Max cycles to prevent infinite loops
    // ─────────────────────────────────────────────────────────
    void run(uint64_t max_cycles = 1000000) {
        while (!halted && cycle_count < max_cycles) {
            Word prev_pc = R[PC];
            step();
            // Detect halt: instruction that jumps to itself
            if (R[PC] == prev_pc) {
                halted = true;
                if (trace_on) printf("\n[CPU] Halted at PC=%04X after %llu cycles\n",
                    R[PC], (unsigned long long)cycle_count);
                break;
            }
        }
        if (cycle_count >= max_cycles) {
            printf("\n[CPU] WARNING: max cycles reached (%llu)\n",
                (unsigned long long)max_cycles);
        }
    }

    // ─────────────────────────────────────────────────────────
    // DUMP — print all register values
    // ─────────────────────────────────────────────────────────
    void dump_registers() const {
        printf("\n╔══════════════════════════════════════════╗\n");
        printf("║           REGISTER DUMP                  ║\n");
        printf("╠══════════════════════════════════════════╣\n");
        for (int i = 0; i < REG_COUNT; i++) {
            const char* name = reg_name(i);
            printf("║  %-4s (R%02d) = %04X  (%6d)            ║\n",
                name, i, R[i], (SWord)R[i]);
        }
        printf("╠══════════════════════════════════════════╣\n");
        printf("║  FLAGS    = %04X  [%s%s%s%s]               ║\n",
            FLAGS,
            (FLAGS & FLAG_ZF) ? "ZF " : "   ",
            (FLAGS & FLAG_CF) ? "CF " : "   ",
            (FLAGS & FLAG_SF) ? "SF " : "   ",
            (FLAGS & FLAG_OF) ? "OF " : "   ");
        printf("╠══════════════════════════════════════════╣\n");
        printf("║  Cycles: %-10llu  Instrs: %-10llu ║\n",
            (unsigned long long)cycle_count,
            (unsigned long long)instr_count);
        printf("╚══════════════════════════════════════════╝\n\n");
    }

private:
    // ── Flag helpers ──────────────────────────────────────────
    void set_flag(Word flag, bool cond) {
        if (cond) FLAGS |=  flag;
        else      FLAGS &= ~flag;
    }
    void clear_flag(Word flag) { FLAGS &= ~flag; }

    void update_flags_add(Word a, Word b, Word result, uint32_t r32) {
        set_flag(FLAG_ZF, result == 0);
        set_flag(FLAG_SF, (result & 0x8000) != 0);
        set_flag(FLAG_CF, r32 > 0xFFFF);
        // Signed overflow: both same sign, result different sign
        set_flag(FLAG_OF, ((~(a ^ b)) & (a ^ result) & 0x8000) != 0);
    }

    void update_flags_sub(Word a, Word b, Word result, uint32_t r32) {
        set_flag(FLAG_ZF, result == 0);
        set_flag(FLAG_SF, (result & 0x8000) != 0);
        set_flag(FLAG_CF, a < b);   // borrow
        // Signed overflow for subtraction
        set_flag(FLAG_OF, ((a ^ b) & (a ^ result) & 0x8000) != 0);
    }

    void update_flags_logical(Word result) {
        set_flag(FLAG_ZF, result == 0);
        set_flag(FLAG_SF, (result & 0x8000) != 0);
        clear_flag(FLAG_CF);
        clear_flag(FLAG_OF);
    }

    // ── Register names for display ────────────────────────────
    const char* reg_name(int idx) const {
        switch(idx) {
            case 13: return "SP";
            case 14: return "LR";
            case 15: return "PC";
            default: {
                static char buf[8];
                snprintf(buf, sizeof(buf), "R%d", idx);
                return buf;
            }
        }
    }

    // ── Trace output (one line per instruction) ───────────────
    void print_trace(const DecodedInstr& instr, Word src_val) const {
        printf("[%04X] %-5s R%d, ", R[PC]-1, opcode_name(instr.op), instr.dest);
        switch(instr.mode) {
            case Mode::REG:      printf("R%d",        instr.src_field);       break;
            case Mode::IMM:      printf("#%d",        instr.src_field);       break;
            case Mode::DIRECT:   printf("[%04X]",     instr.src_field);       break;
            case Mode::INDIRECT: printf("[R%d]",      instr.src_field & 0xF); break;
        }
        printf("  → val=%04X  FLAGS=%s%s%s%s\n",
            src_val,
            (FLAGS & FLAG_ZF) ? "Z" : "-",
            (FLAGS & FLAG_CF) ? "C" : "-",
            (FLAGS & FLAG_SF) ? "S" : "-",
            (FLAGS & FLAG_OF) ? "O" : "-");
    }
};

#endif // CPU_H
