; ============================================================
; timer.asm — Timer Example
; Shows fetch/compute/store cycles explicitly
;
; This program:
;   1. Reads the hardware timer
;   2. Counts from 0 to 5
;   3. Stores each count to memory
;   4. Shows how each step maps to a CPU cycle
; ============================================================

; --- Initialize counter ---
LOAD  R0, #0          ; FETCH: load immediate 0 into R0 (counter)
LOAD  R1, #5          ; FETCH: load loop limit into R1
LOAD  R2, #0x08       ; FETCH: R2 = data memory base address (0x0800)

; --- Main loop ---
LOOP:
    ; FETCH CYCLE: read timer
    LOAD  R3, [0x3F]  ; FETCH: read from IO_TIMER (word addr 0x3F = 0x0FC2/4... approx)

    ; COMPUTE CYCLE: increment counter
    ADD   R0, #1      ; COMPUTE: R0 = R0 + 1

    ; STORE CYCLE: save counter to data memory
    STORE R0, R2      ; STORE: mem[R2] = R0 (save current count)
    ADD   R2, #1      ; advance data pointer

    ; Check loop condition: compare R0 with R1
    SUB   R4, R0      ; R4 = R4 - R0 (temporary)
    LOAD  R4, R0      ; load current count
    SUB   R4, R1      ; R4 = R4 - R1 (sets ZF if equal)
    BNE   LOOP        ; loop if not equal

; --- Halt ---
DONE:
    JMP   DONE        ; self-loop = halt
