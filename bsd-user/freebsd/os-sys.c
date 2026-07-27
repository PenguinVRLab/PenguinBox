/*
 *  FreeBSD sysctl() and sysarch() system call emulation
 *
 *  Copyright (c) 2013-15 Stacey D. Son
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
#include "qemu.h"
#include "target_arch_sysarch.h"

#include <sys/sysctl.h>

static const int guest_ctl_size[CTLTYPE + 1] = {
        [CTLTYPE_INT] = sizeof(abi_int),
        [CTLTYPE_UINT] = sizeof(abi_uint),
        [CTLTYPE_LONG] = sizeof(abi_long),
        [CTLTYPE_ULONG] = sizeof(abi_ulong),
        [CTLTYPE_S8] = sizeof(int8_t),
        [CTLTYPE_S16] = sizeof(int16_t),
        [CTLTYPE_S32] = sizeof(int32_t),
        [CTLTYPE_S64] = sizeof(int64_t),
        [CTLTYPE_U8] = sizeof(uint8_t),
        [CTLTYPE_U16] = sizeof(uint16_t),
        [CTLTYPE_U32] = sizeof(uint32_t),
        [CTLTYPE_U64] = sizeof(uint64_t),
};

static const int host_ctl_size[CTLTYPE + 1] = {
        [CTLTYPE_INT] = sizeof(int),
        [CTLTYPE_UINT] = sizeof(u_int),
        [CTLTYPE_LONG] = sizeof(long),
        [CTLTYPE_ULONG] = sizeof(u_long),
        [CTLTYPE_S8] = sizeof(int8_t),
        [CTLTYPE_S16] = sizeof(int16_t),
        [CTLTYPE_S32] = sizeof(int32_t),
        [CTLTYPE_S64] = sizeof(int64_t),
        [CTLTYPE_U8] = sizeof(uint8_t),
        [CTLTYPE_U16] = sizeof(uint16_t),
        [CTLTYPE_U32] = sizeof(uint32_t),
        [CTLTYPE_U64] = sizeof(uint64_t),
};

#ifdef TARGET_ABI32
static const abi_ulong guest_max_mem = UINT32_MAX - 0x100c000 + 1;

static abi_ulong cap_memory(uint64_t mem)
{
    return MIN(guest_max_mem, mem);
}
#endif

static abi_ulong scale_to_guest_pages(uint64_t pages)
{
    pages = muldiv64(pages, qemu_real_host_page_size(), TARGET_PAGE_SIZE);
#ifdef TARGET_ABI32
    pages = MIN(pages, guest_max_mem / (abi_ulong)TARGET_PAGE_SIZE);
#endif
    return pages;
}

#ifdef TARGET_ABI32
static abi_long h2g_long_sat(long l)
{
    if (l > INT32_MAX) {
        l = INT32_MAX;
    } else if (l < INT32_MIN) {
        l = INT32_MIN;
    }
    return l;
}

static abi_ulong h2g_ulong_sat(u_long ul)
{
    return MIN(ul, UINT32_MAX);
}
#endif

#define bsd_get_ncpu() 1

static int oidfmt(int *oid, int len, char *fmt, uint32_t *kind)
{
    int qoid[CTL_MAXNAME + 2];
    uint8_t buf[BUFSIZ];
    int i;
    size_t j;

    qoid[0] = CTL_SYSCTL;
    qoid[1] = CTL_SYSCTL_OIDFMT;
    memcpy(qoid + 2, oid, len * sizeof(int));

    j = sizeof(buf);
    i = sysctl(qoid, len + 2, buf, &j, 0, 0);
    if (i) {
        return i;
    }

    if (kind) {
        *kind = *(uint32_t *)buf;
    }

    if (fmt) {
        strcpy(fmt, (char *)(buf + sizeof(uint32_t)));
    }
    return 0;
}

static void h2g_old_sysctl(void *holdp, size_t *holdlen, uint32_t kind)
{
    size_t len;
    int hlen, glen;
    uint8_t *hp, *gp;

    gp = hp = (uint8_t *)holdp;
    len = 0;
    hlen = host_ctl_size[kind & CTLTYPE];
    glen = guest_ctl_size[kind & CTLTYPE];

    if (hlen == 0) {
        return;
    }

    while (len < *holdlen) {
        if (hlen == glen) {
            switch (hlen) {
            case 1:
                break;
            case 2:
                *(uint16_t *)gp = tswap16(*(uint16_t *)hp);
                break;
            case 4:
                *(uint32_t *)gp = tswap32(*(uint32_t *)hp);
                break;
            case 8:
                *(uint64_t *)gp = tswap64(*(uint64_t *)hp);
                break;
            default:
                g_assert_not_reached();
            }
        } else {
#ifdef TARGET_ABI32
            switch (kind & CTLTYPE) {
            case CTLTYPE_LONG:
                *(abi_long *)gp = tswap32(h2g_long_sat(*(long *)hp));
                break;
            case CTLTYPE_ULONG:
                *(abi_ulong *)gp = tswap32(h2g_ulong_sat(*(u_long *)hp));
                break;
            default:
                g_assert_not_reached();
            }
#else
            g_assert_not_reached();
#endif
        }
        gp += glen;
        hp += hlen;
        len += hlen;
    }
#ifdef TARGET_ABI32
    if (hlen != glen) {
        *holdlen = (*holdlen / hlen) * glen;
    }
#endif
}

static inline void sysctl_name2oid(uint32_t *holdp, size_t holdlen)
{
    size_t i, num = holdlen / sizeof(uint32_t);

    for (i = 0; i < num; i++) {
        holdp[i] = tswap32(holdp[i]);
    }
}

static inline void sysctl_oidfmt(uint32_t *holdp)
{
    holdp[0] = tswap32(holdp[0]);
}

static abi_long do_freebsd_sysctl_oid(CPUArchState *env, int32_t *snamep,
        int32_t namelen, void *holdp, size_t *holdlenp, void *hnewp,
        size_t newlen)
{
    uint32_t kind = 0;
    abi_long ret;
    size_t holdlen, oldlen;
#ifdef TARGET_ABI32
    void *old_holdp;
#endif

    holdlen = oldlen = *holdlenp;
    oidfmt(snamep, namelen, NULL, &kind);

    switch (snamep[0]) {
    case CTL_KERN:
        switch (snamep[1]) {
        case KERN_USRSTACK:
            if (oldlen) {
                (*(abi_ulong *)holdp) = tswapal(TARGET_USRSTACK);
            }
            holdlen = sizeof(abi_ulong);
            ret = 0;
            goto out;

        case KERN_PS_STRINGS:
            if (oldlen) {
                (*(abi_ulong *)holdp) = tswapal(TARGET_PS_STRINGS);
            }
            holdlen = sizeof(abi_ulong);
            ret = 0;
            goto out;

        default:
            break;
        }
        break;

    case CTL_HW:
        switch (snamep[1]) {
        case HW_MACHINE:
            holdlen = sizeof(TARGET_HW_MACHINE);
            if (holdp) {
                strlcpy(holdp, TARGET_HW_MACHINE, oldlen);
            }
            ret = 0;
            goto out;

        case HW_MACHINE_ARCH:
        {
            holdlen = sizeof(TARGET_HW_MACHINE_ARCH);
            if (holdp) {
                strlcpy(holdp, TARGET_HW_MACHINE_ARCH, oldlen);
            }
            ret = 0;
            goto out;
        }
        case HW_NCPU:
            if (oldlen) {
                (*(abi_int *)holdp) = tswap32(bsd_get_ncpu());
            }
            holdlen = sizeof(int32_t);
            ret = 0;
            goto out;
#if defined(TARGET_ARM)
        case HW_FLOATINGPT:
            if (oldlen) {
                ARMCPU *cpu = env_archcpu(env);
                *(abi_int *)holdp = cpu_isar_feature(aa32_vfp, cpu);
            }
            holdlen = sizeof(abi_int);
            ret = 0;
            goto out;
#endif


#ifdef TARGET_ABI32
        case HW_PHYSMEM:
        case HW_USERMEM:
        case HW_REALMEM:
            holdlen = sizeof(abi_ulong);
            ret = 0;

            if (oldlen) {
                int mib[2] = {snamep[0], snamep[1]};
                unsigned long lvalue;
                size_t len = sizeof(lvalue);

                if (sysctl(mib, 2, &lvalue, &len, NULL, 0) == -1) {
                    ret = -1;
                } else {
                    lvalue = cap_memory(lvalue);
                    (*(abi_ulong *)holdp) = tswapal((abi_ulong)lvalue);
                }
            }
            goto out;
#endif

        default:
        {
            static int oid_hw_availpages;
            static int oid_hw_pagesizes;

            if (!oid_hw_availpages) {
                int real_oid[CTL_MAXNAME + 2];
                size_t len = sizeof(real_oid) / sizeof(int);

                if (sysctlnametomib("hw.availpages", real_oid, &len) >= 0) {
                    oid_hw_availpages = real_oid[1];
                }
            }
            if (!oid_hw_pagesizes) {
                int real_oid[CTL_MAXNAME + 2];
                size_t len = sizeof(real_oid) / sizeof(int);

                if (sysctlnametomib("hw.pagesizes", real_oid, &len) >= 0) {
                    oid_hw_pagesizes = real_oid[1];
                }
            }

            if (oid_hw_availpages && snamep[1] == oid_hw_availpages) {
                long lvalue;
                size_t len = sizeof(lvalue);

                if (sysctlbyname("hw.availpages", &lvalue, &len, NULL, 0) == -1) {
                    ret = -1;
                } else {
                    if (oldlen) {
                        lvalue = scale_to_guest_pages(lvalue);
                        (*(abi_ulong *)holdp) = tswapal((abi_ulong)lvalue);
                    }
                    holdlen = sizeof(abi_ulong);
                    ret = 0;
                }
                goto out;
            }

            if (oid_hw_pagesizes && snamep[1] == oid_hw_pagesizes) {
                if (oldlen) {
                    (*(abi_ulong *)holdp) = tswapal((abi_ulong)TARGET_PAGE_SIZE);
                    ((abi_ulong *)holdp)[1] = 0;
                }
                holdlen = sizeof(abi_ulong) * 2;
                ret = 0;
                goto out;
            }
            break;
        }
        }
        break;

    default:
        break;
    }

#ifdef TARGET_ABI32
    if (kind == CTLTYPE_LONG || kind == CTLTYPE_ULONG) {
        old_holdp = holdp;
        holdlen = holdlen * 2;
        holdp = g_malloc(holdlen);
    }
#endif

    ret = get_errno(sysctl(snamep, namelen, holdp, &holdlen, hnewp, newlen));
    if (!ret && (holdp != 0)) {

        if (snamep[0] == CTL_SYSCTL) {
            switch (snamep[1]) {
            case CTL_SYSCTL_NEXT:
            case CTL_SYSCTL_NAME2OID:
            case CTL_SYSCTL_NEXTNOSKIP:
                sysctl_name2oid(holdp, holdlen);
                break;

            case CTL_SYSCTL_OIDFMT:
                sysctl_oidfmt(holdp);
                break;
            case CTL_SYSCTL_OIDDESCR:
            case CTL_SYSCTL_OIDLABEL:
            default:
                h2g_old_sysctl(holdp, &holdlen, kind);
                break;
            }
        } else {
            h2g_old_sysctl(holdp, &holdlen, kind);
#ifdef TARGET_ABI32
            if (kind == CTLTYPE_LONG || kind == CTLTYPE_ULONG) {
                memcpy(old_holdp, holdp, holdlen);
                g_free(holdp);
                holdp = old_holdp;
            }
#endif
        }
    }

out:
    *holdlenp = holdlen;
    return ret;
}

abi_long do_freebsd_sysctlbyname(CPUArchState *env, abi_ulong namep,
        int32_t namelen, abi_ulong oldp, abi_ulong oldlenp, abi_ulong newp,
        abi_ulong newlen)
{
    abi_long ret = -TARGET_EFAULT;
    void *holdp = NULL, *hnewp = NULL;
    char *snamep = NULL;
    int oid[CTL_MAXNAME + 2];
    size_t holdlen, oidplen;
    abi_ulong oldlen = 0;

    if (oldlenp) {
        if (!access_ok(VERIFY_WRITE, oldlenp, sizeof(abi_ulong)) ||
            get_user_ual(oldlen, oldlenp)) {
            goto out;
        }
    }
    snamep = lock_user_string(namep);
    if (snamep == NULL) {
        goto out;
    }
    if (newp) {
        hnewp = lock_user(VERIFY_READ, newp, newlen, 1);
        if (hnewp == NULL) {
            goto out;
        }
    }
    if (oldp) {
        holdp = lock_user(VERIFY_WRITE, oldp, oldlen, 0);
        if (holdp == NULL) {
            goto out;
        }
    }
    holdlen = oldlen;

    oidplen = ARRAY_SIZE(oid);
    if (sysctlnametomib(snamep, oid, &oidplen) != 0) {
        ret = -TARGET_EINVAL;
        goto out;
    }

    ret = do_freebsd_sysctl_oid(env, oid, oidplen, holdp, &holdlen, hnewp,
        newlen);

    if (oldlenp && (ret == 0 || ret == -TARGET_ENOMEM)) {
        put_user_ual(holdlen, oldlenp);
    }
out:
    unlock_user(snamep, namep, 0);
    unlock_user(holdp, oldp, ret == 0 ? holdlen : 0);
    unlock_user(hnewp, newp, 0);

    return ret;
}

abi_long do_freebsd_sysctl(CPUArchState *env, abi_ulong namep, int32_t namelen,
        abi_ulong oldp, abi_ulong oldlenp, abi_ulong newp, abi_ulong newlen)
{
    abi_long ret = -TARGET_EFAULT;
    void *hnamep, *holdp = NULL, *hnewp = NULL;
    size_t holdlen;
    abi_ulong oldlen = 0;
    int32_t *snamep = g_malloc(sizeof(int32_t) * namelen), *p, *q, i;

    if (oldlenp) {
        if (!access_ok(VERIFY_WRITE, oldlenp, sizeof(abi_ulong)) ||
            get_user_ual(oldlen, oldlenp)) {
            goto out;
        }
    }
    hnamep = lock_user(VERIFY_READ, namep, namelen, 1);
    if (hnamep == NULL) {
        goto out;
    }
    if (newp) {
        hnewp = lock_user(VERIFY_READ, newp, newlen, 1);
        if (hnewp == NULL) {
            goto out;
        }
    }
    if (oldp) {
        holdp = lock_user(VERIFY_WRITE, oldp, oldlen, 0);
        if (holdp == NULL) {
            goto out;
        }
    }
    holdlen = oldlen;
    for (p = hnamep, q = snamep, i = 0; i < namelen; p++, i++, q++) {
        *q = tswap32(*p);
    }

    ret = do_freebsd_sysctl_oid(env, snamep, namelen, holdp, &holdlen, hnewp,
        newlen);

    if (oldlenp && (ret == 0 || ret == -TARGET_ENOMEM)) {
        put_user_ual(holdlen, oldlenp);
    }
    unlock_user(hnamep, namep, 0);
    unlock_user(holdp, oldp, ret == 0 ? holdlen : 0);
out:
    g_free(snamep);
    return ret;
}

abi_long do_freebsd_sysarch(void *cpu_env, abi_long arg1, abi_long arg2)
{
    return do_freebsd_arch_sysarch(cpu_env, arg1, arg2);
}
