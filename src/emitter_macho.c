/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Mach-O Emitter  (64-bit, ARM64 / Apple Silicon)
 *
 *  File:    emitter_macho.c
 *  Purpose: Build a minimal but valid 64-bit macOS Mach-O executable from
 *           a raw AArch64 machine-code buffer.  Zero external dependencies —
 *           all Mach-O structures are serialized manually with <stdint.h>.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  Mach-O Layout (as generated)                                      │
 *  │                                                                    │
 *  │  File offset     Content                                           │
 *  │  ───────────     ──────────────────────────────────────             │
 *  │  0x0000          mach_header_64           (32 bytes)               │
 *  │  0x0020          LC_SEGMENT_64 __PAGEZERO (72 bytes)               │
 *  │  0x0068          LC_SEGMENT_64 __TEXT     (72 + 80 = 152 bytes)    │
 *  │  0x0100          LC_MAIN                  (24 bytes)               │
 *  │  <page-aligned>  __text section content:                           │
 *  │                    [BL user_code]  (4 bytes)  – call stub          │
 *  │                    [user code ...]             – backend output    │
 *  │                    [exit stub]     (16 bytes)  – SYS_exit(X0)      │
 *  │                                                                    │
 *  │  macOS AArch64 exit syscall:                                       │
 *  │    MOV X16, #1     ; SYS_exit                                      │
 *  │    SVC #0x80       ; supervisor call (macOS convention)             │
 *  │    BRK #0          ; safety trap                                   │
 *  │    BRK #0          ; alignment padding                             │
 *  │                                                                    │
 *  │  Call stub:                                                        │
 *  │    BL +1           ; branch-with-link to user code (rel offset)    │
 *  │    → falls through to exit stub on return                          │
 *  │                                                                    │
 *  │  This way the user's HLT → RET returns from the BL, and           │
 *  │  execution falls through to the exit stub with X0 intact.          │
 *  │                                                                    │
 *  │  Virtual address base: 0x100000000  (standard macOS 64-bit)        │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  License: MIT
 * =============================================================================
 */

#include "emitter_macho.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* =========================================================================
 *  Mach-O Constants  (from <mach-o/loader.h>, defined inline to avoid
 *  dependency on macOS SDK)
 * ========================================================================= */

/* Magic numbers */
#define MH_MAGIC_64         0xFEEDFACFu   /* 64-bit Mach-O, native endian */

/* File types */
#define MH_EXECUTE          2             /* demand paged executable */

/* CPU types */
#define CPU_TYPE_ARM64      0x0100000Cu   /* (CPU_TYPE_ARM | CPU_ARCH_ABI64) */
#define CPU_SUBTYPE_ARM64_ALL  0x00000000u

/* Flags */
#define MH_NOUNDEFS         0x00000001u   /* no undefined references */
#define MH_PIE              0x00200000u   /* position independent */

/* Load command types */
#define LC_SEGMENT_64       0x19u
#define LC_MAIN             0x80000028u   /* (0x28 | LC_REQ_DYLD) */
#define LC_LOAD_DYLINKER    0x0Eu

/* VM protection flags */
#define VM_PROT_NONE        0x00
#define VM_PROT_READ        0x01
#define VM_PROT_WRITE       0x02
#define VM_PROT_EXECUTE     0x04

/* Section type */
#define S_REGULAR                   0x00000000u
#define S_ATTR_PURE_INSTRUCTIONS    0x80000000u
#define S_ATTR_SOME_INSTRUCTIONS    0x00000400u

/* Page size (Apple Silicon uses 16 KB pages) */
#define MACHO_PAGE_SIZE     0x4000u       /* 16384 */

/* Base virtual address for macOS 64-bit executables */
#define MACHO_BASE_ADDR     0x100000000ULL

/* =========================================================================
 *  Mach-O structure sizes  (not using structs to avoid padding issues)
 * ========================================================================= */
#define MACH_HEADER_64_SIZE     32
#define SEGMENT_CMD_64_SIZE     72
#define SECTION_64_SIZE         80
#define LC_MAIN_SIZE            24

/* =========================================================================
 *  Exit stub for AArch64 / macOS:
 *
 *    MOV X16, #1      → 0xD2800030  (syscall number for SYS_exit)
 *    SVC #0x80        → 0xD4001001  (macOS supervisor call convention)
 *    BRK #0           → 0xD4200000  (safety trap)
 *    BRK #0           → 0xD4200000  (padding to 16 bytes)
 *
 *  X0 holds the exit code (= user's R0).
 * ========================================================================= */
