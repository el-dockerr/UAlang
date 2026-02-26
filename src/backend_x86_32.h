/*
 * =============================================================================
 *  UA - Unified Assembler
 *  x86-32 Back-End (Code Generation)
 *
 *  File:    backend_x86_32.h
 *  Purpose: Public interface for the x86-32 (IA-32) code generator.
 *
 *  Register Mapping (UA virtual  ->  x86-32):
 *      R0  ->  EAX  (encoding 0)
 *      R1  ->  ECX  (encoding 1)
 *      R2  ->  EDX  (encoding 2)
 *      R3  ->  EBX  (encoding 3)
 *      R4  ->  ESP  (encoding 4)
 *      R5  ->  EBP  (encoding 5)
 *      R6  ->  ESI  (encoding 6)
 *      R7  ->  EDI  (encoding 7)
 *
 *  Supported Opcodes (full MVIS):
 *      LDI  Rx, #val   ->  MOV r32, imm32   (B8+rd id)
 *      ADD  Rx, Ry     ->  ADD r32, r32     (01 ModR/M)
 *      SUB  Rx, Ry     ->  SUB r32, r32     (29 ModR/M)
 *      AND  Rx, Ry     ->  AND r32, r32     (21 ModR/M)
 *      OR   Rx, Ry     ->  OR  r32, r32     (09 ModR/M)
 *      XOR  Rx, Ry     ->  XOR r32, r32     (31 ModR/M)
 *      NOT  Rx         ->  NOT r32          (F7 /2)
 *      MOV  Rx, Ry     ->  MOV r32, r32     (89 ModR/M)
 *      INC  Rx         ->  INC r32          (40+rd)
 *      DEC  Rx         ->  DEC r32          (48+rd)
 *      MUL  Rx, Ry     ->  IMUL r32, r32    (0F AF ModR/M)
 *      DIV  Rx, Ry     ->  CDQ + IDIV       (polyfill)
 *      SHL  Rx, #n     ->  SHL r32, imm8    (C1 /4 ib)
 *      SHR  Rx, #n     ->  SHR r32, imm8    (C1 /5 ib)
 *      CMP  Rx, Ry     ->  CMP r32, r32     (39 ModR/M)
 *      JMP  label      ->  JMP rel32        (E9 cd)
 *      JZ   label      ->  JZ  rel32        (0F 84 cd)
 *      JNZ  label      ->  JNZ rel32        (0F 85 cd)
 *      CALL label      ->  CALL rel32       (E8 cd)
 *      RET             ->  RET              (C3)
 *      PUSH Rx         ->  PUSH r32         (50+rd)
 *      POP  Rx         ->  POP  r32         (58+rd)
 *      NOP             ->  NOP              (90)
 *      HLT             ->  RET              (C3)
 *      INT  #imm       ->  INT imm8         (CD ib)
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UA_BACKEND_X86_32_H
#define UA_BACKEND_X86_32_H

#include "parser.h"
#include "codegen.h"    /* CodeBuffer, free_code_buffer, hexdump */

/* =========================================================================
 *  Public API
 * ========================================================================= */

/*
 * generate_x86_32()
 *   Translates the architecture-neutral UA IR into raw x86-32 machine code.
 *   Returns a CodeBuffer that the caller must free with free_code_buffer().
 *
 *   Only R0-R7 are supported.  Unsupported opcodes cause a diagnostic
 *   on stderr followed by exit(1).
 */
CodeBuffer* generate_x86_32(const Instruction *ir, int ir_count);

#endif /* UA_BACKEND_X86_32_H */
