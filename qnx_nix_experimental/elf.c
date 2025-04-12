#include "elf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>    // For PATH_MAX
#include <sys/param.h> // For MAXPATHLEN if PATH_MAX is not defined

#ifndef PATH_MAX
#define PATH_MAX MAXPATHLEN
#endif

#define DT_RPATH 15
#define DT_RUNPATH 29
#define NIX_STORE_PATH "/nix/store"

static void* map_file(const char* path, size_t* size, int writable) {
    int fd = open(path, writable ? O_RDWR : O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "Failed to stat %s: %s\n", path, strerror(errno));
        close(fd);
        return NULL;
    }
    *size = st.st_size;

    // Round up to page size
    size_t map_size = (*size + 4095) & ~4095;

    void* map = mmap(NULL, map_size, 
                     writable ? (PROT_READ|PROT_WRITE) : PROT_READ,
                     MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap %s: %s\n", path, strerror(errno));
        close(fd);
        return NULL;
    }
    close(fd);

    return map;
}

int elf_is_elf(const char* path) {
    size_t size;
    void* map = map_file(path, &size, 0);
    if (!map) return 0;

    Elf_Ehdr* ehdr = map;
    int is_elf = (size >= sizeof(Elf_Ehdr) &&
                  memcmp(ehdr->e_ident, ELFMAG, SELFMAG) == 0 &&
                  ehdr->e_ident[EI_CLASS] == ELFCLASS32);  // Verify 32-bit
    
    munmap(map, size);
    return is_elf;
}

static Elf_Off find_space_for_string(void* map, size_t size, const char* str) {
    Elf_Ehdr* ehdr = map;
    Elf_Shdr* shdr = (Elf_Shdr*)((char*)map + ehdr->e_shoff);

    // Find .dynstr section
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_STRTAB && 
            strcmp((char*)map + shdr[ehdr->e_shstrndx].sh_offset + shdr[i].sh_name, ".dynstr") == 0) {
            // Found .dynstr, look for space
            char* strtab = (char*)map + shdr[i].sh_offset;
            size_t str_len = strlen(str) + 1;
            size_t pos = 0;

            while (pos < shdr[i].sh_size) {
                if (strtab[pos] == '\0') {
                    // Found potential space, check if enough room
                    size_t space = 0;
                    while (pos + space < shdr[i].sh_size && strtab[pos + space] == '\0') {
                        space++;
                    }
                    if (space >= str_len) {
                        return shdr[i].sh_offset + pos;
                    }
                }
                pos++;
            }
        }
    }
    return 0;
}

// Replace add_new_dynstr_section with this QNX-compatible version
static Elf64_Off add_new_dynstr_section(const char* path, const char* str) {
    int fd = open(path, O_RDWR);
    if (fd < 0) return 0;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return 0;
    }

    // Calculate new section size (round up to page size)
    size_t str_len = strlen(str) + 1;
    size_t new_section_size = (str_len + 4095) & ~4095;
    
    // Extend the file
    if (ftruncate(fd, st.st_size + new_section_size) != 0) {
        close(fd);
        return 0;
    }

    // Map the extended file
    void* map = mmap(NULL, st.st_size + new_section_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return 0;
    }

    // Get header pointers
    Elf64_Ehdr* ehdr = map;
    Elf64_Shdr* shdr = map + ehdr->e_shoff;
    
    // Create new section
    Elf64_Shdr new_section = {0};
    new_section.sh_type = SHT_STRTAB;
    new_section.sh_flags = SHF_ALLOC;
    new_section.sh_offset = st.st_size;
    new_section.sh_size = new_section_size;
    new_section.sh_addralign = 1;
    
    // Add new section header
    memcpy(&shdr[ehdr->e_shnum], &new_section, sizeof(Elf64_Shdr));
    ehdr->e_shnum++;
    
    // Copy string to new section
    char* new_strtab = map + new_section.sh_offset;
    strcpy(new_strtab, str);
    
    // Sync and unmap
    msync(map, st.st_size + new_section_size, MS_SYNC);
    munmap(map, st.st_size + new_section_size);
    close(fd);
    
    return new_section.sh_offset;
}

