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
;    read:    Read up to N bytes from stdin into a buffer.
;             Input:  R0 = pointer to buffer, R1 = max bytes to read
;             Output: R0 = number of bytes actually read (Linux);
;                     R0 = undefined (Win32, see notes)
;
;  Platform support:
;    x86    + linux  : syscall 1/0  (write/read) via SYSCALL
;    x86    + win32  : SYS dispatcher → WriteFile / ReadFile (kernel32)
;    x86_32 + linux  : syscall 4/3  (write/read) via INT 0x80
;    arm    + linux  : syscall 4/3  (write/read) via SVC #0
;    arm64  + linux  : syscall 64/63 (write/read) via MOV X8,X7 + SVC #0
;    riscv  + linux  : syscall 64/63 (write/read) via ECALL
;    mcs51           : not supported (no OS)
;
;  Register mapping summary:
;    ┌──────────┬────────┬────────┬────────┬────────┬────────┐
;    │ UA Reg   │ x86-64 │ x86-32 │ ARM/64 │ RISC-V │ Role   │
;    ├──────────┼────────┼────────┼────────┼────────┼────────┤
;    │ R0       │ RAX    │ EAX    │ r0/X0  │ a0     │ sysnum/fd │
;    │ R1       │ RCX    │ ECX    │ r1/X1  │ a1     │ buf    │
;    │ R2       │ RDX    │ EDX    │ r2/X2  │ a2     │ count  │
;    │ R3       │ RBX    │ EBX    │ r3/X3  │ a3     │ --     │
;    │ R6       │ RSI    │ ESI    │ r6/X6  │ a6     │ x86 buf│
;    │ R7       │ RDI    │ EDI    │ r7/X7  │ a7     │ x86 fd / ARM+RV sysnum │
;    └──────────┴────────┴────────┴────────┴────────┴────────┘
;
;  Each section computes strlen internally (for print), then issues a
;  write/read syscall using the platform's register convention.
;  Requires MVIS instructions: MOV, LDI, SYS, LOADB, CMP, JZ, JMP,
;  INC, PUSH, POP, RET.
; =============================================================================

; ===========================================================================
;                            std_io.print
; ===========================================================================
;  Writes a null-terminated string to stdout.
;  Input:  R0 = pointer to null-terminated string
;  Output: none (registers clobbered)
; ===========================================================================

; ---------------------------------------------------------------------------
;  x86-64 (Linux and Win32)
;
;  Syscall convention (Linux):
;    RAX (R0) = syscall number (1 = write)
;    RDI (R7) = fd (1 = stdout)
;    RSI (R6) = buf pointer
;    RDX (R2) = byte count
;
;  Win32:
;    RSI (R6) = buf pointer, RDX (R2) = count, RAX (R0) = 1 (write)
;    SYS invokes the syscall_dispatcher which routes to write_dispatcher
;    and translates to WriteFile via kernel32.dll.
; ---------------------------------------------------------------------------
@IF_ARCH x86

print:
    MOV  R6, R0          ; R6 (RSI) = buf pointer for syscall
    LDI  R2, 0           ; R2 (RDX) = length counter
    MOV  R3, R0          ; R3 = working pointer for strlen
    LDI  R0, 0           ; R0 = null sentinel for comparison
print_strlen:
    LOADB R1, R3         ; R1 = byte at [R3]
    CMP  R1, R0          ; compare with null
    JZ   print_write     ; if null -> done counting
    INC  R3              ; next byte
    INC  R2              ; increment length
    JMP  print_strlen
print_write:
    LDI  R0, 1           ; syscall number: write (Linux) / dispatch flag (Win32)
    LDI  R7, 1           ; fd: stdout (Linux; ignored on Win32)
    SYS                  ; invoke syscall (Linux) or write_dispatcher (Win32)
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
    MOV  R1, R0          ; R1 (ECX) = buf pointer for syscall
    LDI  R2, 0           ; R2 (EDX) = length counter
    MOV  R6, R0          ; R6 = working pointer for strlen
    LDI  R7, 0           ; R7 = null sentinel
