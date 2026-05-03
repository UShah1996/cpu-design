/*
 * =============================================================================
 * CMPE-220 Software CPU — CPU Emulator Implementation
 * =============================================================================
 *
 * This file models the entire CPU pipeline:
 *
 *   FETCH    → Read the next instruction word from mem[PC]; increment PC
 *   DECODE   → Split the 16-bit word into opcode, dest, mode, src fields
 *   RESOLVE  → Turn the src field into an actual value (via addressing mode)
 *   EXECUTE  → Route to ALU, memory, or PC-control based on the opcode
 *   WRITEBACK → Store the result in a register or memory word
 *
 * Key components modelled here:
 *   • ALU      — arithmetic and logic operations, flag updates
 *   • Control Unit — decodes instruction and drives the ALU and bus
 *   • Register File — 16 x 16-bit registers (R0–R15 = SP, LR, PC)
 *   • Stack    — push/pop via CALL / RET using SP (R13) and LR (R14)
 *
 * =============================================================================
 */

#include "cpu.h"
#include <stdio.h>
#include <string.h>

/* ── Decoded instruction (internal representation) ───────────────────────── */
/* After DECODE, the raw 16-bit word is split into these four named fields.  */
typedef struct {
    Opcode   op;          /* which operation to perform   */
    int      dest;        /* destination register index   */
    AddrMode mode;        /* how to interpret src_field   */
    int      src_field;   /* raw 6-bit source field       */
} DecodedInstr;

/* ── Internal function prototypes ────────────────────────────────────────── */
static Word         cpu_fetch      (CPU* cpu);
static DecodedInstr cpu_decode     (Word instr);
static Word         cpu_resolve_src(CPU* cpu, AddrMode mode, int src_field);
static Word         cpu_alu        (CPU* cpu, Opcode op, Word a, Word b);
static void         cpu_execute    (CPU* cpu, const DecodedInstr* d);

static void cpu_update_flags_add    (CPU* cpu, Word a, Word b, Word result, uint32_t r32);
static void cpu_update_flags_sub    (CPU* cpu, Word a, Word b, Word result, uint32_t r32);
static void cpu_update_flags_logical(CPU* cpu, Word result);
static void cpu_print_trace         (const CPU* cpu, Word pc, Opcode op, int dest,
                                     AddrMode mode, int src_field, Word src_val);
static void set_flag                (CPU* cpu, Word flag, int condition);

/* =============================================================================
 * cpu_init — Power-on reset
 * =============================================================================
 * Sets all 16 registers to 0, then positions the two special registers:
 *   PC = CODE_START  so the first fetch reads from 0x0100
 *   SP = STACK_BASE  so the first CALL writes the return address to 0x0FBE
 */
void cpu_init(CPU* cpu, Memory* mem) {
    memset(cpu->R, 0, sizeof(cpu->R));
    cpu->FLAGS       = 0;
    cpu->mem         = mem;
    cpu->cycle_count = 0;
    cpu->instr_count = 0;
    cpu->halted      = 0;
    cpu->trace_on    = 0;
    cpu->R[REG_PC]   = CODE_START;   /* first instruction at 0x0100   */
    cpu->R[REG_SP]   = STACK_BASE;   /* stack pointer starts at 0x0FBF */
}

/* ── cpu_reset — soft reset (preserves memory attachment) ───────────────── */
void cpu_reset(CPU* cpu) {
    Memory* m     = cpu->mem;
    int     trace = cpu->trace_on;
    cpu_init(cpu, m);
    cpu->trace_on = trace;           /* keep the trace setting across resets */
}

/* =============================================================================
 * PHASE 1: FETCH
 * =============================================================================
 * Read the 16-bit instruction word at address PC, then advance PC by 1
 * so the next fetch reads the following instruction.
 *
 * This is exactly what the instruction fetch unit in real hardware does.
 * mem_tick() is called here to advance the hardware timer every cycle.
 */
static Word cpu_fetch(CPU* cpu) {
    Word instr = mem_read(cpu->mem, cpu->R[REG_PC]);
    cpu->R[REG_PC]++;      /* advance program counter to next instruction */
    cpu->cycle_count++;    /* count this as one clock cycle               */
    mem_tick(cpu->mem);    /* tick the hardware timer                     */
    return instr;
}

