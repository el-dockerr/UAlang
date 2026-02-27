; =========================================================================
;  8051_timer.ua  —  Bare-Metal Timer 0 Interrupt Demo (MCS-51)
; =========================================================================
;  Demonstrates the @ORG directive for placing code at fixed addresses
;  in the 8051 interrupt vector table.
;
;  Memory map:
;    0x0000  Reset vector  →  LJMP main
;    0x000B  Timer 0 ISR   →  increment tick counter, RETI
;    0x0030  main          →  configure Timer 0, loop forever
;
;  SFR addresses used:
;    TMOD  = 0x89   (Timer Mode register)
;    TH0   = 0x8C   (Timer 0 High byte)
;    TL0   = 0x8A   (Timer 0 Low byte)
;    IE    = 0xA8   (Interrupt Enable register)
;    TCON  = 0x88   (Timer Control — bit 4 = TR0)
;
;  Build:
;    uas 8051_timer.ua -arch mcs51 -o timer.bin
; =========================================================================
@ARCH_ONLY mcs51

; ---- Tick counter in internal RAM (uninitialized; set to 0 in main) --
VAR ticks

; =========================================================================
;  Reset Vector (address 0x0000)
; =========================================================================
@ORG 0x0000
    JMP main

; =========================================================================
;  Timer 0 Interrupt Service Routine (address 0x000B)
; =========================================================================
@ORG 0x000B
timer0_isr:
    PUSH R0                 ; save context

    GET  R0, ticks          ; R0 = ticks
    ADD  R0, 1              ; R0++
    SET  ticks, R0          ; ticks = R0

    POP  R0                 ; restore context
    RETI                    ; return from interrupt

; =========================================================================
;  Main Program (address 0x0030)
; =========================================================================
@ORG 0x0030
main:
    ; ---- Initialize tick counter to 0 --------------------------------
    LDI  R0, 0
    SET  ticks, R0

    ; ---- Configure Timer 0 in Mode 1 (16-bit timer) ------------------
    ;  TMOD = 0x01  →  Gate=0, C/T=0, M1:M0=01 (16-bit)
    LDI  R0, 0x01
    LDI  R1, 0x89          ; R1 = TMOD address
    STORE R0, R1            ; TMOD = 0x01

    ; ---- Set Timer 0 reload value for ~50 ms @ 12 MHz ----------------
    ;  50 ms @ 12 MHz = 50000 cycles  →  65536 - 50000 = 15536 = 0x3CB0
    ;  TH0 = 0x3C, TL0 = 0xB0
    LDI  R0, 0x3C
    LDI  R1, 0x8C          ; R1 = TH0 address
    STORE R0, R1            ; TH0 = 0x3C
    LDI  R0, 0xB0
    LDI  R1, 0x8A          ; R1 = TL0 address
    STORE R0, R1            ; TL0 = 0xB0

    ; ---- Enable Timer 0 interrupt  (IE = 0x82) -----------------------
    ;  IE bit 7 = EA (global enable), bit 1 = ET0 (Timer 0 enable)
    LDI  R0, 0x82
    LDI  R1, 0xA8          ; R1 = IE address
    STORE R0, R1            ; IE = 0x82

    ; ---- Start Timer 0  (set TR0 bit in TCON) ------------------------
    ;  TCON bit 4 = TR0 → SETB on bit-addressable address 0x8C
    ;  Bit address of TR0 = TCON.4 = 0x88 + 4 = 0x8C
    SETB R4                 ; sets bit at address 4 (TR0 = TCON.4)

    ; ---- Infinite loop — interrupts do the work ----------------------
idle:
    NOP
    JMP  idle

; =========================================================================
;  End of bare-metal firmware
; =========================================================================
