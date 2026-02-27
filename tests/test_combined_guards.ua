; Test combined directives: @ARCH_ONLY + @SYS_ONLY + @IF_ARCH
@ARCH_ONLY x86, x86_32, arm, arm64, riscv
@SYS_ONLY  win32, linux

; Platform-specific code test
@IF_ARCH x86
    LDI R0, 64  ; 64-bit path
@ENDIF

@IF_ARCH arm
    LDI R0, 32  ; 32-bit path
@ENDIF

HLT
