; =============================================================================
;  UA Standard Library — std_iostream
;
;  File I/O functions using system calls (file streams).
;
;  Functions:
;    fopen:   Open a file for reading or writing.
;             Input:  LDS R0, "path"; SET iostream_path, R0
;                     SET iostream_mode, 0|1  (0=read, 1=write)
;             Output: R0 = file descriptor / handle (-1 on error)
;                     iostream_fd is set to the returned fd/handle.
;
;    fread:   Read up to N bytes from a file into a buffer.
;             Input:  GET R0, mybuf; SET iostream_buf, R0  (destination)
;                     SET iostream_count, <n>               (max bytes)
;                     iostream_fd must be set (by fopen).
;             Output: R0 = number of bytes actually read
;
;    fwrite:  Write N bytes from a buffer/string to a file.
;             Input:  LDS R0, "text"; SET iostream_buf, R0   (source)
;                     SET iostream_count, <n>  (byte count)
;                     iostream_fd must be set.
;             Output: R0 = number of bytes actually written
;
;    fclose:  Close a file descriptor / handle.
;             Input:  iostream_fd must be set.
;             Output: R0 = 0 on success (Linux), nonzero on success (Win32)
;
;  Platform support:
;    x86    + linux  : syscall 2/0/1/3  (open/read/write/close) via SYSCALL
;    x86    + win32  : SYS dispatcher -> CreateFileA/ReadFile/WriteFile/
;                      CloseHandle via kernel32.dll
;    x86_32 + linux  : syscall 5/3/4/6  via INT 0x80
;    arm    + linux  : syscall 5/3/4/6  via SVC #0
;    arm64  + linux  : syscall 56/63/64/57 (openat/read/write/close) via SVC
;    riscv  + linux  : syscall 56/63/64/57 (openat/read/write/close) via ECALL
;    mcs51           : not supported (no OS / no file system)
;
;  Register mapping summary (file I/O):
;    UA Reg  x86-64  x86-32  ARM/64  RISC-V  Role
;    R0      RAX     EAX     r0/X0   a0      sysnum / fd / ret
;    R1      RCX     ECX     r1/X1   a1      buf / path (32/ARM)
;    R2      RDX     EDX     r2/X2   a2      count / flags
;    R3      RBX     EBX     r3/X3   a3      fd (x86-32) / mode
;    R6      RSI     ESI     r6/X6   a6      buf / flags (x86)
;    R7      RDI     EDI     r7/X7   a7      fd(x86) / sysnum
;
;  Open modes:
;    0 = read   (O_RDONLY on Linux, GENERIC_READ + OPEN_EXISTING on Win32)
;    1 = write  (O_WRONLY|O_CREAT|O_TRUNC on Linux,
;                GENERIC_WRITE + CREATE_ALWAYS on Win32)
;
;  Requires MVIS instructions: MOV, LDI, SYS, GET, SET, CMP, JZ, JMP,
;  SUB, RET.
; =============================================================================

@ARCH_ONLY x86, x86_32, arm, arm64, riscv

; ---- Shared variables ---------------------------------------------------
VAR iostream_fd,    0       ; current file descriptor / handle
VAR iostream_path,  0       ; pointer to null-terminated path string
VAR iostream_mode,  0       ; open mode: 0=read, 1=write
VAR iostream_buf,   0       ; pointer to read/write buffer
VAR iostream_count, 0       ; byte count for read/write


; ===========================================================================
;                            fopen
; ===========================================================================

; ---------------------------------------------------------------------------
;  x86-64  (Linux)
;  open(path, flags, mode): RAX=2, RDI=path, RSI=flags, RDX=mode
; ---------------------------------------------------------------------------
@IF_ARCH x86
@IF_SYS linux

fopen:
    GET  R3, iostream_mode    ; R3 = mode (temp, RBX)
    LDI  R0, 0
    CMP  R3, R0
    JZ   _io_fopen_rd
    ; Write mode: O_WRONLY|O_CREAT|O_TRUNC = 577
    LDI  R6, 577             ; RSI = flags
    LDI  R2, 438             ; RDX = mode 0666
    JMP  _io_fopen_call
