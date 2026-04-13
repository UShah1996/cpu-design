; ============================================================
; factorial.asm — Factorial using Iterative Function Call
; CMPE-220 Software CPU
; ============================================================
;
; PURPOSE:
;   Show function calls (CALL/RET), the call stack,
;   and how a C function like factorial() maps to assembly.
;
; C EQUIVALENT:
;   int factorial(int n) {
;       int result = 1;
;       int i;
;       for (i = 2; i <= n; i++) {
;           result = result * i;    // via repeated addition
;       }
;       return result;
;   }
;   int main() {
;       int ans = factorial(5);     // expect 120
;   }
;
; CALLING CONVENTION:
;   Input:  R0 = argument n
;   Output: R0 = return value
;   LR     = return address (set by CALL, used by RET)
;   SP     = stack pointer (CALL pushes old LR to stack)
;
; STACK DURING EXECUTION:
;   main calls factorial(5):
;     SP = STACK_BASE - 1
;     [SP] = LR (saved return address back to main)
;
; MULTIPLICATION (no MUL instruction):
;   result * i = add result to itself i times
;   Example: 6 * 3 = 6 + 6 + 6 = 18
;
; REGISTERS in factorial():
;   R0 = n (argument, preserved)
;   R1 = result (starts at 1)
;   R2 = i (loop counter, starts at 2)
;   R3 = accumulator for multiplication
;   R4 = inner loop counter
;   R6 = scratch for comparison
;   R7 = jump target
; ============================================================

.org 0x0000
.data 0x0800     ; pool[0x00] = DATA_START (result store)
.data 0x0000     ; pool[0x01] = factorial function address (filled at runtime)

; ── These pool slots hold loop addresses for JMP ─────────────
.data 0x0000     ; pool[0x02] = FACT_LOOP address
.data 0x0000     ; pool[0x03] = MUL_LOOP  address
.data 0x0000     ; pool[0x04] = MUL_DONE  address
.data 0x0000     ; pool[0x05] = FACT_DONE address

.org 0x0100      ; CODE_START

; ============================================================
; MAIN — entry point
; ============================================================
MAIN:
    LOAD  R0, #5          ; argument: n = 5
    CALL  FACTORIAL       ; call factorial(5) → result in R0
    ; store result at DATA_START
    LOAD  R8, [0x00]      ; R8 = DATA_START
    STORE R0, [R8]        ; mem[DATA_START] = factorial(5)
MAIN_DONE:
    JMP   MAIN_DONE       ; halt

; ============================================================
; FACTORIAL(n) — iterative
; Input:  R0 = n
; Output: R0 = n!
; ============================================================
FACTORIAL:
    ; result = 1
    LOAD  R1, #1          ; R1 = result = 1
    ; i = 2
    LOAD  R2, #2          ; R2 = i = 2

FACT_LOOP:
    ; if i > n: done
    LOAD  R6, R2          ; R6 = i
    SUB   R6, R0          ; R6 = i - n
    BEQ   LAST_MULT       ; if i == n: do last multiply then exit
    BLT   DO_MULT         ; if i < n: keep multiplying
    ; i > n: finished
    JMP   FACT_DONE

LAST_MULT:
    ; fall through to DO_MULT, then exit

DO_MULT:
    ; Multiply: R1 = R1 * R2  (result = result * i)
    ; Using repeated addition: R3 = 0; repeat R2 times: R3 += R1
    LOAD  R3, #0          ; R3 = accumulator = 0
    LOAD  R4, #0          ; R4 = inner loop counter = 0

MUL_LOOP:
    ADD   R3, R1          ; R3 += result
    ADD   R4, #1          ; inner counter++
    LOAD  R9, R4          ; R9 = inner counter
    SUB   R9, R2          ; R9 = counter - i
    BEQ   MUL_DONE        ; if counter == i, done
    JMP   MUL_LOOP        ; else keep adding

MUL_DONE:
    LOAD  R1, R3          ; result = accumulated value
    ADD   R2, #1          ; i++
    ; check if we just did i==n (LAST_MULT case)
    LOAD  R6, R2          ; R6 = new i
    SUB   R6, R0          ; R6 = i - n
    BLT   FACT_LOOP       ; if new i < n, loop (BLT: SF != OF)
    JMP   FACT_DONE       ; else we're done

FACT_DONE:
    LOAD  R0, R1          ; return value: R0 = result
    RET                   ; return to caller (PC = LR)
