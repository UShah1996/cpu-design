; ============================================================
; hello.asm — Hello, World!
; Writes "Hello, World!\n" to stdout via memory-mapped I/O
;
; IO_STDOUT is at address 0x0FC0 (word address = 0x3F0)
; Writing a word to IO_STDOUT prints the low byte as a char
; ============================================================

; R0 = character to print
; R1 = pointer into string data
; R2 = end of string sentinel (0)

    LOAD  R1, #0x20       ; R1 = start of string in data memory (word 0x20 = addr 32)
    LOAD  R5, #0x3F       ; R5 = IO_STDOUT word address (0x0FC0 >> 2 approx)

PRINT_LOOP:
    LOAD  R0, [R1]        ; fetch next character from string
    BEQ   DONE            ; if char == 0, we're done (ZF set by LOAD)
    STORE R0, R5          ; write char to IO_STDOUT → prints it
    ADD   R1, #1          ; advance string pointer
    JMP   PRINT_LOOP      ; loop

DONE:
    JMP   DONE            ; halt

; ── String data (placed at word address 0x20 = offset 32 from CODE_START) ──
; Each word holds one ASCII character (low byte)
; Terminated by 0x0000

.org 0x0120
.data 0x0048   ; 'H'
.data 0x0065   ; 'e'
.data 0x006C   ; 'l'
.data 0x006C   ; 'l'
.data 0x006F   ; 'o'
.data 0x002C   ; ','
.data 0x0020   ; ' '
.data 0x0057   ; 'W'
.data 0x006F   ; 'o'
.data 0x0072   ; 'r'
.data 0x006C   ; 'l'
.data 0x0064   ; 'd'
.data 0x0021   ; '!'
.data 0x000A   ; '\n'
.data 0x0000   ; null terminator
