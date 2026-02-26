; =============================================================================
;  UA Standard Library — std_io
;
;  I/O functions using system calls.
;
;  Functions:
;    print:   Write a null-terminated string to stdout.
;             Input:  R0 = pointer to null-terminated string
;             Output: none (R0-R7 may be clobbered)
;
;  Platform support:
;    x86    + linux  : syscall 1  (write) via SYSCALL
;    x86    + win32  : syscall 1  (write) via win32 write_dispatcher
;    x86_32 + linux  : syscall 4  (write) via INT 0x80
;    arm    + linux  : syscall 4  (write) via SVC #0
;    riscv  + linux  : syscall 64 (write) via ECALL
;    arm64           : not yet supported (syscall register X8 not in UA)
;    mcs51           : not supported (no OS)
;
;  Each section computes strlen internally, then issues a write syscall
;  using the platform's register convention.  Requires SYS and LOADB.
; =============================================================================

; ---------------------------------------------------------------------------
;  x86-64 (Linux and Win32)
;
;  Syscall convention:
;    RAX (R0) = syscall number (1 = write)
;    RDI (R7) = fd (1 = stdout)
;    RSI (R6) = buf pointer
;    RDX (R2) = byte count
;
;  On win32, SYS invokes the write_dispatcher which translates to
;  WriteFile via kernel32.dll — same register setup as Linux.
;  Avoids R4 (RSP) and R5 (RBP).
; ---------------------------------------------------------------------------
@IF_ARCH x86

print:
    MOV R6, R0           ; R6 (RSI) = buf pointer for syscall
    LDI R2, 0            ; R2 (RDX) = length counter
    MOV R3, R0           ; R3 = working pointer for strlen
    LDI R0, 0            ; R0 = null sentinel for comparison
print_strlen:
    LOADB R1, R3         ; R1 = byte at [R3]
    CMP R1, R0           ; compare with null
    JZ print_write       ; if null -> done counting
    INC R3               ; next byte
    INC R2               ; increment length
    JMP print_strlen
print_write:
    LDI R0, 1            ; syscall number: write
    LDI R7, 1            ; fd: stdout
    SYS                  ; invoke syscall (or win32 dispatcher)
    RET

@ENDIF

; ---------------------------------------------------------------------------
;  x86-32 (Linux)
;
;  Syscall convention (INT 0x80):
;    EAX (R0) = syscall number (4 = write)
;    EBX (R3) = fd (1 = stdout)
;    ECX (R1) = buf pointer
;    EDX (R2) = byte count
; ---------------------------------------------------------------------------
@IF_ARCH x86_32
@IF_SYS linux

print:
    MOV R1, R0           ; R1 (ECX) = buf pointer for syscall
    LDI R2, 0            ; R2 (EDX) = length counter
    MOV R6, R0           ; R6 = working pointer for strlen
    LDI R7, 0            ; R7 = null sentinel
print_strlen:
    LOADB R0, R6         ; R0 = byte at [R6]
    CMP R0, R7           ; compare with null
    JZ print_write       ; if null -> done counting
    INC R6               ; next byte
    INC R2               ; increment length
    JMP print_strlen
print_write:
    LDI R0, 4            ; syscall number: write
    LDI R3, 1            ; fd: stdout (EBX)
    SYS                  ; INT 0x80
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM (Linux)
;
;  Syscall convention (SVC #0):
;    r7 (R7) = syscall number (4 = write)
;    r0 (R0) = fd (1 = stdout)
;    r1 (R1) = buf pointer
;    r2 (R2) = byte count
; ---------------------------------------------------------------------------
@IF_ARCH arm
@IF_SYS linux

print:
    MOV R1, R0           ; R1 = buf pointer for syscall
    LDI R2, 0            ; R2 = length counter
    MOV R3, R0           ; R3 = working pointer for strlen
    LDI R6, 0            ; R6 = null sentinel
print_strlen:
    LOADB R0, R3         ; R0 = byte at [R3]
    CMP R0, R6           ; compare with null
    JZ print_write       ; if null -> done counting
    INC R3               ; next byte
    INC R2               ; increment length
    JMP print_strlen
print_write:
    LDI R7, 4            ; syscall number: write
    LDI R0, 1            ; fd: stdout
    SYS                  ; SVC #0
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  RISC-V (Linux)
;
;  Syscall convention (ECALL):
;    a7 (R7) = syscall number (64 = write)
;    a0 (R0) = fd (1 = stdout)
;    a1 (R1) = buf pointer
;    a2 (R2) = byte count
; ---------------------------------------------------------------------------
@IF_ARCH riscv
@IF_SYS linux

print:
    MOV R1, R0           ; R1 (a1) = buf pointer for syscall
    LDI R2, 0            ; R2 (a2) = length counter
    MOV R3, R0           ; R3 = working pointer for strlen
    LDI R6, 0            ; R6 = null sentinel
print_strlen:
    LOADB R0, R3         ; R0 = byte at [R3]
    CMP R0, R6           ; compare with null
    JZ print_write       ; if null -> done counting
    INC R3               ; next byte
    INC R2               ; increment length
    JMP print_strlen
print_write:
    LDI R7, 64           ; syscall number: write
    LDI R0, 1            ; fd: stdout
    SYS                  ; ECALL
    RET

@ENDIF
@ENDIF
