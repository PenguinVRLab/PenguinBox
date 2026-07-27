#ifndef HW_ARM_ALLWINNER_A10_H
#define HW_ARM_ALLWINNER_A10_H

#include "hw/timer/allwinner-a10-pit.h"
#include "hw/intc/allwinner-a10-pic.h"
#include "hw/net/allwinner_emac.h"
#include "hw/sd/allwinner-sdhost.h"
#include "hw/ide/ahci-sysbus.h"
#include "hw/usb/hcd-ohci.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/rtc/allwinner-rtc.h"
#include "hw/misc/allwinner-a10-ccm.h"
#include "hw/misc/allwinner-a10-dramc.h"
#include "hw/i2c/allwinner-i2c.h"
#include "hw/ssi/allwinner-a10-spi.h"
#include "hw/watchdog/allwinner-wdt.h"
#include "system/block-backend.h"

#include "target/arm/cpu.h"
#include "qom/object.h"


#define AW_A10_SDRAM_BASE       0x40000000

#define AW_A10_NUM_USB          2

#define TYPE_AW_A10 "allwinner-a10"
OBJECT_DECLARE_SIMPLE_TYPE(AwA10State, AW_A10)

struct AwA10State {
    DeviceState parent_obj;

    ARMCPU cpu;
    AwA10ClockCtlState ccm;
    AwA10DramControllerState dramc;
    AwA10PITState timer;
    AwA10PICState intc;
    AwEmacState emac;
    AllwinnerAHCIState sata;
    AwSdHostState mmc0;
    AWI2CState i2c0;
    AWA10SPIState spi0;
    AwRtcState rtc;
    AwWdtState wdt;
    MemoryRegion sram_a;
    EHCISysBusState ehci[AW_A10_NUM_USB];
    OHCISysBusState ohci[AW_A10_NUM_USB];
};

void allwinner_a10_bootrom_setup(AwA10State *s, BlockBackend *blk);

#endif
