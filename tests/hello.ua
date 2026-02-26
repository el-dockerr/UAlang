; =============================================================================
;  Hello World — Phase 8 Test
;
;  Demonstrates: @IMPORT std_io, LDS, CALL print, SYS
;  Target: x86-64 Linux ELF
; =============================================================================

@IMPORT std_io

; ---  Entry point  ---
LDS R0, "Hello, World!\n"
CALL std_io.print
HLT
