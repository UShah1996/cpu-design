#ifndef MEMORY_H
#define MEMORY_H

#include "../isa/isa.h"
#include <stddef.h>

typedef struct {
    Word ram[MEM_SIZE];
    Word timer_ticks;
} Memory;

void mem_init        (Memory* m);
Word mem_read        (Memory* m, Address addr);
void mem_write       (Memory* m, Address addr, Word value);
void mem_tick        (Memory* m);
void mem_load_program(Memory* m, Address start, const Word* program, size_t count);
void mem_dump        (const Memory* m, Address start, Address end);
void mem_dump_nonzero(const Memory* m);

#endif /* MEMORY_H */