/* =============================================================================
 * PHASE 2: DECODE
 * =============================================================================
 * Split the 16-bit instruction word into its four fields.
 * See isa.h for the bit layout:
 *   bits 15:12 → opcode (4 bits → 16 possible instructions)
 *   bits 11:8  → dest register (4 bits → R0–R15)
 *   bits  7:6  → addressing mode (2 bits → REG/IMM/DIRECT/INDIRECT)
 *   bits  5:0  → src/immediate field (6 bits → 0–63)
 */
static DecodedInstr cpu_decode(Word instr) {
    DecodedInstr d;
    d.op        = decode_opcode(instr);   /* bits 15:12 */
    d.dest      = decode_dest(instr);     /* bits 11:8  */
    d.mode      = decode_mode(instr);     /* bits  7:6  */
    d.src_field = decode_src(instr);      /* bits  5:0  */
    return d;
}

/* =============================================================================
 * PHASE 3: RESOLVE SOURCE
 * =============================================================================
 * The 6-bit src_field means different things depending on the mode:
 *
 *   MODE_REG      → src_field is a register index; return R[src_field]
 *   MODE_IMM      → src_field IS the value; return it directly (0–63)
 *   MODE_DIRECT   → src_field is a memory address; return mem[src_field]
 *   MODE_INDIRECT → src_field is a register index; return mem[R[src_field]]
 *
 * INDIRECT is how we dereference pointers: "load R0, [R5]" means
 * "read the memory word at the address stored in R5".
 */
static Word cpu_resolve_src(CPU* cpu, AddrMode mode, int src_field) {
    switch(mode) {
        case MODE_REG:
            return cpu->R[src_field & 0xF];

        case MODE_IMM:
            return (Word)src_field;           /* immediate: the value itself */

        case MODE_DIRECT:
            return mem_read(cpu->mem, (Address)src_field);

        case MODE_INDIRECT:
            /* R[src_field] holds an address; read the word at that address */
            return mem_read(cpu->mem, (Address)cpu->R[src_field & 0xF]);
    }
    return 0;   /* unreachable, but satisfies the compiler */
}

/* =============================================================================
 * THE ALU — Arithmetic Logic Unit
 * =============================================================================
 * Models the combinational logic that performs arithmetic and bitwise ops.
 * Every operation updates the FLAGS register.
 *
 * The 32-bit intermediate 'result32' is used to detect carry/overflow —
 * when a 16-bit + 16-bit sum exceeds 0xFFFF, the result32 > 0xFFFF.
 *
 * Inputs:  a = current value of dest register (left operand)
 *          b = resolved source value          (right operand)
 * Output:  16-bit result (also stored in R[dest] by cpu_execute)
 */
static Word cpu_alu(CPU* cpu, Opcode op, Word a, Word b) {
    uint32_t result32 = 0;
    Word     result   = 0;

    switch(op) {
        case OP_ADD:
            result32 = (uint32_t)a + (uint32_t)b;
            result   = (Word)result32;
            cpu_update_flags_add(cpu, a, b, result, result32);
            break;

        case OP_SUB:
            /* Use 32-bit for the subtraction so we can detect borrow */
            result32 = (uint32_t)a - (uint32_t)b;
            result   = (Word)result32;
            cpu_update_flags_sub(cpu, a, b, result, result32);
            break;

        case OP_AND:
            result = a & b;
            cpu_update_flags_logical(cpu, result);
            break;

        case OP_OR:
            result = a | b;
            cpu_update_flags_logical(cpu, result);
            break;

        case OP_NOT:
            result = (Word)(~a);          /* bitwise NOT on dest only */
            cpu_update_flags_logical(cpu, result);
            break;

        case OP_SHL:
            /* Logical left shift. CF = last bit shifted out (bit 15 side). */
            if (b >= 16) {
                result = 0;
                cpu->FLAGS |= FLAG_CF;    /* shifted everything out */
            } else {
                result32 = (uint32_t)a << b;
                result   = (Word)result32;
                if (b > 0)
                    (result32 > 0xFFFF) ? (cpu->FLAGS |= FLAG_CF)
                                        : (cpu->FLAGS &= ~FLAG_CF);
            }
            cpu_update_flags_logical(cpu, result);
            break;

        case OP_SHR:
            /* Logical right shift. Fills with 0 from the left. */
            if (b >= 16) {
                result = 0;
            } else {
                if (b > 0) {
                    /* CF = last bit shifted out (bit 0 side) */
                    if ((a >> (b-1)) & 1) cpu->FLAGS |= FLAG_CF;
                    else                  cpu->FLAGS &= ~FLAG_CF;
                }
                result = a >> b;
            }
            cpu_update_flags_logical(cpu, result);
            break;

        case OP_SAR:
            /* Arithmetic right shift. Fills with the sign bit (bit 15). */
            if (b >= 16) {
                result = (a & 0x8000) ? 0xFFFF : 0;   /* fill with sign */
            } else {
                if (b > 0) {
                    if ((a >> (b-1)) & 1) cpu->FLAGS |= FLAG_CF;
                    else                  cpu->FLAGS &= ~FLAG_CF;
                }
                result = (Word)((SWord)a >> b);        /* signed shift   */
            }
            cpu_update_flags_logical(cpu, result);
            break;

        default:
            result = b;   /* for non-ALU ops routed here by mistake */
            break;
    }
    return result;
}

