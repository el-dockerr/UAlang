; =============================================================================
; Test: Precompiler directives
; Build with: ua test_precompiler.ua -arch x86 -sys win32
; =============================================================================

; --- @IF_ARCH x86 (should be included when -arch x86) ---
@IF_ARCH x86
    LDI R0, 0x10       ; x86 only
@ENDIF

; --- @IF_ARCH arm (should be excluded when -arch x86) ---
@IF_ARCH arm
    LDI R0, 0xFF       ; ARM only — should NOT appear
@ENDIF

; --- @IF_SYS win32 (should be included when -sys win32) ---
@IF_SYS win32
    LDI R1, 0x40       ; Windows-specific
@ENDIF

; --- @IF_SYS linux (should be excluded when -sys win32) ---
@IF_SYS linux
    LDI R2, 0xDD       ; Linux-specific — should NOT appear
@ENDIF

; --- Nested conditionals ---
@IF_ARCH x86
    LDI R3, 0x01       ; x86 outer
    @IF_SYS win32
        LDI R4, 0x02   ; x86 + win32 inner
    @ENDIF
    @IF_SYS linux
        LDI R5, 0x03   ; x86 + linux — should NOT appear
    @ENDIF
@ENDIF

; --- @DUMMY ---
@DUMMY This function is not yet implemented
@DUMMY

; --- Common code (always included) ---
LDI R0, 42
HLT
