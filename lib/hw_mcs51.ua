; =========================================================================
;  hw_mcs51.ua  —  Intel 8051 Standard Special Function Registers (SFRs)
; =========================================================================
;  Hardware definition library for the MCS-51 architecture.
;  Import with:  @IMPORT hw_mcs51
;
;  All symbols are replaced at compile time by the precompiler's @DEFINE
;  mechanism — no runtime RAM is consumed.
; =========================================================================
@ARCH_ONLY mcs51

; ---- Port registers -----------------------------------------------------
@DEFINE P0      0x80
@DEFINE P1      0x90
@DEFINE P2      0xA0
@DEFINE P3      0xB0

; ---- Core registers -----------------------------------------------------
@DEFINE SP      0x81
@DEFINE DPL     0x82
@DEFINE DPH     0x83
@DEFINE PCON    0x87
@DEFINE PSW     0xD0
@DEFINE ACC     0xE0
@DEFINE B       0xF0

; ---- Timer / Counter ----------------------------------------------------
@DEFINE TCON    0x88
@DEFINE TMOD    0x89
@DEFINE TL0     0x8A
@DEFINE TL1     0x8B
@DEFINE TH0     0x8C
@DEFINE TH1     0x8D

; ---- Serial port --------------------------------------------------------
@DEFINE SCON    0x98
@DEFINE SBUF    0x99

; ---- Interrupt ----------------------------------------------------------
@DEFINE IE      0xA8
@DEFINE IP      0xB8
