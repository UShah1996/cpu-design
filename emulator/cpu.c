/*
 * ============================================================
 * CMPE-220 Software CPU — CPU Emulator Implementation
 * ============================================================
 */

#include "cpu.h"
#include <stdio.h>
#include <string.h>

/* ── Decoded instruction (internal to this file) ─────────────*/
typedef struct {
    Opcode   op;          /* which instruction        */
    int      dest;        /* destination register     */
    AddrMode mode;        /* addressing mode          */
    int      src_field;   /* raw 6-bit source field   */
} DecodedInstr;

/* ── Internal function forward declarations ──────────────────*/
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

/* ── Initialize CPU to power-on state ────────────────────────
 *
 * Sets all registers to 0, then positions:
 *   PC = CODE_START (first instruction is at 0x0100)
 *   SP = STACK_BASE (stack starts at top of stack region)
 */
void cpu_init(CPU* cpu, Memory* mem) {
    memset(cpu->R, 0, sizeof(cpu->R));
    cpu->FLAGS       = 0;
    cpu->mem         = mem;
    cpu->cycle_count = 0;
    cpu->instr_count = 0;
    cpu->halted      = 0;
    cpu->trace_on    = 0;
    cpu->R[REG_PC]   = CODE_START;   /* start fetching from 0x0100  */
    cpu->R[REG_SP]   = STACK_BASE;   /* stack pointer at 0x0FBF     */
}

/* ── Reset CPU (same as init but keeps memory attached) ──────*/
void cpu_reset(CPU* cpu) {
    Memory* m = cpu->mem;
    int trace = cpu->trace_on;
    cpu_init(cpu, m);
    cpu->trace_on = trace;
}

/* ============================================================
 * PHASE 1: FETCH
 * ============================================================
 * Read the next instruction from memory at address PC.
 * Increment PC so it points to the instruction after this one.
 */
static Word cpu_fetch(CPU* cpu) {
    Word instr = mem_read(cpu->mem, cpu->R[REG_PC]);
    cpu->R[REG_PC]++;        /* advance program counter */
    cpu->cycle_count++;      /* one fetch = one cycle   */
    mem_tick(cpu->mem);      /* advance hardware timer  */
    return instr;
}

/* ============================================================
 * PHASE 2: DECODE
 * ============================================================
 * Extract the four fields from the 16-bit instruction word.
 */
static DecodedInstr cpu_decode(Word instr) {
    DecodedInstr d;
    d.op        = decode_opcode(instr);
    d.dest      = decode_dest(instr);
    d.mode      = decode_mode(instr);
    d.src_field = decode_src(instr);
    return d;
}

/* ============================================================
 * PHASE 3: RESOLVE SOURCE
 * ============================================================
 * Get the actual VALUE that the instruction will operate on,
 * based on the addressing mode.
 */
static Word cpu_resolve_src(CPU* cpu, AddrMode mode, int src_field) {
    switch(mode) {
        case MODE_REG:
            return cpu->R[src_field & 0xF];
        case MODE_IMM:
            return (Word)src_field;
        case MODE_DIRECT:
            return mem_read(cpu->mem, (Address)src_field);
        case MODE_INDIRECT:
            return mem_read(cpu->mem, (Address)cpu->R[src_field & 0xF]);
    }
    return 0;
}

/* ============================================================
 * THE ALU — Arithmetic Logic Unit
 * ============================================================
 * Models the ALU: receives two operands, performs the operation,
 * updates FLAGS, and returns the result.
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
            result = (Word)(~a);
            cpu_update_flags_logical(cpu, result);
            break;

        case OP_SHL:
            if (b >= 16) {
                result = 0;
                cpu->FLAGS |= FLAG_CF;
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
            if (b >= 16) { result = 0; }
            else {
                if (b > 0) {
                    if ((a >> (b-1)) & 1) cpu->FLAGS |= FLAG_CF;
                    else                  cpu->FLAGS &= ~FLAG_CF;
                }
                result = a >> b;
            }
            cpu_update_flags_logical(cpu, result);
            break;

        case OP_SAR:
            if (b >= 16) {
                result = (a & 0x8000) ? 0xFFFF : 0;
            } else {
                if (b > 0) {
                    if ((a >> (b-1)) & 1) cpu->FLAGS |= FLAG_CF;
                    else                  cpu->FLAGS &= ~FLAG_CF;
                }
                result = (Word)((SWord)a >> b);
            }
            cpu_update_flags_logical(cpu, result);
            break;

        default:
            result = b;
            break;
    }
    return result;
}

/* ============================================================
 * PHASE 4+5: EXECUTE + WRITEBACK
 * ============================================================
 * The Control Unit reads the decoded instruction and routes
 * signals to the appropriate hardware unit.
 */
