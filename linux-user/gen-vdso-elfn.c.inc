/*
 * Post-process a vdso elf image for inclusion into qemu.
 * Elf size specialization.
 *
 * Copyright 2023 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

static void elfN(bswap_ehdr)(ElfN(Ehdr) *ehdr)
{
    bswaps(&ehdr->e_type);
    bswaps(&ehdr->e_machine);
    bswaps(&ehdr->e_version);
    bswaps(&ehdr->e_entry);
    bswaps(&ehdr->e_phoff);
    bswaps(&ehdr->e_shoff);
    bswaps(&ehdr->e_flags);
    bswaps(&ehdr->e_ehsize);
    bswaps(&ehdr->e_phentsize);
    bswaps(&ehdr->e_phnum);
    bswaps(&ehdr->e_shentsize);
    bswaps(&ehdr->e_shnum);
    bswaps(&ehdr->e_shstrndx);
}

static void elfN(bswap_phdr)(ElfN(Phdr) *phdr)
{
    bswaps(&phdr->p_type);
    bswaps(&phdr->p_flags);
    bswaps(&phdr->p_offset);
    bswaps(&phdr->p_vaddr);
    bswaps(&phdr->p_paddr);
    bswaps(&phdr->p_filesz);
    bswaps(&phdr->p_memsz);
    bswaps(&phdr->p_align);
}

static void elfN(bswap_shdr)(ElfN(Shdr) *shdr)
{
    bswaps(&shdr->sh_name);
    bswaps(&shdr->sh_type);
    bswaps(&shdr->sh_flags);
    bswaps(&shdr->sh_addr);
    bswaps(&shdr->sh_offset);
    bswaps(&shdr->sh_size);
    bswaps(&shdr->sh_link);
    bswaps(&shdr->sh_info);
    bswaps(&shdr->sh_addralign);
    bswaps(&shdr->sh_entsize);
}

static void elfN(bswap_sym)(ElfN(Sym) *sym)
{
    bswaps(&sym->st_name);
    bswaps(&sym->st_value);
    bswaps(&sym->st_size);
    bswaps(&sym->st_shndx);
}

static void elfN(bswap_dyn)(ElfN(Dyn) *dyn)
{
    bswaps(&dyn->d_tag);
    bswaps(&dyn->d_un.d_ptr);
}

static void elfN(search_symtab)(ElfN(Shdr) *shdr, unsigned sym_idx,
                                void *buf, bool need_bswap)
{
    unsigned str_idx = shdr[sym_idx].sh_link;
    ElfN(Sym) *target_sym = buf + shdr[sym_idx].sh_offset;
    unsigned sym_n = shdr[sym_idx].sh_size / sizeof(*target_sym);
    const char *str = buf + shdr[str_idx].sh_offset;

    for (unsigned i = 0; i < sym_n; ++i) {
        const char *name;
        ElfN(Sym) sym;

        memcpy(&sym, &target_sym[i], sizeof(sym));
        if (need_bswap) {
            elfN(bswap_sym)(&sym);
        }
        name = str + sym.st_name;

        if (sigreturn_sym && strcmp(sigreturn_sym, name) == 0) {
            sigreturn_addr = sym.st_value;
        } else if (rt_sigreturn_sym && strcmp(rt_sigreturn_sym, name) == 0) {
            rt_sigreturn_addr = sym.st_value;
        } else if (strcmp("sigreturn_region_start", name) == 0) {
            sigreturn_region_start_addr = sym.st_value;
        } else if (strcmp("sigreturn_region_end", name) == 0) {
            sigreturn_region_end_addr = sym.st_value;
        }
    }
}

static void elfN(bswap_ps_hdrs)(ElfN(Ehdr) *ehdr)
{
    ElfN(Phdr) *phdr = (void *)ehdr + ehdr->e_phoff;
    ElfN(Shdr) *shdr = (void *)ehdr + ehdr->e_shoff;
    ElfN(Half) i;

    for (i = 0; i < ehdr->e_phnum; ++i) {
        elfN(bswap_phdr)(&phdr[i]);
    }

    for (i = 0; i < ehdr->e_shnum; ++i) {
        elfN(bswap_shdr)(&shdr[i]);
    }
}

static void elfN(process)(FILE *outf, void *buf, long len, bool need_bswap)
{
    ElfN(Ehdr) *ehdr = buf;
    ElfN(Phdr) *phdr;
    ElfN(Shdr) *shdr;
    unsigned phnum, shnum;
    unsigned dynamic_ofs = 0;
    unsigned dynamic_addr = 0;
    unsigned symtab_idx = 0;
    unsigned dynsym_idx = 0;
    unsigned first_segsz = 0;
    int errors = 0;

    if (need_bswap) {
        elfN(bswap_ehdr)(buf);
        elfN(bswap_ps_hdrs)(buf);
    }

    phnum = ehdr->e_phnum;
    phdr = buf + ehdr->e_phoff;
    shnum = ehdr->e_shnum;
    shdr = buf + ehdr->e_shoff;
    for (unsigned i = 0; i < shnum; ++i) {
        switch (shdr[i].sh_type) {
        case SHT_SYMTAB:
            symtab_idx = i;
            break;
        case SHT_DYNSYM:
            dynsym_idx = i;
            break;
        }
    }

    for (unsigned i = 0; i < phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD) {
            continue;
        }
        if (first_segsz != 0) {
            fprintf(stderr, "Multiple LOAD segments\n");
            errors++;
        }
        if (phdr[i].p_offset != 0) {
            fprintf(stderr, "LOAD segment does not cover EHDR\n");
            errors++;
        }
        if (phdr[i].p_vaddr != 0) {
            fprintf(stderr, "LOAD segment not loaded at address 0\n");
            errors++;
        }
        if (phdr[i].p_filesz != phdr[i].p_memsz) {
            fprintf(stderr, "LOAD segment's filesz and memsz differ\n");
            errors++;
        }
        if (phdr[i].p_filesz > len) {
            fprintf(stderr, "LOAD segment is larger than the whole VDSO\n");
            errors++;
        }
        phdr[i].p_filesz = len;
        phdr[i].p_memsz = len;
        first_segsz = len;
        if (first_segsz < ehdr->e_phoff + phnum * sizeof(*phdr)) {
            fprintf(stderr, "LOAD segment does not cover PHDRs\n");
            errors++;
        }
        if ((phdr[i].p_flags & (PF_R | PF_W)) != (PF_R | PF_W)) {
            fprintf(stderr, "LOAD segment is not read-write\n");
            errors++;
        }
    }
    for (unsigned i = 0; i < phnum; ++i) {
        const char *which;

        switch (phdr[i].p_type) {
        case PT_PHDR:
            which = "PT_PHDR";
            break;
        case PT_NOTE:
            which = "PT_NOTE";
            break;
        case PT_DYNAMIC:
            dynamic_ofs = phdr[i].p_offset;
            dynamic_addr = phdr[i].p_vaddr;
            which = "PT_DYNAMIC";
            break;
        default:
            continue;
        }
        if (first_segsz < phdr[i].p_vaddr + phdr[i].p_filesz) {
            fprintf(stderr, "LOAD segment does not cover %s\n", which);
            errors++;
        }
    }
    if (errors) {
        exit(EXIT_FAILURE);
    }

    for (unsigned i = 0; i < phnum; ++i) {
        output_reloc(outf, buf, &phdr[i].p_vaddr);
        output_reloc(outf, buf, &phdr[i].p_paddr);
    }

    for (unsigned i = 0; i < shnum; ++i) {
        output_reloc(outf, buf, &shdr[i].sh_addr);
    }

    if (dynamic_addr) {
        ElfN(Dyn) *target_dyn = buf + dynamic_ofs;
        __typeof(((ElfN(Dyn) *)target_dyn)->d_tag) tag;

        do {
            ElfN(Dyn) dyn;

            memcpy(&dyn, target_dyn, sizeof(dyn));
            if (need_bswap) {
                elfN(bswap_dyn)(&dyn);
            }
            tag = dyn.d_tag;

            switch (tag) {
            case DT_HASH:
            case DT_SYMTAB:
            case DT_STRTAB:
            case DT_VERDEF:
            case DT_VERSYM:
            case DT_PLTGOT:
            case DT_ADDRRNGLO ... DT_ADDRRNGHI:
                output_reloc(outf, buf, &target_dyn->d_un.d_val);
                break;

            case DT_NULL:
            case DT_STRSZ:
            case DT_SONAME:
            case DT_DEBUG:
            case DT_FLAGS:
            case DT_FLAGS_1:
            case DT_SYMBOLIC:
            case DT_BIND_NOW:
            case DT_VERDEFNUM:
            case DT_VALRNGLO ... DT_VALRNGHI:
                break;

            case DT_SYMENT:
                if (dyn.d_un.d_val != sizeof(ElfN(Sym))) {
                    fprintf(stderr, "VDSO has incorrect dynamic symbol size\n");
                    errors++;
                }
                break;

            case DT_REL:
            case DT_RELSZ:
            case DT_RELA:
            case DT_RELASZ:
                if (dyn.d_un.d_val != 0) {
                    fprintf(stderr, "VDSO has dynamic relocations\n");
                    errors++;
                }
                break;
            case DT_RELENT:
            case DT_RELAENT:
            case DT_TEXTREL:
                break;

            case DT_NEEDED:
            case DT_VERNEED:
            case DT_PLTREL:
            case DT_JMPREL:
            case DT_RPATH:
            case DT_RUNPATH:
                fprintf(stderr, "VDSO has external dependencies\n");
                errors++;
                break;

            case PT_LOPROC + 3:
                if (ehdr->e_machine == EM_PPC64) {
                    break;
                }
                goto do_default;

            default:
            do_default:
                fprintf(stderr, "VDSO has unknown DYNAMIC entry (%lx)\n",
                        (unsigned long)tag);
                errors++;
                break;
            }
            target_dyn++;
        } while (tag != DT_NULL);
        if (errors) {
            exit(EXIT_FAILURE);
        }
    }

    if (dynsym_idx) {
        ElfN(Sym) *target_sym = buf + shdr[dynsym_idx].sh_offset;
        unsigned sym_n = shdr[dynsym_idx].sh_size / sizeof(*target_sym);

        for (unsigned i = 0; i < sym_n; ++i) {
            output_reloc(outf, buf, &target_sym[i].st_value);
        }
    }

    if (dynsym_idx) {
        elfN(search_symtab)(shdr, dynsym_idx, buf, need_bswap);
    }
    if (symtab_idx) {
        elfN(search_symtab)(shdr, symtab_idx, buf, need_bswap);
    }

    if (need_bswap) {
        elfN(bswap_ps_hdrs)(buf);
        elfN(bswap_ehdr)(buf);
    }
}
