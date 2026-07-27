/*
 *  cpu to uname machine name map
 *
 *  Copyright (c) 2009 Loïc Minier
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
#include "user-internals.h"
#include "uname.h"

const char *cpu_to_uname_machine(CPUArchState *cpu_env)
{
#if defined(TARGET_ARM) && !defined(TARGET_AARCH64)

    /* utsname machine name on linux arm is CPU arch name + endianness, e.g.
     * armv7l; to get a list of CPU arch names from the linux source, use:
     *     grep arch_name: -A1 linux/arch/arm/mm/proc-*.S
     * see arch/arm/kernel/setup.c: setup_processor()
     */

#if TARGET_BIG_ENDIAN
#define utsname_suffix "b"
#else
#define utsname_suffix "l"
#endif
    if (arm_feature(cpu_env, ARM_FEATURE_V7))
        return "armv7" utsname_suffix;
    if (arm_feature(cpu_env, ARM_FEATURE_V6))
        return "armv6" utsname_suffix;
    return "armv5te" utsname_suffix;
#elif defined(TARGET_I386) && !defined(TARGET_X86_64)
    CPUState *cpu = env_cpu(cpu_env);
    int family = object_property_get_int(OBJECT(cpu), "family", NULL);
    if (family == 4) {
        return "i486";
    }
    if (family == 5) {
        return "i586";
    }
    return "i686";
#else
    return UNAME_MACHINE;
#endif
}


#define COPY_UTSNAME_FIELD(dest, src) \
  do { \
      memcpy((dest), (src), MIN(sizeof(src), sizeof(dest))); \
      (dest)[sizeof(dest) - 1] = '\0'; \
  } while (0)

int sys_uname(struct new_utsname *buf)
{
  struct utsname uts_buf;

  if (uname(&uts_buf) < 0)
      return (-1);

  memset(buf, 0, sizeof(*buf));
  COPY_UTSNAME_FIELD(buf->sysname, uts_buf.sysname);
  COPY_UTSNAME_FIELD(buf->nodename, uts_buf.nodename);
  COPY_UTSNAME_FIELD(buf->release, uts_buf.release);
  COPY_UTSNAME_FIELD(buf->version, uts_buf.version);
  COPY_UTSNAME_FIELD(buf->machine, uts_buf.machine);
#ifdef _GNU_SOURCE
  COPY_UTSNAME_FIELD(buf->domainname, uts_buf.domainname);
#endif
  return (0);

#undef COPY_UTSNAME_FIELD
}

static int relstr_to_int(const char *s)
{
    int i, n, tmp;

    tmp = 0;
    for (i = 0; i < 3; i++) {
        n = 0;
        while (*s >= '0' && *s <= '9') {
            n *= 10;
            n += *s - '0';
            s++;
        }
        tmp = (tmp << 8) + n;
        if (*s == '.') {
            s++;
        }
    }
    return tmp;
}

int get_osversion(void)
{
    static int osversion;
    struct new_utsname buf;
    const char *s;

    if (osversion)
        return osversion;
    if (qemu_uname_release && *qemu_uname_release) {
        s = qemu_uname_release;
    } else {
        if (sys_uname(&buf))
            return 0;
        s = buf.release;
    }
    osversion = relstr_to_int(s);
    return osversion;
}

void init_qemu_uname_release(void)
{
    struct new_utsname buf;

    if (qemu_uname_release && *qemu_uname_release) {
        return;
    }

    if (sys_uname(&buf)) {
        return;
    }

    if (relstr_to_int(buf.release) < relstr_to_int(UNAME_MINIMUM_RELEASE)) {
        qemu_uname_release = UNAME_MINIMUM_RELEASE;
    }
}
