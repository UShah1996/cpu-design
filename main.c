/*
 * ============================================================
 * CMPE-220 Software CPU — Main Driver v2 (C Version)
 * ============================================================
 * Run:
 *   ./software_cpu_c         all demos
 *   ./software_cpu_c 1       timer (fetch/compute/store)
 *   ./software_cpu_c 2       hello world (memory-mapped I/O)
 *   ./software_cpu_c 3       fibonacci (loops + memory)
 *   ./software_cpu_c 4       factorial (function call + stack)
 *   ./software_cpu_c 5       memory map diagram
 *   ./software_cpu_c 6       assembler listing demo
 * ============================================================
 */

#include "isa/isa.h"
#include "emulator/memory.h"
#include "emulator/cpu.h"
#include "assembler/assembler.h"

/* ── Note: this is the C port. Build with:
 *     make
 * or manually:
 *     gcc -std=c99 -Wall -Wextra -O2 -I. \
 *         -o software_cpu_c \
 *         main.c emulator/memory.c emulator/cpu.c assembler/assembler.c
 * ──────────────────────────────────────────────────────────── */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define INS(op,dst,mode,src) encode_instr(op, dst, mode, src)

static void banner(const char* t) {
    printf("\n+==================================================+\n");
    printf("|  %-48s|\n", t);
    printf("+==================================================+\n");
}

/* ============================================================
 * DEMO 1: TIMER — Fetch / Compute / Store cycles
 * ============================================================ */
void demo_timer(void) {
    Memory mem; CPU cpu; int i;
    banner("DEMO 1: Timer — Fetch / Compute / Store");
    mem_init(&mem); cpu_init(&cpu, &mem); cpu.trace_on = 1;

    mem.ram[0x00] = DATA_START;
    mem.ram[0x01] = 0; mem.ram[0x02] = 0;

    Address pc = CODE_START;
    Address loop_addr, done_addr;

    mem.ram[pc++] = INS(OP_LOAD, 0, MODE_IMM,    0);
    mem.ram[pc++] = INS(OP_LOAD, 1, MODE_IMM,    5);
    mem.ram[pc++] = INS(OP_LOAD, 5, MODE_DIRECT, 0);

    loop_addr = pc; mem.ram[0x01] = loop_addr;
    mem.ram[pc++] = INS(OP_ADD,   0, MODE_IMM,    1);   /* COMPUTE: R0++  */
    mem.ram[pc++] = INS(OP_STORE, 0, MODE_REG,    5);   /* STORE          */
    mem.ram[pc++] = INS(OP_ADD,   5, MODE_IMM,    1);
    mem.ram[pc++] = INS(OP_LOAD,  6, MODE_REG,    0);
    mem.ram[pc++] = INS(OP_SUB,   6, MODE_REG,    1);
    mem.ram[pc++] = INS(OP_BEQ,   0, MODE_DIRECT, 2);
    mem.ram[pc++] = INS(OP_LOAD,  7, MODE_DIRECT, 1);
    mem.ram[pc++] = INS(OP_JMP,   0, MODE_REG,    7);

    done_addr = pc; mem.ram[0x02] = done_addr;
    mem.ram[pc++] = INS(OP_LOAD, 7, MODE_DIRECT, 2);
    mem.ram[pc++] = INS(OP_JMP,  0, MODE_REG,    7);

    cpu_run(&cpu, 500);
    printf("\nResults (expect 1 2 3 4 5): ");
    for (i = 0; i < 5; i++) printf("%d ", (int)mem.ram[DATA_START+i]);
    printf("\n");
    cpu_dump_registers(&cpu);
    mem_dump(&mem, DATA_START, DATA_START+5);
}

/* ============================================================
 * DEMO 2: HELLO WORLD
 * ============================================================ */
