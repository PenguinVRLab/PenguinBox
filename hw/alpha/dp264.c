/*
 * QEMU Alpha DP264/CLIPPER hardware system emulator.
 *
 * Choose CLIPPER IRQ mappings over, say, DP264, MONET, or WEBBRICK
 * variants because CLIPPER doesn't have an SMC669 SuperIO controller
 * that we need to emulate as well.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/target_page.h"
#include "elf.h"
#include "hw/loader.h"
#include "alpha_sys.h"
#include "qemu/error-report.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/ide/pci.h"
#include "hw/isa/superio.h"
#include "net/net.h"
#include "qemu/cutils.h"
#include "qemu/datadir.h"

static uint64_t cpu_alpha_superpage_to_phys(void *opaque, uint64_t addr)
{
    if (((addr >> 41) & 3) == 2) {
        addr &= 0xffffffffffull;
    }
    return addr;
}

static int clipper_pci_map_irq(PCIDevice *d, int irq_num)
{
    int slot = d->devfn >> 3;

    assert(irq_num >= 0 && irq_num <= 3);

    return (slot + 1) * 4 + irq_num;
}

static void clipper_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    AlphaCPU *cpus[4];
    PCIBus *pci_bus;
    PCIDevice *pci_dev;
    DeviceState *i82378_dev;
    ISABus *isa_bus;
    qemu_irq rtc_irq;
    qemu_irq isa_irq;
    long size, i;
    char *palcode_filename;
    uint64_t palcode_entry;
    uint64_t kernel_entry, kernel_low;
    unsigned int smp_cpus = machine->smp.cpus;

    memset(cpus, 0, sizeof(cpus));
    for (i = 0; i < smp_cpus; ++i) {
        cpus[i] = ALPHA_CPU(cpu_create(machine->cpu_type));
    }

    cpus[0]->env.trap_arg0 = ram_size;
    cpus[0]->env.trap_arg1 = 0;
    cpus[0]->env.trap_arg2 = smp_cpus | (!machine->enable_graphics << 6);

    pci_bus = typhoon_init(machine->ram, &isa_irq, &rtc_irq, cpus,
                           clipper_pci_map_irq, PCI_DEVFN(1, 0));

    i82378_dev = DEVICE(pci_create_simple(pci_bus, PCI_DEVFN(7, 0), "i82378"));
    isa_bus = ISA_BUS(qdev_get_child_bus(i82378_dev, "isa.0"));

    qdev_connect_gpio_out(i82378_dev, 0, isa_irq);

    mc146818_rtc_init(isa_bus, 1900, rtc_irq);

    pci_vga_init(pci_bus);

    pci_init_nic_devices(pci_bus, mc->default_nic);

    isa_create_simple(isa_bus, TYPE_SMC37C669_SUPERIO);

    pci_dev = pci_create_simple(pci_bus, -1, "cmd646-ide");
    pci_ide_create_devs(pci_dev);

    palcode_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS,
                                      machine->firmware ?: "palcode-clipper");
    if (palcode_filename == NULL) {
        error_report("no palcode provided");
        exit(1);
    }
    size = load_elf(palcode_filename, NULL, cpu_alpha_superpage_to_phys,
                    NULL, &palcode_entry, NULL, NULL, NULL,
                    ELFDATA2LSB, EM_ALPHA, 0, 0);
    if (size < 0) {
        error_report("could not load palcode '%s'", palcode_filename);
        exit(1);
    }
    g_free(palcode_filename);

    for (i = 0; i < smp_cpus; ++i) {
        cpus[i]->env.pc = palcode_entry;
        cpus[i]->env.palbr = palcode_entry;
    }

    if (kernel_filename) {
        uint64_t param_offset;

        size = load_elf(kernel_filename, NULL, cpu_alpha_superpage_to_phys,
                        NULL, &kernel_entry, &kernel_low, NULL, NULL,
                        ELFDATA2LSB, EM_ALPHA, 0, 0);
        if (size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }

        cpus[0]->env.trap_arg1 = kernel_entry;

        param_offset = kernel_low - 0x6000;

        if (kernel_cmdline) {
            pstrcpy_targphys("cmdline", param_offset, 0x100, kernel_cmdline);
        }

        if (initrd_filename) {
            long initrd_base;
            int64_t initrd_size;

            initrd_size = get_image_size(initrd_filename, NULL);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }

            initrd_base = (ram_size - initrd_size) & TARGET_PAGE_MASK;
            load_image_targphys(initrd_filename, initrd_base,
                                ram_size - initrd_base, NULL);

            address_space_stq(&address_space_memory, param_offset + 0x100,
                              initrd_base + 0xfffffc0000000000ULL,
                              MEMTXATTRS_UNSPECIFIED,
                              NULL);
            address_space_stq(&address_space_memory, param_offset + 0x108,
                              initrd_size, MEMTXATTRS_UNSPECIFIED, NULL);
        }
    }
}

static void clipper_machine_init(MachineClass *mc)
{
    mc->desc = "Alpha DP264/CLIPPER";
    mc->init = clipper_init;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = 4;
    mc->is_default = true;
    mc->default_cpu_type = ALPHA_CPU_TYPE_NAME("ev67");
    mc->default_ram_id = "ram";
    mc->default_nic = "e1000";
}

DEFINE_MACHINE("clipper", clipper_machine_init)
