/**
 * CMPE-220 Software CPU — Fixed Main Driver
 * Key fix: addresses > 63 must be loaded into registers first
 * then use register-indirect mode for memory access
 */
#include "isa/isa.h"
#include "emulator/memory.h"
#include "emulator/cpu.h"
#include <cstdio>
#include <cstring>

void banner(const char* t) {
    printf("\n╔══════════════════════════════════════════════════╗\n");
    printf("║  %-48s║\n", t);
    printf("╚══════════════════════════════════════════════════╝\n");
}

// Helper: load a full 16-bit address into a register using two steps
// (load high byte shifted, OR with low byte) — simplified: direct word load
// Since immediate is only 6-bit, we use a trick: store address in data mem, load from there
// Better: encode address in two instructions (load 8 bits at a time via SHL+OR)
// Simplest: put address constants at fixed data locations
void load_address(Memory& mem, Address& pc, int reg, Address addr) {
    // We store the address word at a known location in low memory and load it
    // Use addresses 0x0000-0x000F as a constant pool
    static int pool_slot = 0;
    Address pool_addr = pool_slot++;
    mem[pool_addr] = addr;
    // LOAD reg, [pool_addr] using DIRECT mode (pool_addr fits in 6 bits since < 64)
    mem[pc++] = encode(Opcode::LOAD, reg, Mode::DIRECT, pool_addr & 0x3F);
}

// ── FIBONACCI ─────────────────────────────────────────────────
void demo_fibonacci() {
    banner("DEMO 3: Fibonacci Sequence F(0)..F(9)");

    Memory mem;
    CPU    cpu(mem);
    cpu.trace_on = false;

    // Store constants in pool (addresses 0x00-0x0F)
    mem[0x00] = DATA_START;    // pool[0] = 0x0800 (data base addr)
    mem[0x01] = IO_STDOUT;     // pool[1] = IO addr

    Address pc = CODE_START;

    // R0=a=0, R1=b=1, R3=counter=0, R4=10, R5=store ptr=DATA_START
    mem[pc++] = encode(Opcode::LOAD, 0, Mode::IMM,    0);  // R0=0 (a)
    mem[pc++] = encode(Opcode::LOAD, 1, Mode::IMM,    1);  // R1=1 (b)
    mem[pc++] = encode(Opcode::LOAD, 3, Mode::IMM,    0);  // R3=counter
    mem[pc++] = encode(Opcode::LOAD, 4, Mode::IMM,   10);  // R4=N=10
    mem[pc++] = encode(Opcode::LOAD, 5, Mode::DIRECT, 0);  // R5=DATA_START (from pool[0])

    Address loop = pc;
    // STORE a at mem[R5]
    mem[pc++] = encode(Opcode::STORE, 0, Mode::REG,   5);  // [R5]=R0
    mem[pc++] = encode(Opcode::ADD,   5, Mode::IMM,   1);  // R5++

    // temp = a + b
    mem[pc++] = encode(Opcode::LOAD,  2, Mode::REG,   0);  // R2=a
    mem[pc++] = encode(Opcode::ADD,   2, Mode::REG,   1);  // R2=a+b

    // a=b, b=temp
    mem[pc++] = encode(Opcode::LOAD,  0, Mode::REG,   1);  // R0=b
    mem[pc++] = encode(Opcode::LOAD,  1, Mode::REG,   2);  // R1=a+b

    // counter++
    mem[pc++] = encode(Opcode::ADD,   3, Mode::IMM,   1);  // R3++

    // if R3 != R4, loop  (use R6 as scratch for compare)
    mem[pc++] = encode(Opcode::LOAD,  6, Mode::REG,   3);  // R6=counter
    mem[pc++] = encode(Opcode::SUB,   6, Mode::REG,   4);  // R6=counter-N (ZF if equal)

    // BNE loop — loop addr fits in 6 bits since CODE_START=0x100 and loop offset is small
    // loop - CODE_START fits in byte, but full address 0x100+ doesn't fit in 6 bits
    // Solution: put loop address in pool, load into reg, JMP indirect
    mem[0x02] = loop;  // pool[2] = loop address
    // if ZF: fall through to halt
    // if !ZF: load loop addr and jump
    Address bne_site = pc;
    mem[pc++] = encode(Opcode::BEQ,   0, Mode::IMM,   0);  // BEQ done (patch later)
    mem[pc++] = encode(Opcode::LOAD,  7, Mode::DIRECT, 2); // R7 = loop addr from pool
    mem[pc++] = encode(Opcode::JMP,   0, Mode::REG,   7);  // JMP R7

    // halt
    Address done = pc;
    mem[bne_site] = encode(Opcode::BEQ, 0, Mode::IMM, (done - CODE_START) & 0x3F);
    // Hmm, BEQ immediate is relative offset issue. Let's use register jump for done too
    mem[0x03] = done;
    mem[pc++] = encode(Opcode::LOAD,  7, Mode::DIRECT, 3);  // R7=done addr
    mem[pc++] = encode(Opcode::JMP,   0, Mode::REG,   7);   // JMP R7 (halt loop)
    // Patch BEQ: skip 2 instructions (load+jmp) when equal = done
    mem[bne_site] = encode(Opcode::BEQ, 0, Mode::DIRECT, 3); // BEQ [pool3]=done

    cpu.reset();
    cpu.run(1000);

    printf("\nFibonacci sequence (stored at 0x%04X):\n  ", DATA_START);
    for (int i = 0; i < 10; i++) printf("%d ", (int)mem[DATA_START + i]);
    printf("\n  Expected: 0 1 1 2 3 5 8 13 21 34\n");
    printf("  Cycles:   %llu\n\n", (unsigned long long)cpu.cycle_count);
    cpu.dump_registers();
    mem.dump(DATA_START, DATA_START + 11);
}