int elf_set_rpath(const char* path, const char* rpath) {
    size_t size;
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "Failed to stat %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    size = st.st_size;

    void* map = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    // Verify ELF header
    Elf_Ehdr* ehdr = (Elf_Ehdr*)map;
    if (size < sizeof(Elf_Ehdr) || 
        memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "Invalid or non-32-bit ELF file\n");
        goto cleanup;
    }

    // Find dynamic section and string table
    Elf_Phdr* phdr = (Elf_Phdr*)((char*)map + ehdr->e_phoff);
    Elf_Shdr* shdr = (Elf_Shdr*)((char*)map + ehdr->e_shoff);
    
    Elf_Off dynamic_offset = 0;
    size_t dynamic_size = 0;
    
    // First find dynamic section
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dynamic_offset = phdr[i].p_offset;
            dynamic_size = phdr[i].p_filesz;
            break;
        }
    }

    if (!dynamic_offset) {
        fprintf(stderr, "No dynamic section found\n");
        goto cleanup;
    }

    // Find .dynstr section
    char* dynstr = NULL;
    size_t dynstr_size = 0;
    Elf_Off dynstr_offset = 0;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_STRTAB &&
            strcmp((char*)map + shdr[ehdr->e_shstrndx].sh_offset + shdr[i].sh_name, ".dynstr") == 0) {
            dynstr = (char*)map + shdr[i].sh_offset;
            dynstr_size = shdr[i].sh_size;
            dynstr_offset = shdr[i].sh_offset;
            break;
        }
    }

    if (!dynstr) {
        fprintf(stderr, "No .dynstr section found\n");
        goto cleanup;
    }

    // Find or create RPATH entry
    Elf_Dyn* dyn = (Elf_Dyn*)((char*)map + dynamic_offset);
    Elf_Dyn* rpath_dyn = NULL;
    
    // QNX: Only look for DT_RPATH, ignore DT_RUNPATH
    for (size_t i = 0; i < dynamic_size/sizeof(Elf_Dyn); i++) {
        if (dyn[i].d_tag == DT_RPATH) {  // QNX: Only use DT_RPATH
            rpath_dyn = &dyn[i];
            printf("Found existing RPATH at offset %ld\n", (long)i);
            break;
        }
    }

    // Create new RPATH string
    char new_rpath[PATH_MAX];
    snprintf(new_rpath, sizeof(new_rpath), "$ORIGIN/../lib");

    if (rpath_dyn) {
        // Update existing RPATH
        if (strlen(new_rpath) >= dynstr_size - (rpath_dyn->d_un.d_val - dynstr_offset)) {
            fprintf(stderr, "New RPATH too long for existing space\n");
            goto cleanup;
        }
        strcpy(dynstr + (rpath_dyn->d_un.d_val - dynstr_offset), new_rpath);
    } else {
        // Find space in dynstr for new string
        size_t pos = 0;
        while (pos < dynstr_size) {
            if (dynstr[pos] == '\0') {
                size_t space = 0;
                while (pos + space < dynstr_size && dynstr[pos + space] == '\0') space++;
                if (space >= strlen(new_rpath) + 1) {
                    // Found space, create new RPATH entry
                    while (dyn->d_tag != DT_NULL) dyn++;
                    dyn->d_tag = DT_RPATH;  // QNX: Force DT_RPATH
                    dyn->d_un.d_val = dynstr_offset + pos;
                    strcpy(dynstr + pos, new_rpath);
                    break;
                }
            }
            pos++;
        }
        if (pos >= dynstr_size) {
            fprintf(stderr, "No space for new RPATH string\n");
            goto cleanup;
        }
    }

    if (msync(map, size, MS_SYNC) != 0) {
        fprintf(stderr, "Failed to sync changes: %s\n", strerror(errno));
        goto cleanup;
    }

    munmap(map, size);
    close(fd);
    return 0;

cleanup:
    if (map != MAP_FAILED) munmap(map, size);
    close(fd);
    return -1;
}

char* elf_get_rpath(const char* path) {
    size_t size;
    void* map = map_file(path, &size, 0);
    if (!map) return NULL;

    Elf_Ehdr* ehdr = (Elf_Ehdr*)map;
    if (size < sizeof(Elf_Ehdr) || 
        memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        munmap(map, size);
        return NULL;
    }

    Elf_Phdr* phdr = (Elf_Phdr*)((char*)map + ehdr->e_phoff);
    Elf_Shdr* shdr = (Elf_Shdr*)((char*)map + ehdr->e_shoff);
    
    // Find dynamic section
    Elf_Addr dynamic_addr = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dynamic_addr = phdr[i].p_vaddr;
            break;
        }
    }

    if (!dynamic_addr) {
        munmap(map, size);
        return NULL;
    }

    // Find string table
    char* strtab = NULL;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_STRTAB) {
            strtab = (char*)map + shdr[i].sh_offset;
            break;
        }
    }

    if (!strtab) {
        munmap(map, size);
        return NULL;
    }

    // Find RPATH/RUNPATH entry
    Elf_Dyn* dyn = (Elf_Dyn*)((char*)map + dynamic_addr);
    char* rpath = NULL;
    
    for (int i = 0; dyn[i].d_tag != DT_NULL; i++) {
        if (dyn[i].d_tag == DT_RPATH || dyn[i].d_tag == DT_RUNPATH) {
            // Found RPATH entry, get string from string table
            const char* rpath_str = strtab + dyn[i].d_un.d_val;
            // Allocate and copy the string
            rpath = strdup(rpath_str);
            break;
        }
    }

    munmap(map, size);
    return rpath; // Caller must free this
}