void demo_hello(void) {
    Memory mem; CPU cpu; int i;
    const char* msg = "Hello, World!\n";
    banner("DEMO 2: Hello, World! (Memory-Mapped I/O)");
    mem_init(&mem); cpu_init(&cpu, &mem);

    for (i = 0; msg[i]; i++) mem.ram[DATA_START+i] = (Word)msg[i];
    mem.ram[DATA_START + (int)strlen(msg)] = 0;

    mem.ram[0x00] = DATA_START; mem.ram[0x01] = IO_STDOUT;
    mem.ram[0x02] = 0; mem.ram[0x03] = 0;

    Address pc = CODE_START; Address loop_addr, done_addr;
    mem.ram[pc++] = INS(OP_LOAD, 1, MODE_DIRECT,   0);
    mem.ram[pc++] = INS(OP_LOAD, 5, MODE_DIRECT,   1);
    loop_addr = pc; mem.ram[0x02] = loop_addr;
    mem.ram[pc++] = INS(OP_LOAD,  0, MODE_INDIRECT, 1);
    mem.ram[pc++] = INS(OP_BEQ,   0, MODE_DIRECT,   3);
    mem.ram[pc++] = INS(OP_STORE, 0, MODE_REG,       5);
    mem.ram[pc++] = INS(OP_ADD,   1, MODE_IMM,       1);
    mem.ram[pc++] = INS(OP_LOAD,  7, MODE_DIRECT,    2);
    mem.ram[pc++] = INS(OP_JMP,   0, MODE_REG,       7);
    done_addr = pc; mem.ram[0x03] = done_addr;
    mem.ram[pc++] = INS(OP_LOAD, 7, MODE_DIRECT, 3);
    mem.ram[pc++] = INS(OP_JMP,  0, MODE_REG,    7);

    printf("Output: "); fflush(stdout);
    cpu_run(&cpu, 5000);
    printf("\n  Cycles: %llu\n", (unsigned long long)cpu.cycle_count);
}

/* ============================================================
 * DEMO 3: FIBONACCI
 * ============================================================ */
void demo_fibonacci(void) {
    Memory mem; CPU cpu; int i;
    banner("DEMO 3: Fibonacci Sequence F(0)..F(9)");
    mem_init(&mem); cpu_init(&cpu, &mem);

    mem.ram[0x00] = DATA_START; mem.ram[0x01] = 0; mem.ram[0x02] = 0;

    Address pc = CODE_START; Address loop_addr, done_addr;
    mem.ram[pc++] = INS(OP_LOAD, 0, MODE_IMM,    0);
    mem.ram[pc++] = INS(OP_LOAD, 1, MODE_IMM,    1);
    mem.ram[pc++] = INS(OP_LOAD, 3, MODE_IMM,    0);
    mem.ram[pc++] = INS(OP_LOAD, 4, MODE_IMM,   10);
    mem.ram[pc++] = INS(OP_LOAD, 5, MODE_DIRECT, 0);
    loop_addr = pc; mem.ram[0x01] = loop_addr;
    mem.ram[pc++] = INS(OP_STORE, 0, MODE_REG,   5);
    mem.ram[pc++] = INS(OP_ADD,   5, MODE_IMM,   1);
    mem.ram[pc++] = INS(OP_LOAD,  2, MODE_REG,   0);
    mem.ram[pc++] = INS(OP_ADD,   2, MODE_REG,   1);
    mem.ram[pc++] = INS(OP_LOAD,  0, MODE_REG,   1);
    mem.ram[pc++] = INS(OP_LOAD,  1, MODE_REG,   2);
    mem.ram[pc++] = INS(OP_ADD,   3, MODE_IMM,   1);
    mem.ram[pc++] = INS(OP_LOAD,  6, MODE_REG,   3);
    mem.ram[pc++] = INS(OP_SUB,   6, MODE_REG,   4);
    mem.ram[pc++] = INS(OP_BEQ,   0, MODE_DIRECT, 2);
    mem.ram[pc++] = INS(OP_LOAD,  7, MODE_DIRECT, 1);
    mem.ram[pc++] = INS(OP_JMP,   0, MODE_REG,    7);
    done_addr = pc; mem.ram[0x02] = done_addr;
    mem.ram[pc++] = INS(OP_LOAD, 7, MODE_DIRECT, 2);
    mem.ram[pc++] = INS(OP_JMP,  0, MODE_REG,    7);

    cpu_run(&cpu, 1000);
    printf("\nFibonacci at 0x%04X:\n  ", DATA_START);
    for (i = 0; i < 10; i++) printf("%d ", (int)mem.ram[DATA_START+i]);
    printf("\n  Expected: 0 1 1 2 3 5 8 13 21 34\n");
    printf("  Cycles: %llu\n\n", (unsigned long long)cpu.cycle_count);
    cpu_dump_registers(&cpu);
    mem_dump(&mem, DATA_START, DATA_START+11);
}

/* ============================================================
 * DEMO 4: FACTORIAL — Function Call & Stack
 * ============================================================ */