_io_fopen_rd:
    LDI  R6, 0               ; RSI = O_RDONLY
    LDI  R2, 0               ; RDX = mode unused
_io_fopen_call:
    GET  R7, iostream_path    ; RDI = path pointer
    LDI  R0, 2               ; RAX = syscall 2 (open)
    SYS
    SET  iostream_fd, R0      ; save fd
    RET

@ENDIF

; ---------------------------------------------------------------------------
;  x86-64  (Win32)
;  Dispatch code 2: RDI=path, RSI=mode(0/1) -> CreateFileA
; ---------------------------------------------------------------------------
@IF_SYS win32

fopen:
    GET  R7, iostream_path    ; RDI = path pointer
    GET  R6, iostream_mode    ; RSI = mode (0=read, 1=write)
    LDI  R0, 2               ; dispatch code = open
    SYS
    SET  iostream_fd, R0      ; save HANDLE
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  x86-32  (Linux)
;  open(path, flags, mode): EAX=5, EBX=path, ECX=flags, EDX=mode
; ---------------------------------------------------------------------------
@IF_ARCH x86_32
@IF_SYS linux

fopen:
    GET  R6, iostream_mode    ; R6 = mode (temp, ESI)
    LDI  R0, 0
    CMP  R6, R0
    JZ   _io_fopen_rd_32
    LDI  R1, 577             ; ECX = O_WRONLY|O_CREAT|O_TRUNC
    LDI  R2, 438             ; EDX = 0666
    JMP  _io_fopen_call_32
_io_fopen_rd_32:
    LDI  R1, 0               ; ECX = O_RDONLY
    LDI  R2, 0
_io_fopen_call_32:
    GET  R3, iostream_path    ; EBX = path
    LDI  R0, 5               ; EAX = syscall 5 (open)
    SYS
    SET  iostream_fd, R0
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM  (Linux)
;  open(path, flags, mode): r7=5, r0=path, r1=flags, r2=mode
; ---------------------------------------------------------------------------
@IF_ARCH arm
@IF_SYS linux

fopen:
    GET  R6, iostream_mode    ; R6 = mode (temp, r6)
    LDI  R3, 0
    CMP  R6, R3
    JZ   _io_fopen_rd_arm
    LDI  R1, 577             ; r1 = O_WRONLY|O_CREAT|O_TRUNC
    LDI  R2, 438             ; r2 = 0666
    JMP  _io_fopen_call_arm
_io_fopen_rd_arm:
    LDI  R1, 0
    LDI  R2, 0
_io_fopen_call_arm:
    GET  R0, iostream_path    ; r0 = path
    LDI  R7, 5               ; r7 = syscall 5 (open)
    SYS
    SET  iostream_fd, R0
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM64  (Linux)
;  openat(dirfd, path, flags, mode): X8=56, X0=-100, X1=path, X2=flags,
;  X3=mode.  AT_FDCWD = -100.
; ---------------------------------------------------------------------------
@IF_ARCH arm64
@IF_SYS linux

fopen:
    GET  R6, iostream_mode    ; R6 = mode (temp, X6)
    LDI  R0, 0
    CMP  R6, R0
    JZ   _io_fopen_rd_a64
    LDI  R2, 577             ; X2 = O_WRONLY|O_CREAT|O_TRUNC
    LDI  R3, 438             ; X3 = 0666
    JMP  _io_fopen_call_a64
_io_fopen_rd_a64:
    LDI  R2, 0               ; X2 = O_RDONLY
    LDI  R3, 0
_io_fopen_call_a64:
    GET  R1, iostream_path    ; X1 = path
    LDI  R0, 0
    SUB  R0, 100              ; X0 = AT_FDCWD = -100
    LDI  R7, 56              ; X8 = openat
    SYS
    SET  iostream_fd, R0
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  RISC-V  (Linux)
;  openat(dirfd, path, flags, mode): a7=56, a0=-100, a1=path, a2=flags,
;  a3=mode.  Same syscall numbers as ARM64.
; ---------------------------------------------------------------------------
@IF_ARCH riscv
@IF_SYS linux

