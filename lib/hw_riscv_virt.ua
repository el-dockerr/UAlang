; =========================================================================
;  hw_riscv_virt.ua  —  RISC-V QEMU 'virt' Machine MMIO Addresses
; =========================================================================
;  Hardware definition library for the RISC-V architecture.
;  Import with:  @IMPORT hw_riscv_virt
;
;  All symbols are replaced at compile time by the precompiler's @DEFINE
;  mechanism — no runtime RAM is consumed.
; =========================================================================
@ARCH_ONLY riscv

; ---- UART (NS16550A compatible) -----------------------------------------
@DEFINE UART0_BASE  0x10000000

; ---- UART register offsets (add to UART0_BASE) --------------------------
@DEFINE UART_RHR    0
@DEFINE UART_THR    0
@DEFINE UART_IER    1
@DEFINE UART_FCR    2
@DEFINE UART_LCR    3
@DEFINE UART_LSR    5

; ---- LSR bit masks ------------------------------------------------------
@DEFINE UART_LSR_TX_EMPTY  0x20
@DEFINE UART_LSR_RX_READY  0x01

; ---- CLINT (Core Local Interruptor) -------------------------------------
@DEFINE CLINT_BASE      0x02000000
@DEFINE CLINT_MTIMECMP  0x02004000
@DEFINE CLINT_MTIME     0x0200BFF8

; ---- PLIC (Platform-Level Interrupt Controller) -------------------------
@DEFINE PLIC_BASE       0x0C000000
