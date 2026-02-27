; =============================================================================
;  UA Standard Library — std_string
;
;  String utility functions for null-terminated strings.
;
;  Functions:
;    strlen:     Compute length of a null-terminated string.
;                Input:  R0 = pointer to string
;                Output: R1 = length (number of bytes, excluding null)
;                Clobbers: R0, R2, R3
;
;    parse_int:  Parse an ASCII decimal string into an integer.
;                Input:  R0 = pointer to null-terminated digit string
;                Output: R0 = parsed integer value
;                Clobbers: R1, R2, R3, R6, R7
;                Notes:  Stops at null (\0), newline (\n), or carriage
;                        return (\r).  Handles unsigned values only.
;
;    to_string:  Convert an integer to a null-terminated ASCII string.
;                Input:  R0 = integer value (signed), R1 = output buffer ptr
;                Output: buffer at R1 filled with ASCII representation + null
;                Clobbers: R0, R1, R2, R3, R6, R7
;                Notes:  Handles negative numbers (writes leading '-').
;                        Buffer must be at least 12 bytes (32-bit) or
;                        22 bytes (64-bit) to hold the longest value.
;
;  Platform support:
;    This library uses only architecture-neutral MVIS instructions
;    and works on all backends (see 8051 note below).
;
;  8051 note:
;    LOADB uses indirect addressing (@R0/@R1).  strlen uses R0 as the
;    pointer, which is valid.  However, only internal RAM (0x00-0xFF)
;    is accessible via @Ri — strings in code/external memory require
;    MOVC/MOVX which are not part of MVIS.
; =============================================================================

; ---------------------------------------------------------------------------
;  strlen — count bytes until null terminator
;
;  Entry:  R0 = address of null-terminated string
;  Exit:   R1 = string length
;  Clobbers: R0 (points past the null terminator), R2, R3
; ---------------------------------------------------------------------------
strlen:
    LDI  R1, 0           ; length counter = 0
    LDI  R3, 0           ; null sentinel (outside loop for efficiency)
strlen_loop:
    LOADB R2, R0         ; R2 = byte at [R0]
    CMP  R2, R3          ; compare byte with 0
    JZ   strlen_done     ; if zero -> done
    INC  R0              ; advance pointer
    INC  R1              ; increment length
    JMP  strlen_loop
strlen_done:
    RET

; ---------------------------------------------------------------------------
;  parse_int — convert ASCII digit string to integer
;
;  Algorithm:
;    total = 0
;    for each byte in string:
;        if byte is \0, \n, or \r → stop
;        digit = byte - 48        (ASCII '0' = 48)
;        total = total * 10 + digit
;    return total
;
;  Entry:  R0 = pointer to ASCII digit string (e.g. "42\n" or "123\0")
;  Exit:   R0 = integer value
;  Clobbers: R1, R2, R3, R6, R7
; ---------------------------------------------------------------------------
parse_int:
    MOV  R3, R0          ; R3 = string pointer (working copy)
    LDI  R0, 0           ; R0 = running total (will be the result)
    LDI  R6, 10          ; R6 = 10 (multiply constant & newline sentinel)
    LDI  R7, 48          ; R7 = 48 (ASCII '0')
parse_int_loop:
    LOADB R1, R3         ; R1 = current byte
    LDI  R2, 0
    CMP  R1, R2          ; null terminator?
    JZ   parse_int_done
    CMP  R1, R6          ; newline (\n = 10)?
    JZ   parse_int_done
    LDI  R2, 13
    CMP  R1, R2          ; carriage return (\r = 13)?
    JZ   parse_int_done
    SUB  R1, R7          ; R1 = digit value (byte - '0')
    MUL  R0, R6          ; total = total * 10
    ADD  R0, R1          ; total = total + digit
    INC  R3              ; advance to next character
    JMP  parse_int_loop
parse_int_done:
    RET

; ---------------------------------------------------------------------------
;  to_string — convert integer to null-terminated ASCII string
;
;  Algorithm:
;    1. Handle zero as special case  → write "0\0", return
;    2. If negative, write '-' and negate the value
;    3. Extract digits by repeated division by 10:
;         digit     = value - (value / 10) * 10    (manual modulo)
;         ascii     = digit + 48
;         write ascii to buffer, advance pointer
;         value     = value / 10
;       until value == 0
;    4. Null-terminate the buffer
;    5. Reverse the digit portion in-place (digits come out backwards)
;
;  Entry:  R0 = integer value (signed), R1 = output buffer pointer
;  Exit:   buffer contains null-terminated ASCII string
;  Clobbers: R0, R1, R2, R3, R6, R7
; ---------------------------------------------------------------------------
to_string:
    MOV  R3, R0          ; R3 = value to convert
    ; --- Handle zero ---
    LDI  R2, 0
    CMP  R3, R2
    JNZ  to_string_sign  ; non-zero → check sign
    LDI  R2, 48          ; '0'
    STOREB R2, R1        ; buffer[0] = '0'
    INC  R1
    LDI  R2, 0
    STOREB R2, R1        ; buffer[1] = '\0'
    RET

to_string_sign:
    ; --- Handle negative ---
    LDI  R2, 0
    CMP  R3, R2
    JG   to_string_pos   ; value > 0 → skip sign handling
    ; Value is negative: write '-' and negate
    LDI  R2, 45          ; '-'
    STOREB R2, R1        ; write '-' to buffer
    INC  R1              ; advance past '-'
    LDI  R2, 0
    SUB  R2, R3          ; R2 = 0 - value = |value|
    MOV  R3, R2          ; R3 = absolute value

to_string_pos:
    ; R3 = absolute value, R1 = write position (past any '-')
    MOV  R6, R1          ; R6 = start of digit area (for reversing later)
    LDI  R7, 10          ; R7 = constant 10

to_string_digit:
    ; Extract least-significant digit without MOD:
    ;   quotient  = value / 10
    ;   remainder = value - quotient * 10
    MOV  R0, R3          ; R0 = current value
    MOV  R2, R3          ; R2 = current value (backup for remainder)
    DIV  R0, R7          ; R0 = quotient
    PUSH R0              ; save quotient (will become next R3)
    MUL  R0, R7          ; R0 = quotient * 10
    SUB  R2, R0          ; R2 = remainder (the digit, 0-9)
    LDI  R0, 48
    ADD  R2, R0          ; R2 = ASCII digit (digit + '0')
    STOREB R2, R1        ; write digit to buffer
    INC  R1              ; advance write pointer
    POP  R3              ; R3 = quotient (for next iteration)
    LDI  R0, 0
    CMP  R3, R0          ; quotient == 0?
    JNZ  to_string_digit ; more digits remain

    ; --- Null-terminate ---
    LDI  R0, 0
    STOREB R0, R1        ; write '\0' after last digit

    ; --- Reverse the digit portion ---
    ; Digits were extracted LSB-first, so "123" is stored as "321\0".
    ; R6 = first digit, R1 = one past last digit (the null).
    ; Adjust R1 to point to the last digit character.
    DEC  R1              ; R1 = pointer to last digit

to_string_rev:
    CMP  R6, R1
    JG   to_string_done  ; start > end → fully reversed
    JZ   to_string_done  ; start == end → single middle char, done
    ; Swap bytes at [R6] and [R1]
    LOADB R0, R6         ; R0 = char at start
    LOADB R2, R1         ; R2 = char at end
    STOREB R2, R6        ; [start] = end char
    STOREB R0, R1        ; [end]   = start char
    INC  R6              ; move start forward
    DEC  R1              ; move end backward
    JMP  to_string_rev

to_string_done:
    RET
