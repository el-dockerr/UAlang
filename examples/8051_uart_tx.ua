; =========================================================================
;  8051_uart_tx.ua  —  Bare-Metal 8051 UART Transmit Demo
; =========================================================================
;  Transmits the character 'A' (0x41) over the 8051 serial port (UART).
;
;  Demonstrates the @DEFINE macro system:
;    - SFR names (SCON, SBUF, TMOD, TH1, ...) are replaced at compile
;      time with their numeric addresses via @IMPORT hw_mcs51.
;    - Zero runtime RAM is wasted on hardware register definitions.
;
;  UART setup:  Mode 1 (8-bit, variable baud), Timer 1 as baud generator
;               9600 baud @ 11.0592 MHz → TH1 = 0xFD
;
;  Build:
;    uas examples/8051_uart_tx.ua -arch mcs51 -o uart_tx.bin
; =========================================================================
@ARCH_ONLY mcs51
@IMPORT hw_mcs51

; =========================================================================
;  Reset Vector
; =========================================================================
@ORG 0x0000
    JMP main

; =========================================================================
;  Main Program
; =========================================================================
@ORG 0x0030
main:
    ; ---- Configure Timer 1 as baud rate generator --------------------
    ;  TMOD = 0x20  →  Timer 1 in Mode 2 (8-bit auto-reload)
    ;  After macro expansion: LDI R0, 0x20 / LDI R1, 0x89 / STORE R0,R1
    LDI  R0, 0x20
    LDI  R1, TMOD
    STORE R0, R1

    ; ---- Set baud rate: TH1 = 0xFD (9600 baud @ 11.0592 MHz) --------
    LDI  R0, 0xFD
    LDI  R1, TH1
    STORE R0, R1

    ; ---- Configure serial port: SCON = 0x50 (Mode 1, REN enabled) ----
    LDI  R0, 0x50
    LDI  R1, SCON
    STORE R0, R1

    ; ---- Start Timer 1: set TR1 bit in TCON --------------------------
    ;  TR1 = TCON.6 → bit address 0x8E  (TCON base 0x88 + bit 6)
    SETB R6

    ; ---- Transmit character 'A' (0x41) via SBUF ----------------------
    LDI  R0, 0x41
    LDI  R1, SBUF
    STORE R0, R1

    ; ---- Wait for transmission complete (poll TI flag) ----------------
    ;  TI = SCON.1 → bit address 0x99  (SCON base 0x98 + bit 1)
wait_ti:
    NOP
    JMP  wait_ti

    ; ---- Done — halt -------------------------------------------------
    HLT
