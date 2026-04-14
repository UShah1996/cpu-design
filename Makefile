CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -I.
TARGET  = software_cpu_c

SRCS = main.c \
       emulator/memory.c \
       emulator/cpu.c \
       assembler/assembler.c

all: $(TARGET)

$(TARGET): $(SRCS) isa/isa.h emulator/memory.h emulator/cpu.h assembler/assembler.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

run-timer: $(TARGET)
	./$(TARGET) 1

run-hello: $(TARGET)
	./$(TARGET) 2

run-fib: $(TARGET)
	./$(TARGET) 3

run-factorial: $(TARGET)
	./$(TARGET) 4

run-map: $(TARGET)
	./$(TARGET) 5

run-asm: $(TARGET)
	./$(TARGET) 6

.PHONY: all clean run run-timer run-hello run-fib run-factorial run-map run-asm
