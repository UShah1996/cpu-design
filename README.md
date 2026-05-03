# CMPE-220 Software CPU

**A complete 16-bit software CPU emulator written in C99.**

Designed and implemented for CMPE-220. Includes a custom ISA, a two-pass assembler,
a full CPU emulator (ALU + Control Unit + Memory + Memory-Mapped I/O), and four demo
programs: Timer, Hello World, Fibonacci, and Factorial (with function call + stack).

> **GitHub:** https://github.com/UShah1996/cpu-design  
> **Branch:** `c-port` (C99 implementation)

---

## Quick Start

```bash
# 1. Clone the repository
git clone https://github.com/UShah1996/cpu-design.git
cd cpu-design

# 2. Build
make

# 3. Run all demos
make run

# 4. Run one specific demo
make run-fib          # Fibonacci sequence
make run-factorial    # Factorial with CALL/RET stack
make run-timer        # Fetch/Compute/Store cycle trace
make run-hello        # Hello World via memory-mapped I/O
make run-map          # Memory layout + ISA reference
make run-asm          # Assembler listing

# Or directly:
./software_cpu_c      # all demos
./software_cpu_c 1    # timer
./software_cpu_c 2    # hello world
./software_cpu_c 3    # fibonacci
./software_cpu_c 4    # factorial
./software_cpu_c 5    # memory layout
./software_cpu_c 6    # assembler listing
```

**Requirements:** `gcc` (any version supporting `-std=c99`) and `make`.  
No external libraries. Tested on macOS and Linux.

---

## Architecture Overview

```
┌───────────────────────────────────────────────────────────┐
│                    SOFTWARE CPU                           │
│                                                           │
│  ┌──────────┐   ┌──────────┐   ┌───────────────────────┐ │
│  │  Control │   │   ALU    │   │       Registers       │ │
│  │   Unit   │──▶│ ADD SUB  │   │  R0-R12 (general)     │ │
│  │ Fetch    │   │ AND OR   │   │  SP=R13 (stack ptr)   │ │
│  │ Decode   │   │ SHL SHR  │   │  LR=R14 (link reg)    │ │
│  │ Execute  │   │ NOT SAR  │   │  PC=R15 (prog cntr)   │ │
│  └──────────┘   └──────────┘   └───────────────────────┘ │
│        │                                │                 │
│        └──────────────┬─────────────────┘                 │
│                       │  Memory Bus                       │
│              ┌────────▼────────────────┐                  │
│              │         MEMORY          │                  │
│              │  0x0000  Constant Pool  │                  │
│              │  0x0100  Code Segment   │                  │
│              │  0x0800  Data Segment   │                  │
│              │  0x0F00  Stack          │                  │
│              │  0x0FC0  IO_STDOUT      │                  │
│              │  0x0FC1  IO_STDIN       │                  │
│              │  0x0FC2  IO_TIMER       │                  │
│              └─────────────────────────┘                  │
└───────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
cpu-design/
├── Makefile                    Build system (make, make run, make help)
├── main.c                      All 6 demos (entry point)
├── isa/
│   └── isa.h                   ISA: opcodes, instruction encoding, memory map
├── emulator/
│   ├── cpu.h                   CPU struct and public API
│   ├── cpu.c                   Fetch/Decode/Execute, ALU, Control Unit
│   ├── memory.h                Memory struct and public API
│   └── memory.c                RAM, memory-mapped I/O, hex dump
├── assembler/
│   ├── assembler.h             Assembler types and public API
│   └── assembler.c             Two-pass assembler with constant pool
└── programs/
    ├── fibonacci.asm           Fibonacci sequence (loops + memory)
    ├── factorial.asm           Factorial (CALL/RET, stack frames)
    ├── timer.asm               Timer (Fetch/Compute/Store example)
    └── hello.asm               Hello World (memory-mapped I/O)
```

---

## ISA Reference

### Instruction Format (16 bits)

```
[ 15 .. 12 | 11 .. 8 | 7 .. 6 | 5 .. 0 ]
   opcode     dest      mode    src/imm
```

Each instruction is one 16-bit word. The 4-bit opcode gives 16 instructions.
The 4-bit dest field selects a destination register (R0–R15).
The 2-bit mode field selects the addressing mode.
The 6-bit src/imm field holds a register index, immediate value (0–63), or memory address.

### Opcodes

| Mnemonic | Hex  | Operation                          |
|----------|------|------------------------------------|
| ADD      | 0x0  | dest = dest + src                  |
| SUB      | 0x1  | dest = dest − src                  |
| AND      | 0x2  | dest = dest & src                  |
| OR       | 0x3  | dest = dest \| src                 |
| NOT      | 0x4  | dest = ~dest                       |
| SHL      | 0x5  | dest = dest << src (logical left)  |
| SHR      | 0x6  | dest = dest >> src (logical right) |
| SAR      | 0x7  | dest = dest >> src (arithmetic)    |
| LOAD     | 0x8  | dest = src                         |
| STORE    | 0x9  | mem[addr] = dest                   |
| JMP      | 0xA  | PC = src                           |
| BEQ      | 0xB  | if ZF: PC = src                    |
| BNE      | 0xC  | if !ZF: PC = src                   |
| BLT      | 0xD  | if SF≠OF: PC = src                 |
| CALL     | 0xE  | SP--, mem[SP]=LR, LR=PC, PC=src    |
| RET      | 0xF  | PC=LR, LR=mem[SP], SP++            |

### Addressing Modes