#define MACHO_EXIT_STUB_SIZE   16
static const uint8_t MACHO_EXIT_STUB[MACHO_EXIT_STUB_SIZE] = {
    0x30, 0x00, 0x80, 0xD2,    /* MOV X16, #1    */
    0x01, 0x10, 0x00, 0xD4,    /* SVC #0x80      */
    0x00, 0x00, 0x20, 0xD4,    /* BRK #0         */
    0x00, 0x00, 0x20, 0xD4     /* BRK #0         */
};

/* Call stub: BL +offset (patched at emit time) — 4 bytes */
#define MACHO_CALL_STUB_SIZE   4

/* =========================================================================
 *  Little-endian serializers
 * ========================================================================= */
static void macho_write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)( v        & 0xFF);
    p[1] = (uint8_t)((v >>  8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void macho_write_le64(uint8_t *p, uint64_t v)
{
    macho_write_le32(p,     (uint32_t)(v & 0xFFFFFFFFu));
    macho_write_le32(p + 4, (uint32_t)(v >> 32));
}

/* =========================================================================
 *  Utility: round up to alignment
 * ========================================================================= */
static uint64_t align_up(uint64_t val, uint64_t alignment)
{
    return (val + alignment - 1) & ~(alignment - 1);
}

/* =========================================================================
 *  emit_macho_exe()
 * ========================================================================= */
int emit_macho_exe(const char *filename, const CodeBuffer *code)
{
    if (!code || code->size == 0) {
        fprintf(stderr, "Mach-O emitter: no code to emit.\n");
        return 1;
    }

    /* ================================================================
     *  Compute layout dimensions
     * ================================================================ */

    /* Dylinker path (required for LC_LOAD_DYLINKER even for static-ish exes
     * on modern macOS/Apple Silicon — dyld validates the binary) */
    const char *dylinker_path = "/usr/lib/dyld";
    uint32_t dylinker_path_len = (uint32_t)strlen(dylinker_path) + 1; /* +NUL */
    /* LC_LOAD_DYLINKER: cmd(4) + cmdsize(4) + name_offset(4) + path + pad */
    uint32_t lc_dylinker_size = 12 + dylinker_path_len;
    /* Pad to 8-byte alignment */
    lc_dylinker_size = (uint32_t)align_up(lc_dylinker_size, 8);

    /* Number of load commands */
    uint32_t ncmds = 4;  /* __PAGEZERO, __TEXT, LC_MAIN, LC_LOAD_DYLINKER */

    /* Total header + load commands size */
    uint32_t header_and_cmds_size = MACH_HEADER_64_SIZE
                                  + SEGMENT_CMD_64_SIZE       /* __PAGEZERO */
                                  + SEGMENT_CMD_64_SIZE       /* __TEXT */
                                  + SECTION_64_SIZE           /* __text sect */
                                  + LC_MAIN_SIZE
                                  + lc_dylinker_size;

    /* The __text section starts at a page-aligned offset */
    uint32_t text_file_offset = (uint32_t)align_up(header_and_cmds_size,
                                                    MACHO_PAGE_SIZE);

    /* Segment content = call stub + user code + exit stub */
    uint32_t user_code_size  = (uint32_t)code->size;
    uint32_t segment_content = MACHO_CALL_STUB_SIZE + user_code_size
                             + MACHO_EXIT_STUB_SIZE;

    /* Total file size: header/cmds padding + segment content, page-aligned */
    uint32_t total_file_size = text_file_offset + segment_content;

    /* __TEXT segment covers from file offset 0 to end — this is the standard
     * macOS layout (the load commands are part of the __TEXT segment). */
    uint64_t text_seg_vmaddr  = MACHO_BASE_ADDR;
    uint64_t text_seg_vmsize  = align_up(total_file_size, MACHO_PAGE_SIZE);
    uint64_t text_seg_fileoff = 0;
    uint64_t text_seg_filesz  = (uint64_t)total_file_size;

    /* __text section virtual address */
    uint64_t text_sect_addr = text_seg_vmaddr + text_file_offset;

    /* Entry point: offset from start of __TEXT segment.
     * LC_MAIN uses "entryoff" = file offset from start of binary. */
    uint64_t entry_off = (uint64_t)text_file_offset;

    /* sizeofcmds = total bytes of all load commands (without mach_header) */
    uint32_t sizeofcmds = header_and_cmds_size - MACH_HEADER_64_SIZE;

    fprintf(stderr, "[Mach-O] User code size    : %u bytes\n", user_code_size);
    fprintf(stderr, "[Mach-O] Segment content   : %u bytes (stub + code + exit)\n",
            segment_content);
    fprintf(stderr, "[Mach-O] Text file offset  : 0x%X\n", text_file_offset);
    fprintf(stderr, "[Mach-O] Entry offset      : 0x%llX\n",
            (unsigned long long)entry_off);
    fprintf(stderr, "[Mach-O] Total file size   : %u bytes\n", total_file_size);

    /* ================================================================
     *  Allocate zero-filled file image
     * ================================================================ */
    uint8_t *img = (uint8_t *)calloc(1, total_file_size);
    if (!img) {
        fprintf(stderr, "Mach-O emitter: out of memory.\n");
        return 1;
    }

    uint32_t off = 0;  /* write cursor */

    /* ================================================================
     *  mach_header_64  (32 bytes)
     *
     *  struct mach_header_64 {
     *      uint32_t  magic;
     *      int32_t   cputype;
     *      int32_t   cpusubtype;
     *      uint32_t  filetype;
     *      uint32_t  ncmds;
     *      uint32_t  sizeofcmds;
     *      uint32_t  flags;
     *      uint32_t  reserved;
     *  };
     * ================================================================ */
    macho_write_le32(img + off +  0, MH_MAGIC_64);
    macho_write_le32(img + off +  4, CPU_TYPE_ARM64);
    macho_write_le32(img + off +  8, CPU_SUBTYPE_ARM64_ALL);
    macho_write_le32(img + off + 12, MH_EXECUTE);
    macho_write_le32(img + off + 16, ncmds);
    macho_write_le32(img + off + 20, sizeofcmds);
    macho_write_le32(img + off + 24, MH_NOUNDEFS | MH_PIE);
    macho_write_le32(img + off + 28, 0);  /* reserved */
    off += MACH_HEADER_64_SIZE;

    /* ================================================================
     *  LC_SEGMENT_64: __PAGEZERO  (72 bytes)
     *
     *  The __PAGEZERO segment occupies virtual address 0..0x100000000
     *  with zero file size.  It's the standard null-pointer guard.
     *
     *  struct segment_command_64 {
     *      uint32_t  cmd;
     *      uint32_t  cmdsize;
     *      char      segname[16];
     *      uint64_t  vmaddr;
     *      uint64_t  vmsize;
     *      uint64_t  fileoff;
     *      uint64_t  filesize;
     *      int32_t   maxprot;
     *      int32_t   initprot;
     *      uint32_t  nsects;
     *      uint32_t  flags;
     *  };
     * ================================================================ */
    macho_write_le32(img + off +  0, LC_SEGMENT_64);
    macho_write_le32(img + off +  4, SEGMENT_CMD_64_SIZE);  /* no sections */
    memcpy(img + off + 8, "__PAGEZERO\0\0\0\0\0\0", 16);   /* segname */
    macho_write_le64(img + off + 24, 0);                     /* vmaddr */
    macho_write_le64(img + off + 32, MACHO_BASE_ADDR);       /* vmsize */
    macho_write_le64(img + off + 40, 0);                     /* fileoff */
    macho_write_le64(img + off + 48, 0);                     /* filesize */
    macho_write_le32(img + off + 56, VM_PROT_NONE);          /* maxprot */
    macho_write_le32(img + off + 60, VM_PROT_NONE);          /* initprot */
    macho_write_le32(img + off + 64, 0);                     /* nsects */
    macho_write_le32(img + off + 68, 0);                     /* flags */
    off += SEGMENT_CMD_64_SIZE;

    /* ================================================================
     *  LC_SEGMENT_64: __TEXT  (72 + 80 = 152 bytes, includes 1 section)
     *
     *  Contains the load commands (as part of the mapping) and the
     *  __text section with executable code.
     * ================================================================ */
    uint32_t text_seg_cmd_size = SEGMENT_CMD_64_SIZE + SECTION_64_SIZE;

    macho_write_le32(img + off +  0, LC_SEGMENT_64);
    macho_write_le32(img + off +  4, text_seg_cmd_size);
    memcpy(img + off + 8, "__TEXT\0\0\0\0\0\0\0\0\0\0\0", 16);
    macho_write_le64(img + off + 24, text_seg_vmaddr);
    macho_write_le64(img + off + 32, text_seg_vmsize);
    macho_write_le64(img + off + 40, text_seg_fileoff);
    macho_write_le64(img + off + 48, text_seg_filesz);
    macho_write_le32(img + off + 56, VM_PROT_READ | VM_PROT_EXECUTE);
    macho_write_le32(img + off + 60, VM_PROT_READ | VM_PROT_EXECUTE);
    macho_write_le32(img + off + 64, 1);    /* nsects = 1 (__text) */
    macho_write_le32(img + off + 68, 0);    /* flags */
    off += SEGMENT_CMD_64_SIZE;

    /* ---- __text section header  (80 bytes) ---------------------------- */
    /*
     * struct section_64 {
     *     char      sectname[16];
     *     char      segname[16];
     *     uint64_t  addr;
     *     uint64_t  size;
     *     uint32_t  offset;
     *     uint32_t  align;      (power of 2)
     *     uint32_t  reloff;
     *     uint32_t  nreloc;
     *     uint32_t  flags;
     *     uint32_t  reserved1;
     *     uint32_t  reserved2;
     *     uint32_t  reserved3;
     * };
     */
    memcpy(img + off +  0, "__text\0\0\0\0\0\0\0\0\0\0", 16);   /* sectname */
    memcpy(img + off + 16, "__TEXT\0\0\0\0\0\0\0\0\0\0\0", 16);  /* segname  */
    macho_write_le64(img + off + 32, text_sect_addr);             /* addr     */
    macho_write_le64(img + off + 40, (uint64_t)segment_content);  /* size     */
    macho_write_le32(img + off + 48, text_file_offset);           /* offset   */
    macho_write_le32(img + off + 52, 2);        /* align = 2^2 = 4 bytes */
    macho_write_le32(img + off + 56, 0);        /* reloff */
    macho_write_le32(img + off + 60, 0);        /* nreloc */
    macho_write_le32(img + off + 64,
                     S_REGULAR | S_ATTR_PURE_INSTRUCTIONS
                               | S_ATTR_SOME_INSTRUCTIONS);
    macho_write_le32(img + off + 68, 0);        /* reserved1 */
    macho_write_le32(img + off + 72, 0);        /* reserved2 */
    macho_write_le32(img + off + 76, 0);        /* reserved3 */
    off += SECTION_64_SIZE;

    /* ================================================================
     *  LC_MAIN  (24 bytes)
     *
     *  struct entry_point_command {
     *      uint32_t  cmd;
     *      uint32_t  cmdsize;
     *      uint64_t  entryoff;    (file offset of entry point)
     *      uint64_t  stacksize;   (0 = default)
     *  };
     * ================================================================ */
    macho_write_le32(img + off +  0, LC_MAIN);
    macho_write_le32(img + off +  4, LC_MAIN_SIZE);
    macho_write_le64(img + off +  8, entry_off);
    macho_write_le64(img + off + 16, 0);     /* stacksize = default */
    off += LC_MAIN_SIZE;

    /* ================================================================
     *  LC_LOAD_DYLINKER  (variable size, 8-byte padded)
     *
     *  struct dylinker_command {
     *      uint32_t  cmd;
     *      uint32_t  cmdsize;
     *      uint32_t  name;       (offset from start of command)
     *  };
     *  followed by the NUL-terminated path string + padding.
     * ================================================================ */
    macho_write_le32(img + off + 0, LC_LOAD_DYLINKER);
    macho_write_le32(img + off + 4, lc_dylinker_size);
    macho_write_le32(img + off + 8, 12);    /* offset to name string */
    memcpy(img + off + 12, dylinker_path, dylinker_path_len);
    /* Remaining bytes are zero (calloc). */
    off += lc_dylinker_size;

    /* ================================================================
     *  __text section content  (at text_file_offset)
     *
     *  Layout:
     *    [0]    BL stub (4 bytes) — calls user code
     *    [4]    User machine code
     *    [4+N]  Exit stub (16 bytes) — SYS_exit(X0)
     *
     *  The user's HLT → RET returns from the BL, and execution falls
     *  through to the exit stub with X0 intact.
     * ================================================================ */
    uint8_t *text = img + text_file_offset;

    /* ---- Call stub: BL user_code -----------------------------------
     * BL offset: the user code starts 4 bytes after the BL instruction.
     * BL encoding: 1001 01 imm26, where imm26 = offset/4.
     * offset = +4 bytes (next instruction), imm26 = 1.                */
    {
        int32_t bl_offset = MACHO_CALL_STUB_SIZE;  /* +4 bytes */
        int32_t imm26 = (bl_offset >> 2) & 0x03FFFFFF;
        uint32_t bl_word = (0x25u << 26) | (uint32_t)imm26;
        macho_write_le32(text, bl_word);
    }

    /* ---- User code ------------------------------------------------- */
    memcpy(text + MACHO_CALL_STUB_SIZE, code->bytes, user_code_size);

    /* ---- Exit stub ------------------------------------------------- */
    memcpy(text + MACHO_CALL_STUB_SIZE + user_code_size,
           MACHO_EXIT_STUB, MACHO_EXIT_STUB_SIZE);

    /* ================================================================
     *  Write file
     * ================================================================ */
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Mach-O emitter: cannot open '%s' for writing: ",
                filename);
        perror(NULL);
        free(img);
        return 1;
    }

    size_t written = fwrite(img, 1, total_file_size, fp);
    fclose(fp);
    free(img);

    if (written != total_file_size) {
        fprintf(stderr, "Mach-O emitter: short write (%zu of %u bytes).\n",
                written, total_file_size);
        return 1;
    }

    fprintf(stderr, "[Mach-O] Wrote %u bytes to %s\n",
            total_file_size, filename);
    return 0;
}