/* =============================================================================
 * PHASES 4+5: EXECUTE + WRITEBACK
 * =============================================================================
 * The Control Unit reads the decoded instruction and routes control signals
 * to the correct hardware unit (ALU, memory bus, or PC register).
 *
 * After this function returns, one instruction has been fully retired:
 *   - ALU ops    : result is in R[dest]
 *   - LOAD       : value is in R[dest]
 *   - STORE      : value is in mem[addr]
 *   - JMP/BEQ/etc: PC holds the new address
 *   - CALL       : LR saved, return address saved to stack, PC = target
 *   - RET        : PC = LR, LR = stack pop
 */
static void cpu_execute(CPU* cpu, const DecodedInstr* d) {
    Word src_val   = cpu_resolve_src(cpu, d->mode, d->src_field);
    Word dest_val  = cpu->R[d->dest];     /* current register value (ALU input A) */
    Word pc_before = cpu->R[REG_PC];
    (void)pc_before;   /* suppress unused-variable warning when trace is off */

    /* Print one trace line per instruction when trace mode is on */
    if (cpu->trace_on) {
        cpu_print_trace(cpu, (Word)(cpu->R[REG_PC] - 1), d->op, d->dest,
                        d->mode, d->src_field, src_val);
    }

    switch(d->op) {

        /* ── Arithmetic and logic — all route through the ALU ────────── */
        case OP_ADD:
        case OP_SUB:
        case OP_AND:
        case OP_OR:
        case OP_NOT:
        case OP_SHL:
        case OP_SHR:
        case OP_SAR:
            cpu->R[d->dest] = cpu_alu(cpu, d->op, dest_val, src_val);
            break;

        /* ── LOAD — copy src value into dest register ─────────────────── */
        case OP_LOAD:
            cpu->R[d->dest] = src_val;
            cpu_update_flags_logical(cpu, src_val);   /* BEQ after LOAD works */
            break;

        /* ── STORE — write dest register value to a memory address ────── */
        case OP_STORE: {
            Address target;
            /*
             * For STORE the "destination" is a memory address, not a register.
             * The address comes from the src field (or register):
             *   STORE R0, [R5]    → MODE_REG   → target = R[5]
             *   STORE R0, [R5]    → MODE_INDIRECT → target = R[src_field]
             *   STORE R0, [0x10]  → MODE_DIRECT → target = 0x10
             */
            if (d->mode == MODE_REG || d->mode == MODE_INDIRECT)
                target = (Address)cpu->R[d->src_field & 0xF];
            else
                target = (Address)d->src_field;
            mem_write(cpu->mem, target, cpu->R[d->dest]);
            break;
        }

        /* ── JMP — unconditional jump ────────────────────────────────── */
        case OP_JMP:
            cpu->R[REG_PC] = src_val;
            break;

        /* ── BEQ — branch if Zero Flag is set ───────────────────────── */
        case OP_BEQ:
            if (cpu->FLAGS & FLAG_ZF)
                cpu->R[REG_PC] = src_val;
            break;

        /* ── BNE — branch if Zero Flag is clear ─────────────────────── */
        case OP_BNE:
            if (!(cpu->FLAGS & FLAG_ZF))
                cpu->R[REG_PC] = src_val;
            break;

        /* ── BLT — branch if signed less-than (SF != OF) ────────────── */
        case OP_BLT: {
            int sf = (cpu->FLAGS & FLAG_SF) != 0;
            int of = (cpu->FLAGS & FLAG_OF) != 0;
            if (sf != of)                 /* standard signed comparison */
                cpu->R[REG_PC] = src_val;
            break;
        }

        /*
         * ── CALL — function call ─────────────────────────────────────
         * Hardware sequence (same as ARM BL / x86 CALL):
         *   1. Decrement SP (allocate one stack slot)
         *   2. Save current LR to mem[SP]  (preserve nested call chain)
         *   3. Set LR = PC                 (return address for RET)
         *   4. Set PC = target address     (jump to the function)
         *
         * After CALL, the stack looks like:
         *   mem[SP] = old LR   ← RET will restore this
         *   LR      = return address (next instruction after CALL)
         */
        case OP_CALL:
            cpu->R[REG_SP]--;                              /* grow stack down  */
            mem_write(cpu->mem, cpu->R[REG_SP], cpu->R[REG_LR]); /* save LR   */
            cpu->R[REG_LR] = cpu->R[REG_PC];              /* LR = return addr */
            cpu->R[REG_PC] = src_val;                     /* jump to function */
            break;

        /*
         * ── RET — return from function ──────────────────────────────
         * Hardware sequence (reverse of CALL):
         *   1. Set PC = LR    (jump back to caller)
         *   2. Restore LR from mem[SP]  (so nested calls can still return)
         *   3. Increment SP   (free the stack slot)
         */
        case OP_RET:
            cpu->R[REG_PC] = cpu->R[REG_LR];              /* jump back        */
            cpu->R[REG_LR] = mem_read(cpu->mem, cpu->R[REG_SP]); /* pop LR    */
            cpu->R[REG_SP]++;                              /* shrink stack     */
            break;
    }

    cpu->instr_count++;    /* one more instruction retired */
    cpu->cycle_count++;    /* one more execute cycle       */
}

