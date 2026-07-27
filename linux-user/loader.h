/*
 * loader.h: prototypes for linux-user guest binary loader
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LINUX_USER_LOADER_H
#define LINUX_USER_LOADER_H

typedef struct {
    const void *cache;
    unsigned int cache_size;
    int fd;
} ImageSource;

bool imgsrc_read(void *dst, off_t offset, size_t len,
                 const ImageSource *img, Error **errp);

void *imgsrc_read_alloc(off_t offset, size_t len,
                        const ImageSource *img, Error **errp);

abi_long imgsrc_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, const ImageSource *src, abi_ulong offset);

#define BPRM_BUF_SIZE  1024

struct linux_binprm {
    char buf[BPRM_BUF_SIZE] __attribute__((aligned));
    ImageSource src;
    abi_ulong p;
    int e_uid, e_gid;
    int argc, envc;
    char **argv;
    char **envp;
    char *filename;
    int (*core_dump)(int, const CPUArchState *);
};

abi_ulong loader_build_argptr(int envc, int argc, abi_ulong sp,
                              abi_ulong stringp, int push_ptr);
int loader_exec(int fdexec, const char *filename, char **argv, char **envp,
                struct image_info *infop, struct linux_binprm *);

uint32_t get_elf_eflags(int fd);
int load_elf_binary(struct linux_binprm *bprm, struct image_info *info);
int load_flt_binary(struct linux_binprm *bprm, struct image_info *info);

abi_long memcpy_to_target(abi_ulong dest, const void *src,
                          unsigned long len);

extern unsigned long guest_stack_size;

const char *get_elf_cpu_model(uint32_t eflags);

abi_ulong get_elf_hwcap(CPUState *cs);
abi_ulong get_elf_hwcap2(CPUState *cs);
const char *elf_hwcap_str(uint32_t bit);
const char *elf_hwcap2_str(uint32_t bit);
const char *get_elf_platform(CPUState *cs);
const char *get_elf_base_platform(CPUState *cs);
bool init_guest_commpage(void);

struct target_elf_gregset_t;
void elf_core_copy_regs(struct target_elf_gregset_t *, const CPUArchState *);

typedef struct {
    const uint8_t *image;
    const uint32_t *relocs;
    unsigned image_size;
    unsigned reloc_count;
    unsigned sigreturn_ofs;
    unsigned rt_sigreturn_ofs;
    unsigned sigreturn_region_start_ofs;
    unsigned sigreturn_region_end_ofs;
} VdsoImageInfo;

const VdsoImageInfo *get_vdso_image_info(uint32_t elf_flags);

bool arch_parse_elf_property(uint32_t pr_type, uint32_t pr_datasz,
                             const uint32_t *data,
                             struct image_info *info,
                             Error **errp);

#endif
