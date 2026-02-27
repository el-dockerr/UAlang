; =========================================================================
;  hw_x86_pc.ua  —  Standard x86 PC I/O Port Addresses
; =========================================================================
;  Hardware definition library for x86 and x86_32 architectures.
;  Import with:  @IMPORT hw_x86_pc
;
;  All symbols are replaced at compile time by the precompiler's @DEFINE
;  mechanism — no runtime RAM is consumed.
; =========================================================================
@ARCH_ONLY x86, x86_32

; ---- Serial ports (COM) ------------------------------------------------
@DEFINE PORT_COM1       0x3F8
@DEFINE PORT_COM2       0x2F8
@DEFINE PORT_COM3       0x3E8
@DEFINE PORT_COM4       0x2E8

; ---- VGA / Display ------------------------------------------------------
@DEFINE PORT_VGA_CMD    0x3D4
@DEFINE PORT_VGA_DATA   0x3D5

; ---- Keyboard controller ------------------------------------------------
@DEFINE PORT_KEYBOARD   0x60

; ---- PIC (Programmable Interrupt Controller) ----------------------------
@DEFINE PORT_PIC1_CMD   0x20
@DEFINE PORT_PIC1_DATA  0x21
@DEFINE PORT_PIC2_CMD   0xA0
@DEFINE PORT_PIC2_DATA  0xA1

; ---- PIT (Programmable Interval Timer) ----------------------------------
@DEFINE PORT_PIT_CH0    0x40
@DEFINE PORT_PIT_CMD    0x43