/* =============================================================================
 * cpu_step — execute exactly one instruction
 * =============================================================================
 * Runs one full pipeline cycle: Fetch → Decode → Execute/Writeback.
 * The main loop, demos, and unit tests all call this.
 */
void cpu_step(CPU* cpu) {
    Word         raw;
    DecodedInstr instr;

    if (cpu->halted) return;   /* nothing to do once halted */

    raw   = cpu_fetch(cpu);    /* FETCH  */
    instr = cpu_decode(raw);   /* DECODE */
    cpu_execute(cpu, &instr);  /* EXECUTE + WRITEBACK */
}

/* =============================================================================
 * cpu_run — execute up to max_cycles instructions
 * =============================================================================
 * Loops calling cpu_step() until:
 *   a) The CPU halts (PC doesn't change — self-loop pattern "JMP here")
 *   b) max_cycles is reached (safety limit to prevent infinite loops)
 */
void cpu_run(CPU* cpu, uint64_t max_cycles) {
    while (!cpu->halted && cpu->cycle_count < max_cycles) {
        Word prev_pc = cpu->R[REG_PC];
        cpu_step(cpu);

        /* Detect halt: if PC hasn't moved, the program is stuck in a
         * self-loop ("JMP $" or "JMP FIB_DONE: JMP FIB_DONE"), which
         * is the conventional way to halt this CPU. */
        if (cpu->R[REG_PC] == prev_pc) {
            cpu->halted = 1;
            if (cpu->trace_on) {
                printf("\n[CPU] Halted at PC=%04X after %llu cycles\n",
                    cpu->R[REG_PC], (unsigned long long)cpu->cycle_count);
            }
            break;
        }
    }

    if (cpu->cycle_count >= max_cycles && !cpu->halted) {
        printf("[CPU] WARNING: max cycles (%llu) reached\n",
            (unsigned long long)max_cycles);
    }
}

