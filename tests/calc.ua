; =============================================================================
;  calc.ua — Interactive Calculator
;
;  Demonstrates:
;    - std_io.print / std_io.read   (console I/O)
;    - std_string.parse_int         (ASCII → integer)
;    - std_string.to_string         (integer → ASCII)
;    - BUFFER, VAR, GET, SET        (memory management)
;    - CMP / JZ branching           (operator dispatch)
;
;  Supported targets: x86, x86_32, arm, arm64, riscv  (skip 8051)
;
;  Usage:
;    uas calc.ua -arch x86 -sys linux -format elf -o calc
;    chmod +x calc && ./calc
;
;    uas calc.ua -arch x86 -sys win32 -format pe -o calc.exe
;    calc.exe
; =============================================================================

@ARCH_ONLY x86, x86_32, arm, arm64, riscv
@IMPORT std_io
@IMPORT std_string

; ----------------------------- Data Section ---------------------------------
    BUFFER input_1, 32       ; keyboard buffer for first number
    BUFFER input_2, 32       ; keyboard buffer for second number
    BUFFER input_op, 8       ; keyboard buffer for operator (+, -)
    BUFFER output_buf, 32    ; output buffer for result string

    VAR num1                 ; parsed first operand
    VAR num2                 ; parsed second operand
    VAR result               ; computed result

; ----------------------------- Entry Point ----------------------------------
    JMP  main

main:
    ; === Step 1: Read first number ===
    LDS  R0, "Enter first number: "
    CALL std_io.print

    GET  R0, input_1         ; R0 = address of input buffer
    LDI  R1, 31              ; max bytes (leave room for null)
    CALL std_io.read         ; read from stdin

    GET  R0, input_1         ; R0 = buffer address (parse_int input)
    CALL std_string.parse_int
    SET  num1, R0            ; save parsed integer

    ; === Step 2: Read second number ===
    LDS  R0, "Enter second number: "
    CALL std_io.print

    GET  R0, input_2
    LDI  R1, 31
    CALL std_io.read

    GET  R0, input_2
    CALL std_string.parse_int
    SET  num2, R0

    ; === Step 3: Read operator ===
    LDS  R0, "Operator (+ or -): "
    CALL std_io.print

    GET  R0, input_op
    LDI  R1, 7
    CALL std_io.read

    ; === Step 4: Decode operator ===
    GET  R3, input_op        ; R3 = address of operator buffer
    LOADB R0, R3             ; R0 = first byte (the operator character)

    LDI  R1, 43              ; '+' = ASCII 43
    CMP  R0, R1
    JZ   do_add

    LDI  R1, 45              ; '-' = ASCII 45
    CMP  R0, R1
    JZ   do_sub

    ; Unknown operator — print error and exit
    LDS  R0, "Error: unknown operator\n"
    CALL std_io.print
    HLT

; ----------------------------- Addition -------------------------------------
do_add:
    GET  R0, num1
    GET  R1, num2
    ADD  R0, R1              ; R0 = num1 + num2
    SET  result, R0
    JMP  show_result

; ----------------------------- Subtraction ----------------------------------
do_sub:
    GET  R0, num1
    GET  R1, num2
    SUB  R0, R1              ; R0 = num1 - num2
    SET  result, R0
    JMP  show_result

; ----------------------------- Display Result -------------------------------
show_result:
    GET  R0, result          ; R0 = computed integer
    GET  R1, output_buf      ; R1 = output buffer address
    CALL std_string.to_string

    LDS  R0, "Result: "
    CALL std_io.print

    GET  R0, output_buf      ; R0 = pointer to result string
    CALL std_io.print

    LDS  R0, "\n"
    CALL std_io.print

    HLT
