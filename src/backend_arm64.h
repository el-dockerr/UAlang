/*
 * =============================================================================
 *  UA - Unified Assembler
 *  ARM64 (AArch64) Back-End (Code Generation)
 *
 *  File:    backend_arm64.h
 *  Purpose: Public interface for the ARM64 / AArch64 code generator.
 *
 *  Register Mapping (UA virtual  ->  AArch64):
 *      R0  ->  X0   (encoding 0)
 *      R1  ->  X1   (encoding 1)
 *      R2  ->  X2   (encoding 2)
 *      R3  ->  X3   (encoding 3)
 *      R4  ->  X4   (encoding 4)
 *      R5  ->  X5   (encoding 5)
 *      R6  ->  X6   (encoding 6)
 *      R7  ->  X7   (encoding 7)
 *
 *  Scratch registers:
 *      X9   — temporary / scratch
 *      X10  — secondary scratch (for SET imm)
 *
 *  All instructions are 32 bits (4 bytes), little-endian.
 *
 *  Supported Opcodes (full MVIS):
 *      LDI  Rx, #val   ->  MOVZ / MOVZ+MOVK           4-8 bytes
 *      MOV  Rx, Ry     ->  MOV  Xd, Xn                 4 bytes
 *      LOAD Rx, Ry     ->  LDR  Xd, [Xn]               4 bytes
 *      STORE Rx, Ry    ->  STR  Xn, [Xd]               4 bytes
 *      ADD  Rx, Ry     ->  ADD  Xd, Xd, Xm             4 bytes
 *      SUB  Rx, Ry     ->  SUB  Xd, Xd, Xm             4 bytes
 *      AND  Rx, Ry     ->  AND  Xd, Xd, Xm             4 bytes
 *      OR   Rx, Ry     ->  ORR  Xd, Xd, Xm             4 bytes
 *      XOR  Rx, Ry     ->  EOR  Xd, Xd, Xm             4 bytes
 *      NOT  Rx         ->  MVN  Xd, Xd                  4 bytes
 *      INC  Rx         ->  ADD  Xd, Xd, #1              4 bytes
 *      DEC  Rx         ->  SUB  Xd, Xd, #1              4 bytes
 *      MUL  Rx, Ry     ->  MUL  Xd, Xd, Xm             4 bytes
 *      DIV  Rx, Ry     ->  SDIV Xd, Xd, Xm             4 bytes
 *      SHL  Rx, #n     ->  LSL  Xd, Xd, #n             4 bytes
 *      SHR  Rx, #n     ->  LSR  Xd, Xd, #n             4 bytes
 *      CMP  Rx, Ry     ->  SUBS XZR, Xd, Xm            4 bytes
 *      JMP  label      ->  B    label                   4 bytes
 *      JZ   label      ->  B.EQ label                   4 bytes
 *      JNZ  label      ->  B.NE label                   4 bytes
 *      CALL label      ->  BL   label                   4 bytes
 *      RET             ->  RET                          4 bytes
 *      PUSH Rx         ->  STR  Xd, [SP, #-16]!        4 bytes
 *      POP  Rx         ->  LDR  Xd, [SP], #16          4 bytes
 *      NOP             ->  NOP                          4 bytes
 *      HLT             ->  RET                          4 bytes
 *      INT  #imm       ->  SVC  #imm                    4 bytes
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UA_BACKEND_ARM64_H
#define UA_BACKEND_ARM64_H

#include "parser.h"
#include "codegen.h"    /* CodeBuffer, free_code_buffer, hexdump */

/* =========================================================================
 *  Public API
 * ========================================================================= */

/*
 * generate_arm64()
 *   Translates the architecture-neutral UA IR into raw AArch64 machine code.
 *   Returns a CodeBuffer that the caller must free with free_code_buffer().
 *
 *   Generates ARMv8-A (AArch64) 64-bit instructions.
 *   Compatible with Apple Silicon (M1/M2/M3/M4) and all AArch64 processors.
 */
CodeBuffer* generate_arm64(const Instruction *ir, int ir_count);

#endif /* UA_BACKEND_ARM64_H */
