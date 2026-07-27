/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include "iclass.h"
#include "attribs.h"
#include "genptr.h"
#include "decode.h"
#include "insn.h"
#include "printinsn.h"
#include "mmvec/decode_ext_mmvec.h"

#define fZXTN(N, M, VAL) ((VAL) & ((1LL << (N)) - 1))

enum {
    EXT_IDX_noext = 0,
    EXT_IDX_noext_AFTER = 4,
    EXT_IDX_mmvec = 4,
    EXT_IDX_mmvec_AFTER = 8,
    XX_LAST_EXT_IDX
};

#define DEF_REGMAP(NAME, ELEMENTS, ...) \
    static const unsigned int DECODE_REGISTER_##NAME[ELEMENTS] = \
    { __VA_ARGS__ };
DEF_REGMAP(R_16,  16, 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23)
DEF_REGMAP(R__8,  8,  0, 2, 4, 6, 16, 18, 20, 22)
DEF_REGMAP(R_8,   8,  0, 1, 2, 3, 4, 5, 6, 7)

#define DECODE_MAPPED_REG(OPNUM, NAME) \
    insn->regno[OPNUM] = DECODE_REGISTER_##NAME[insn->regno[OPNUM]];

#define DECODE_MAPPED(NAME) \
static int decode_mapped_reg_##NAME(DisasContext *ctx, int x) \
{ \
    return DECODE_REGISTER_##NAME[x]; \
}
DECODE_MAPPED(R_16)
DECODE_MAPPED(R_8)
DECODE_MAPPED(R__8)

static int shift_left(DisasContext *ctx, int x, int n, int immno)
{
    int ret = x;
    Insn *insn = ctx->insn;
    if (!insn->extension_valid ||
        insn->which_extended != immno) {
        ret <<= n;
    }
    return ret;
}

#include "decode_normal_generated.c.inc"
#include "decode_hvx_generated.c.inc"

#include "decode_subinsn_a_generated.c.inc"
#include "decode_subinsn_l1_generated.c.inc"
#include "decode_subinsn_l2_generated.c.inc"
#include "decode_subinsn_s1_generated.c.inc"
#include "decode_subinsn_s2_generated.c.inc"

#include "decodetree_trans_funcs_generated.c.inc"

void decode_send_insn_to(Packet *packet, int start, int newloc)
{
    Insn tmpinsn;
    int direction;
    int i;
    if (start == newloc) {
        return;
    }
    if (start < newloc) {
        direction = 1;
    } else {
        direction = -1;
    }
    for (i = start; i != newloc; i += direction) {
        tmpinsn = packet->insn[i];
        packet->insn[i] = packet->insn[i + direction];
        packet->insn[i + direction] = tmpinsn;
    }
}

static void
decode_fill_newvalue_regno(Packet *packet)
{
    int i, use_regidx, offset, def_idx, dst_idx;

    for (i = 1; i < packet->num_insns; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_DOTNEWVALUE) &&
            !GET_ATTRIB(packet->insn[i].opcode, A_EXTENSION)) {

            g_assert(packet->insn[i].new_read_idx != -1);
            use_regidx = packet->insn[i].new_read_idx;

            offset = packet->insn[i].regno[use_regidx] >> 1;
            def_idx = i - offset;
            for (int j = 0; j < offset; j++) {
                if (GET_ATTRIB(packet->insn[i - j - 1].opcode, A_IT_EXTENDER)) {
                    def_idx--;
                }
            }

            g_assert(!((def_idx < 0) || (def_idx > (packet->num_insns - 1))));

            g_assert(packet->insn[def_idx].dest_idx != -1);
            dst_idx = packet->insn[def_idx].dest_idx;
            packet->insn[i].regno[use_regidx] =
                packet->insn[def_idx].regno[dst_idx];
            packet->insn[i].new_value_producer_slot =
                packet->insn[def_idx].slot;
        }
    }
}

