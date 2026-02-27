; =============================================================================
;  UA Standard Library — std_math
;
;  Integer math utility functions.
;
;  Functions:
;    pow:       Integer exponentiation  (base^exp).
;               Input:  R0 = base, R1 = exponent (>= 0)
;               Output: R0 = result
;               Clobbers: R1, R2
;
;    factorial: Compute n! (n factorial).
;               Input:  R0 = n (>= 0)
;               Output: R0 = n!
;               Clobbers: R1, R2
;
;    max:       Return the larger of two signed integers.
;               Input:  R0 = a, R1 = b
;               Output: R0 = max(a, b)
;               Clobbers: (none besides R0)
;
;    abs:       Absolute value of a signed integer.
;               Input:  R0 = value
;               Output: R0 = |value|
;               Clobbers: R1, R2
;
;  Platform support:
;    This library uses only architecture-neutral MVIS instructions
;    (LDI, MUL, DEC, CMP, JZ, JG, JL, JMP, SUB, RET) and works on
;    all backends.
;
;  8051 note:
;    MUL on 8051 uses the hardware 8×8 multiplier (MUL AB).  Results
;    are limited to 8-bit operands; overflow wraps silently.
; =============================================================================

; ---------------------------------------------------------------------------
;  pow — integer exponentiation: R0 = R0 ^ R1
;
;  Algorithm:  result = 1; while (exp > 0) { result *= base; exp--; }
;
;  Entry:  R0 = base, R1 = exponent (>= 0)
;  Exit:   R0 = base^exponent
;  Clobbers: R1 (zeroed), R2
; ---------------------------------------------------------------------------
pow:
    LDI R2, 1            ; R2 = accumulator = 1
    LDI R3, 0            ; zero sentinel
pow_loop:
    CMP R1, R3           ; if exp == 0, done
    JZ pow_done
    MUL R2, R0           ; accumulator *= base
    DEC R1               ; exp--
    JMP pow_loop
pow_done:
    MOV R0, R2           ; result -> R0
    RET

; ---------------------------------------------------------------------------
;  factorial — compute n!: R0 = R0!
;
;  Algorithm:  result = 1; while (n > 1) { result *= n; n--; }
;
;  Entry:  R0 = n (>= 0)
;  Exit:   R0 = n!
;  Clobbers: R1, R2
; ---------------------------------------------------------------------------
factorial:
    LDI R1, 1            ; R1 = accumulator = 1
    LDI R2, 1            ; R2 = constant 1 (loop bound)
factorial_loop:
    CMP R0, R2           ; if n <= 1, done
    JZ factorial_done
    JL factorial_done
    MUL R1, R0           ; accumulator *= n
    DEC R0               ; n--
    JMP factorial_loop
factorial_done:
    MOV R0, R1           ; result -> R0
    RET

; ---------------------------------------------------------------------------
;  max — return the larger of R0 and R1
;
;  Algorithm:  if (R0 >= R1) return R0; else return R1;
;
;  Entry:  R0 = a, R1 = b
;  Exit:   R0 = max(a, b)
;  Clobbers: (none besides R0)
; ---------------------------------------------------------------------------
max:
    CMP R0, R1           ; compare a - b
    JG max_done          ; if a > b, R0 already holds max
    JZ max_done          ; if a == b, R0 is fine
    MOV R0, R1           ; a < b, so max = b
max_done:
    RET

; ---------------------------------------------------------------------------
;  abs — absolute value: R0 = |R0|
;
;  Algorithm:  if (R0 < 0) R0 = 0 - R0;
;
;  Entry:  R0 = signed value
;  Exit:   R0 = |value|
;  Clobbers: R1, R2
; ---------------------------------------------------------------------------
abs:
    LDI R1, 0            ; zero sentinel
    CMP R0, R1           ; compare value with 0
    JG abs_done          ; if value > 0, already positive
    JZ abs_done          ; if value == 0, done
    ; value < 0: negate by computing 0 - value
    MOV R2, R0           ; save original
    LDI R0, 0
    SUB R0, R2           ; R0 = 0 - original
abs_done:
    RET
