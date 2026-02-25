/*
 * =============================================================================
 *  UA - Unified Assembler
 *  ELF (Executable and Linkable Format) Emitter
 *
 *  File:    emitter_elf.c
 *  Purpose: Build a minimal but valid 64-bit Linux ELF executable from
 *           a raw x86-64 machine-code buffer.  Zero external dependencies —
 *           all ELF structures are defined inline with <stdint.h>.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  ELF Layout (as generated)                                         │
 *  │                                                                    │
 *  │  File offset   Size   Content                                      │
 *  │  ────────────  ─────  ────────────────────────────────────          │
 *  │  0x0000        64     ELF64 header (e_ident + fields)              │
 *  │  0x0040        56     Program header (PT_LOAD)                     │
 *  │  0x0078        code   .text (raw machine code)                     │
 *  │  + epilogue    12     exit(R0) syscall stub                        │
 *  └──────────────────────────────────────────────────────────────────────┘
 *  │                                                                    │
 *  │  The emitter appends a Linux exit syscall after the user code so   │
 *  │  that HLT (RET = 0xC3) branches into:                             │
 *  │                                                                    │
 *  │    mov  rdi, rax       ; exit code = R0                            │
 *  │    mov  eax, 60        ; __NR_exit                                 │
 *  │    syscall                                                         │
 *  │                                                                    │
 *  │  The entry point calls the user code, then falls through to the    │
 *  │  exit stub.                                                        │
 *  │                                                                    │
 *  │  Entry stub (5 bytes):                                             │
 *  │    call  user_code     ; rel32 to user code start                  │
 *  │                                                                    │
 *  │  So the full layout in the segment is:                             │
 *  │    [CALL stub (5)] [user code ...] [exit stub (12)]                │
 *  │                                                                    │
 *  │  This way the user's HLT → RET returns from the CALL, and         │
 *  │  execution falls through to the exit syscall with RAX intact.      │
 *  │                                                                    │
 *  │  Constants:                                                        │
 *  │    BaseAddress = 0x00400000                                        │
 *  │    HeaderSize  = 0x78  (ELF hdr + 1 phdr = 64 + 56 = 120)         │
 *  │    EntryPoint  = BaseAddress + HeaderSize                          │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  License: MIT
 * =============================================================================
 */

#include "emitter_elf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* =========================================================================
 *  ELF Constants
 * ========================================================================= */

/* e_ident indices */
#define EI_MAG0         0
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4
#define EI_DATA         5
#define EI_VERSION      6
#define EI_OSABI        7
#define EI_ABIVERSION   8
/* 9..15 = padding */
#define EI_NIDENT       16

/* e_ident values */
#define ELFMAG0         0x7F
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'
#define ELFCLASS64      2
#define ELFDATA2LSB     1       /* little-endian */
#define EV_CURRENT      1

/* e_type */
#define ET_EXEC         2       /* executable */

/* e_machine */
#define EM_X86_64       62

/* p_type */
#define PT_LOAD         1

/* p_flags */
#define PF_X            1       /* execute */
#define PF_W            2       /* write */
#define PF_R            4       /* read */

/* Layout constants */
#define ELF_BASE_ADDR       0x00400000ULL
#define ELF_EHDR_SIZE       64      /* sizeof(Elf64_Ehdr) */
#define ELF_PHDR_SIZE       56      /* sizeof(Elf64_Phdr) */
#define ELF_HEADER_SIZE     (ELF_EHDR_SIZE + ELF_PHDR_SIZE)  /* 120 = 0x78 */

/* Exit stub:
 *   48 89 C7          mov rdi, rax        (3 bytes)
 *   B8 3C 00 00 00    mov eax, 60         (5 bytes)
 *   0F 05             syscall             (2 bytes)
 *   EB FE             jmp $  (safety)     (2 bytes)
 *                                  total: 12 bytes
 */
#define ELF_EXIT_STUB_SIZE  12

/* Call stub:
 *   E8 xx xx xx xx    call rel32          (5 bytes)
 */
#define ELF_CALL_STUB_SIZE  5

/* =========================================================================
 *  Little-endian serialisers
 * ========================================================================= */
