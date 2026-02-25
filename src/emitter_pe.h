/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Phase 6: Windows PE (Portable Executable) Emitter
 *
 *  File:    emitter_pe.h
 *  Purpose: Public interface for emitting a minimal 64-bit Windows PE file.
 *
 *  The emitter constructs a valid PE/COFF executable from scratch using
 *  only standard C and <stdint.h>.  No Windows SDK required to build.
 *
 *  Memory layout of the generated PE:
 *
 *    File offset  0x0000 .. 0x01FF   Headers (DOS + NT + Section table)
 *    File offset  0x0200 .. end      .text section (user machine code)
 *
 *    Virtual addr 0x00400000         ImageBase
 *    Virtual addr 0x00401000         .text section  (EntryPoint)
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UA_EMITTER_PE_H
#define UA_EMITTER_PE_H

#include "codegen.h"    /* CodeBuffer */

/*
 * emit_pe_exe()
 *   Write a minimal 64-bit Windows PE executable to `filename`.
 *   The `.text` section contains the raw bytes from `code`.
 *
 *   Returns 0 on success, non-zero on failure (diagnostic on stderr).
 */
int emit_pe_exe(const char *filename, const CodeBuffer *code);

#endif /* UA_EMITTER_PE_H */