/* ── cpu_dump_registers — print all register values and flags ────────────── */
void cpu_dump_registers(const CPU* cpu) {
    int i;
    const char* names[] = {
        "R0","R1","R2","R3","R4","R5","R6","R7",
        "R8","R9","R10","R11","R12","SP","LR","PC"
    };
    printf("\n+------------------------------------------+\n");
    printf("|           REGISTER DUMP                  |\n");
    printf("+------------------------------------------+\n");
    for (i = 0; i < REG_COUNT; i++) {
        printf("|  %-4s (R%02d) = %04X  (%6d)            |\n",
            names[i], i, cpu->R[i], (SWord)cpu->R[i]);
    }
    printf("+------------------------------------------+\n");
    printf("|  FLAGS = %04X  [%s%s%s%s]                   |\n",
        cpu->FLAGS,
        (cpu->FLAGS & FLAG_ZF) ? "ZF " : "   ",
        (cpu->FLAGS & FLAG_CF) ? "CF " : "   ",
        (cpu->FLAGS & FLAG_SF) ? "SF " : "   ",
        (cpu->FLAGS & FLAG_OF) ? "OF " : "   ");
    printf("+------------------------------------------+\n");
    printf("|  Cycles: %-10llu  Instrs: %-10llu|\n",
        (unsigned long long)cpu->cycle_count,
        (unsigned long long)cpu->instr_count);
    printf("+------------------------------------------+\n\n");
}

/* =============================================================================
 * Internal helpers — flag updates
 * =============================================================================
 * Each flag rule is derived from the standard x86/ARM flag semantics.
 * ZF: result is zero. SF: result MSB is 1. CF: unsigned overflow/borrow.
 * OF: signed overflow (inputs had same sign, result has different sign).
 */

static void set_flag(CPU* cpu, Word flag, int condition) {
    if (condition) cpu->FLAGS |=  flag;   /* set bit   */
    else           cpu->FLAGS &= ~flag;   /* clear bit */
}

/* Flag update after ADD: CF = unsigned overflow, OF = signed overflow */
static void cpu_update_flags_add(CPU* cpu, Word a, Word b,
                                  Word result, uint32_t r32) {
    set_flag(cpu, FLAG_ZF, result == 0);
    set_flag(cpu, FLAG_SF, (result & 0x8000) != 0);
    set_flag(cpu, FLAG_CF, r32 > 0xFFFF);                     /* unsigned overflow */
    set_flag(cpu, FLAG_OF, ((~(a ^ b)) & (a ^ result) & 0x8000) != 0); /* signed */
}

/* Flag update after SUB: CF = borrow (unsigned), OF = signed overflow */
static void cpu_update_flags_sub(CPU* cpu, Word a, Word b,
                                  Word result, uint32_t r32) {
    (void)r32;    /* unused; borrow is detected via a < b */
    set_flag(cpu, FLAG_ZF, result == 0);
    set_flag(cpu, FLAG_SF, (result & 0x8000) != 0);
    set_flag(cpu, FLAG_CF, a < b);                              /* borrow occurred */
    set_flag(cpu, FLAG_OF, ((a ^ b) & (a ^ result) & 0x8000) != 0);
}

/* Flag update after AND, OR, NOT, SHL, SHR, SAR, LOAD:
 * CF and OF are cleared (no carry/overflow from bitwise ops). */
static void cpu_update_flags_logical(CPU* cpu, Word result) {
    set_flag(cpu, FLAG_ZF, result == 0);
    set_flag(cpu, FLAG_SF, (result & 0x8000) != 0);
    cpu->FLAGS &= ~FLAG_CF;    /* bitwise ops never set carry   */
    cpu->FLAGS &= ~FLAG_OF;    /* bitwise ops never set overflow */
}

/* ── cpu_print_trace — one line per instruction (trace mode) ─────────────── */
static void cpu_print_trace(const CPU* cpu, Word pc, Opcode op,
                             int dest, AddrMode mode, int src_field, Word src_val) {
    /* Format: [PC] OPCODE Rdest, <src>  val=XXXX  flags=ZCSO */
    printf("[%04X] %-5s R%d, ", pc, opcode_name(op), dest);
    switch(mode) {
        case MODE_REG:      printf("R%d",    src_field & 0xF); break;
        case MODE_IMM:      printf("#%d",    src_field);       break;
        case MODE_DIRECT:   printf("[%04X]", src_field);       break;
        case MODE_INDIRECT: printf("[R%d]",  src_field & 0xF); break;
    }
    printf("  val=%04X  flags=%s%s%s%s\n",
        src_val,
        (cpu->FLAGS & FLAG_ZF) ? "Z" : "-",
        (cpu->FLAGS & FLAG_CF) ? "C" : "-",
        (cpu->FLAGS & FLAG_SF) ? "S" : "-",
        (cpu->FLAGS & FLAG_OF) ? "O" : "-");
}
