; Test @ARCH_ONLY directive — this file only compiles for ARM targets
@ARCH_ONLY arm, arm64

LDI R0, 42
HLT
