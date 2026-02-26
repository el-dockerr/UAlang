/*
 * =============================================================================
 *  UA - Unified Assembler
 *  RISC-V Back-End (Code Generation)
 *
 *  File:    backend_risc_v.h
 *  Purpose: Public interface for the RISC-V (RV32I/RV64I) code generator.
 *
 *  Register Mapping (UA virtual  ->  RISC-V):
 *      R0  ->  x10  (a0)    (encoding 10)
 *      R1  ->  x11  (a1)    (encoding 11)
 *      R2  ->  x12  (a2)    (encoding 12)
 *      R3  ->  x13  (a3)    (encoding 13)
 *      R4  ->  x14  (a4)    (encoding 14)
 *      R5  ->  x15  (a5)    (encoding 15)
 *      R6  ->  x16  (a6)    (encoding 16)
 *      R7  ->  x17  (a7)    (encoding 17)
 *
 *  Scratch registers:
 *      t0  ->  x5   (temporary, used for immediates / scratch)
 *      t1  ->  x6   (temporary, secondary scratch)
 *
 *  All instructions are 32 bits (4 bytes), little-endian.
 *
 *  Supported Opcodes (full MVIS):
 *      LDI  Rx, #val   ->  LUI+ADDI / ADDI           4-8 bytes
 *      MOV  Rx, Ry     ->  ADDI Rd, Rs, 0             4 bytes
 *      LOAD Rx, Ry     ->  LD/LW Rd, 0(Rs)            4 bytes
 *      STORE Rx, Ry    ->  SD/SW Rs, 0(Rx)             4 bytes
 *      ADD  Rx, Ry     ->  ADD  Rd, Rd, Rm             4 bytes
 *      SUB  Rx, Ry     ->  SUB  Rd, Rd, Rm             4 bytes
 *      AND  Rx, Ry     ->  AND  Rd, Rd, Rm             4 bytes
 *      OR   Rx, Ry     ->  OR   Rd, Rd, Rm             4 bytes
 *      XOR  Rx, Ry     ->  XOR  Rd, Rd, Rm             4 bytes
 *      NOT  Rx         ->  XORI Rd, Rd, -1             4 bytes
 *      INC  Rx         ->  ADDI Rd, Rd, 1              4 bytes
 *      DEC  Rx         ->  ADDI Rd, Rd, -1             4 bytes
 *      MUL  Rx, Ry     ->  MUL  Rd, Rd, Rm  (RV32M)   4 bytes
 *      DIV  Rx, Ry     ->  DIV  Rd, Rd, Rm  (RV32M)   4 bytes
 *      SHL  Rx, #n     ->  SLLI Rd, Rd, #n             4 bytes
 *      SHR  Rx, #n     ->  SRLI Rd, Rd, #n             4 bytes
 *      CMP  Rx, Ry     ->  SUB  t0, Rd, Rm (flag-sim)  4 bytes
 *      JMP  label      ->  JAL  x0, offset             4 bytes
 *      JZ   label      ->  BEQ  t0, x0, offset         4 bytes
 *      JNZ  label      ->  BNE  t0, x0, offset         4 bytes
 *      CALL label      ->  JAL  ra, offset             4 bytes
 *      RET             ->  JALR x0, ra, 0              4 bytes
 *      PUSH Rx         ->  ADDI sp, sp, -8; SD Rx, 0(sp) 8 bytes
 *      POP  Rx         ->  LD Rx, 0(sp); ADDI sp, sp, 8  8 bytes
 *      NOP             ->  ADDI x0, x0, 0              4 bytes
 *      HLT             ->  JALR x0, ra, 0 (RET)        4 bytes
 *      INT  #imm       ->  ECALL                        4 bytes
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UA_BACKEND_RISC_V_H
#define UA_BACKEND_RISC_V_H

#include "parser.h"
#include "codegen.h"    /* CodeBuffer, free_code_buffer, hexdump */

/* =========================================================================
 *  Public API
 * ========================================================================= */

/*
 * generate_risc_v()
 *   Translates the architecture-neutral UA IR into raw RISC-V machine code.
 *   Returns a CodeBuffer that the caller must free with free_code_buffer().
 *
 *   Generates RV64I + RV64M instructions (64-bit base integer + multiply).
 *   Uses the standard RISC-V calling convention for register allocation.
 */
CodeBuffer* generate_risc_v(const Instruction *ir, int ir_count);

#endif /* UA_BACKEND_RISC_V_H */
