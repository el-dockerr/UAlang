/*
 * =============================================================================
 *  UAS - Unified Assembler System
 *  Phase 5: x86-64 Back-End (Code Generation)
 *
 *  File:    backend_x86_64.h
 *  Purpose: Public interface for the x86-64 code generator.
 *
 *  Register Mapping (UAS virtual  ->  x86-64):
 *      R0  ->  RAX  (encoding 0)
 *      R1  ->  RCX  (encoding 1)
 *      R2  ->  RDX  (encoding 2)
 *      R3  ->  RBX  (encoding 3)
 *
 *  Supported Opcodes (Phase 5 minimal set):
 *      LDI  Rx, #val   ->  MOV r64, imm32  (REX.W C7 /0)
 *      ADD  Rx, Ry     ->  ADD r64, r64    (REX.W 01 ModR/M)
 *      MOV  Rx, Ry     ->  MOV r64, r64    (REX.W 89 ModR/M)
 *      HLT             ->  RET             (C3)
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UAS_BACKEND_X86_64_H
#define UAS_BACKEND_X86_64_H

#include "parser.h"
#include "codegen.h"    /* CodeBuffer, free_code_buffer, hexdump */

/* =========================================================================
 *  Public API
 * ========================================================================= */

/*
 * generate_x86_64()
 *   Translates the architecture-neutral UAS IR into raw x86-64 machine code.
 *   Returns a CodeBuffer that the caller must free with free_code_buffer().
 *
 *   Only R0-R3 are supported in Phase 5.  Unsupported opcodes cause a
 *   diagnostic on stderr followed by exit(1).
 */
CodeBuffer* generate_x86_64(const Instruction *ir, int ir_count);

#endif /* UAS_BACKEND_X86_64_H */
