; =============================================================================
;  UA Standard Library — std_io
;
;  I/O functions using system calls.
;
;  Functions:
;    print:   Write a string to stdout.
;             Input:  R0 = pointer to null-terminated string
;             Output: none (R0-R7 may be clobbered)
;
;  Platform notes (x86-64 Linux):
;    syscall 1 = write(fd, buf, count)
;    RAX=1, RDI=fd(1=stdout), RSI=buf, RDX=count
;    UA mapping: R0=RAX, R2=RDX, R6=RSI, R7=RDI
;
;  This library computes string length internally, then issues a write
;  syscall. It requires the SYS instruction and LOADB for strlen.
; =============================================================================

; ---------------------------------------------------------------------------
;  print — write null-terminated string to stdout
;
;  Entry:  R0 = address of null-terminated string
;  Exit:   all registers may be clobbered
; ---------------------------------------------------------------------------
print:
    ; Save string pointer in R6 (RSI = buf for syscall)
    MOV R6, R0

    ; Compute string length → R2 (RDX = count for syscall)
    ; We inline a strlen here to avoid circular dependency
    LDI R2, 0           ; length counter
    MOV R3, R0           ; working pointer
print_strlen:
    LOADB R4, R3         ; R4 = byte at [R3]
    LDI R5, 0
    CMP R4, R5           ; compare with null
    JZ print_write       ; if null → done counting
    INC R3               ; next byte
    INC R2               ; increment length
    JMP print_strlen

print_write:
    ; Set up syscall: write(1, buf, count)
    ; R0 (RAX) = syscall number (1 = write)
    ; R7 (RDI) = fd (1 = stdout)
    ; R6 (RSI) = buf (already set)
    ; R2 (RDX) = count (already set)
    LDI R0, 1           ; syscall number: write
    LDI R7, 1           ; fd: stdout
    SYS                  ; invoke syscall
    RET
