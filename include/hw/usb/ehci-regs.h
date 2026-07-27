#ifndef HW_USB_EHCI_REGS_H
#define HW_USB_EHCI_REGS_H

#define CAPLENGTH        0x0000
#define HCIVERSION       0x0002
#define HCSPARAMS        0x0004
#define HCCPARAMS        0x0008
#define EECP             HCCPARAMS + 1
#define HCSPPORTROUTE1   0x000c
#define HCSPPORTROUTE2   0x0010

#define USBCMD           0x0000
#define USBCMD_RUNSTOP   (1 << 0)
#define USBCMD_HCRESET   (1 << 1)
#define USBCMD_FLS       (3 << 2)
#define USBCMD_FLS_SH    2
#define USBCMD_PSE       (1 << 4)
#define USBCMD_ASE       (1 << 5)
#define USBCMD_IAAD      (1 << 6)
#define USBCMD_LHCR      (1 << 7)
#define USBCMD_ASPMC     (3 << 8)
#define USBCMD_ASPME     (1 << 11)
#define USBCMD_ITC       (0x7f << 16)
#define USBCMD_ITC_SH    16

#define USBSTS           0x0004
#define USBSTS_RO_MASK   0x0000003f
#define USBSTS_INT       (1 << 0)
#define USBSTS_ERRINT    (1 << 1)
#define USBSTS_PCD       (1 << 2)
#define USBSTS_FLR       (1 << 3)
#define USBSTS_HSE       (1 << 4)
#define USBSTS_IAA       (1 << 5)
#define USBSTS_HALT      (1 << 12)
#define USBSTS_REC       (1 << 13)
#define USBSTS_PSS       (1 << 14)
#define USBSTS_ASS       (1 << 15)

#define USBINTR              0x0008
#define USBINTR_MASK         0x0000003f

#define FRINDEX              0x000c
#define CTRLDSSEGMENT        0x0010
#define PERIODICLISTBASE     0x0014
#define ASYNCLISTADDR        0x0018
#define ASYNCLISTADDR_MASK   0xffffffe0

#define CONFIGFLAG           0x0040

#define PORTSC_RO_MASK       0x007001c0
#define PORTSC_RWC_MASK      0x0000002a
#define PORTSC_WKOC_E        (1 << 22)
#define PORTSC_WKDS_E        (1 << 21)
#define PORTSC_WKCN_E        (1 << 20)
#define PORTSC_PTC           (15 << 16)
#define PORTSC_PTC_SH        16
#define PORTSC_PIC           (3 << 14)
#define PORTSC_PIC_SH        14
#define PORTSC_POWNER        (1 << 13)
#define PORTSC_PPOWER        (1 << 12)
#define PORTSC_LINESTAT      (3 << 10)
#define PORTSC_LINESTAT_SH   10
#define PORTSC_PRESET        (1 << 8)
#define PORTSC_SUSPEND       (1 << 7)
#define PORTSC_FPRES         (1 << 6)
#define PORTSC_OCC           (1 << 5)
#define PORTSC_OCA           (1 << 4)
#define PORTSC_PEDC          (1 << 3)
#define PORTSC_PED           (1 << 2)
#define PORTSC_CSC           (1 << 1)
#define PORTSC_CONNECT       (1 << 0)

#endif
