; =============================================================================
;  UA Standard Library — std_string
;
;  String utility functions for null-terminated strings.
;
;  Functions:
;    strlen:  Compute length of a null-terminated string.
;             Input:  R0 = pointer to string
;             Output: R1 = length (number of bytes, excluding null)
;             Clobbers: R0 (advanced past end), R2, R3
;
;  Platform support:
;    This library uses only architecture-neutral MVIS instructions
;    (LOADB, CMP, JZ, INC, JMP, LDI, RET) and works on all backends.
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
    LDI R1, 0            ; length counter = 0
    LDI R3, 0            ; null sentinel (outside loop for efficiency)
strlen_loop:
    LOADB R2, R0         ; R2 = byte at [R0]
    CMP R2, R3           ; compare byte with 0
    JZ strlen_done       ; if zero -> done
    INC R0               ; advance pointer
    INC R1               ; increment length
    JMP strlen_loop
strlen_done:
    RET