static void cpu_execute(CPU* cpu, const DecodedInstr* d) {
    Word src_val   = cpu_resolve_src(cpu, d->mode, d->src_field);
    Word dest_val  = cpu->R[d->dest];
    Word pc_before = cpu->R[REG_PC];
    (void)pc_before;  /* suppress unused-variable warning when trace_on=0 */

    if (cpu->trace_on) {
        cpu_print_trace(cpu, (Word)(cpu->R[REG_PC] - 1), d->op, d->dest,
                        d->mode, d->src_field, src_val);
    }

    switch(d->op) {
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

        case OP_LOAD:
            cpu->R[d->dest] = src_val;
            cpu_update_flags_logical(cpu, src_val);
            break;

        case OP_STORE: {
            Address target;
            if (d->mode == MODE_REG || d->mode == MODE_INDIRECT)
                target = (Address)cpu->R[d->src_field & 0xF];
            else
                target = (Address)d->src_field;
            mem_write(cpu->mem, target, cpu->R[d->dest]);
            break;
        }

        case OP_JMP:
            cpu->R[REG_PC] = src_val;
            break;

        case OP_BEQ:
            if (cpu->FLAGS & FLAG_ZF) cpu->R[REG_PC] = src_val;
            break;

        case OP_BNE:
            if (!(cpu->FLAGS & FLAG_ZF)) cpu->R[REG_PC] = src_val;
            break;

        case OP_BLT: {
            int sf = (cpu->FLAGS & FLAG_SF) != 0;
            int of = (cpu->FLAGS & FLAG_OF) != 0;
            if (sf != of) cpu->R[REG_PC] = src_val;
            break;
        }

        case OP_CALL:
            cpu->R[REG_SP]--;
            mem_write(cpu->mem, cpu->R[REG_SP], cpu->R[REG_LR]);
            cpu->R[REG_LR] = cpu->R[REG_PC];
            cpu->R[REG_PC] = src_val;
            break;

        case OP_RET:
            cpu->R[REG_PC] = cpu->R[REG_LR];
            cpu->R[REG_LR] = mem_read(cpu->mem, cpu->R[REG_SP]);
            cpu->R[REG_SP]++;
            break;
    }

    cpu->instr_count++;
    cpu->cycle_count++;
}

/* ============================================================
 * SINGLE STEP — Execute exactly one instruction
 * ============================================================
 */
void cpu_step(CPU* cpu) {
    Word         raw;
    DecodedInstr instr;

    if (cpu->halted) return;

    raw   = cpu_fetch(cpu);
    instr = cpu_decode(raw);
    cpu_execute(cpu, &instr);
}

/* ============================================================
 * RUN — Execute until halt or max cycles
 * ============================================================
 */
void cpu_run(CPU* cpu, uint64_t max_cycles) {
    while (!cpu->halted && cpu->cycle_count < max_cycles) {
        Word prev_pc = cpu->R[REG_PC];
        cpu_step(cpu);
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

/* ── Dump all register values ─────────────────────────────── */
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

/* ── Internal: flag helpers ──────────────────────────────────*/

static void set_flag(CPU* cpu, Word flag, int condition) {
    if (condition) cpu->FLAGS |=  flag;
    else           cpu->FLAGS &= ~flag;
}

static void cpu_update_flags_add(CPU* cpu, Word a, Word b, Word result, uint32_t r32) {
    set_flag(cpu, FLAG_ZF, result == 0);
    set_flag(cpu, FLAG_SF, (result & 0x8000) != 0);
    set_flag(cpu, FLAG_CF, r32 > 0xFFFF);
    set_flag(cpu, FLAG_OF, ((~(a ^ b)) & (a ^ result) & 0x8000) != 0);
}

static void cpu_update_flags_sub(CPU* cpu, Word a, Word b, Word result, uint32_t r32) {
    (void)r32;
    set_flag(cpu, FLAG_ZF, result == 0);
    set_flag(cpu, FLAG_SF, (result & 0x8000) != 0);
    set_flag(cpu, FLAG_CF, a < b);
    set_flag(cpu, FLAG_OF, ((a ^ b) & (a ^ result) & 0x8000) != 0);
}

static void cpu_update_flags_logical(CPU* cpu, Word result) {
    set_flag(cpu, FLAG_ZF, result == 0);
    set_flag(cpu, FLAG_SF, (result & 0x8000) != 0);
    cpu->FLAGS &= ~FLAG_CF;
    cpu->FLAGS &= ~FLAG_OF;
}

static void cpu_print_trace(const CPU* cpu, Word pc, Opcode op,
                             int dest, AddrMode mode, int src_field, Word src_val) {
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