static void decode_split_cmpjump(Packet *pkt)
{
    int last, i;
    int numinsns = pkt->num_insns;

    for (i = 0; i < numinsns; i++) {
        if (GET_ATTRIB(pkt->insn[i].opcode, A_NEWCMPJUMP)) {
            last = pkt->num_insns;
            pkt->insn[last] = pkt->insn[i];
            pkt->insn[last].part1 = true;
            pkt->insn[i].part1 = false;
            pkt->num_insns++;
        }
    }

    for (i = 0; i < pkt->num_insns; i++) {
        if (pkt->insn[i].part1) {
            decode_send_insn_to(pkt, i, 0);
        }
    }
}

static bool decode_opcode_can_jump(int opcode)
{
    if ((GET_ATTRIB(opcode, A_JUMP)) ||
        (GET_ATTRIB(opcode, A_CALL)) ||
        (opcode == J2_trap0) ||
        (opcode == J2_pause)) {
        if (opcode == J4_hintjumpr) {
            return false;
        }
        return true;
    }

    return false;
}

static bool decode_opcode_ends_loop(int opcode)
{
    return GET_ATTRIB(opcode, A_HWLOOP0_END) ||
           GET_ATTRIB(opcode, A_HWLOOP1_END);
}

static void decode_set_insn_attr_fields(Packet *pkt)
{
    int i;
    int numinsns = pkt->num_insns;
    uint16_t opcode;

    pkt->pkt_has_cof = false;
    pkt->pkt_has_multi_cof = false;
    pkt->pkt_has_endloop = false;
    pkt->pkt_has_dczeroa = false;

    for (i = 0; i < numinsns; i++) {
        opcode = pkt->insn[i].opcode;
        if (pkt->insn[i].part1) {
            continue;
        }

        if (GET_ATTRIB(opcode, A_DCZEROA)) {
            pkt->pkt_has_dczeroa = true;
        }

        if (GET_ATTRIB(opcode, A_STORE)) {
            if (GET_ATTRIB(opcode, A_SCALAR_STORE) &&
                !GET_ATTRIB(opcode, A_MEMSIZE_0B)) {
                if (pkt->insn[i].slot == 0) {
                    pkt->pkt_has_scalar_store_s0 = true;
                } else {
                    pkt->pkt_has_scalar_store_s1 = true;
                }
            }
        }

        if (decode_opcode_can_jump(opcode)) {
            if (pkt->pkt_has_cof) {
                pkt->pkt_has_multi_cof = true;
            }
            pkt->pkt_has_cof = true;
        }

        pkt->insn[i].is_endloop = decode_opcode_ends_loop(opcode);

        pkt->pkt_has_endloop |= pkt->insn[i].is_endloop;

        if (pkt->pkt_has_endloop) {
            if (pkt->pkt_has_cof) {
                pkt->pkt_has_multi_cof = true;
            }
            pkt->pkt_has_cof = true;
        }
    }
}

static void decode_shuffle_for_execution(Packet *packet)
{
    bool changed = false;
    int i;
    bool flag;
    int n_mems;
    int last_insn = packet->num_insns - 1;

    if (decode_opcode_ends_loop(packet->insn[last_insn].opcode)) {
        last_insn--;
    }

    do {
        changed = false;
        for (flag = false, n_mems = 0, i = last_insn; i >= 0; i--) {
            int opcode = packet->insn[i].opcode;

            if (flag && GET_ATTRIB(opcode, A_STORE)) {
                decode_send_insn_to(packet, i, last_insn - n_mems);
                n_mems++;
                changed = true;
            } else if (GET_ATTRIB(opcode, A_STORE)) {
                n_mems++;
            } else if (GET_ATTRIB(opcode, A_LOAD)) {
                n_mems++;
            } else if (GET_ATTRIB(opcode, A_DOTNEWVALUE)) {
            } else {
                flag = true;
            }
        }

        if (changed) {
            continue;
        }
        for (flag = false, i = 0; i < last_insn + 1; i++) {
            int opcode = packet->insn[i].opcode;

            if (packet->insn[i].has_pred_dest &&
                GET_ATTRIB(opcode, A_STORE) == 0) {
                if (flag) {
                    decode_send_insn_to(packet, i, 0);
                    changed = true;
                    continue;
                }
            } else if (GET_ATTRIB(opcode, A_IMPLICIT_WRITES_P3) &&
                       !decode_opcode_ends_loop(packet->insn[i].opcode)) {
                if (flag) {
                    decode_send_insn_to(packet, i, 0);
                    changed = true;
                    continue;
                }
            } else if (GET_ATTRIB(opcode, A_IMPLICIT_WRITES_P0) &&
                       !GET_ATTRIB(opcode, A_NEWCMPJUMP)) {
                if (flag) {
                    decode_send_insn_to(packet, i, 0);
                    changed = true;
                    continue;
                }
            } else {
                flag = true;
            }
        }
        if (changed) {
            continue;
        }
    } while (changed);

    for (i = 0; i < last_insn; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_DOTNEWVALUE)) {
            decode_send_insn_to(packet, i, last_insn);
            break;
        }
    }
}