static void elf_write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)( v        & 0xFF);
    p[1] = (uint8_t)((v >>  8) & 0xFF);
}

static void elf_write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)( v        & 0xFF);
    p[1] = (uint8_t)((v >>  8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void elf_write_le64(uint8_t *p, uint64_t v)
{
    elf_write_le32(p,     (uint32_t)(v & 0xFFFFFFFF));
    elf_write_le32(p + 4, (uint32_t)(v >> 32));
}

/* =========================================================================
 *  emit_elf_exe()
 * ========================================================================= */
int emit_elf_exe(const char *filename, const CodeBuffer *code)
{
    if (!code || code->size == 0) {
        fprintf(stderr, "ELF emitter: no code to emit.\n");
        return 1;
    }

    /* ---- Compute sizes ------------------------------------------------ */
    uint32_t user_code_size = (uint32_t)code->size;
    uint32_t segment_size   = ELF_CALL_STUB_SIZE + user_code_size
                            + ELF_EXIT_STUB_SIZE;
    uint32_t total_file_size = ELF_HEADER_SIZE + segment_size;

    uint64_t entry_vaddr    = ELF_BASE_ADDR + ELF_HEADER_SIZE;

    fprintf(stderr, "[ELF] User code size   : %u bytes\n", user_code_size);
    fprintf(stderr, "[ELF] Segment size     : %u bytes (stub + code + exit)\n",
            segment_size);
    fprintf(stderr, "[ELF] Entry point      : 0x%llX\n",
            (unsigned long long)entry_vaddr);
    fprintf(stderr, "[ELF] Total file size  : %u bytes\n", total_file_size);

    /* ---- Allocate a zero-filled file image ----------------------------- */
    uint8_t *img = (uint8_t *)calloc(1, total_file_size);
    if (!img) {
        fprintf(stderr, "ELF emitter: out of memory.\n");
        return 1;
    }

    /* ====================================================================
     *  ELF64 Header  (64 bytes at offset 0x0000)
     *
     *  typedef struct {
     *      unsigned char e_ident[16];
     *      uint16_t      e_type;
     *      uint16_t      e_machine;
     *      uint32_t      e_version;
     *      uint64_t      e_entry;
     *      uint64_t      e_phoff;
     *      uint64_t      e_shoff;
     *      uint32_t      e_flags;
     *      uint16_t      e_ehsize;
     *      uint16_t      e_phentsize;
     *      uint16_t      e_phnum;
     *      uint16_t      e_shentsize;
     *      uint16_t      e_shnum;
     *      uint16_t      e_shstrndx;
     *  } Elf64_Ehdr;
     * ==================================================================== */
    uint8_t *eh = img;

    /* e_ident */
    eh[EI_MAG0]       = ELFMAG0;
    eh[EI_MAG1]       = ELFMAG1;
    eh[EI_MAG2]       = ELFMAG2;
    eh[EI_MAG3]       = ELFMAG3;
    eh[EI_CLASS]      = ELFCLASS64;
    eh[EI_DATA]       = ELFDATA2LSB;
    eh[EI_VERSION]    = EV_CURRENT;
    eh[EI_OSABI]      = 0;          /* ELFOSABI_NONE (System V) */
    eh[EI_ABIVERSION] = 0;
    /* bytes 9..15 are zero (padding) */

    elf_write_le16(eh + 16, ET_EXEC);               /* e_type       */
    elf_write_le16(eh + 18, EM_X86_64);              /* e_machine    */
    elf_write_le32(eh + 20, EV_CURRENT);             /* e_version    */
    elf_write_le64(eh + 24, entry_vaddr);            /* e_entry      */
    elf_write_le64(eh + 32, (uint64_t)ELF_EHDR_SIZE); /* e_phoff    */
    elf_write_le64(eh + 40, 0);                      /* e_shoff (none) */
    elf_write_le32(eh + 48, 0);                      /* e_flags      */
    elf_write_le16(eh + 52, ELF_EHDR_SIZE);          /* e_ehsize     */
    elf_write_le16(eh + 54, ELF_PHDR_SIZE);          /* e_phentsize  */
    elf_write_le16(eh + 56, 1);                      /* e_phnum      */
    elf_write_le16(eh + 58, 0);                      /* e_shentsize  */
    elf_write_le16(eh + 60, 0);                      /* e_shnum      */
    elf_write_le16(eh + 62, 0);                      /* e_shstrndx (SHN_UNDEF) */

    /* ====================================================================
     *  Program Header (PT_LOAD)  (56 bytes at offset 0x0040)
     *
     *  Maps the entire file (headers + code) as a single read+execute
     *  segment.  This is the simplest valid layout.
     *
     *  typedef struct {
     *      uint32_t p_type;
     *      uint32_t p_flags;
     *      uint64_t p_offset;
     *      uint64_t p_vaddr;
     *      uint64_t p_paddr;
     *      uint64_t p_filesz;
     *      uint64_t p_memsz;
     *      uint64_t p_align;
     *  } Elf64_Phdr;
     * ==================================================================== */
    uint8_t *ph = img + ELF_EHDR_SIZE;

    elf_write_le32(ph +  0, PT_LOAD);                /* p_type       */
    elf_write_le32(ph +  4, PF_R | PF_X);            /* p_flags      */
    elf_write_le64(ph +  8, 0);                      /* p_offset (whole file) */
    elf_write_le64(ph + 16, ELF_BASE_ADDR);          /* p_vaddr      */
    elf_write_le64(ph + 24, ELF_BASE_ADDR);          /* p_paddr      */
    elf_write_le64(ph + 32, (uint64_t)total_file_size); /* p_filesz  */
    elf_write_le64(ph + 40, (uint64_t)total_file_size); /* p_memsz   */
    elf_write_le64(ph + 48, 0x200000ULL);            /* p_align (2 MB) */

    /* ====================================================================
     *  Segment Data  (at file offset 0x0078)
     *
     *  Layout:
     *    [0]    CALL stub (5 bytes) — calls user code
     *    [5]    User machine code
     *    [5+N]  Exit stub (12 bytes) — sys_exit(rax)
     *
     *  The user's HLT → RET returns from the CALL, and execution falls
     *  through to the exit stub.
     * ==================================================================== */
    uint8_t *seg = img + ELF_HEADER_SIZE;

    /* ---- Call stub: E8 <rel32> ---------------------------------------- */
    /* Call target = user code start, which is 5 bytes after the CALL.
     * But since CALL rel32 is relative to the *next* instruction,
     * the rel32 offset is 0 (user code immediately follows the stub). */
    seg[0] = 0xE8;              /* CALL rel32 */
    elf_write_le32(seg + 1, 0); /* rel32 = 0 → next instruction = user code */

    /* ---- User code ---------------------------------------------------- */
    memcpy(seg + ELF_CALL_STUB_SIZE, code->bytes, user_code_size);

    /* ---- Exit stub ---------------------------------------------------- */
    uint8_t *ex = seg + ELF_CALL_STUB_SIZE + user_code_size;

    /* mov rdi, rax  (48 89 C7) */
    ex[0] = 0x48;
    ex[1] = 0x89;
    ex[2] = 0xC7;

    /* mov eax, 60   (B8 3C 00 00 00) — __NR_exit */
    ex[3] = 0xB8;
    elf_write_le32(ex + 4, 60);

    /* syscall       (0F 05) */
    ex[8] = 0x0F;
    ex[9] = 0x05;

    /* jmp $         (EB FE) — infinite loop as safety net */
    ex[10] = 0xEB;
    ex[11] = 0xFE;

    /* ====================================================================
     *  Write file
     * ==================================================================== */
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "ELF emitter: cannot open '%s' for writing: ", filename);
        perror(NULL);
        free(img);
        return 1;
    }

    size_t written = fwrite(img, 1, total_file_size, fp);
    fclose(fp);
    free(img);

    if (written != total_file_size) {
        fprintf(stderr, "ELF emitter: short write (%zu of %u bytes).\n",
                written, total_file_size);
        return 1;
    }

    fprintf(stderr, "[ELF] Wrote %u bytes to %s\n", total_file_size, filename);
    return 0;
}
