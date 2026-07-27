/*
 * TI OMAP DMA gigacell.
 *
 * Copyright (C) 2006-2008 Andrzej Zaborowski  <balrog@zabor.org>
 * Copyright (C) 2007-2008 Lauro Ramos Venancio  <lauro.venancio@indt.org.br>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/arm/omap.h"
#include "hw/irq.h"
#include "hw/arm/soc_dma.h"

struct omap_dma_channel_s {
    int burst[2];
    int pack[2];
    int endian[2];
    int endian_lock[2];
    int translate[2];
    enum omap_dma_port port[2];
    hwaddr addr[2];
    omap_dma_addressing_t mode[2];
    uint32_t elements;
    uint16_t frames;
    int32_t frame_index[2];
    int16_t element_index[2];
    int data_type;

    int transparent_copy;
    int constant_fill;
    uint32_t color;
    int prefetch;

    int end_prog;
    int repeat;
    int auto_init;
    int link_enabled;
    int link_next_ch;

    int interrupts;
    int status;
    int cstatus;

    int active;
    int enable;
    int sync;
    int src_sync;
    int pending_request;
    int waiting_end_prog;
    uint16_t cpc;
    int set_update;

    int fs;
    int bs;

    int omap_3_1_compatible_disable;

    qemu_irq irq;
    struct omap_dma_channel_s *sibling;

    struct omap_dma_reg_set_s {
        hwaddr src, dest;
        int frame;
        int element;
        int pck_element;
        int frame_delta[2];
        int elem_delta[2];
        int frames;
        int elements;
        int pck_elements;
    } active_set;

    struct soc_dma_ch_s *dma;

    int write_mode;
    int priority;
    int interleave_disabled;
    int type;
    int suspend;
    int buf_disable;
};

struct omap_dma_s {
    struct soc_dma_s *dma;
    MemoryRegion iomem;

    struct omap_mpu_state_s *mpu;
    omap_clk clk;
    qemu_irq irq[4];
    void (*intr_update)(struct omap_dma_s *s);
    enum omap_dma_model model;
    int omap_3_1_mapping_disabled;

    uint32_t gcr;
    uint32_t ocp;
    uint32_t caps[5];
    uint32_t irqen[4];
    uint32_t irqstat[4];

    int chans;
    struct omap_dma_channel_s ch[32];
    struct omap_dma_lcd_channel_s lcd_ch;
};

#define TIMEOUT_INTR    (1 << 0)
#define EVENT_DROP_INTR (1 << 1)
#define HALF_FRAME_INTR (1 << 2)
#define END_FRAME_INTR  (1 << 3)
#define LAST_FRAME_INTR (1 << 4)
#define END_BLOCK_INTR  (1 << 5)
#define SYNC            (1 << 6)
#define END_PKT_INTR    (1 << 7)
#define TRANS_ERR_INTR  (1 << 8)
#define MISALIGN_INTR   (1 << 11)

static inline void omap_dma_interrupts_update(struct omap_dma_s *s)
{
    s->intr_update(s);
}

static void omap_dma_channel_load(struct omap_dma_channel_s *ch)
{
    struct omap_dma_reg_set_s *a = &ch->active_set;
    int i, normal;
    int omap_3_1 = !ch->omap_3_1_compatible_disable;

    a->src = ch->addr[0];
    a->dest = ch->addr[1];
    a->frames = ch->frames;
    a->elements = ch->elements;
    a->pck_elements = ch->frame_index[!ch->src_sync];
    a->frame = 0;
    a->element = 0;
    a->pck_element = 0;

    if (unlikely(!ch->elements || !ch->frames)) {
        printf("%s: bad DMA request\n", __func__);
        return;
    }

    for (i = 0; i < 2; i ++)
        switch (ch->mode[i]) {
        case constant:
            a->elem_delta[i] = 0;
            a->frame_delta[i] = 0;
            break;
        case post_incremented:
            a->elem_delta[i] = ch->data_type;
            a->frame_delta[i] = 0;
            break;
        case single_index:
            a->elem_delta[i] = ch->data_type +
                    ch->element_index[omap_3_1 ? 0 : i] - 1;
            a->frame_delta[i] = 0;
            break;
        case double_index:
            a->elem_delta[i] = ch->data_type +
                    ch->element_index[omap_3_1 ? 0 : i] - 1;
            a->frame_delta[i] = ch->frame_index[omap_3_1 ? 0 : i] -
                    ch->element_index[omap_3_1 ? 0 : i];
            break;
        default:
            break;
        }

    normal = !ch->transparent_copy && !ch->constant_fill &&
            (ch->endian[0] | ch->endian_lock[0]) ==
            (ch->endian[1] | ch->endian_lock[1]);
    for (i = 0; i < 2; i ++) {
        if (!a->elem_delta[i] && normal &&
                        (a->frames == 1 || !a->frame_delta[i]))
            ch->dma->type[i] = soc_dma_access_const;
        else if (a->elem_delta[i] == ch->data_type && normal &&
                        (a->frames == 1 || !a->frame_delta[i]))
            ch->dma->type[i] = soc_dma_access_linear;
        else
            ch->dma->type[i] = soc_dma_access_other;

        ch->dma->vaddr[i] = ch->addr[i];
    }
    soc_dma_ch_update(ch->dma);
}

static void omap_dma_activate_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    if (!ch->active) {
        if (ch->set_update) {
            omap_dma_channel_load(ch);
            ch->set_update = 0;
        }

        ch->active = 1;
        soc_dma_set_request(ch->dma, 1);
        if (ch->sync)
            ch->status |= SYNC;
    }
}

static void omap_dma_deactivate_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    ch->cpc = ch->active_set.dest & 0xffff;

    if (ch->pending_request && !ch->waiting_end_prog && ch->enable) {
        ch->pending_request = 0;
        return;
    }

    if (ch->sync && ch->enable && (s->dma->drqbmp & (1ULL << ch->sync)))
        return;

    if (ch->active) {
        ch->active = 0;
        ch->status &= ~SYNC;
        soc_dma_set_request(ch->dma, 0);
    }
}

static void omap_dma_enable_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    if (!ch->enable) {
        ch->enable = 1;
        ch->waiting_end_prog = 0;
        omap_dma_channel_load(ch);
        if ((!ch->sync) || (s->dma->drqbmp & (1ULL << ch->sync))) {
            omap_dma_activate_channel(s, ch);
        }
    }
}

static void omap_dma_disable_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    if (ch->enable) {
        ch->enable = 0;
        ch->pending_request = 0;
        omap_dma_deactivate_channel(s, ch);
    }
}

static void omap_dma_channel_end_prog(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    if (ch->waiting_end_prog) {
        ch->waiting_end_prog = 0;
        if (!ch->sync || ch->pending_request) {
            ch->pending_request = 0;
            omap_dma_activate_channel(s, ch);
        }
    }
}

static void omap_dma_interrupts_3_1_update(struct omap_dma_s *s)
{
    struct omap_dma_channel_s *ch = s->ch;

    if (ch[0].status | ch[6].status)
        qemu_irq_raise(ch[0].irq);
    if (ch[1].status | ch[7].status)
        qemu_irq_raise(ch[1].irq);
    if (ch[2].status | ch[8].status)
        qemu_irq_raise(ch[2].irq);
    if (ch[3].status)
        qemu_irq_raise(ch[3].irq);
    if (ch[4].status)
        qemu_irq_raise(ch[4].irq);
    if (ch[5].status)
        qemu_irq_raise(ch[5].irq);
}

static void omap_dma_interrupts_3_2_update(struct omap_dma_s *s)
{
    struct omap_dma_channel_s *ch = s->ch;
    int i;

    for (i = s->chans; i; ch ++, i --)
        if (ch->status)
            qemu_irq_raise(ch->irq);
}

static void omap_dma_enable_3_1_mapping(struct omap_dma_s *s)
{
    s->omap_3_1_mapping_disabled = 0;
    s->chans = 9;
    s->intr_update = omap_dma_interrupts_3_1_update;
}

static void omap_dma_disable_3_1_mapping(struct omap_dma_s *s)
{
    s->omap_3_1_mapping_disabled = 1;
    s->chans = 16;
    s->intr_update = omap_dma_interrupts_3_2_update;
}

static void omap_dma_process_request(struct omap_dma_s *s, int request)
{
    int channel;
    int drop_event = 0;
    struct omap_dma_channel_s *ch = s->ch;

    for (channel = 0; channel < s->chans; channel ++, ch ++) {
        if (ch->enable && ch->sync == request) {
            if (!ch->active)
                omap_dma_activate_channel(s, ch);
            else if (!ch->pending_request)
                ch->pending_request = 1;
            else {
                ch->status |= EVENT_DROP_INTR;
                drop_event = 1;
            }
        }
    }

    if (drop_event)
        omap_dma_interrupts_update(s);
}

static void omap_dma_transfer_generic(struct soc_dma_ch_s *dma)
{
    uint8_t value[4];
    struct omap_dma_channel_s *ch = dma->opaque;
    struct omap_dma_reg_set_s *a = &ch->active_set;
    int bytes = dma->bytes;
#ifdef MULTI_REQ
    uint16_t status = ch->status;
#endif

    do {
        if (!ch->constant_fill)
            cpu_physical_memory_read(a->src, value, ch->data_type);
        else
            *(uint32_t *) value = ch->color;

        if (!ch->transparent_copy || *(uint32_t *) value != ch->color)
            cpu_physical_memory_write(a->dest, value, ch->data_type);

        a->src += a->elem_delta[0];
        a->dest += a->elem_delta[1];
        a->element ++;

#ifndef MULTI_REQ
        if (a->element == a->elements) {
            a->element = 0;
            a->src += a->frame_delta[0];
            a->dest += a->frame_delta[1];
            a->frame ++;

            if (!ch->sync)
                ch->cpc = a->dest & 0xffff;
        }
    } while ((bytes -= ch->data_type));
#else
        if (ch->sync && !ch->fs && !ch->bs)
            omap_dma_deactivate_channel(s, ch);

        if (a->element == 1 && a->frame == a->frames - 1)
            if (ch->interrupts & LAST_FRAME_INTR)
                ch->status |= LAST_FRAME_INTR;

        if (a->element == (a->elements >> 1))
            if (ch->interrupts & HALF_FRAME_INTR)
                ch->status |= HALF_FRAME_INTR;

        if (ch->fs && ch->bs) {
            a->pck_element ++;
            if (a->pck_element == a->pck_elements) {
                a->pck_element = 0;

                if ((ch->interrupts & END_PKT_INTR) && !ch->src_sync)
                    ch->status |= END_PKT_INTR;

                if (ch->sync)
                    omap_dma_deactivate_channel(s, ch);
            }
        }

        if (a->element == a->elements) {
            a->element = 0;
            a->src += a->frame_delta[0];
            a->dest += a->frame_delta[1];
            a->frame ++;

            if (ch->sync && ch->fs && !ch->bs)
                omap_dma_deactivate_channel(s, ch);

            if (!ch->sync)
                ch->cpc = a->dest & 0xffff;

            if (ch->interrupts & END_FRAME_INTR)
                ch->status |= END_FRAME_INTR;

            if (a->frame == a->frames) {

                if (ch->omap_3_1_compatible_disable) {
                    omap_dma_disable_channel(s, ch);
                    if (ch->link_enabled)
                        omap_dma_enable_channel(s,
                                        &s->ch[ch->link_next_ch]);
                } else {
                    if (!ch->auto_init)
                        omap_dma_disable_channel(s, ch);
                    else if (ch->repeat || ch->end_prog)
                        omap_dma_channel_load(ch);
                    else {
                        ch->waiting_end_prog = 1;
                        omap_dma_deactivate_channel(s, ch);
                    }
                }

                if (ch->interrupts & END_BLOCK_INTR)
                    ch->status |= END_BLOCK_INTR;
            }
        }
    } while (status == ch->status && ch->active);

    omap_dma_interrupts_update(s);
#endif
}

enum {
    omap_dma_intr_element_sync,
    omap_dma_intr_last_frame,
    omap_dma_intr_half_frame,
    omap_dma_intr_frame,
    omap_dma_intr_frame_sync,
    omap_dma_intr_packet,
    omap_dma_intr_packet_sync,
    omap_dma_intr_block,
    __omap_dma_intr_last,
};

static void omap_dma_transfer_setup(struct soc_dma_ch_s *dma)
{
    struct omap_dma_port_if_s *src_p, *dest_p;
    struct omap_dma_reg_set_s *a;
    struct omap_dma_channel_s *ch = dma->opaque;
    struct omap_dma_s *s = dma->dma->opaque;
    int frames, min_elems, elements[__omap_dma_intr_last];

    a = &ch->active_set;

    src_p = &s->mpu->port[ch->port[0]];
    dest_p = &s->mpu->port[ch->port[1]];
    if ((!ch->constant_fill && !src_p->addr_valid(s->mpu, a->src)) ||
                    (!dest_p->addr_valid(s->mpu, a->dest))) {
#if 0
        if (ch->interrupts & TIMEOUT_INTR)
            ch->status |= TIMEOUT_INTR;
        omap_dma_deactivate_channel(s, ch);
        continue;
#endif
        printf("%s: Bus time-out in DMA%i operation\n",
                        __func__, dma->num);
    }

    min_elems = INT_MAX;

#define INTR_CHECK(cond, id, nelements) \
    if (cond) {         \
        elements[id] = nelements;   \
        if (elements[id] < min_elems)   \
            min_elems = elements[id];   \
    } else              \
        elements[id] = INT_MAX;

    INTR_CHECK(
                    ch->sync && !ch->fs && !ch->bs,
                    omap_dma_intr_element_sync,
                    1)

    INTR_CHECK(
                    (ch->interrupts & LAST_FRAME_INTR) &&
                    ((a->frame < a->frames - 1) || !a->element),
                    omap_dma_intr_last_frame,
                    (a->frames - a->frame - 2) * a->elements +
                    (a->elements - a->element + 1))
    INTR_CHECK(
                    ch->interrupts & HALF_FRAME_INTR,
                    omap_dma_intr_half_frame,
                    (a->elements >> 1) +
                    (a->element >= (a->elements >> 1) ? a->elements : 0) -
                    a->element)
    INTR_CHECK(
                    ch->sync && ch->fs && (ch->interrupts & END_FRAME_INTR),
                    omap_dma_intr_frame,
                    a->elements - a->element)
    INTR_CHECK(
                    ch->sync && ch->fs && !ch->bs,
                    omap_dma_intr_frame_sync,
                    a->elements - a->element)

    INTR_CHECK(
                    ch->fs && ch->bs &&
                    (ch->interrupts & END_PKT_INTR) && !ch->src_sync,
                    omap_dma_intr_packet,
                    a->pck_elements - a->pck_element)
    INTR_CHECK(
                    ch->fs && ch->bs && ch->sync,
                    omap_dma_intr_packet_sync,
                    a->pck_elements - a->pck_element)

    INTR_CHECK(
                    1,
                    omap_dma_intr_block,
                    (a->frames - a->frame - 1) * a->elements +
                    (a->elements - a->element))

    dma->bytes = min_elems * ch->data_type;

#ifdef MULTI_REQ
    if (dma->update) {
#endif

        if (min_elems == elements[omap_dma_intr_element_sync])
            omap_dma_deactivate_channel(s, ch);

        if (min_elems == elements[omap_dma_intr_last_frame])
            ch->status |= LAST_FRAME_INTR;

        if (min_elems == elements[omap_dma_intr_half_frame])
            ch->status |= HALF_FRAME_INTR;

        if (min_elems == elements[omap_dma_intr_packet])
            ch->status |= END_PKT_INTR;

        if (min_elems == elements[omap_dma_intr_packet_sync])
            omap_dma_deactivate_channel(s, ch);

        if (min_elems == elements[omap_dma_intr_frame_sync])
            omap_dma_deactivate_channel(s, ch);

        if (min_elems == elements[omap_dma_intr_frame])
            ch->status |= END_FRAME_INTR;

        if (min_elems == elements[omap_dma_intr_block]) {

            if (ch->omap_3_1_compatible_disable) {
                omap_dma_disable_channel(s, ch);
                if (ch->link_enabled)
                    omap_dma_enable_channel(s, &s->ch[ch->link_next_ch]);
            } else {
                if (!ch->auto_init)
                    omap_dma_disable_channel(s, ch);
                else if (ch->repeat || ch->end_prog)
                    omap_dma_channel_load(ch);
                else {
                    ch->waiting_end_prog = 1;
                    omap_dma_deactivate_channel(s, ch);
                }
            }

            if (ch->interrupts & END_BLOCK_INTR)
                ch->status |= END_BLOCK_INTR;
        }

        if (ch->fs && ch->bs) {
            a->pck_element += min_elems;
            a->pck_element %= a->pck_elements;
        }

#ifndef MULTI_REQ
        if (dma->update) {
#endif
            a->element += min_elems;

            frames = a->element / a->elements;
            a->element = a->element % a->elements;
            a->frame += frames;
            a->src += min_elems * a->elem_delta[0] + frames * a->frame_delta[0];
            a->dest += min_elems * a->elem_delta[1] + frames * a->frame_delta[1];

            if (!ch->sync && frames)
                ch->cpc = a->dest & 0xffff;

#ifndef MULTI_REQ
        }
#else
    }
#endif

    omap_dma_interrupts_update(s);
}

void omap_dma_reset(struct soc_dma_s *dma)
{
    int i;
    struct omap_dma_s *s = dma->opaque;

    soc_dma_reset(s->dma);
    s->gcr = 0x0004;
    s->ocp = 0x00000000;
    memset(&s->irqstat, 0, sizeof(s->irqstat));
    memset(&s->irqen, 0, sizeof(s->irqen));
    s->lcd_ch.src = emiff;
    s->lcd_ch.condition = 0;
    s->lcd_ch.interrupts = 0;
    s->lcd_ch.dual = 0;
    omap_dma_enable_3_1_mapping(s);
    for (i = 0; i < s->chans; i ++) {
        s->ch[i].suspend = 0;
        s->ch[i].prefetch = 0;
        s->ch[i].buf_disable = 0;
        s->ch[i].src_sync = 0;
        memset(&s->ch[i].burst, 0, sizeof(s->ch[i].burst));
        memset(&s->ch[i].port, 0, sizeof(s->ch[i].port));
        memset(&s->ch[i].mode, 0, sizeof(s->ch[i].mode));
        memset(&s->ch[i].frame_index, 0, sizeof(s->ch[i].frame_index));
        memset(&s->ch[i].element_index, 0, sizeof(s->ch[i].element_index));
        memset(&s->ch[i].endian, 0, sizeof(s->ch[i].endian));
        memset(&s->ch[i].endian_lock, 0, sizeof(s->ch[i].endian_lock));
        memset(&s->ch[i].translate, 0, sizeof(s->ch[i].translate));
        s->ch[i].write_mode = 0;
        s->ch[i].data_type = 0;
        s->ch[i].transparent_copy = 0;
        s->ch[i].constant_fill = 0;
        s->ch[i].color = 0x00000000;
        s->ch[i].end_prog = 0;
        s->ch[i].repeat = 0;
        s->ch[i].auto_init = 0;
        s->ch[i].link_enabled = 0;
        s->ch[i].interrupts = 0x0003;
        s->ch[i].status = 0;
        s->ch[i].cstatus = 0;
        s->ch[i].active = 0;
        s->ch[i].enable = 0;
        s->ch[i].sync = 0;
        s->ch[i].pending_request = 0;
        s->ch[i].waiting_end_prog = 0;
        s->ch[i].cpc = 0x0000;
        s->ch[i].fs = 0;
        s->ch[i].bs = 0;
        s->ch[i].omap_3_1_compatible_disable = 0;
        memset(&s->ch[i].active_set, 0, sizeof(s->ch[i].active_set));
        s->ch[i].priority = 0;
        s->ch[i].interleave_disabled = 0;
        s->ch[i].type = 0;
    }
}

static int omap_dma_ch_reg_read(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch, int reg, uint16_t *value)
{
    switch (reg) {
    case 0x00:
        *value = (ch->burst[1] << 14) |
                (ch->pack[1] << 13) |
                (ch->port[1] << 9) |
                (ch->burst[0] << 7) |
                (ch->pack[0] << 6) |
                (ch->port[0] << 2) |
                (ch->data_type >> 1);
        break;

    case 0x02:
        if (s->model <= omap_dma_3_1)
            *value = 0 << 10;
        else
            *value = ch->omap_3_1_compatible_disable << 10;
        *value |= (ch->mode[1] << 14) |
                (ch->mode[0] << 12) |
                (ch->end_prog << 11) |
                (ch->repeat << 9) |
                (ch->auto_init << 8) |
                (ch->enable << 7) |
                (ch->priority << 6) |
                (ch->fs << 5) | ch->sync;
        break;

    case 0x04:
        *value = ch->interrupts;
        break;

    case 0x06:
        *value = ch->status;
        ch->status &= SYNC;
        if (!ch->omap_3_1_compatible_disable && ch->sibling) {
            *value |= (ch->sibling->status & 0x3f) << 6;
            ch->sibling->status &= SYNC;
        }
        qemu_irq_lower(ch->irq);
        break;

    case 0x08:
        *value = ch->addr[0] & 0x0000ffff;
        break;

    case 0x0a:
        *value = ch->addr[0] >> 16;
        break;

    case 0x0c:
        *value = ch->addr[1] & 0x0000ffff;
        break;

    case 0x0e:
        *value = ch->addr[1] >> 16;
        break;

    case 0x10:
        *value = ch->elements;
        break;

    case 0x12:
        *value = ch->frames;
        break;

    case 0x14:
        *value = ch->frame_index[0];
        break;

    case 0x16:
        *value = ch->element_index[0];
        break;

    case 0x18:
        if (ch->omap_3_1_compatible_disable)
            *value = ch->active_set.src & 0xffff;
        else
            *value = ch->cpc;
        break;

    case 0x1a:
        *value = ch->active_set.dest & 0xffff;
        break;

    case 0x1c:
        *value = ch->element_index[1];
        break;

    case 0x1e:
        *value = ch->frame_index[1];
        break;

    case 0x20:
        *value = ch->color & 0xffff;
        break;

    case 0x22:
        *value = ch->color >> 16;
        break;

    case 0x24:
        *value = (ch->bs << 2) |
                (ch->transparent_copy << 1) |
                ch->constant_fill;
        break;

    case 0x28:
        *value = (ch->link_enabled << 15) |
                (ch->link_next_ch & 0xf);
        break;

    case 0x2a:
        *value = (ch->interleave_disabled << 15) |
                ch->type;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_ch_reg_write(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch, int reg, uint16_t value)
{
    switch (reg) {
    case 0x00:
        ch->burst[1] = (value & 0xc000) >> 14;
        ch->pack[1] = (value & 0x2000) >> 13;
        ch->port[1] = (enum omap_dma_port) ((value & 0x1e00) >> 9);
        ch->burst[0] = (value & 0x0180) >> 7;
        ch->pack[0] = (value & 0x0040) >> 6;
        ch->port[0] = (enum omap_dma_port) ((value & 0x003c) >> 2);
        if (ch->port[0] >= __omap_dma_port_last) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid DMA port %i\n",
                          __func__, ch->port[0]);
        }
        if (ch->port[1] >= __omap_dma_port_last) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid DMA port %i\n",
                          __func__, ch->port[1]);
        }
        ch->data_type = 1 << (value & 3);
        if ((value & 3) == 3) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad data_type for DMA channel\n", __func__);
            ch->data_type >>= 1;
        }
        break;

    case 0x02:
        ch->mode[1] = (omap_dma_addressing_t) ((value & 0xc000) >> 14);
        ch->mode[0] = (omap_dma_addressing_t) ((value & 0x3000) >> 12);
        ch->end_prog = (value & 0x0800) >> 11;
        if (s->model >= omap_dma_3_2)
            ch->omap_3_1_compatible_disable  = (value >> 10) & 0x1;
        ch->repeat = (value & 0x0200) >> 9;
        ch->auto_init = (value & 0x0100) >> 8;
        ch->priority = (value & 0x0040) >> 6;
        ch->fs = (value & 0x0020) >> 5;
        ch->sync = value & 0x001f;

        if (value & 0x0080)
            omap_dma_enable_channel(s, ch);
        else
            omap_dma_disable_channel(s, ch);

        if (ch->end_prog)
            omap_dma_channel_end_prog(s, ch);

        break;

    case 0x04:
        ch->interrupts = value & 0x3f;
        break;

    case 0x06:
        OMAP_RO_REG((hwaddr) reg);
        break;

    case 0x08:
        ch->addr[0] &= 0xffff0000;
        ch->addr[0] |= value;
        break;

    case 0x0a:
        ch->addr[0] &= 0x0000ffff;
        ch->addr[0] |= (uint32_t) value << 16;
        break;

    case 0x0c:
        ch->addr[1] &= 0xffff0000;
        ch->addr[1] |= value;
        break;

    case 0x0e:
        ch->addr[1] &= 0x0000ffff;
        ch->addr[1] |= (uint32_t) value << 16;
        break;

    case 0x10:
        ch->elements = value;
        break;

    case 0x12:
        ch->frames = value;
        break;

    case 0x14:
        ch->frame_index[0] = (int16_t) value;
        break;

    case 0x16:
        ch->element_index[0] = (int16_t) value;
        break;

    case 0x18:
        OMAP_RO_REG((hwaddr) reg);
        break;

    case 0x1c:
        ch->element_index[1] = (int16_t) value;
        break;

    case 0x1e:
        ch->frame_index[1] = (int16_t) value;
        break;

    case 0x20:
        ch->color &= 0xffff0000;
        ch->color |= value;
        break;

    case 0x22:
        ch->color &= 0xffff;
        ch->color |= (uint32_t)value << 16;
        break;

    case 0x24:
        ch->bs = (value >> 2) & 0x1;
        ch->transparent_copy = (value >> 1) & 0x1;
        ch->constant_fill = value & 0x1;
        break;

    case 0x28:
        ch->link_enabled = (value >> 15) & 0x1;
        if (value & (1 << 14)) {
            ch->link_enabled = 0;
            omap_dma_disable_channel(s, ch);
        }
        ch->link_next_ch = value & 0x1f;
        break;

    case 0x2a:
        ch->interleave_disabled = (value >> 15) & 0x1;
        ch->type = value & 0xf;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_3_2_lcd_write(struct omap_dma_lcd_channel_s *s, int offset,
                uint16_t value)
{
    switch (offset) {
    case 0xbc0:
        s->brust_f2 = (value >> 14) & 0x3;
        s->pack_f2 = (value >> 13) & 0x1;
        s->data_type_f2 = (1 << ((value >> 11) & 0x3));
        s->brust_f1 = (value >> 7) & 0x3;
        s->pack_f1 = (value >> 6) & 0x1;
        s->data_type_f1 = (1 << ((value >> 0) & 0x3));
        break;

    case 0xbc2:
        s->mode_f2 = (value >> 14) & 0x3;
        s->mode_f1 = (value >> 12) & 0x3;
        s->end_prog = (value >> 11) & 0x1;
        s->omap_3_1_compatible_disable = (value >> 10) & 0x1;
        s->repeat = (value >> 9) & 0x1;
        s->auto_init = (value >> 8) & 0x1;
        s->running = (value >> 7) & 0x1;
        s->priority = (value >> 6) & 0x1;
        s->bs = (value >> 4) & 0x1;
        break;

    case 0xbc4:
        s->dst = (value >> 8) & 0x1;
        s->src = ((value >> 6) & 0x3) << 1;
        s->condition = 0;
        s->interrupts = (value >> 1) & 1;
        s->dual = value & 1;
        break;

    case 0xbc8:
        s->src_f1_top &= 0xffff0000;
        s->src_f1_top |= 0x0000ffff & value;
        break;

    case 0xbca:
        s->src_f1_top &= 0x0000ffff;
        s->src_f1_top |= (uint32_t)value << 16;
        break;

    case 0xbcc:
        s->src_f1_bottom &= 0xffff0000;
        s->src_f1_bottom |= 0x0000ffff & value;
        break;

    case 0xbce:
        s->src_f1_bottom &= 0x0000ffff;
        s->src_f1_bottom |= (uint32_t) value << 16;
        break;

    case 0xbd0:
        s->src_f2_top &= 0xffff0000;
        s->src_f2_top |= 0x0000ffff & value;
        break;

    case 0xbd2:
        s->src_f2_top &= 0x0000ffff;
        s->src_f2_top |= (uint32_t) value << 16;
        break;

    case 0xbd4:
        s->src_f2_bottom &= 0xffff0000;
        s->src_f2_bottom |= 0x0000ffff & value;
        break;

    case 0xbd6:
        s->src_f2_bottom &= 0x0000ffff;
        s->src_f2_bottom |= (uint32_t) value << 16;
        break;

    case 0xbd8:
        s->element_index_f1 = value;
        break;

    case 0xbda:
        s->frame_index_f1 &= 0xffff0000;
        s->frame_index_f1 |= 0x0000ffff & value;
        break;

    case 0xbf4:
        s->frame_index_f1 &= 0x0000ffff;
        s->frame_index_f1 |= (uint32_t) value << 16;
        break;

    case 0xbdc:
        s->element_index_f2 = value;
        break;

    case 0xbde:
        s->frame_index_f2 &= 0xffff0000;
        s->frame_index_f2 |= 0x0000ffff & value;
        break;

    case 0xbf6:
        s->frame_index_f2 &= 0x0000ffff;
        s->frame_index_f2 |= (uint32_t) value << 16;
        break;

    case 0xbe0:
        s->elements_f1 = value;
        break;

    case 0xbe4:
        s->frames_f1 = value;
        break;

    case 0xbe2:
        s->elements_f2 = value;
        break;

    case 0xbe6:
        s->frames_f2 = value;
        break;

    case 0xbea:
        s->lch_type = value & 0xf;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_3_2_lcd_read(struct omap_dma_lcd_channel_s *s, int offset,
                uint16_t *ret)
{
    switch (offset) {
    case 0xbc0:
        *ret = (s->brust_f2 << 14) |
            (s->pack_f2 << 13) |
            ((s->data_type_f2 >> 1) << 11) |
            (s->brust_f1 << 7) |
            (s->pack_f1 << 6) |
            ((s->data_type_f1 >> 1) << 0);
        break;

    case 0xbc2:
        *ret = (s->mode_f2 << 14) |
            (s->mode_f1 << 12) |
            (s->end_prog << 11) |
            (s->omap_3_1_compatible_disable << 10) |
            (s->repeat << 9) |
            (s->auto_init << 8) |
            (s->running << 7) |
            (s->priority << 6) |
            (s->bs << 4);
        break;

    case 0xbc4:
        qemu_irq_lower(s->irq);
        *ret = (s->dst << 8) |
            ((s->src & 0x6) << 5) |
            (s->condition << 3) |
            (s->interrupts << 1) |
            s->dual;
        break;

    case 0xbc8:
        *ret = s->src_f1_top & 0xffff;
        break;

    case 0xbca:
        *ret = s->src_f1_top >> 16;
        break;

    case 0xbcc:
        *ret = s->src_f1_bottom & 0xffff;
        break;

    case 0xbce:
        *ret = s->src_f1_bottom >> 16;
        break;

    case 0xbd0:
        *ret = s->src_f2_top & 0xffff;
        break;

    case 0xbd2:
        *ret = s->src_f2_top >> 16;
        break;

    case 0xbd4:
        *ret = s->src_f2_bottom & 0xffff;
        break;

    case 0xbd6:
        *ret = s->src_f2_bottom >> 16;
        break;

    case 0xbd8:
        *ret = s->element_index_f1;
        break;

    case 0xbda:
        *ret = s->frame_index_f1 & 0xffff;
        break;

    case 0xbf4:
        *ret = s->frame_index_f1 >> 16;
        break;

    case 0xbdc:
        *ret = s->element_index_f2;
        break;

    case 0xbde:
        *ret = s->frame_index_f2 & 0xffff;
        break;

    case 0xbf6:
        *ret = s->frame_index_f2 >> 16;
        break;

    case 0xbe0:
        *ret = s->elements_f1;
        break;

    case 0xbe4:
        *ret = s->frames_f1;
        break;

    case 0xbe2:
        *ret = s->elements_f2;
        break;

    case 0xbe6:
        *ret = s->frames_f2;
        break;

    case 0xbea:
        *ret = s->lch_type;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_3_1_lcd_write(struct omap_dma_lcd_channel_s *s, int offset,
                uint16_t value)
{
    switch (offset) {
    case 0x300:
        s->src = (value & 0x40) ? imif : emiff;
        s->condition = 0;
        s->interrupts = (value >> 1) & 1;
        s->dual = value & 1;
        break;

    case 0x302:
        s->src_f1_top &= 0xffff0000;
        s->src_f1_top |= 0x0000ffff & value;
        break;

    case 0x304:
        s->src_f1_top &= 0x0000ffff;
        s->src_f1_top |= (uint32_t)value << 16;
        break;

    case 0x306:
        s->src_f1_bottom &= 0xffff0000;
        s->src_f1_bottom |= 0x0000ffff & value;
        break;

    case 0x308:
        s->src_f1_bottom &= 0x0000ffff;
        s->src_f1_bottom |= (uint32_t)value << 16;
        break;

    case 0x30a:
        s->src_f2_top &= 0xffff0000;
        s->src_f2_top |= 0x0000ffff & value;
        break;

    case 0x30c:
        s->src_f2_top &= 0x0000ffff;
        s->src_f2_top |= (uint32_t)value << 16;
        break;

    case 0x30e:
        s->src_f2_bottom &= 0xffff0000;
        s->src_f2_bottom |= 0x0000ffff & value;
        break;

    case 0x310:
        s->src_f2_bottom &= 0x0000ffff;
        s->src_f2_bottom |= (uint32_t)value << 16;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_3_1_lcd_read(struct omap_dma_lcd_channel_s *s, int offset,
                uint16_t *ret)
{
    int i;

    switch (offset) {
    case 0x300:
        i = s->condition;
        s->condition = 0;
        qemu_irq_lower(s->irq);
        *ret = ((s->src == imif) << 6) | (i << 3) |
                (s->interrupts << 1) | s->dual;
        break;

    case 0x302:
        *ret = s->src_f1_top & 0xffff;
        break;

    case 0x304:
        *ret = s->src_f1_top >> 16;
        break;

    case 0x306:
        *ret = s->src_f1_bottom & 0xffff;
        break;

    case 0x308:
        *ret = s->src_f1_bottom >> 16;
        break;

    case 0x30a:
        *ret = s->src_f2_top & 0xffff;
        break;

    case 0x30c:
        *ret = s->src_f2_top >> 16;
        break;

    case 0x30e:
        *ret = s->src_f2_bottom & 0xffff;
        break;

    case 0x310:
        *ret = s->src_f2_bottom >> 16;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_sys_write(struct omap_dma_s *s, int offset, uint16_t value)
{
    switch (offset) {
    case 0x400:
        s->gcr = value;
        break;

    case 0x404:
        if (value & 0x8)
            omap_dma_disable_3_1_mapping(s);
        else
            omap_dma_enable_3_1_mapping(s);
        break;

    case 0x408:
        if (value & 0x1)
            omap_dma_reset(s->dma);
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_sys_read(struct omap_dma_s *s, int offset,
                uint16_t *ret)
{
    switch (offset) {
    case 0x400:
        *ret = s->gcr;
        break;

    case 0x404:
        *ret = s->omap_3_1_mapping_disabled << 3;
        break;

    case 0x408:
        *ret = 0;
        break;

    case 0x442:
    case 0x444:
    case 0x446:
    case 0x448:
    case 0x44a:
    case 0x44c:
        *ret = 1;
        break;

    case 0x44e:
        *ret = (s->caps[0] >> 16) & 0xffff;
        break;
    case 0x450:
        *ret = (s->caps[0] >>  0) & 0xffff;
        break;

    case 0x452:
        *ret = (s->caps[1] >> 16) & 0xffff;
        break;
    case 0x454:
        *ret = (s->caps[1] >>  0) & 0xffff;
        break;

    case 0x456:
        *ret = s->caps[2];
        break;

    case 0x458:
        *ret = s->caps[3];
        break;

    case 0x45a:
        *ret = s->caps[4];
        break;

    case 0x460:
    case 0x480:
    case 0x482:
    case 0x4c0:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Physical Channel Status Registers not implemented\n",
                      __func__);
        *ret = 0xff;
        break;

    default:
        return 1;
    }
    return 0;
}

static uint64_t omap_dma_read(void *opaque, hwaddr addr, unsigned size)
{
    struct omap_dma_s *s = opaque;
    int reg, ch;
    uint16_t ret;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (addr) {
    case 0x300 ... 0x3fe:
        if (s->model <= omap_dma_3_1 || !s->omap_3_1_mapping_disabled) {
            if (omap_dma_3_1_lcd_read(&s->lcd_ch, addr, &ret))
                break;
            return ret;
        }
        /* Fall through. */
    case 0x000 ... 0x2fe:
        reg = addr & 0x3f;
        ch = (addr >> 6) & 0x0f;
        if (omap_dma_ch_reg_read(s, &s->ch[ch], reg, &ret))
            break;
        return ret;

    case 0x404 ... 0x4fe:
        if (s->model <= omap_dma_3_1)
            break;
        /* Fall through. */
    case 0x400:
        if (omap_dma_sys_read(s, addr, &ret))
            break;
        return ret;

    case 0xb00 ... 0xbfe:
        if (s->model == omap_dma_3_2 && s->omap_3_1_mapping_disabled) {
            if (omap_dma_3_2_lcd_read(&s->lcd_ch, addr, &ret))
                break;
            return ret;
        }
        break;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_dma_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    struct omap_dma_s *s = opaque;
    int reg, ch;

    if (size != 2) {
        omap_badwidth_write16(opaque, addr, value);
        return;
    }

    switch (addr) {
    case 0x300 ... 0x3fe:
        if (s->model <= omap_dma_3_1 || !s->omap_3_1_mapping_disabled) {
            if (omap_dma_3_1_lcd_write(&s->lcd_ch, addr, value))
                break;
            return;
        }
        /* Fall through.  */
    case 0x000 ... 0x2fe:
        reg = addr & 0x3f;
        ch = (addr >> 6) & 0x0f;
        if (omap_dma_ch_reg_write(s, &s->ch[ch], reg, value))
            break;
        return;

    case 0x404 ... 0x4fe:
        if (s->model <= omap_dma_3_1)
            break;
        /* fall through */
    case 0x400:
        if (omap_dma_sys_write(s, addr, value))
            break;
        return;

    case 0xb00 ... 0xbfe:
        if (s->model == omap_dma_3_2 && s->omap_3_1_mapping_disabled) {
            if (omap_dma_3_2_lcd_write(&s->lcd_ch, addr, value))
                break;
            return;
        }
        break;
    }

    OMAP_BAD_REG(addr);
}

