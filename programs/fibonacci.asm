; ============================================================
; fibonacci.asm — Fibonacci Sequence F(0)..F(9)
; CMPE-220 Software CPU
; ============================================================
;
; PURPOSE:
;   Compute the first 10 Fibonacci numbers and store them
;   in data memory starting at DATA_START (0x0800).
;
; ALGORITHM (iterative):
;   a = 0, b = 1
;   for i = 0 to 9:
;       store(a)          ; save current fib number
;       temp = a + b      ; compute next
;       a = b             ; shift
;       b = temp
;
; EXPECTED OUTPUT in data memory:
;   0 1 1 2 3 5 8 13 21 34
;
; REGISTERS:
;   R0 = a    (current fibonacci number)
;   R1 = b    (next fibonacci number)
;   R2 = temp (a + b)
;   R3 = i    (loop counter)
;   R4 = N    (loop limit = 10)
;   R5 = store pointer (data memory address)
;   R6 = scratch for comparison
;   R7 = jump target
; ============================================================

.org 0x0000
.data 0x0800     ; pool[0x00] = DATA_START

.org 0x0100      ; CODE_START

; ── Initialize registers ─────────────────────────────────────
    LOAD  R0, #0          ; a = 0
    LOAD  R1, #1          ; b = 1
    LOAD  R3, #0          ; i = 0
    LOAD  R4, #10         ; N = 10
    LOAD  R5, [0x00]      ; R5 = DATA_START

; ── Main loop ────────────────────────────────────────────────
FIB_LOOP:
    STORE R0, [R5]        ; store current fib: mem[R5] = a
    ADD   R5, #1          ; advance store pointer

    LOAD  R2, R0          ; temp = a
    ADD   R2, R1          ; temp = a + b

    LOAD  R0, R1          ; a = b
    LOAD  R1, R2          ; b = temp

    ADD   R3, #1          ; i++

    ; Check: if i == N, done
    LOAD  R6, R3          ; R6 = i
    SUB   R6, R4          ; R6 = i - N
    BEQ   FIB_DONE        ; if i == N, exit loop
    JMP   FIB_LOOP        ; else continue

FIB_DONE:
    JMP   FIB_DONE        ; halt