void demo_factorial(void) {
    Memory mem; CPU cpu;
    banner("DEMO 4: Factorial(5) — Function Call & Stack");
    mem_init(&mem); cpu_init(&cpu, &mem);

    printf("\n  CALL/RET mechanism:\n");
    printf("  CALL: SP--, mem[SP]=LR, LR=PC, PC=target\n");
    printf("  RET:  PC=LR, LR=mem[SP], SP++\n\n");
    printf("  Stack frame (STACK_BASE=0x%04X, grows down):\n", STACK_BASE);
    printf("  [SP] = saved LR (return address to caller)\n\n");

    mem.ram[0x00]=DATA_START; mem.ram[0x01]=0; mem.ram[0x02]=0;
    mem.ram[0x03]=0; mem.ram[0x04]=0; mem.ram[0x05]=0;
    mem.ram[0x06]=0; mem.ram[0x07]=0;

    Address pc=CODE_START;
    Address fact_start,fact_loop,do_mult,mul_loop,mul_done,fact_done,main_done;

    /* MAIN */
    mem.ram[pc++]=INS(OP_LOAD,0,MODE_IMM,5);
    mem.ram[pc++]=INS(OP_LOAD,7,MODE_DIRECT,1);
    mem.ram[pc++]=INS(OP_CALL,0,MODE_REG,7);
    mem.ram[pc++]=INS(OP_LOAD,8,MODE_DIRECT,0);
    mem.ram[pc++]=INS(OP_STORE,0,MODE_REG,8);
    main_done=pc; mem.ram[0x02]=main_done;
    mem.ram[pc++]=INS(OP_LOAD,7,MODE_DIRECT,2);
    mem.ram[pc++]=INS(OP_JMP,0,MODE_REG,7);

    /* FACTORIAL */
    fact_start=pc; mem.ram[0x01]=fact_start;
    mem.ram[pc++]=INS(OP_LOAD,1,MODE_IMM,1);
    mem.ram[pc++]=INS(OP_LOAD,2,MODE_IMM,2);
    fact_loop=pc; mem.ram[0x03]=fact_loop;
    mem.ram[pc++]=INS(OP_LOAD,6,MODE_REG,2);
    mem.ram[pc++]=INS(OP_SUB,6,MODE_REG,0);
    mem.ram[pc++]=INS(OP_BEQ,0,MODE_DIRECT,7);
    mem.ram[pc++]=INS(OP_BLT,0,MODE_DIRECT,7);
    mem.ram[pc++]=INS(OP_LOAD,7,MODE_DIRECT,6);
    mem.ram[pc++]=INS(OP_JMP,0,MODE_REG,7);
    do_mult=pc; mem.ram[0x07]=do_mult;
    mem.ram[pc++]=INS(OP_LOAD,3,MODE_IMM,0);
    mem.ram[pc++]=INS(OP_LOAD,4,MODE_IMM,0);
    mul_loop=pc; mem.ram[0x05]=mul_loop;
    mem.ram[pc++]=INS(OP_ADD,3,MODE_REG,1);
    mem.ram[pc++]=INS(OP_ADD,4,MODE_IMM,1);
    mem.ram[pc++]=INS(OP_LOAD,9,MODE_REG,4);
    mem.ram[pc++]=INS(OP_SUB,9,MODE_REG,2);
    mem.ram[pc++]=INS(OP_BEQ,0,MODE_DIRECT,4);
    mem.ram[pc++]=INS(OP_LOAD,7,MODE_DIRECT,5);
    mem.ram[pc++]=INS(OP_JMP,0,MODE_REG,7);
    mul_done=pc; mem.ram[0x04]=mul_done;
    mem.ram[pc++]=INS(OP_LOAD,1,MODE_REG,3);
    mem.ram[pc++]=INS(OP_ADD,2,MODE_IMM,1);
    mem.ram[pc++]=INS(OP_LOAD,7,MODE_DIRECT,3);
    mem.ram[pc++]=INS(OP_JMP,0,MODE_REG,7);
    fact_done=pc; mem.ram[0x06]=fact_done;
    mem.ram[pc++]=INS(OP_LOAD,0,MODE_REG,1);
    mem.ram[pc++]=INS(OP_RET,0,MODE_REG,0);

    cpu_run(&cpu, 50000);

    printf("factorial(5) = %d\n", (SWord)mem.ram[DATA_START]);
    printf("Expected:       120\n");
    printf("Cycles:         %llu\n\n", (unsigned long long)cpu.cycle_count);
    printf("Stack region (showing LR was saved and restored):\n");
    mem_dump(&mem, STACK_BASE-3, STACK_BASE);
    cpu_dump_registers(&cpu);
}

/* ============================================================
 * DEMO 5: MEMORY LAYOUT
 * ============================================================ */
