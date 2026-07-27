/*
 *  mmap support for qemu
 *
 *  Copyright (c) 2003 - 2008 Fabrice Bellard
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
#include "qemu/osdep.h"
#include "exec/mmap-lock.h"
#include "exec/page-protection.h"
#include "user/page-protection.h"

#include "qemu.h"

static pthread_mutex_t mmap_mutex = PTHREAD_MUTEX_INITIALIZER;
static __thread int mmap_lock_count;

void mmap_lock(void)
{
    if (mmap_lock_count++ == 0) {
        pthread_mutex_lock(&mmap_mutex);
    }
}

void mmap_unlock(void)
{
    assert(mmap_lock_count > 0);
    if (--mmap_lock_count == 0) {
        pthread_mutex_unlock(&mmap_mutex);
    }
}

bool have_mmap_lock(void)
{
    return mmap_lock_count > 0 ? true : false;
}

void mmap_fork_start(void)
{
    if (mmap_lock_count)
        abort();
    pthread_mutex_lock(&mmap_mutex);
}

void mmap_fork_end(int child)
{
    if (child)
        pthread_mutex_init(&mmap_mutex, NULL);
    else
        pthread_mutex_unlock(&mmap_mutex);
}

int target_mprotect(abi_ulong start, abi_ulong len, int prot)
{
    abi_ulong end, host_start, host_end, addr;
    int prot1, ret;

    qemu_log_mask(CPU_LOG_PAGE, "mprotect: start=0x" TARGET_ABI_FMT_lx
                  " len=0x" TARGET_ABI_FMT_lx " prot=%c%c%c\n", start, len,
                  prot & PROT_READ ? 'r' : '-',
                  prot & PROT_WRITE ? 'w' : '-',
                  prot & PROT_EXEC ? 'x' : '-');
    if ((start & ~TARGET_PAGE_MASK) != 0)
        return -EINVAL;
    len = TARGET_PAGE_ALIGN(len);
    end = start + len;
    if (end < start)
        return -EINVAL;
    prot &= PROT_READ | PROT_WRITE | PROT_EXEC;
    if (len == 0)
        return 0;

    mmap_lock();
    host_start = start & qemu_host_page_mask;
    host_end = HOST_PAGE_ALIGN(end);
    if (start > host_start) {
        prot1 = prot;
        for (addr = host_start; addr < start; addr += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(addr);
        }
        if (host_end == host_start + qemu_host_page_size) {
            for (addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
                prot1 |= page_get_flags(addr);
            }
            end = host_end;
        }
        ret = mprotect(g2h_untagged(host_start),
                       qemu_host_page_size, prot1 & PAGE_RWX);
        if (ret != 0)
            goto error;
        host_start += qemu_host_page_size;
    }
    if (end < host_end) {
        prot1 = prot;
        for (addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(addr);
        }
        ret = mprotect(g2h_untagged(host_end - qemu_host_page_size),
                       qemu_host_page_size, prot1 & PAGE_RWX);
        if (ret != 0)
            goto error;
        host_end -= qemu_host_page_size;
    }

    if (host_start < host_end) {
        ret = mprotect(g2h_untagged(host_start), host_end - host_start, prot);
        if (ret != 0)
            goto error;
    }
    page_set_flags(start, start + len - 1, prot, PAGE_RWX);
    mmap_unlock();
    return 0;
error:
    mmap_unlock();
    return ret;
}

static bool mmap_pread(int fd, void *p, size_t len, off_t offset, bool zero)
{
    while (1) {
        ssize_t r = pread(fd, p, len, offset);

        if (likely(r == len)) {
            return true;
        }
        if (r == 0) {
            if (zero) {
                memset(p, 0, len);
            }
            return true;
        }
        if (r > 0) {
            p += r;
            len -= r;
            offset += r;
        } else if (errno != EINTR) {
            return false;
        }
    }
}

static int mmap_frag(abi_ulong real_start,
                     abi_ulong start, abi_ulong end,
                     int prot, int flags, int fd, abi_ulong offset)
{
    abi_ulong real_end, addr;
    void *host_start;
    int prot1, prot_new;

    real_end = real_start + qemu_host_page_size;
    host_start = g2h_untagged(real_start);

    prot1 = 0;
    for (addr = real_start; addr < real_end; addr++) {
        if (addr < start || addr >= end)
            prot1 |= page_get_flags(addr);
    }

    if (prot1 == 0) {
        void *p = mmap(host_start, qemu_host_page_size, prot,
                       flags | ((fd != -1) ? MAP_ANON : 0), -1, 0);
        if (p == MAP_FAILED)
            return -1;
        prot1 = prot;
    }
    prot1 &= PAGE_RWX;

    prot_new = prot | prot1;
    if (fd != -1) {
        if ((flags & TARGET_BSD_MAP_FLAGMASK) == MAP_SHARED &&
            (prot & PROT_WRITE))
            return -1;

        if (!(prot1 & PROT_WRITE))
            mprotect(host_start, qemu_host_page_size, prot1 | PROT_WRITE);

        if (!mmap_pread(fd, g2h_untagged(start), end - start, offset, true)) {
            return -1;
        }

        if (prot_new != (prot1 | PROT_WRITE))
            mprotect(host_start, qemu_host_page_size, prot_new);
    } else {
        if (prot_new != prot1) {
            mprotect(host_start, qemu_host_page_size, prot_new);
        }
        if (prot_new & PROT_WRITE) {
            memset(g2h_untagged(start), 0, end - start);
        }
    }
    return 0;
}

#if HOST_LONG_BITS == 64 && TARGET_ABI_BITS == 64
# define TASK_UNMAPPED_BASE  (1ul << 38)
#else
# define TASK_UNMAPPED_BASE  0x40000000
#endif
abi_ulong mmap_next_start = TASK_UNMAPPED_BASE;

static abi_ulong mmap_find_vma_reserved(abi_ulong start, abi_ulong size,
                                        abi_ulong alignment)
{
    abi_ulong ret;

    ret = page_find_range_empty(start, reserved_va, size, alignment);
    if (ret == -1 && start > TARGET_PAGE_SIZE) {
        ret = page_find_range_empty(TARGET_PAGE_SIZE, start - 1,
                                    size, alignment);
    }

    return ret;
}

abi_ulong mmap_find_vma(abi_ulong start, abi_ulong size, abi_ulong alignment)
{
    void *ptr, *prev;
    abi_ulong addr;
    int flags;
    int wrapped, repeat;

    if (start == 0) {
        start = mmap_next_start;
    } else {
        start &= qemu_host_page_mask;
    }

    size = HOST_PAGE_ALIGN(size);

    if (reserved_va) {
        return mmap_find_vma_reserved(start, size,
            (alignment != 0 ? 1 << alignment :
             MAX(qemu_host_page_size, TARGET_PAGE_SIZE)));
    }

    addr = start;
    wrapped = repeat = 0;
    prev = 0;
    flags = MAP_ANON | MAP_PRIVATE;
    if (alignment != 0) {
        flags |= MAP_ALIGNED(alignment);
    }

    for (;; prev = ptr) {
        ptr = mmap(g2h_untagged(addr), size, PROT_NONE,
                   flags, -1, 0);

        if (ptr == MAP_FAILED) {
            return (abi_ulong)-1;
        }

        repeat = (ptr == prev ? repeat + 1 : 0);

        if (h2g_valid(ptr + size - 1)) {
            addr = h2g(ptr);

            if ((addr & ~TARGET_PAGE_MASK) == 0) {
                if (start == mmap_next_start && addr >= TASK_UNMAPPED_BASE) {
                    mmap_next_start = addr + size;
                }
                return addr;
            }

            switch (repeat) {
            case 0:
                addr = TARGET_PAGE_ALIGN(addr);
                break;
            case 1:
                addr &= TARGET_PAGE_MASK;
                break;
            case 2:
                addr = 0;
                break;
            default:
                addr = -1;
                break;
            }
        } else {
            addr = (repeat ? -1 : 0);
        }

        munmap(ptr, size);

        if (addr == (abi_ulong)-1) {
            return (abi_ulong)-1;
        } else if (addr == 0) {
            if (wrapped) {
                return (abi_ulong)-1;
            }
            wrapped = 1;
            addr = TARGET_PAGE_SIZE;
        } else if (wrapped && addr >= start) {
            return (abi_ulong)-1;
        }
    }
}

abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, off_t offset)
{
    abi_ulong ret, end, real_start, real_end, retaddr, host_offset, host_len;

    mmap_lock();
    if (qemu_loglevel_mask(CPU_LOG_PAGE)) {
        qemu_log("mmap: start=0x" TARGET_ABI_FMT_lx
                 " len=0x" TARGET_ABI_FMT_lx " prot=%c%c%c flags=",
                 start, len,
                 prot & PROT_READ ? 'r' : '-',
                 prot & PROT_WRITE ? 'w' : '-',
                 prot & PROT_EXEC ? 'x' : '-');
        if (flags & MAP_ALIGNMENT_MASK) {
            qemu_log("MAP_ALIGNED(%u) ",
                     (flags & MAP_ALIGNMENT_MASK) >> MAP_ALIGNMENT_SHIFT);
        }
        if (flags & MAP_GUARD) {
            qemu_log("MAP_GUARD ");
        }
        if (flags & MAP_FIXED) {
            qemu_log("MAP_FIXED ");
        }
        if (flags & MAP_ANON) {
            qemu_log("MAP_ANON ");
        }
        if (flags & MAP_EXCL) {
            qemu_log("MAP_EXCL ");
        }
        if (flags & MAP_PRIVATE) {
            qemu_log("MAP_PRIVATE ");
        }
        if (flags & MAP_SHARED) {
            qemu_log("MAP_SHARED ");
        }
        if (flags & MAP_NOCORE) {
            qemu_log("MAP_NOCORE ");
        }
        if (flags & MAP_STACK) {
            qemu_log("MAP_STACK ");
        }
        qemu_log("fd=%d offset=0x%lx\n", fd, offset);
    }

    if ((flags & MAP_ANON) && fd != -1) {
        errno = EINVAL;
        goto fail;
    }
    if (flags & MAP_STACK) {
        if ((fd != -1) || ((prot & (PROT_READ | PROT_WRITE)) !=
                    (PROT_READ | PROT_WRITE))) {
            errno = EINVAL;
            goto fail;
        }
    }
    if ((flags & MAP_GUARD) && (prot != PROT_NONE || fd != -1 ||
        offset != 0 || (flags & (MAP_SHARED | MAP_PRIVATE |
        MAP_PREFAULT_READ | MAP_ANON | MAP_STACK)) != 0)) {
        errno = EINVAL;
        goto fail;
    }

    if (offset & ~TARGET_PAGE_MASK) {
        errno = EINVAL;
        goto fail;
    }

    if (len == 0) {
        errno = EINVAL;
        goto fail;
    }

    len = TARGET_PAGE_ALIGN(len);
    if (len == 0) {
        errno = ENOMEM;
        goto fail;
    }

    real_start = start & qemu_host_page_mask;
    host_offset = offset & qemu_host_page_mask;

    if (!(flags & MAP_FIXED)) {
        abi_ulong alignment;

        host_len = len + offset - host_offset;
        host_len = HOST_PAGE_ALIGN(host_len);
        alignment = (flags & MAP_ALIGNMENT_MASK) >> MAP_ALIGNMENT_SHIFT;
        start = mmap_find_vma(real_start, host_len, alignment);
        if (start == (abi_ulong)-1) {
            errno = ENOMEM;
            goto fail;
        }
    }

    if ((qemu_real_host_page_size() < qemu_host_page_size) && fd != -1) {
        struct stat sb;

        if (fstat(fd, &sb) == -1) {
            goto fail;
        }

        if (offset + len > sb.st_size) {
            len = REAL_HOST_PAGE_ALIGN(sb.st_size - offset);
        }
    }

    if (!(flags & MAP_FIXED)) {
        unsigned long host_start;
        void *p;

        host_len = len + offset - host_offset;
        host_len = HOST_PAGE_ALIGN(host_len);

        p = mmap(g2h_untagged(start), host_len, prot,
                 flags | MAP_FIXED | ((fd != -1) ? MAP_ANON : 0), -1, 0);
        if (p == MAP_FAILED)
            goto fail;
        host_start = (unsigned long)p;
        if (fd != -1) {
            p = mmap(g2h_untagged(start), len, prot,
                     flags | MAP_FIXED, fd, host_offset);
            if (p == MAP_FAILED) {
                munmap(g2h_untagged(start), host_len);
                goto fail;
            }
            host_start += offset - host_offset;
        }
        start = h2g(host_start);
    } else {
        if (start & ~TARGET_PAGE_MASK) {
            errno = EINVAL;
            goto fail;
        }
        end = start + len;
        real_end = HOST_PAGE_ALIGN(end);

        if (!guest_range_valid_untagged(start, len)) {
            errno = EINVAL;
            goto fail;
        }

        if (fd != -1 &&
            (offset & ~qemu_host_page_mask) != (start & ~qemu_host_page_mask)) {
            if ((flags & TARGET_BSD_MAP_FLAGMASK) == MAP_SHARED &&
                (prot & PROT_WRITE)) {
                errno = EINVAL;
                goto fail;
            }
            retaddr = target_mmap(start, len, prot | PROT_WRITE,
                                  MAP_FIXED | MAP_PRIVATE | MAP_ANON,
                                  -1, 0);
            if (retaddr == -1)
                goto fail;
            if (!mmap_pread(fd, g2h_untagged(start), len, offset, false)) {
                goto fail;
            }
            if (!(prot & PROT_WRITE)) {
                ret = target_mprotect(start, len, prot);
                assert(ret == 0);
            }
            goto the_end;
        }

        if ((flags & MAP_EXCL) && !page_check_range_empty(start, end - 1)) {
            errno = EINVAL;
            goto fail;
        }

        if (start > real_start) {
            if (real_end == real_start + qemu_host_page_size) {
                ret = mmap_frag(real_start, start, end,
                                prot, flags, fd, offset);
                if (ret == -1)
                    goto fail;
                goto the_end1;
            }
            ret = mmap_frag(real_start, start, real_start + qemu_host_page_size,
                            prot, flags, fd, offset);
            if (ret == -1)
                goto fail;
            real_start += qemu_host_page_size;
        }
        if (end < real_end) {
            ret = mmap_frag(real_end - qemu_host_page_size,
                            real_end - qemu_host_page_size, end,
                            prot, flags, fd,
                            offset + real_end - qemu_host_page_size - start);
            if (ret == -1)
                goto fail;
            real_end -= qemu_host_page_size;
        }

        if (real_start < real_end) {
            void *p;
            unsigned long offset1;
            if (flags & MAP_ANON)
                offset1 = 0;
            else
                offset1 = offset + real_start - start;
            p = mmap(g2h_untagged(real_start), real_end - real_start,
                     prot, flags, fd, offset1);
            if (p == MAP_FAILED)
                goto fail;
        }
    }
 the_end1:
    page_set_flags(start, start + len - 1, prot | PAGE_VALID, PAGE_VALID);
 the_end:
#ifdef DEBUG_MMAP
    printf("ret=0x" TARGET_ABI_FMT_lx "\n", start);
    page_dump(stdout);
    printf("\n");
#endif
    mmap_unlock();
    return start;
fail:
    mmap_unlock();
    return -1;
}

void mmap_reserve(abi_ulong start, abi_ulong size)
{
    abi_ulong real_start;
    abi_ulong real_end;
    abi_ulong addr;
    abi_ulong end;
    int prot;

    real_start = start & qemu_host_page_mask;
    real_end = HOST_PAGE_ALIGN(start + size);
    end = start + size;
    if (start > real_start) {
        prot = 0;
        for (addr = real_start; addr < start; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (real_end == real_start + qemu_host_page_size) {
            for (addr = end; addr < real_end; addr += TARGET_PAGE_SIZE) {
                prot |= page_get_flags(addr);
            }
            end = real_end;
        }
        if (prot != 0) {
            real_start += qemu_host_page_size;
        }
    }
    if (end < real_end) {
        prot = 0;
        for (addr = end; addr < real_end; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (prot != 0) {
            real_end -= qemu_host_page_size;
        }
    }
    if (real_start != real_end) {
        mmap(g2h_untagged(real_start), real_end - real_start, PROT_NONE,
                 MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
    }
}

int target_munmap(abi_ulong start, abi_ulong len)
{
    abi_ulong end, real_start, real_end, addr;
    int prot, ret;

#ifdef DEBUG_MMAP
    printf("munmap: start=0x" TARGET_ABI_FMT_lx " len=0x"
           TARGET_ABI_FMT_lx "\n",
           start, len);
#endif
    if (start & ~TARGET_PAGE_MASK)
        return -EINVAL;
    len = TARGET_PAGE_ALIGN(len);
    if (len == 0)
        return -EINVAL;
    mmap_lock();
    end = start + len;
    real_start = start & qemu_host_page_mask;
    real_end = HOST_PAGE_ALIGN(end);

    if (start > real_start) {
        prot = 0;
        for (addr = real_start; addr < start; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (real_end == real_start + qemu_host_page_size) {
            for (addr = end; addr < real_end; addr += TARGET_PAGE_SIZE) {
                prot |= page_get_flags(addr);
            }
            end = real_end;
        }
        if (prot != 0)
            real_start += qemu_host_page_size;
    }
    if (end < real_end) {
        prot = 0;
        for (addr = end; addr < real_end; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (prot != 0)
            real_end -= qemu_host_page_size;
    }

    ret = 0;
    if (real_start < real_end) {
        if (reserved_va) {
            mmap_reserve(real_start, real_end - real_start);
        } else {
            ret = munmap(g2h_untagged(real_start), real_end - real_start);
        }
    }

    if (ret == 0) {
        page_set_flags(start, start + len - 1, 0, PAGE_VALID);
    }
    mmap_unlock();
    return ret;
}

int target_msync(abi_ulong start, abi_ulong len, int flags)
{
    abi_ulong end;

    if (start & ~TARGET_PAGE_MASK)
        return -EINVAL;
    len = TARGET_PAGE_ALIGN(len);
    end = start + len;
    if (end < start)
        return -EINVAL;
    if (end == start)
        return 0;

    start &= qemu_host_page_mask;
    return msync(g2h_untagged(start), end - start, flags);
}