static void
apply_extender(Packet *pkt, int i, uint32_t extender)
{
    int immed_num;
    uint32_t base_immed;

    immed_num = pkt->insn[i].which_extended;
    base_immed = pkt->insn[i].immed[immed_num];

    pkt->insn[i].immed[immed_num] = extender | fZXTN(6, 32, base_immed);
}

static void decode_apply_extenders(Packet *packet)
{
    int i;
    for (i = 0; i < packet->num_insns; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_IT_EXTENDER)) {
            packet->insn[i + 1].extension_valid = true;
            apply_extender(packet, i + 1, packet->insn[i].immed[0]);
        }
    }
}

static void decode_remove_extenders(Packet *packet)
{
    int i, j;
    for (i = 0; i < packet->num_insns; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_IT_EXTENDER)) {
            for (j = i;
                (j < packet->num_insns - 1) && (j < INSTRUCTIONS_MAX - 1);
                j++) {
                packet->insn[j] = packet->insn[j + 1];
            }
            packet->num_insns--;
        }
    }
}

static SlotMask get_valid_slots(const Packet *pkt, unsigned int slot)
{
    if (GET_ATTRIB(pkt->insn[slot].opcode, A_EXTENSION)) {
        return mmvec_ext_decode_find_iclass_slots(pkt->insn[slot].opcode);
    } else {
        return find_iclass_slots(pkt->insn[slot].opcode,
                                 pkt->insn[slot].iclass);
    }
}

static uint32_t get_duplex_iclass(uint32_t encoding)
{
    uint32_t iclass = extract32(encoding, 13, 1);
    iclass = deposit32(iclass, 1, 3, extract32(encoding, 29, 3));
    return iclass;
}

typedef bool (*subinsn_decode_func)(DisasContext *ctx, uint16_t insn);
typedef struct {
    subinsn_decode_func decode_slot0_subinsn;
    subinsn_decode_func decode_slot1_subinsn;
} subinsn_decode_groups;

static const subinsn_decode_groups decode_groups[16] = {
    [0x0] = { decode_subinsn_l1, decode_subinsn_l1 },
    [0x1] = { decode_subinsn_l2, decode_subinsn_l1 },
    [0x2] = { decode_subinsn_l2, decode_subinsn_l2 },
    [0x3] = { decode_subinsn_a,  decode_subinsn_a },
    [0x4] = { decode_subinsn_l1, decode_subinsn_a },
    [0x5] = { decode_subinsn_l2, decode_subinsn_a },
    [0x6] = { decode_subinsn_s1, decode_subinsn_a },
    [0x7] = { decode_subinsn_s2, decode_subinsn_a },
    [0x8] = { decode_subinsn_s1, decode_subinsn_l1 },
    [0x9] = { decode_subinsn_s1, decode_subinsn_l2 },
    [0xa] = { decode_subinsn_s1, decode_subinsn_s1 },
    [0xb] = { decode_subinsn_s2, decode_subinsn_s1 },
    [0xc] = { decode_subinsn_s2, decode_subinsn_l1 },
    [0xd] = { decode_subinsn_s2, decode_subinsn_l2 },
    [0xe] = { decode_subinsn_s2, decode_subinsn_s2 },
    [0xf] = { NULL,              NULL },
};