static const MemoryRegionOps omap_dma_ops = {
    .read = omap_dma_read,
    .write = omap_dma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_dma_request(void *opaque, int drq, int req)
{
    struct omap_dma_s *s = opaque;
    if (req) {
        if (~s->dma->drqbmp & (1ULL << drq)) {
            s->dma->drqbmp |= 1ULL << drq;
            omap_dma_process_request(s, drq);
        }
    } else
        s->dma->drqbmp &= ~(1ULL << drq);
}

static void omap_dma_clk_update(void *opaque, int line, int on)
{
    struct omap_dma_s *s = opaque;
    int i;

    s->dma->freq = omap_clk_getrate(s->clk);

    for (i = 0; i < s->chans; i ++)
        if (s->ch[i].active)
            soc_dma_set_request(s->ch[i].dma, on);
}

static void omap_dma_setcaps(struct omap_dma_s *s)
{
    switch (s->model) {
    default:
    case omap_dma_3_1:
        break;
    case omap_dma_3_2:
        s->caps[0] =
                (1 << 19) |
                (1 << 18);
        s->caps[1] =
                (1 << 1);
        s->caps[2] =
                (1 << 8) |
                (1 << 7) |
                (1 << 6) |
                (1 << 5) |
                (1 << 4) |
                (1 << 3) |
                (1 << 2) |
                (1 << 1) |
                (1 << 0);
        s->caps[3] =
                (1 << 6) |
                (1 << 7) |
                (1 << 5) |
                (1 << 4) |
                (1 << 3) |
                (1 << 2) |
                (1 << 1) |
                (1 << 0);
        s->caps[4] =
                (1 << 7) |
                (1 << 6) |
                (1 << 5) |
                (1 << 4) |
                (1 << 3) |
                (1 << 2) |
                (1 << 1) |
                (1 << 0);
        break;
    }
}

struct soc_dma_s *omap_dma_init(hwaddr base, qemu_irq *irqs,
                MemoryRegion *sysmem,
                qemu_irq lcd_irq, struct omap_mpu_state_s *mpu, omap_clk clk,
                enum omap_dma_model model)
{
    int num_irqs, memsize, i;
    struct omap_dma_s *s = g_new0(struct omap_dma_s, 1);

    if (model <= omap_dma_3_1) {
        num_irqs = 6;
        memsize = 0x800;
    } else {
        num_irqs = 16;
        memsize = 0xc00;
    }
    s->model = model;
    s->mpu = mpu;
    s->clk = clk;
    s->lcd_ch.irq = lcd_irq;
    s->lcd_ch.mpu = mpu;

    s->dma = soc_dma_init((model <= omap_dma_3_1) ? 9 : 16);
    s->dma->freq = omap_clk_getrate(clk);
    s->dma->transfer_fn = omap_dma_transfer_generic;
    s->dma->setup_fn = omap_dma_transfer_setup;
    s->dma->drq = qemu_allocate_irqs(omap_dma_request, s, 32);
    s->dma->opaque = s;

    while (num_irqs --)
        s->ch[num_irqs].irq = irqs[num_irqs];
    for (i = 0; i < 3; i ++) {
        s->ch[i].sibling = &s->ch[i + 6];
        s->ch[i + 6].sibling = &s->ch[i];
    }
    for (i = (model <= omap_dma_3_1) ? 8 : 15; i >= 0; i --) {
        s->ch[i].dma = &s->dma->ch[i];
        s->dma->ch[i].opaque = &s->ch[i];
    }

    omap_dma_setcaps(s);
    omap_clk_adduser(s->clk, qemu_allocate_irq(omap_dma_clk_update, s, 0));
    omap_dma_reset(s->dma);
    omap_dma_clk_update(s, 0, 1);

    memory_region_init_io(&s->iomem, NULL, &omap_dma_ops, s, "omap.dma", memsize);
    memory_region_add_subregion(sysmem, base, &s->iomem);

    mpu->drq = s->dma->drq;

    return s->dma;
}

struct omap_dma_lcd_channel_s *omap_dma_get_lcdch(struct soc_dma_s *dma)
{
    struct omap_dma_s *s = dma->opaque;

    return &s->lcd_ch;
}
