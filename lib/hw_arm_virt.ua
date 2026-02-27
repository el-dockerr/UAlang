; =========================================================================
;  hw_arm_virt.ua  —  ARM/ARM64 QEMU 'virt' Machine MMIO Addresses
; =========================================================================
;  Hardware definition library for ARM and ARM64 architectures.
;  Import with:  @IMPORT hw_arm_virt
;
;  All symbols are replaced at compile time by the precompiler's @DEFINE
;  mechanism — no runtime RAM is consumed.
; =========================================================================
@ARCH_ONLY arm, arm64

; ---- PL011 UART ---------------------------------------------------------
@DEFINE UART0_BASE  0x09000000

; ---- UART register offsets (add to UART0_BASE) --------------------------
@DEFINE UART_DR     0x00
@DEFINE UART_FR     0x18
@DEFINE UART_IBRD   0x24
@DEFINE UART_FBRD   0x28
@DEFINE UART_LCRH   0x2C
@DEFINE UART_CR     0x30
@DEFINE UART_IMSC   0x38

; ---- FR (Flag Register) bit masks ---------------------------------------
@DEFINE UART_FR_TXFF  0x20
@DEFINE UART_FR_RXFE  0x10

; ---- GIC (Generic Interrupt Controller) ---------------------------------
@DEFINE GIC_DIST_BASE   0x08000000
@DEFINE GIC_CPU_BASE    0x08010000
