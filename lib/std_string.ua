; =============================================================================
;  UA Standard Library — std_string
;
;  String utility functions for null-terminated strings.
;
;  Functions:
;    strlen:  Compute length of a null-terminated string.
;             Input:  R0 = pointer to string
;             Output: R1 = length (number of bytes, excluding null)
;             Clobbers: R2
; =============================================================================

; ---------------------------------------------------------------------------
;  strlen — count bytes until null terminator
;
;  Entry:  R0 = address of null-terminated string
;  Exit:   R1 = string length
;  Clobbers: R2
; ---------------------------------------------------------------------------
strlen:
    LDI R1, 0           ; length counter = 0
strlen_loop:
    LOADB R2, R0         ; R2 = byte at [R0]
    LDI R3, 0
    CMP R2, R3           ; compare byte with 0
    JZ strlen_done       ; if zero → done
    INC R0               ; advance pointer
    INC R1               ; increment length
    JMP strlen_loop
strlen_done:
    RET
