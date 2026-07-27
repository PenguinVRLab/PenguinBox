#ifndef I386_TARGET_SYSCALL_H
#define I386_TARGET_SYSCALL_H

#define __USER_CS	(0x23)
#define __USER_DS	(0x2B)

#define TARGET_LDT_ENTRIES      8192
#define TARGET_LDT_ENTRY_SIZE	8

#define TARGET_GDT_ENTRIES             9
#define TARGET_GDT_ENTRY_TLS_ENTRIES   3
#define TARGET_GDT_ENTRY_TLS_MIN       6
#define TARGET_GDT_ENTRY_TLS_MAX       (TARGET_GDT_ENTRY_TLS_MIN + TARGET_GDT_ENTRY_TLS_ENTRIES - 1)

struct target_modify_ldt_ldt_s {
    unsigned int  entry_number;
    abi_ulong base_addr;
    unsigned int limit;
    unsigned int flags;
};

#define TARGET_BIOSSEG		0x0f000

#define TARGET_CPU_086		0
#define TARGET_CPU_186		1
#define TARGET_CPU_286		2
#define TARGET_CPU_386		3
#define TARGET_CPU_486		4
#define TARGET_CPU_586		5

#define TARGET_VM86_SIGNAL	0
#define TARGET_VM86_UNKNOWN	1
#define TARGET_VM86_INTx	2
#define TARGET_VM86_STI	3

#define TARGET_VM86_PICRETURN	4
#define TARGET_VM86_TRAP	6

#define TARGET_VM86_PLUS_INSTALL_CHECK	0
#define TARGET_VM86_ENTER		1
#define TARGET_VM86_ENTER_NO_BYPASS	2
#define	TARGET_VM86_REQUEST_IRQ	3
#define TARGET_VM86_FREE_IRQ		4
#define TARGET_VM86_GET_IRQ_BITS	5
#define TARGET_VM86_GET_AND_RESET_IRQ	6

struct target_vm86_regs {
	abi_long ebx;
	abi_long ecx;
	abi_long edx;
	abi_long esi;
	abi_long edi;
	abi_long ebp;
	abi_long eax;
	abi_long __null_ds;
	abi_long __null_es;
	abi_long __null_fs;
	abi_long __null_gs;
	abi_long orig_eax;
	abi_long eip;
	unsigned short cs, __csh;
	abi_long eflags;
	abi_long esp;
	unsigned short ss, __ssh;
	unsigned short es, __esh;
	unsigned short ds, __dsh;
	unsigned short fs, __fsh;
	unsigned short gs, __gsh;
};

struct target_revectored_struct {
	abi_ulong __map[8];
};

struct target_vm86_struct {
	struct target_vm86_regs regs;
	abi_ulong flags;
	abi_ulong screen_bitmap;
	abi_ulong cpu_type;
	struct target_revectored_struct int_revectored;
	struct target_revectored_struct int21_revectored;
};

#define TARGET_VM86_SCREEN_BITMAP	0x0001

struct target_vm86plus_info_struct {
        abi_ulong flags;
#define TARGET_force_return_for_pic (1 << 0)
#define TARGET_vm86dbg_active       (1 << 1)
#define TARGET_vm86dbg_TFpendig     (1 << 2)
#define TARGET_is_vm86pus           (1 << 31)
	unsigned char vm86dbg_intxxtab[32];
};

struct target_vm86plus_struct {
	struct target_vm86_regs regs;
	abi_ulong flags;
	abi_ulong screen_bitmap;
	abi_ulong cpu_type;
	struct target_revectored_struct int_revectored;
	struct target_revectored_struct int21_revectored;
	struct target_vm86plus_info_struct vm86plus;
};

#define UNAME_MACHINE "i686"
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_CLONE_BACKWARDS
#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4
#define TARGET_WANT_OLD_SYS_SELECT

#endif