fopen:
    GET  R6, iostream_mode    ; R6 = mode (temp, a6)
    LDI  R0, 0
    CMP  R6, R0
    JZ   _io_fopen_rd_rv
    LDI  R2, 577             ; a2 = O_WRONLY|O_CREAT|O_TRUNC
    LDI  R3, 438             ; a3 = 0666
    JMP  _io_fopen_call_rv
_io_fopen_rd_rv:
    LDI  R2, 0
    LDI  R3, 0
_io_fopen_call_rv:
    GET  R1, iostream_path    ; a1 = path
    LDI  R0, 0
    SUB  R0, 100              ; a0 = AT_FDCWD = -100
    LDI  R7, 56              ; a7 = openat
    SYS
    SET  iostream_fd, R0
    RET

@ENDIF
@ENDIF


; ===========================================================================
;                            fread
; ===========================================================================
;  Reads up to iostream_count bytes into iostream_buf from iostream_fd.
;  Returns bytes actually read in R0.
; ===========================================================================

; ---------------------------------------------------------------------------
;  x86-64  (Linux + Win32)
;  read: RAX=0, RDI=fd, RSI=buf, RDX=count
;  Win32: dispatch 0, RDI=handle, RSI=buf, RDX=count -> ReadFile
; ---------------------------------------------------------------------------
@IF_ARCH x86

fread:
    GET  R7, iostream_fd      ; RDI = fd / handle
    GET  R6, iostream_buf     ; RSI = buffer pointer
    GET  R2, iostream_count   ; RDX = count
    LDI  R0, 0               ; RAX = read syscall / dispatch 0
    SYS
    RET

@ENDIF

; ---------------------------------------------------------------------------
;  x86-32  (Linux)
;  read: EAX=3, EBX=fd, ECX=buf, EDX=count
; ---------------------------------------------------------------------------
@IF_ARCH x86_32
@IF_SYS linux

fread:
    GET  R3, iostream_fd      ; EBX = fd
    GET  R1, iostream_buf     ; ECX = buffer
    GET  R2, iostream_count   ; EDX = count
    LDI  R0, 3               ; EAX = syscall 3 (read)
    SYS
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM  (Linux)
;  read: r7=3, r0=fd, r1=buf, r2=count
; ---------------------------------------------------------------------------
@IF_ARCH arm
@IF_SYS linux

fread:
    GET  R0, iostream_fd      ; r0 = fd
    GET  R1, iostream_buf     ; r1 = buf
    GET  R2, iostream_count   ; r2 = count
    LDI  R7, 3               ; r7 = syscall 3 (read)
    SYS
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM64  (Linux)
;  read: X8=63, X0=fd, X1=buf, X2=count
; ---------------------------------------------------------------------------
@IF_ARCH arm64
@IF_SYS linux

fread:
    GET  R0, iostream_fd      ; X0 = fd
    GET  R1, iostream_buf     ; X1 = buf
    GET  R2, iostream_count   ; X2 = count
    LDI  R7, 63              ; X8 = read
    SYS
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  RISC-V  (Linux)
;  read: a7=63, a0=fd, a1=buf, a2=count
; ---------------------------------------------------------------------------
@IF_ARCH riscv
@IF_SYS linux

fread:
    GET  R0, iostream_fd      ; a0 = fd
    GET  R1, iostream_buf     ; a1 = buf
    GET  R2, iostream_count   ; a2 = count
    LDI  R7, 63              ; a7 = read
    SYS
    RET

@ENDIF
@ENDIF


; ===========================================================================
;                            fwrite
; ===========================================================================
;  Writes iostream_count bytes from iostream_buf to iostream_fd.
;  Returns bytes actually written in R0.
; ===========================================================================

; ---------------------------------------------------------------------------
;  x86-64  (Linux + Win32)
;  write: RAX=1, RDI=fd, RSI=buf, RDX=count
;  Win32: dispatch 1, RDI=handle, RSI=buf, RDX=count -> WriteFile
; ---------------------------------------------------------------------------
@IF_ARCH x86

