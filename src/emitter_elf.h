/*
 * =============================================================================
 *  UA - Unified Assembler
 *  ELF (Executable and Linkable Format) Emitter
 *
 *  File:    emitter_elf.h
 *  Purpose: Public interface for emitting a minimal 64-bit Linux ELF
 *           executable from a raw x86-64 machine-code buffer.
 *
 *  The emitter constructs a valid ELF64 executable from scratch using
 *  only standard C and <stdint.h>.  No Linux headers required to build.
 *
 *  Memory layout of the generated ELF:
 *
 *    File offset  0x0000 .. 0x0077   ELF header   (64 bytes)
 *    File offset  0x0040 .. 0x00B7   Program header (56 bytes, LOAD)
 *    File offset  0x0078 ..          .text segment (user machine code)
 *
 *    Virtual addr 0x00400000         Base address
 *    Virtual addr 0x00400078         Entry point  (.text)
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UA_EMITTER_ELF_H
#define UA_EMITTER_ELF_H

#include "codegen.h"    /* CodeBuffer */

/*
 * emit_elf_exe()
 *
 *   Build a minimal 64-bit Linux ELF executable from a raw machine-code
 *   buffer and write it to 'filename'.
 *
 *   The ELF contains an ELF64 header plus a single PT_LOAD program header
 *   that maps the code as read+execute at virtual address 0x00400078.
 *   The generated code must end with a proper exit sequence (e.g. syscall).
 *
 *   Returns 0 on success, non-zero on error (diagnostics to stderr).
 */
int emit_elf_exe(const char *filename, const CodeBuffer *code);

#endif /* UA_EMITTER_ELF_H */