print_strlen:
    LOADB R0, R6         ; R0 = byte at [R6]
    CMP  R0, R7          ; compare with null
    JZ   print_write     ; if null -> done counting
    INC  R6              ; next byte
    INC  R2              ; increment length
    JMP  print_strlen
print_write:
    LDI  R0, 4           ; syscall number: write
    LDI  R3, 1           ; fd: stdout (EBX)
    SYS                  ; INT 0x80
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM (ARMv7-A, Linux)
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
    MOV  R1, R0          ; R1 = buf pointer for syscall
    LDI  R2, 0           ; R2 = length counter
    MOV  R3, R0          ; R3 = working pointer for strlen
    LDI  R6, 0           ; R6 = null sentinel
print_strlen:
    LOADB R0, R3         ; R0 = byte at [R3]
    CMP  R0, R6          ; compare with null
    JZ   print_write     ; if null -> done counting
    INC  R3              ; next byte
    INC  R2              ; increment length
    JMP  print_strlen
print_write:
    LDI  R7, 4           ; syscall number: write
    LDI  R0, 1           ; fd: stdout
    SYS                  ; SVC #0
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM64 (AArch64, Linux)
;
;  Syscall convention (SVC #0):
;    X8  = syscall number (64 = write)  — loaded from R7 by SYS backend
;    X0 (R0) = fd (1 = stdout)
;    X1 (R1) = buf pointer
;    X2 (R2) = byte count
;
;  Note: AArch64 Linux places the syscall number in X8, which is not
;  directly addressable as a UA register.  The ARM64 backend's SYS
;  instruction automatically copies R7 (X7) to X8 before the SVC #0,
;  so we load the syscall number into R7.
; ---------------------------------------------------------------------------
@IF_ARCH arm64
@IF_SYS linux

print:
    MOV  R1, R0          ; R1 (X1) = buf pointer for syscall
    LDI  R2, 0           ; R2 (X2) = length counter
    MOV  R3, R0          ; R3 = working pointer for strlen
    LDI  R6, 0           ; R6 = null sentinel
print_strlen:
    LOADB R0, R3         ; R0 = byte at [R3]
    CMP  R0, R6          ; compare with null
    JZ   print_write     ; if null -> done counting
    INC  R3              ; next byte
    INC  R2              ; increment length
    JMP  print_strlen
print_write:
    LDI  R7, 64          ; syscall number: write (AArch64 Linux)
    LDI  R0, 1           ; fd: stdout
    SYS                  ; MOV X8,X7 + SVC #0
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  RISC-V (RV64I, Linux)
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
    MOV  R1, R0          ; R1 (a1) = buf pointer for syscall
    LDI  R2, 0           ; R2 (a2) = length counter
    MOV  R3, R0          ; R3 = working pointer for strlen
    LDI  R6, 0           ; R6 = null sentinel
print_strlen:
    LOADB R0, R3         ; R0 = byte at [R3]
    CMP  R0, R6          ; compare with null
    JZ   print_write     ; if null -> done counting
    INC  R3              ; next byte
    INC  R2              ; increment length
    JMP  print_strlen
print_write:
    LDI  R7, 64          ; syscall number: write
    LDI  R0, 1           ; fd: stdout
    SYS                  ; ECALL
    RET

@ENDIF
@ENDIF

; ===========================================================================
;                             std_io.read
; ===========================================================================
;  Reads up to N bytes from stdin into a buffer.
;  Input:  R0 = pointer to buffer,  R1 = max bytes to read
;  Output: R0 = bytes actually read (Linux); undefined (Win32)
;
;  NOTE: The buffer is NOT null-terminated by the OS.  If you need a
;        null terminator, write it yourself after reading:
;            CALL std_io.read
;            ; R0 = bytes read
;            GET  R3, my_buffer
;            ADD  R3, R0          ; R3 points one past the last byte read
;            LDI  R1, 0
;            STOREB R1, R3        ; null-terminate
; ===========================================================================

; ---------------------------------------------------------------------------
;  x86-64 (Linux and Win32)
;
;  Linux read: RAX(R0)=0, RDI(R7)=0(stdin), RSI(R6)=buf, RDX(R2)=count
;  Win32 read: RAX(R0)=0 (dispatch flag), RSI(R6)=buf, RDX(R2)=count
;              SYS routes to read_dispatcher → ReadFile(hStdin, ...)
; ---------------------------------------------------------------------------
@IF_ARCH x86

read:
    MOV  R6, R0          ; R6 (RSI) = buffer pointer
    MOV  R2, R1          ; R2 (RDX) = max bytes to read
    LDI  R0, 0           ; syscall number: read (Linux) / dispatch flag (Win32)
    LDI  R7, 0           ; fd: stdin (Linux; ignored on Win32)
    SYS                  ; invoke syscall (Linux) or read_dispatcher (Win32)
    RET                  ; R0 (RAX) = bytes read (on Linux)

@ENDIF

; ---------------------------------------------------------------------------
;  x86-32 (Linux)
;
;  read: EAX(R0)=3, EBX(R3)=0(stdin), ECX(R1)=buf, EDX(R2)=count
; ---------------------------------------------------------------------------
@IF_ARCH x86_32
@IF_SYS linux

read:
    ; R0 = buffer, R1 = max_len on entry
    ; Need: R0 = 3 (sysnum), R1 = buf (ECX), R2 = count (EDX), R3 = 0 (fd)
    MOV  R2, R1          ; R2 (EDX) = max bytes to read (save before clobber)
    MOV  R1, R0          ; R1 (ECX) = buffer pointer
    LDI  R0, 3           ; syscall number: read
    LDI  R3, 0           ; fd: stdin (EBX)
    SYS                  ; INT 0x80
    RET                  ; R0 (EAX) = bytes read

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM (ARMv7-A, Linux)
;
;  read: r7(R7)=3, r0(R0)=0(stdin), r1(R1)=buf, r2(R2)=count
; ---------------------------------------------------------------------------
@IF_ARCH arm
@IF_SYS linux

read:
    ; R0 = buffer, R1 = max_len on entry
    ; Need: R7 = 3, R0 = 0 (fd), R1 = buf, R2 = count
    MOV  R2, R1          ; R2 = max bytes to read
    MOV  R1, R0          ; R1 = buffer pointer
    LDI  R7, 3           ; syscall number: read
    LDI  R0, 0           ; fd: stdin
    SYS                  ; SVC #0
    RET                  ; R0 = bytes read

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM64 (AArch64, Linux)
;
;  read: X8=63(sysnum via R7), X0(R0)=0(stdin), X1(R1)=buf, X2(R2)=count
; ---------------------------------------------------------------------------
@IF_ARCH arm64
@IF_SYS linux

read:
    ; R0 = buffer, R1 = max_len on entry
    MOV  R2, R1          ; R2 (X2) = max bytes to read
    MOV  R1, R0          ; R1 (X1) = buffer pointer
    LDI  R7, 63          ; syscall number: read (AArch64 Linux)
    LDI  R0, 0           ; fd: stdin
    SYS                  ; MOV X8,X7 + SVC #0
    RET                  ; R0 = bytes read

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  RISC-V (RV64I, Linux)
;
;  read: a7(R7)=63, a0(R0)=0(stdin), a1(R1)=buf, a2(R2)=count
; ---------------------------------------------------------------------------
@IF_ARCH riscv
@IF_SYS linux

read:
    ; R0 = buffer, R1 = max_len on entry
    MOV  R2, R1          ; R2 (a2) = max bytes to read
    MOV  R1, R0          ; R1 (a1) = buffer pointer
    LDI  R7, 63          ; syscall number: read
    LDI  R0, 0           ; fd: stdin
    SYS                  ; ECALL
    RET                  ; R0 = bytes read

@ENDIF
@ENDIF
