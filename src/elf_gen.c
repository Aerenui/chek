//
// Created by frantisek on 26. 5. 2026.
//

#include "elf_gen.h"


#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "compiler.h"

// load address for the executable segment
// #define LOAD_ADDR 0x400000

// congruence rule must hold — p_offset % p_align == p_vaddr % p_align
#define PAGE_SIZE 0x1000
#define CODE_OFFSET PAGE_SIZE  // pad headers into a full page


void write_elf64(const char *path, const uint8_t *code, size_t code_len, uint64_t load_addr) {
    /*// --- prologue/epilogue ---
    // push rbp; mov rbp, rsp; sub rsp, N  (N = frame.next_offset rounded to 16)
    uint8_t prologue[] = {
        0x55,                               // push rbp
        0x48, 0x89, 0xE5,                   // mov rbp, rsp

        0x48, 0x83, 0xEC, (uint8_t)((get_frame()->next_offset + 15) & ~15), // sub rsp, N
    };
    // mov eax, 0 (exit code); mov rdi, rax; mov rax, 60 (sys_exit); syscall
    uint8_t epilogue[] = {
        0x48, 0x83, 0xC4, (uint8_t)((get_frame()->next_offset + 15) & ~15), // add rsp, N
        0x5D,                                       // pop rbp

        0xB8, 0x00, 0x00, 0x00, 0x00,               // mov eax, 0
        0x48, 0x89, 0xC7,                           // mov rdi, rax  (exit code = 0)
        0x48, 0xC7, 0xC0, 0x3C, 0x00, 0x00, 0x00,   // mov rax, 60 (sys_exit)
        0x0F, 0x05                                  // syscall
    };*/

    // size_t total_code = sizeof(prologue) + code_len + sizeof(epilogue);
    size_t total_code = code_len;
    // uint64_t entry = LOAD_ADDR + CODE_OFFSET;
    // uint64_t entry = LOAD_ADDR;
    uint64_t entry = load_addr;

    Elf64_Ehdr ehdr = {
        .e_ident = {
            ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3,
            ELFCLASS64, ELFDATA2LSB, EV_CURRENT, ELFOSABI_NONE,
            0, 0, 0, 0, 0, 0, 0, 0
        },
        .e_type      = ET_EXEC,
        .e_machine   = EM_X86_64,
        .e_version   = EV_CURRENT,
        .e_entry     = entry,
        .e_phoff     = sizeof(Elf64_Ehdr),
        .e_shoff     = 0,
        .e_flags     = 0,
        .e_ehsize    = sizeof(Elf64_Ehdr),
        .e_phentsize = sizeof(Elf64_Phdr),
        .e_phnum     = 1,
        .e_shentsize = sizeof(Elf64_Shdr),
        .e_shnum     = 0,
        .e_shstrndx  = SHN_UNDEF,
    };

    Elf64_Phdr phdr = {
        .p_type   = PT_LOAD,
        .p_flags  = PF_R | PF_X,
        .p_offset = CODE_OFFSET,
        .p_vaddr  = load_addr, //LOAD_ADDR,
        .p_paddr  = load_addr, //LOAD_ADDR,
        .p_filesz = total_code,
        .p_memsz  = total_code,
        .p_align = 0x1000,
    };

    uint8_t pad[PAGE_SIZE - sizeof(Elf64_Ehdr) - sizeof(Elf64_Phdr)] = {0};

    FILE *f = fopen(path, "wb");
    fwrite(&ehdr,     1, sizeof(ehdr),     f);
    fwrite(&phdr,     1, sizeof(phdr),     f);
    fwrite(pad,       1, sizeof(pad),      f);

    fwrite(code,      1, code_len,         f);
    // fwrite(epilogue,  1, sizeof(epilogue), f);
    fclose(f);

    chmod(path, 0755);
}