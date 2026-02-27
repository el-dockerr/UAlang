; Test @arch_only directive — this file only compiles for ARM targets
@arch_only arm, arm64

LDI R0, 42
HLT
