# CMPE-220 Software CPU

A complete 16-bit software CPU emulator written in C++17.

## Architecture

```
┌─────────────────────────────────────────────┐
│              SOFTWARE CPU                   │
│                                             │
│  ISA: 16 instructions, 16-bit words         │
│  Registers: R0-R12, SP(R13), LR(R14), PC(R15)│
│  Memory: 4096 words (8KB)                   │
│  Flags: ZF CF SF OF                         │
└─────────────────────────────────────────────┘
```

## Project Structure

```
software-cpu/
├── isa/
│   └── isa.h           ISA definitions, opcodes, encoding
├── emulator/
│   ├── cpu.h           CPU: registers, ALU, CU, fetch-decode-execute
│   └── memory.h        Memory + memory-mapped I/O
├── assembler/
│   └── assembler.h     Two-pass assembler
├── programs/
│   ├── timer.asm       Timer example
│   └── hello.asm       Hello World
└── main.cpp            All demos
```

## Building and Running

```bash
# Compile
g++ -std=c++17 -I. -O2 -o software_cpu main.cpp

# Run all demos
./software_cpu

# Run specific demo
./software_cpu 1    # Timer (fetch/compute/store cycles)
./software_cpu 2    # Hello, World
./software_cpu 3    # Fibonacci sequence
./software_cpu 4    # Factorial (function calls)
./software_cpu 5    # Memory layout diagram
```

## ISA Reference

### Instruction Format (16 bits)
```
[ 15..12 | 11..8 | 7..6 | 5..0 ]
  opcode   dest    mode   src/imm
```

### Opcodes
| Opcode | Hex | Instruction |
|--------|-----|-------------|
| ADD    | 0x0 | dest = dest + src |
| SUB    | 0x1 | dest = dest - src |
| AND    | 0x2 | dest = dest & src |
| OR     | 0x3 | dest = dest \| src |
| NOT    | 0x4 | dest = ~dest |
| SHL    | 0x5 | dest = dest << src (logical) |
| SHR    | 0x6 | dest = dest >> src (logical) |
| SAR    | 0x7 | dest = dest >> src (arithmetic) |
| LOAD   | 0x8 | dest = src |
| STORE  | 0x9 | mem[src] = dest |
| JMP    | 0xA | PC = src |
| BEQ    | 0xB | if ZF: PC = src |
| BNE    | 0xC | if !ZF: PC = src |
| BLT    | 0xD | if SF!=OF: PC = src |
| CALL   | 0xE | LR=PC, PC=src |
| RET    | 0xF | PC=LR |

### Addressing Modes
| Mode | Bits | Syntax | Meaning |
|------|------|--------|---------|
| REG  | 00   | Rn     | Value in register |
| IMM  | 01   | #n     | Literal value (0-63) |
| DIRECT | 10 | [n]  | mem[n] |
| INDIRECT | 11 | [Rn] | mem[Rn] |

### Memory Map
```
0x0000-0x000F  Constant pool (address constants)
0x0100-0x07FF  Code segment
0x0800-0x0EFF  Data segment
0x0F00-0x0FBF  Stack (grows downward)
0x0FC0         IO_STDOUT
0x0FC1         IO_STDIN
0x0FC2         IO_TIMER
```

## Fetch-Decode-Execute Cycle

```
FETCH:   instr = mem[PC]; PC++
DECODE:  opcode=bits[15:12], dest=bits[11:8], mode=bits[7:6], src=bits[5:0]
RESOLVE: REG→R[src] | IMM→src | DIRECT→mem[src] | INDIRECT→mem[R[src]]
EXECUTE: ALU computes | memory accessed | PC changes
WRITEBK: result→register | result→memory
```
