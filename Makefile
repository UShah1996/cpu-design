# =============================================================================
# CMPE-220 Software CPU — Makefile
# =============================================================================
#
# QUICK START:
#   make          → build the emulator
#   make run      → build + run all 6 demos
#   make help     → show all available targets
#
# INDIVIDUAL DEMOS:
#   make run-timer      → Demo 1: Fetch/Compute/Store cycles
#   make run-hello      → Demo 2: Hello World (memory-mapped I/O)
#   make run-fib        → Demo 3: Fibonacci Sequence
#   make run-factorial  → Demo 4: Factorial (function calls + stack)
#   make run-map        → Demo 5: Memory layout diagram
#   make run-asm        → Demo 6: Assembler listing
#
# =============================================================================

# ── Compiler settings ─────────────────────────────────────────────────────────
CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -I.
TARGET  = software_cpu_c

# ── Source files (all .c files that must be compiled) ─────────────────────────
SRCS = main.c \
       emulator/memory.c \
       emulator/cpu.c \
       assembler/assembler.c

# ── Header files (used to detect when a rebuild is needed) ────────────────────
HDRS = isa/isa.h \
       emulator/memory.h \
       emulator/cpu.h \
       assembler/assembler.h

# =============================================================================
# PRIMARY TARGETS
# =============================================================================

# Default target: compile everything into the executable
all: $(TARGET)

# Link and compile all sources into a single binary
$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)
	@echo ""
	@echo "  Build successful → ./$(TARGET)"
	@echo "  Run:  make run         (all demos)"
	@echo "  Run:  make run-fib     (fibonacci only)"
	@echo "  Run:  make help        (all targets)"
	@echo ""

# Remove compiled binary
clean:
	rm -f $(TARGET)
	@echo "  Cleaned."

# =============================================================================
# RUN TARGETS  (each maps to a demo number in main.c)
# =============================================================================

# Run all 6 demos in sequence
run: $(TARGET)
	./$(TARGET)

# Demo 1 — Timer: shows Fetch/Compute/Store cycle explicitly
run-timer: $(TARGET)
	./$(TARGET) 1

# Demo 2 — Hello World: shows memory-mapped I/O to stdout
run-hello: $(TARGET)
	./$(TARGET) 2

# Demo 3 — Fibonacci: loops, data memory, register usage
run-fib: $(TARGET)
	./$(TARGET) 3

# Demo 4 — Factorial: CALL/RET, stack frames, function calling convention
run-factorial: $(TARGET)
	./$(TARGET) 4

# Demo 5 — Memory Map: print memory layout and ISA reference
run-map: $(TARGET)
	./$(TARGET) 5

# Demo 6 — Assembler: assemble fibonacci source, print listing
run-asm: $(TARGET)
	./$(TARGET) 6

# =============================================================================
# HELP
# =============================================================================
help:
	@echo ""
	@echo "  CMPE-220 Software CPU — available make targets"
	@echo "  ────────────────────────────────────────────────"
	@echo "  make              → compile (same as make all)"
	@echo "  make clean        → remove compiled binary"
	@echo "  make run          → run all 6 demos"
	@echo "  make run-timer    → Demo 1: Fetch/Compute/Store"
	@echo "  make run-hello    → Demo 2: Hello World (MMIO)"
	@echo "  make run-fib      → Demo 3: Fibonacci Sequence"
	@echo "  make run-factorial → Demo 4: Factorial + Stack"
	@echo "  make run-map      → Demo 5: Memory Layout"
	@echo "  make run-asm      → Demo 6: Assembler Listing"
	@echo "  make help         → this message"
	@echo ""

# =============================================================================
# Tell make these targets are not files
# =============================================================================
.PHONY: all clean run run-timer run-hello run-fib run-factorial run-map run-asm help
