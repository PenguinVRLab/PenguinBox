#ifndef OPENPIC_H
#define OPENPIC_H

#include "hw/sysbus.h"
#include "hw/core/cpu.h"
#include "qom/object.h"

#define MAX_CPU     32
#define MAX_MSI     8
#define VID         0x03

enum {
    OPENPIC_OUTPUT_INT = 0,
    OPENPIC_OUTPUT_CINT,
    OPENPIC_OUTPUT_MCK,
    OPENPIC_OUTPUT_DEBUG,
    OPENPIC_OUTPUT_RESET,
    OPENPIC_OUTPUT_NB,
};

typedef struct IrqLines { qemu_irq irq[OPENPIC_OUTPUT_NB]; } IrqLines;

#define OPENPIC_MODEL_FSL_MPIC_20 1
#define OPENPIC_MODEL_FSL_MPIC_42 2
#define OPENPIC_MODEL_KEYLARGO    3

#define OPENPIC_MAX_SRC     256
#define OPENPIC_MAX_TMR     4
#define OPENPIC_MAX_IPI     4
#define OPENPIC_MAX_IRQ     (OPENPIC_MAX_SRC + OPENPIC_MAX_IPI + \
                             OPENPIC_MAX_TMR)

#define KEYLARGO_MAX_CPU  4
#define KEYLARGO_MAX_EXT  64
#define KEYLARGO_MAX_IPI  4
#define KEYLARGO_MAX_IRQ  (64 + KEYLARGO_MAX_IPI)
#define KEYLARGO_MAX_TMR  0
#define KEYLARGO_IPI_IRQ  (KEYLARGO_MAX_EXT)
#define KEYLARGO_TMR_IRQ  (KEYLARGO_IPI_IRQ + KEYLARGO_MAX_IPI)

typedef struct FslMpicInfo {
    int max_ext;
} FslMpicInfo;

typedef enum IRQType {
    IRQ_TYPE_NORMAL = 0,
    IRQ_TYPE_FSLINT,
    IRQ_TYPE_FSLSPECIAL,
} IRQType;

#define IRQQUEUE_SIZE_BITS ROUND_UP(OPENPIC_MAX_IRQ, 64)

typedef struct IRQQueue {
    unsigned long *queue;
    int32_t queue_size;
    int next;
    int priority;
} IRQQueue;

typedef struct IRQSource {
    uint32_t ivpr;
    uint32_t idr;
    uint32_t destmask;
    int last_cpu;
    int output;
    int pending;
    IRQType type;
    bool level:1;
    bool nomask:1;
} IRQSource;

#define IVPR_MASK_SHIFT       31
#define IVPR_MASK_MASK        (1U << IVPR_MASK_SHIFT)
#define IVPR_ACTIVITY_SHIFT   30
#define IVPR_ACTIVITY_MASK    (1U << IVPR_ACTIVITY_SHIFT)
#define IVPR_MODE_SHIFT       29
#define IVPR_MODE_MASK        (1U << IVPR_MODE_SHIFT)
#define IVPR_POLARITY_SHIFT   23
#define IVPR_POLARITY_MASK    (1U << IVPR_POLARITY_SHIFT)
#define IVPR_SENSE_SHIFT      22
#define IVPR_SENSE_MASK       (1U << IVPR_SENSE_SHIFT)

#define IVPR_PRIORITY_MASK     (0xFU << 16)
#define IVPR_PRIORITY(_ivprr_) ((int)(((_ivprr_) & IVPR_PRIORITY_MASK) >> 16))
#define IVPR_VECTOR(opp, _ivprr_) ((_ivprr_) & (opp)->vector_mask)

#define IDR_EP      0x80000000
#define IDR_CI      0x40000000

typedef struct OpenPICTimer {
    uint32_t tccr;
    uint32_t tbcr;
    int                   n_IRQ;
    bool                  qemu_timer_active;
    struct QEMUTimer     *qemu_timer;
    struct OpenPICState  *opp;
    uint64_t              origin_time;
} OpenPICTimer;

typedef struct OpenPICMSI {
    uint32_t msir;
} OpenPICMSI;

typedef struct IRQDest {
    int32_t ctpr;
    IRQQueue raised;
    IRQQueue servicing;
    qemu_irq *irqs;

    uint32_t outputs_active[OPENPIC_OUTPUT_NB];
} IRQDest;

#define TYPE_OPENPIC "openpic"
OBJECT_DECLARE_SIMPLE_TYPE(OpenPICState, OPENPIC)

struct OpenPICState {
    SysBusDevice parent_obj;

    MemoryRegion mem;

    FslMpicInfo *fsl;
    uint32_t model;
    uint32_t flags;
    uint32_t nb_irqs;
    uint32_t vid;
    uint32_t vir;
    uint32_t vector_mask;
    uint32_t tfrr_reset;
    uint32_t ivpr_reset;
    uint32_t idr_reset;
    uint32_t brr1;
    uint32_t mpic_mode_mask;

    MemoryRegion sub_io_mem[6];

    uint32_t frr;
    uint32_t gcr;
    uint32_t pir;
    uint32_t spve;
    uint32_t tfrr;
    IRQSource src[OPENPIC_MAX_IRQ];
    IRQDest dst[MAX_CPU];
    uint32_t nb_cpus;
    OpenPICTimer timers[OPENPIC_MAX_TMR];
    uint32_t max_tmr;

    OpenPICMSI msi[MAX_MSI];
    uint32_t max_irq;
    uint32_t irq_ipi0;
    uint32_t irq_tim0;
    uint32_t irq_msi;
};

#endif