| Mode     | Bits | Assembly Syntax | Meaning                     |
|----------|------|-----------------|-----------------------------|
| REG      | 00   | `R3`            | value of register R3        |
| IMM      | 01   | `#5`            | literal constant 5 (0–63)   |
| DIRECT   | 10   | `[0x04]`        | value at memory address 4   |
| INDIRECT | 11   | `[R3]`          | value at address stored in R3|

### Flags

| Flag | Bit | Set when                            |
|------|-----|-------------------------------------|
| ZF   | 0   | result == 0                         |
| CF   | 1   | unsigned overflow / borrow          |
| SF   | 2   | result is negative (bit 15 = 1)     |
| OF   | 3   | signed overflow                     |

### Memory Map

```
Address Range    Region          Description
─────────────────────────────────────────────────────
0x0000–0x000F    Constant Pool   Address literals for branches > 63
0x0100–0x07FF    Code Segment    PC starts at 0x0100
0x0800–0x0EFF    Data Segment    Program variables and arrays
0x0F00–0x0FBF    Stack           Grows downward; SP starts at 0x0FBF
0x0FC0           IO_STDOUT       Write a word → prints low byte as ASCII
0x0FC1           IO_STDIN        Read → blocks waiting for keyboard input
0x0FC2           IO_TIMER        Read → current clock tick count
```

---

## Fetch-Decode-Execute Cycle

Every instruction goes through five steps:

```
FETCH:    instr = mem[PC];  PC++
DECODE:   opcode = bits[15:12]  dest = bits[11:8]
          mode   = bits[7:6]    src  = bits[5:0]
RESOLVE:  REG  → value = R[src]
          IMM  → value = src
          DIR  → value = mem[src]
          IND  → value = mem[R[src]]
EXECUTE:  ALU computes | memory accessed | PC changed
WRITEBACK: result → R[dest] or mem[addr]
```

---

## Function Calls and Stack

The CALL and RET instructions implement a hardware call stack:

```
CALL target:          RET:
  SP = SP - 1           PC = LR
  mem[SP] = LR          LR = mem[SP]
  LR = PC               SP = SP + 1
  PC = target
```

Stack grows **downward** from 0x0FBF. Each nested call uses one stack slot to save
the return address. The Factorial demo (Demo 4 / `make run-factorial`) shows this
in action, including a register dump and memory dump of the stack region.

---

## Two-Pass Assembler

The assembler in `assembler/assembler.c` works in two passes:

- **Pass 1:** Scan source lines, build the label table (label name → address), and
  allocate constant pool slots for any branch target whose address > 63 (because the
  6-bit src field cannot hold addresses above 0x3F, and code starts at 0x0100).

- **Pass 2:** Re-scan source lines, encode each instruction using the label table and
  pool table from Pass 1.

The constant pool trick (same idea as ARM Thumb literal pools) stores large addresses
in `mem[0x00]–mem[0x0F]` and encodes branches as `DIRECT` mode pointing to the pool
slot, so the CPU reads the full address at runtime.

---

## Program Layout in Memory

When a program runs, the emulator lays out memory like this:

```
0x0000  ┌──────────────────┐
        │  Constant Pool   │  ← branch targets, data addresses stored here
0x0010  ├──────────────────┤
        │    (unused)      │
0x0100  ├──────────────────┤
        │   Code Segment   │  ← assembled instructions (PC starts here)
0x0800  ├──────────────────┤
        │   Data Segment   │  ← program results (Fibonacci array, etc.)
0x0F00  ├──────────────────┤
        │      Stack       │  ← grows downward from 0x0FBF
0x0FBF  │      SP →        │  ← each CALL pushes one word here
0x0FC0  ├──────────────────┤
        │  Memory-Mapped   │  ← IO_STDOUT / IO_STDIN / IO_TIMER
0x0FC2  └──────────────────┘
```

---

## Team Contributions

| Member | Contributions |
|--------|--------------|
| Member 1 | Designed ISA (`isa/isa.h`): opcodes, instruction encoding, memory map, flag semantics. Wrote CPU schematic. |
| Member 2 | Implemented CPU emulator (`emulator/cpu.c`, `emulator/cpu.h`): Fetch/Decode/Execute pipeline, ALU, flag updates, CALL/RET, trace output. |
| Member 3 | Implemented memory subsystem (`emulator/memory.c`, `emulator/memory.h`): RAM, memory-mapped I/O, hex dump. Wrote Timer and Hello World programs. |
| Member 4 | Implemented two-pass assembler (`assembler/assembler.c`, `assembler/assembler.h`): tokenizer, label table, constant pool, instruction encoding. Wrote Fibonacci and Factorial assembly programs. Integrated all demos in `main.c`. |

---

## C vs C++ Port Notes

This branch (`c-port`) is a C99 port. The `main` branch has an earlier C++ version.

| Concept        | C++ (main branch)          | C (this branch)                        |
|----------------|----------------------------|----------------------------------------|
| Modules        | Header-only `.h`           | Split `.h` + `.c`                      |
| Classes        | `class CPU { ... }`        | `typedef struct CPU` + `cpu_init()`    |
| Constructors   | `CPU(Memory& m)`           | `cpu_init(CPU* cpu, Memory* mem)`      |
| STL containers | `std::map`, `std::string`  | Fixed-size arrays, `char*`             |
| Standard       | C++17                      | C99                                    |
| Build          | `g++ -std=c++17`           | `gcc -std=c99`                         |