void demo_memory_layout(void) {
    banner("DEMO 5: Memory Layout & Architecture Reference");
    printf("\n  MEMORY MAP (4096 words = 8KB):\n");
    printf("  +-----------+----------------------------------+\n");
    printf("  | 0x0000    | Constant Pool                    |\n");
    printf("  | 0x0100    | CODE SEGMENT  (PC starts here)   |\n");
    printf("  | 0x0800    | DATA SEGMENT  (variables/arrays) |\n");
    printf("  | 0x0F00    | STACK         (grows downward)   |\n");
    printf("  | 0x0FC0    | IO_STDOUT     (write=print)      |\n");
    printf("  | 0x0FC1    | IO_STDIN      (read=input)       |\n");
    printf("  | 0x0FC2    | IO_TIMER      (clock ticks)      |\n");
    printf("  +-----------+----------------------------------+\n\n");
    printf("  INSTRUCTION FORMAT (16-bit word):\n");
    printf("  +--------+----------+------+------------------+\n");
    printf("  | 15..12 |  11..8   | 7..6 |      5..0        |\n");
    printf("  | opcode | dest reg | mode |   src/immediate  |\n");
    printf("  +--------+----------+------+------------------+\n");
    printf("  mode 00=REG  01=IMM  10=DIRECT  11=INDIRECT\n\n");
    printf("  FETCH-DECODE-EXECUTE:\n");
    printf("  FETCH:    instr=mem[PC]; PC++\n");
    printf("  DECODE:   op=bits[15:12] dest=bits[11:8] mode=bits[7:6] src=bits[5:0]\n");
    printf("  RESOLVE:  REG->R[src] | IMM->src | DIRECT->mem[src] | IND->mem[R[src]]\n");
    printf("  EXECUTE:  ALU compute | mem access | PC change\n");
    printf("  WRITEBACK: result->R[dest] or mem[addr]\n\n");
    printf("  STACK FRAME per CALL:\n");
    printf("  CALL: SP--, mem[SP]=old_LR, LR=PC, PC=target\n");
    printf("  RET:  PC=LR, LR=mem[SP], SP++\n");
    printf("  Stack grows DOWN from 0x0FBF\n\n");
}

/* ============================================================
 * DEMO 6: ASSEMBLER LISTING
 * ============================================================ */
void demo_assembler_listing(void) {
    banner("DEMO 6: Assembler — Source Code to Binary");

    const char* fib_src =
        ".org 0x0000\n"
        ".data 0x0800\n"
        ".org 0x0100\n"
        "    LOAD  R0, #0\n"
        "    LOAD  R1, #1\n"
        "    LOAD  R3, #0\n"
        "    LOAD  R4, #10\n"
        "    LOAD  R5, [0x00]\n"
        "FIB_LOOP:\n"
        "    STORE R0, [R5]\n"
        "    ADD   R5, #1\n"
        "    LOAD  R2, R0\n"
        "    ADD   R2, R1\n"
        "    LOAD  R0, R1\n"
        "    LOAD  R1, R2\n"
        "    ADD   R3, #1\n"
        "    LOAD  R6, R3\n"
        "    SUB   R6, R4\n"
        "    BEQ   FIB_DONE\n"
        "    JMP   FIB_LOOP\n"
        "FIB_DONE:\n"
        "    JMP   FIB_DONE\n";

    printf("\nSource:\n%s\n", fib_src);

    AsmProgram prog = asm_assemble(fib_src, CODE_START);
    if (!prog.ok) { printf("ERROR: %s\n", prog.error); return; }

    printf("Assembled %d words\n", prog.count);
    asm_print_listing(&prog);

    Memory mem; CPU cpu;
    mem_init(&mem); cpu_init(&cpu, &mem);
    asm_load_into_memory(&prog, &mem);
    mem.ram[0x00] = DATA_START;

    cpu_run(&cpu, 3000);

    int i;
    printf("Results: ");
    for (i = 0; i < 10; i++) printf("%d ", (int)mem.ram[DATA_START+i]);
    printf("\nExpected: 0 1 1 2 3 5 8 13 21 34\n\n");
}

/* ============================================================ */
int main(int argc, char* argv[]) {
    int demo = 0;
    printf("+==================================================+\n");
    printf("|  CMPE-220 Software CPU — C Implementation        |\n");
    printf("|  ISA | Assembler | Emulator | All 4 Programs     |\n");
    printf("+==================================================+\n");
    if (argc > 1) demo = atoi(argv[1]);
    switch (demo) {
        case 1: demo_timer();             break;
        case 2: demo_hello();             break;
        case 3: demo_fibonacci();         break;
        case 4: demo_factorial();         break;
        case 5: demo_memory_layout();     break;
        case 6: demo_assembler_listing(); break;
        default:
            demo_memory_layout();
            demo_timer();
            demo_hello();
            demo_fibonacci();
            demo_factorial();
            demo_assembler_listing();
    }
    return 0;
}
