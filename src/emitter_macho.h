/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Mach-O Emitter  (64-bit, ARM64 / Apple Silicon)
 *
 *  File:    emitter_macho.h
 *  Purpose: Public interface for emitting a minimal 64-bit macOS Mach-O
 *           executable from a raw AArch64 machine-code buffer.
 *
 *  The emitter constructs a valid Mach-O 64-bit executable from scratch
 *  using only standard C and <stdint.h>.  No macOS SDK headers required.
 *
 *  Memory layout of the generated Mach-O:
 *
 *    File offset    Content
 *    ───────────    ─────────────────────────────────────
 *    0x0000         Mach-O header  (mach_header_64: 32 bytes)
 *    0x0020         LC_SEGMENT_64  __PAGEZERO (72 bytes)
 *    0x0068         LC_SEGMENT_64  __TEXT      (72 + 80 = 152 bytes)
 *    0x0100         LC_MAIN        (24 bytes)
 *    0x0118         LC_DYLINKER    (variable, padded to 8-byte alignment)
 *    aligned        __text section  (user machine code + exit stub)
 *
 *    Virtual addr   0x100000000    Base address (__TEXT segment)
 *
 *  Supported targets:
 *    - macOS on Apple Silicon (M1/M2/M3/M4) — ARM64 / AArch64
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UA_EMITTER_MACHO_H
#define UA_EMITTER_MACHO_H

#include "codegen.h"    /* CodeBuffer */

/*
 * emit_macho_exe()
 *
 *   Writes a minimal Mach-O 64-bit executable to disk.
 *
 *   Parameters:
 *     filename – output file path
 *     code     – raw AArch64 machine code from the ARM64 backend
 *
 *   Returns:
 *     0 on success, non-zero on failure.
 *
 *   The generated executable contains:
 *     - A __PAGEZERO segment (guard page, no file backing)
 *     - A __TEXT segment with a __text section holding the machine code
 *     - LC_MAIN specifying the entry point offset
 *     - A small exit stub that calls _exit() via a syscall
 *
 *   On Apple Silicon / macOS, the entry convention for LC_MAIN is:
 *     - Dyld calls the entry point as a function returning int.
 *     - X0 = return value (like main's return).
 *     - The exit stub performs:
 *         MOV X16, #1          ; SYS_exit
 *         SVC #0x80            ; supervisor call
 */
int emit_macho_exe(const char *filename, const CodeBuffer *code);

#endif /* UA_EMITTER_MACHO_H */