// ── HELLO WORLD ───────────────────────────────────────────────
void demo_hello() {
    banner("DEMO 2: Hello, World!");

    Memory mem;
    CPU cpu(mem);

    // Put string at DATA_START
    const char* msg = "Hello, World!\n";
    for (int i = 0; msg[i]; i++) mem[DATA_START + i] = (Word)msg[i];
    mem[DATA_START + strlen(msg)] = 0;

    // Pool
    mem[0x00] = DATA_START;
    mem[0x01] = IO_STDOUT;
    mem[0x02] = CODE_START + 3; // loop addr
    mem[0x03] = 0; // done (patched later)

    Address pc = CODE_START;
    mem[pc++] = encode(Opcode::LOAD, 1, Mode::DIRECT, 0);  // R1=DATA_START
    mem[pc++] = encode(Opcode::LOAD, 5, Mode::DIRECT, 1);  // R5=IO_STDOUT

    Address loop = pc;
    mem[0x02] = loop;
    mem[pc++] = encode(Opcode::LOAD,  0, Mode::INDIRECT, 1); // R0=[R1]
    Address beq_site = pc;
    mem[pc++] = encode(Opcode::BEQ,   0, Mode::IMM, 0);      // BEQ done (patch)
    mem[pc++] = encode(Opcode::STORE, 0, Mode::REG, 5);      // [R5]=R0 → stdout
    mem[pc++] = encode(Opcode::ADD,   1, Mode::IMM, 1);      // R1++
    mem[pc++] = encode(Opcode::LOAD,  7, Mode::DIRECT, 2);   // R7=loop
    mem[pc++] = encode(Opcode::JMP,   0, Mode::REG, 7);      // JMP loop

    Address done = pc;
    mem[0x03] = done;
    mem[beq_site] = encode(Opcode::BEQ, 0, Mode::DIRECT, 3); // BEQ [pool3]=done
    mem[pc++] = encode(Opcode::LOAD,  7, Mode::DIRECT, 3);   // R7=done
    mem[pc++] = encode(Opcode::JMP,   0, Mode::REG, 7);      // halt

    printf("Output: ");
    fflush(stdout);
    cpu.reset();
    cpu.run(2000);
    printf("\n  Cycles: %llu\n", (unsigned long long)cpu.cycle_count);
}

// ── TIMER ─────────────────────────────────────────────────────
void demo_timer() {
    banner("DEMO 1: Timer — Fetch/Compute/Store Cycles");

    Memory mem;
    CPU cpu(mem);
    cpu.trace_on = true;

    mem[0x00] = DATA_START;

    Address pc = CODE_START;
    mem[pc++] = encode(Opcode::LOAD, 0, Mode::IMM,    0);   // FETCH: R0=0
    mem[pc++] = encode(Opcode::LOAD, 1, Mode::IMM,    5);   // FETCH: R1=5 (limit)
    mem[pc++] = encode(Opcode::LOAD, 5, Mode::DIRECT, 0);   // FETCH: R5=DATA_START

    Address loop = pc;
    mem[0x01] = loop;
    mem[pc++] = encode(Opcode::ADD,   0, Mode::IMM, 1);     // COMPUTE: R0++
    mem[pc++] = encode(Opcode::STORE, 0, Mode::REG, 5);     // STORE: [R5]=R0
    mem[pc++] = encode(Opcode::ADD,   5, Mode::IMM, 1);     // advance ptr

    mem[pc++] = encode(Opcode::LOAD,  6, Mode::REG, 0);
    mem[pc++] = encode(Opcode::SUB,   6, Mode::REG, 1);     // R6=R0-R1
    Address beq_site = pc;
    mem[pc++] = encode(Opcode::BEQ,   0, Mode::IMM, 0);     // BEQ done
    mem[pc++] = encode(Opcode::LOAD,  7, Mode::DIRECT, 1);  // R7=loop
    mem[pc++] = encode(Opcode::JMP,   0, Mode::REG, 7);     // JMP loop

    Address done = pc;
    mem[0x02] = done;
    mem[beq_site] = encode(Opcode::BEQ, 0, Mode::DIRECT, 2);
    mem[pc++] = encode(Opcode::LOAD,  7, Mode::DIRECT, 2);
    mem[pc++] = encode(Opcode::JMP,   0, Mode::REG, 7);

    cpu.reset();
    cpu.run(200);
    printf("\nCounter values stored in data memory: ");
    for (int i = 0; i < 5; i++) printf("%d ", (int)mem[DATA_START+i]);
    printf("\nExpected: 1 2 3 4 5\n");
    cpu.dump_registers();
    mem.dump(DATA_START, DATA_START+6);
}