static uint16_t get_slot0_subinsn(uint32_t encoding)
{
    return extract32(encoding, 0, 13);
}

static uint16_t get_slot1_subinsn(uint32_t encoding)
{
    return extract32(encoding, 16, 13);
}

static unsigned int
decode_insns(DisasContext *ctx, Insn *insn, uint32_t encoding)
{
    if (parse_bits(encoding) != 0) {
        if (decode_normal(ctx, encoding) ||
            decode_hvx(ctx, encoding)) {
            insn->generate = opcode_genptr[insn->opcode];
            insn->iclass = iclass_bits(encoding);
            return 1;
        }
        g_assert_not_reached();
    } else {
        uint32_t iclass = get_duplex_iclass(encoding);
        unsigned int slot0_subinsn = get_slot0_subinsn(encoding);
        unsigned int slot1_subinsn = get_slot1_subinsn(encoding);
        subinsn_decode_func decode_slot0_subinsn =
            decode_groups[iclass].decode_slot0_subinsn;
        subinsn_decode_func decode_slot1_subinsn =
            decode_groups[iclass].decode_slot1_subinsn;

        if (decode_slot1_subinsn(ctx, slot1_subinsn)) {
            insn->generate = opcode_genptr[insn->opcode];
            insn->iclass = iclass_bits(encoding);
            ctx->insn = ++insn;
            if (decode_slot0_subinsn(ctx, slot0_subinsn)) {
                insn->generate = opcode_genptr[insn->opcode];
                insn->iclass = iclass_bits(encoding);
                return 2;
            }
        }
        g_assert_not_reached();
    }
}

static void decode_add_endloop_insn(Insn *insn, int loopnum)
{
    if (loopnum == 10) {
        insn->opcode = J2_endloop01;
        insn->generate = opcode_genptr[J2_endloop01];
    } else if (loopnum == 1) {
        insn->opcode = J2_endloop1;
        insn->generate = opcode_genptr[J2_endloop1];
    } else if (loopnum == 0) {
        insn->opcode = J2_endloop0;
        insn->generate = opcode_genptr[J2_endloop0];
    } else {
        g_assert_not_reached();
    }
}

static bool decode_parsebits_is_loopend(uint32_t encoding32)
{
    uint32_t bits = parse_bits(encoding32);
    return bits == 0x2;
}

static bool has_valid_slot_assignment(Packet *pkt)
{
    int used_slots = 0;
    for (int i = 0; i < pkt->num_insns; i++) {
        int slot_mask;
        Insn *insn = &pkt->insn[i];
        if (decode_opcode_ends_loop(insn->opcode)) {
            continue;
        }
        slot_mask = 1 << insn->slot;
        if (used_slots & slot_mask) {
            return false;
        }
        used_slots |= slot_mask;
    }
    return true;
}

static bool
decode_set_slot_number(Packet *pkt)
{
    int slot;
    int i;
    bool hit_mem_insn = false;
    bool hit_duplex = false;
    bool slot0_found = false;
    bool slot1_found = false;
    int slot1_iidx = 0;

    for (i = 0, slot = 3; i < pkt->num_insns; i++) {
        SlotMask valid_slots = get_valid_slots(pkt, i);

        while (!(valid_slots & (1 << slot))) {
            slot--;
        }
        pkt->insn[i].slot = slot;
        if (slot) {
            slot--;
        }
    }

    for (i = pkt->num_insns - 1; i >= 0; i--) {
        if ((GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE) ||
             GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE_PACKET_RULES)) &&
            !hit_mem_insn) {
            hit_mem_insn = true;
            pkt->insn[i].slot = 0;
            continue;
        }

        if ((GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE) ||
             GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE_PACKET_RULES)) &&
            hit_mem_insn) {
            pkt->insn[i].slot = 1;
        }
    }

    for (i = pkt->num_insns - 1; i >= 0; i--) {
        if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN) && !hit_duplex) {
            hit_duplex = true;
            pkt->insn[i].slot = 0;
            continue;
        }

        if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN) && hit_duplex) {
            pkt->insn[i].slot = 1;
        }
    }

    for (i = pkt->num_insns - 1; i >= 0; i--) {
        if (pkt->insn[i].slot == 0) {
            bool is_endloop = (pkt->insn[i].opcode == J2_endloop01);
            is_endloop |= (pkt->insn[i].opcode == J2_endloop0);
            is_endloop |= (pkt->insn[i].opcode == J2_endloop1);

            if (!is_endloop) {
                slot0_found = true;
            }
        }
        if (pkt->insn[i].slot == 1) {
            slot1_found = true;
            slot1_iidx = i;
        }
    }
    if ((!slot0_found) && slot1_found) {
        pkt->insn[slot1_iidx].slot = 0;
    }

    return has_valid_slot_assignment(pkt);
}

