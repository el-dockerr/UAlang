; Test combined directives: @arch_only + @sys_only + @IF_ARCH
@arch_only x86, x86_32, arm, arm64, riscv
@sys_only  win32, linux

; Platform-specific code test
@IF_ARCH x86
    LDI R0, 64  ; 64-bit path
@ENDIF

@IF_ARCH arm
    LDI R0, 32  ; 32-bit path
@ENDIF

HLT