// ── FACTORIAL (iterative, simpler) ───────────────────────────
void demo_factorial() {
    banner("DEMO 4: Factorial(5) — Iterative with Function Call");

    Memory mem;
    CPU cpu(mem);
    cpu.trace_on = false;

    /*
     * factorial(n): iterative
     *   result = 1
     *   for i = 2 to n: result *= i  (repeated add)
     *   return result
     *
     * main: R0=5, CALL factorial, STORE result
     */

    // Pool
    mem[0x00] = DATA_START;          // result store address
    Address fact_addr_pool = 0x01;   // factorial func addr (patched)
    Address main_done_pool = 0x02;
    Address loop_pool      = 0x03;

    Address pc = CODE_START;

    // MAIN
    mem[pc++] = encode(Opcode::LOAD, 0, Mode::IMM, 5);     // R0=n=5
    mem[pc++] = encode(Opcode::LOAD, 7, Mode::DIRECT, fact_addr_pool); // R7=fact_addr
    mem[pc++] = encode(Opcode::CALL, 0, Mode::REG, 7);     // CALL factorial
    // result in R0, store it
    mem[pc++] = encode(Opcode::LOAD,  8, Mode::DIRECT, 0); // R8=DATA_START
    mem[pc++] = encode(Opcode::STORE, 0, Mode::REG,    8); // [DATA_START]=R0
    Address main_done = pc;
    mem[main_done_pool] = main_done;
    mem[pc++] = encode(Opcode::LOAD, 7, Mode::DIRECT, main_done_pool);
    mem[pc++] = encode(Opcode::JMP,  0, Mode::REG,    7);  // halt

    // FACTORIAL FUNCTION
    Address fact_start = pc;
    mem[fact_addr_pool] = fact_start;

    // R0=n (argument), R1=result=1, R2=i=2
    mem[pc++] = encode(Opcode::LOAD, 1, Mode::IMM, 1);    // result=1
    mem[pc++] = encode(Opcode::LOAD, 2, Mode::IMM, 2);    // i=2

    // Loop: multiply result * i using repeated add (R3=accum, R4=counter)
    Address fact_loop = pc;
    mem[loop_pool] = fact_loop;

    // Check i > n: if R2 - R0 has ZF or overflow, done
    mem[pc++] = encode(Opcode::LOAD, 6, Mode::REG, 2);    // R6=i
    mem[pc++] = encode(Opcode::SUB,  6, Mode::REG, 0);    // R6=i-n
    // if i > n (ZF not set, but SF==OF means >=), use BEQ for == then BLT for <
    // Simpler: BEQ (i==n done after one more mult) -- just check if ZF after sub
    // We loop while i <= n, stop when i-n > 0 (positive, no SF)
    Address check_done = pc;
    mem[pc++] = encode(Opcode::BEQ, 0, Mode::IMM, 0);     // BEQ mul_done (patch)

    // Also check SF=0 and not zero means i > n
    // Actually: after SUB R6=i-n: if SF=1 → i<n (keep looping), SF=0,ZF=0 → i>n (done)
    Address blt_site = pc;
    mem[pc++] = encode(Opcode::BLT, 0, Mode::IMM, 0);     // BLT mul (i<n, keep going) patch

    // i > n: done with loop
    Address mul_done = pc;
    mem[check_done] = encode(Opcode::BEQ, 0, Mode::DIRECT, loop_pool+1); // patch to mul_done
    mem[pc++] = encode(Opcode::RET, 0, Mode::REG, 0);     // return (R0 has result? no, R1)

    // Fix: move R1 to R0 before RET
    // patch mul_done to be the LOAD R0,R1 + RET
    Address actual_done = pc;
    mem[mul_done-1] = encode(Opcode::BEQ,  0, Mode::DIRECT, 0x04); // temp
    mem[0x04] = actual_done;
    mem[pc++] = encode(Opcode::LOAD, 0, Mode::REG, 1);    // R0 = result
    mem[pc++] = encode(Opcode::RET,  0, Mode::REG, 0);    // return

    // Multiply R1 *= R2 (result = result * i) via repeated add
    // R3 = 0 (accumulator), R4 = loop counter
    Address mul_start = pc;
    mem[blt_site] = encode(Opcode::BLT, 0, Mode::DIRECT, 0x05);
    mem[0x05] = mul_start;
    mem[check_done] = encode(Opcode::BEQ, 0, Mode::DIRECT, 0x04); // i==n → done after mult

    mem[pc++] = encode(Opcode::LOAD, 3, Mode::IMM,  0);   // R3=0 (accum)
    mem[pc++] = encode(Opcode::LOAD, 4, Mode::IMM,  0);   // R4=counter

    Address add_loop = pc;
    mem[0x06] = add_loop;
    mem[pc++] = encode(Opcode::ADD,  3, Mode::REG,  1);   // R3 += R1 (result)
    mem[pc++] = encode(Opcode::ADD,  4, Mode::IMM,  1);   // R4++
    mem[pc++] = encode(Opcode::LOAD, 9, Mode::REG,  4);   // R9=counter
    mem[pc++] = encode(Opcode::SUB,  9, Mode::REG,  2);   // R9=counter-i (ZF when done)
    Address mul_beq = pc;
    mem[pc++] = encode(Opcode::BEQ,  0, Mode::IMM,  0);   // BEQ mul_done2 (patch)
    mem[pc++] = encode(Opcode::LOAD, 7, Mode::DIRECT, 6); // R7=add_loop
    mem[pc++] = encode(Opcode::JMP,  0, Mode::REG,  7);   // loop

    Address mul_done2 = pc;
    mem[mul_beq] = encode(Opcode::BEQ, 0, Mode::DIRECT, 0x07);
    mem[0x07] = mul_done2;
    mem[pc++] = encode(Opcode::LOAD, 1, Mode::REG,  3);   // R1=R3 (new result)
    mem[pc++] = encode(Opcode::ADD,  2, Mode::IMM,  1);   // i++
    // jump back to fact_loop
    mem[pc++] = encode(Opcode::LOAD, 7, Mode::DIRECT, loop_pool);
    mem[pc++] = encode(Opcode::JMP,  0, Mode::REG,  7);

    cpu.reset();
    cpu.run(10000);

    printf("\nfactorial(5) = %d\n", (SWord)mem[DATA_START]);
    printf("Expected:       120\n\n");
    cpu.dump_registers();
}