int decode_packet(DisasContext *ctx, int max_words, const uint32_t *words,
                  Packet *pkt, bool disas_only)
{
    int num_insns = 0;
    int words_read = 0;
    bool end_of_packet = false;
    int new_insns = 0;
    int i;
    uint32_t encoding32;

    memset(pkt, 0, sizeof(*pkt));
    while (!end_of_packet && (words_read < max_words)) {
        Insn *insn = &pkt->insn[num_insns];
        ctx->insn = insn;
        encoding32 = words[words_read];
        end_of_packet = is_packet_end(encoding32);
        new_insns = decode_insns(ctx, insn, encoding32);
        g_assert(new_insns > 0);
        if (pkt->insn[num_insns].opcode == A4_ext) {
            pkt->insn[num_insns + 1].extension_valid = true;
        }
        num_insns += new_insns;
        words_read++;
    }

    pkt->num_insns = num_insns;
    if (!end_of_packet) {
        return 0;
    }
    pkt->encod_pkt_size_in_bytes = words_read * 4;
    pkt->pkt_has_hvx = false;
    for (i = 0; i < num_insns; i++) {
        pkt->pkt_has_hvx |=
            GET_ATTRIB(pkt->insn[i].opcode, A_CVI);
    }

    if ((words_read == 2) && (decode_parsebits_is_loopend(words[0]))) {
        decode_add_endloop_insn(&pkt->insn[pkt->num_insns++], 0);
    }
    if (words_read >= 3) {
        bool has_loop0, has_loop1;
        has_loop0 = decode_parsebits_is_loopend(words[0]);
        has_loop1 = decode_parsebits_is_loopend(words[1]);
        if (has_loop0 && has_loop1) {
            decode_add_endloop_insn(&pkt->insn[pkt->num_insns++], 10);
        } else if (has_loop1) {
            decode_add_endloop_insn(&pkt->insn[pkt->num_insns++], 1);
        } else if (has_loop0) {
            decode_add_endloop_insn(&pkt->insn[pkt->num_insns++], 0);
        }
    }

    decode_apply_extenders(pkt);
    if (!disas_only) {
        decode_remove_extenders(pkt);
        if (!decode_set_slot_number(pkt)) {
            return 0;
        }
    }
    decode_fill_newvalue_regno(pkt);

    if (pkt->pkt_has_hvx) {
        mmvec_ext_decode_checks(pkt, disas_only);
    }

    if (!disas_only) {
        decode_shuffle_for_execution(pkt);
        decode_split_cmpjump(pkt);
        decode_set_insn_attr_fields(pkt);
    }

    return words_read;
}

int disassemble_hexagon(uint32_t *words, int nwords, bfd_vma pc,
                        GString *buf)
{
    DisasContext ctx;
    Packet pkt;

    memset(&ctx, 0, sizeof(DisasContext));
    ctx.pkt = &pkt;

    if (decode_packet(&ctx, nwords, words, &pkt, true) > 0) {
        snprint_a_pkt_disas(buf, &pkt, words, pc);
        return pkt.encod_pkt_size_in_bytes;
    } else {
        g_string_assign(buf, "<invalid>");
        return 0;
    }
}
