/*
 * =============================================================================
 *  UA - Unified Assembler
 *  ARM Back-End (Code Generation)
 *
 *  File:    backend_arm.h
 *  Purpose: Public interface for the ARM (ARMv7-A, 32-bit) code generator.
 *
 *  Register Mapping (UA virtual  ->  ARM):
 *      R0  ->  r0   (encoding 0)
 *      R1  ->  r1   (encoding 1)
 *      R2  ->  r2   (encoding 2)
 *      R3  ->  r3   (encoding 3)
 *      R4  ->  r4   (encoding 4)
 *      R5  ->  r5   (encoding 5)
 *      R6  ->  r6   (encoding 6)
 *      R7  ->  r7   (encoding 7)
 *
 *  All instructions are 32 bits (4 bytes) in ARM mode.
 *  Condition code: AL (always, 0xE) unless otherwise specified.
 *
 *  Supported Opcodes (full MVIS):
 *      LDI  Rx, #val   ->  MOV / MOVW+MOVT            4-8 bytes
 *      MOV  Rx, Ry     ->  MOV Rd, Rm                 4 bytes
 *      LOAD Rx, Ry     ->  LDR Rd, [Rm]               4 bytes
 *      STORE Rx, Ry    ->  STR Ry, [Rx]               4 bytes
 *      ADD  Rx, Ry     ->  ADD Rd, Rd, Rm              4 bytes
 *      SUB  Rx, Ry     ->  SUB Rd, Rd, Rm              4 bytes
 *      AND  Rx, Ry     ->  AND Rd, Rd, Rm              4 bytes
 *      OR   Rx, Ry     ->  ORR Rd, Rd, Rm              4 bytes
 *      XOR  Rx, Ry     ->  EOR Rd, Rd, Rm              4 bytes
 *      NOT  Rx         ->  MVN Rd, Rd                  4 bytes
 *      INC  Rx         ->  ADD Rd, Rd, #1              4 bytes
 *      DEC  Rx         ->  SUB Rd, Rd, #1              4 bytes
 *      MUL  Rx, Ry     ->  MUL Rd, Rd, Rm              4 bytes
 *      DIV  Rx, Ry     ->  SDIV Rd, Rd, Rm (ARMv7VE)  4 bytes
 *      SHL  Rx, #n     ->  LSL Rd, Rd, #n             4 bytes
 *      SHR  Rx, #n     ->  LSR Rd, Rd, #n             4 bytes
 *      CMP  Rx, Ry     ->  CMP Rd, Rm                 4 bytes
 *      JMP  label      ->  B   label                   4 bytes
 *      JZ   label      ->  BEQ label                   4 bytes
 *      JNZ  label      ->  BNE label                   4 bytes
 *      CALL label      ->  BL  label                   4 bytes
 *      RET             ->  BX  LR                      4 bytes
 *      PUSH Rx         ->  PUSH {Rx}                   4 bytes
 *      POP  Rx         ->  POP  {Rx}                   4 bytes
 *      NOP             ->  NOP (mov r0, r0)            4 bytes
 *      HLT             ->  BX  LR                      4 bytes
 *      INT  #imm       ->  SVC #imm                    4 bytes
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UA_BACKEND_ARM_H
#define UA_BACKEND_ARM_H

#include "parser.h"
#include "codegen.h"    /* CodeBuffer, free_code_buffer, hexdump */

/* =========================================================================
 *  Public API
 * ========================================================================= */

/*
 * generate_arm()
 *   Translates the architecture-neutral UA IR into raw ARM (ARMv7-A)
 *   machine code in little-endian format.
 *   Returns a CodeBuffer that the caller must free with free_code_buffer().
 *
 *   Only R0-R7 are supported.  Unsupported opcodes cause a diagnostic
 *   on stderr followed by exit(1).
 */
CodeBuffer* generate_arm(const Instruction *ir, int ir_count);

#endif /* UA_BACKEND_ARM_H */