void demo_memory_layout() {
    banner("DEMO 5: Memory Map & Execution Model");
    printf(R"(
  MEMORY MAP (word-addressed, 16-bit words):
  ┌─────────────────────────────────────────────┐
  │ 0x0000-0x000F  Constant Pool (addresses)   │
  │ 0x0100-0x07FF  CODE SEGMENT (instructions) │
  │ 0x0800-0x0EFF  DATA SEGMENT (variables)    │
  │ 0x0F00-0x0FBF  STACK (grows downward ↓)   │
  │ 0x0FC0         IO_STDOUT (write=print)     │
  │ 0x0FC1         IO_STDIN  (read=input)      │
  │ 0x0FC2         IO_TIMER  (clock counter)   │
  └─────────────────────────────────────────────┘

  INSTRUCTION FORMAT (16 bits):
  ┌────────┬──────────┬────────┬──────────────┐
  │ 15..12 │  11..8   │  7..6  │    5..0      │
  │ opcode │ dest reg │  mode  │  src/imm     │
  └────────┴──────────┴────────┴──────────────┘
  mode 00=REG  01=IMM  10=DIRECT  11=INDIRECT

  REGISTERS: R0(accum) R1-R12(GPR) R13(SP) R14(LR) R15(PC)

  FETCH-DECODE-EXECUTE:
    FETCH:   instr = mem[PC]; PC++
    DECODE:  opcode=bits[15:12], dest=bits[11:8],
             mode=bits[7:6],  src=bits[5:0]
    RESOLVE: REG→R[src] | IMM→src | DIRECT→mem[src] | INDIRECT→mem[R[src]]
    EXECUTE: ALU(op,R[dest],resolved) or mem access or PC change
    WRITEBK: R[dest]=result (ALU/LOAD) | mem[addr]=R[dest] (STORE)
)");
}

int main(int argc, char* argv[]) {
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║   CMPE-220 Software CPU — Complete Demo          ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");
    int d = argc>1 ? atoi(argv[1]) : 0;
    switch(d) {
        case 1: demo_timer();         break;
        case 2: demo_hello();         break;
        case 3: demo_fibonacci();     break;
        case 4: demo_factorial();     break;
        case 5: demo_memory_layout(); break;
        default:
            demo_memory_layout();
            demo_timer();
            demo_hello();
            demo_fibonacci();
            demo_factorial();
    }
    return 0;
}
