#ifndef QNX_NIX_ELF_H
#define QNX_NIX_ELF_H

#include <sys/elf.h>  // QNX's ELF header
#include <stddef.h>
#include <sys/link.h>  // For dynamic linking structures

// Use 32-bit types for QNX
typedef Elf32_Ehdr Elf_Ehdr;
typedef Elf32_Shdr Elf_Shdr;
typedef Elf32_Phdr Elf_Phdr;
typedef Elf32_Dyn  Elf_Dyn;
typedef Elf32_Off  Elf_Off;
typedef Elf32_Addr Elf_Addr;
typedef Elf32_Word Elf_Word;

// Set RPATH in ELF binary
int elf_set_rpath(const char* path, const char* rpath);

// Get current RPATH from ELF binary
char* elf_get_rpath(const char* path);

// Check if file is ELF binary
int elf_is_elf(const char* path);

#endif