fwrite:
    GET  R7, iostream_fd      ; RDI = fd / handle
    GET  R6, iostream_buf     ; RSI = buffer pointer
    GET  R2, iostream_count   ; RDX = count
    LDI  R0, 1               ; RAX = write syscall / dispatch 1
    SYS
    RET

@ENDIF

; ---------------------------------------------------------------------------
;  x86-32  (Linux)
;  write: EAX=4, EBX=fd, ECX=buf, EDX=count
; ---------------------------------------------------------------------------
@IF_ARCH x86_32
@IF_SYS linux

fwrite:
    GET  R3, iostream_fd      ; EBX = fd
    GET  R1, iostream_buf     ; ECX = buffer
    GET  R2, iostream_count   ; EDX = count
    LDI  R0, 4               ; EAX = syscall 4 (write)
    SYS
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM  (Linux)
;  write: r7=4, r0=fd, r1=buf, r2=count
; ---------------------------------------------------------------------------
@IF_ARCH arm
@IF_SYS linux

fwrite:
    GET  R0, iostream_fd      ; r0 = fd
    GET  R1, iostream_buf     ; r1 = buf
    GET  R2, iostream_count   ; r2 = count
    LDI  R7, 4               ; r7 = syscall 4 (write)
    SYS
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM64  (Linux)
;  write: X8=64, X0=fd, X1=buf, X2=count
; ---------------------------------------------------------------------------
@IF_ARCH arm64
@IF_SYS linux

fwrite:
    GET  R0, iostream_fd      ; X0 = fd
    GET  R1, iostream_buf     ; X1 = buf
    GET  R2, iostream_count   ; X2 = count
    LDI  R7, 64              ; X8 = write
    SYS
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  RISC-V  (Linux)
;  write: a7=64, a0=fd, a1=buf, a2=count
; ---------------------------------------------------------------------------
@IF_ARCH riscv
@IF_SYS linux

fwrite:
    GET  R0, iostream_fd      ; a0 = fd
    GET  R1, iostream_buf     ; a1 = buf
    GET  R2, iostream_count   ; a2 = count
    LDI  R7, 64              ; a7 = write
    SYS
    RET

@ENDIF
@ENDIF


; ===========================================================================
;                            fclose
; ===========================================================================
;  Closes the file in iostream_fd.
;  Returns status in R0 (0 = success on Linux; nonzero = success on Win32).
; ===========================================================================

; ---------------------------------------------------------------------------
;  x86-64  (Linux + Win32)
;  close: RAX=3, RDI=fd.  Win32: dispatch 3, RDI=handle -> CloseHandle
; ---------------------------------------------------------------------------
@IF_ARCH x86

fclose:
    GET  R7, iostream_fd      ; RDI = fd / handle
    LDI  R0, 3               ; RAX = close syscall / dispatch 3
    SYS
    RET

@ENDIF

; ---------------------------------------------------------------------------
;  x86-32  (Linux)
;  close: EAX=6, EBX=fd
; ---------------------------------------------------------------------------
@IF_ARCH x86_32
@IF_SYS linux

fclose:
    GET  R3, iostream_fd      ; EBX = fd
    LDI  R0, 6               ; EAX = syscall 6 (close)
    SYS
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM  (Linux)
;  close: r7=6, r0=fd
; ---------------------------------------------------------------------------
@IF_ARCH arm
@IF_SYS linux

fclose:
    GET  R0, iostream_fd      ; r0 = fd
    LDI  R7, 6               ; r7 = syscall 6 (close)
    SYS
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  ARM64  (Linux)
;  close: X8=57, X0=fd
; ---------------------------------------------------------------------------
@IF_ARCH arm64
@IF_SYS linux

fclose:
    GET  R0, iostream_fd      ; X0 = fd
    LDI  R7, 57              ; X8 = close
    SYS
    RET

@ENDIF
@ENDIF

; ---------------------------------------------------------------------------
;  RISC-V  (Linux)
;  close: a7=57, a0=fd
; ---------------------------------------------------------------------------
@IF_ARCH riscv
@IF_SYS linux

fclose:
    GET  R0, iostream_fd      ; a0 = fd
    LDI  R7, 57              ; a7 = close
    SYS
    RET

@ENDIF
@ENDIF
