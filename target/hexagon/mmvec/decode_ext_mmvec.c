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
#include "decode.h"
#include "opcodes.h"
#include "insn.h"
#include "iclass.h"
#include "mmvec/mmvec.h"
#include "mmvec/decode_ext_mmvec.h"

static void
check_new_value(Packet *pkt)
{
    int i, j;
    uint16_t def_opcode;

    for (i = 1; i < pkt->num_insns; i++) {
        uint16_t use_opcode = pkt->insn[i].opcode;
        if (GET_ATTRIB(use_opcode, A_DOTNEWVALUE) &&
            GET_ATTRIB(use_opcode, A_CVI) &&
            GET_ATTRIB(use_opcode, A_STORE)) {
            int use_regidx = pkt->insn[i].new_read_idx;
            g_assert(pkt->insn[i].new_read_idx != -1);
            int def_off = ((pkt->insn[i].regno[use_regidx]) >> 1);
            int def_oreg = pkt->insn[i].regno[use_regidx] & 1;
            int def_idx = -1;
            for (j = i - 1; (j >= 0) && (def_off >= 0); j--) {
                if (!GET_ATTRIB(pkt->insn[j].opcode, A_CVI)) {
                    continue;
                }
                def_off--;
                if (def_off == 0) {
                    def_idx = j;
                    break;
                }
            }
            g_assert(!((def_off != 0) || (def_idx < 0) ||
                       (def_idx > (pkt->num_insns - 1))));

            def_opcode = pkt->insn[def_idx].opcode;
            if ((pkt->insn[def_idx].dest_idx == -1)  &&
                GET_ATTRIB(def_opcode, A_CVI_GATHER)) {
                pkt->insn[i].regno[use_regidx] = def_oreg;
                pkt->insn[i].new_value_producer_slot = pkt->insn[def_idx].slot;
            } else {
                if (pkt->insn[def_idx].dest_idx == -1) {
                    g_assert_not_reached();
                }
                int def_regnum =
                    pkt->insn[def_idx].regno[pkt->insn[def_idx].dest_idx];
                pkt->insn[i].regno[use_regidx] = def_regnum ^ def_oreg;
                pkt->insn[i].new_value_producer_slot = pkt->insn[def_idx].slot;
            }
        }
    }
}

static void
decode_mmvec_move_cvi_to_end(Packet *pkt, int max)
{
    int i;
    for (i = 0; i < max; i++) {
        if (GET_ATTRIB(pkt->insn[i].opcode, A_CVI)) {
            int last_inst = pkt->num_insns - 1;
            uint16_t last_opcode = pkt->insn[last_inst].opcode;

            if ((last_opcode == J2_endloop0) ||
                (last_opcode == J2_endloop1) ||
                (last_opcode == J2_endloop01)) {
                last_inst--;
            }

            decode_send_insn_to(pkt, i, last_inst);
            max--;
            i--;
        }
    }
}

static void
decode_shuffle_for_execution_vops(Packet *pkt)
{
    int i;
    for (i = 0; i < pkt->num_insns; i++) {
        uint16_t opcode = pkt->insn[i].opcode;
        if ((GET_ATTRIB(opcode, A_LOAD) &&
             GET_ATTRIB(opcode, A_CVI_NEW)) ||
            GET_ATTRIB(opcode, A_CVI_TMP)) {
            decode_mmvec_move_cvi_to_end(pkt, i);
            break;
        }
    }

    for (i = 0; i < pkt->num_insns - 1; i++) {
        uint16_t opcode = pkt->insn[i].opcode;
        if (GET_ATTRIB(opcode, A_STORE) &&
            GET_ATTRIB(opcode, A_CVI_NEW) &&
            !GET_ATTRIB(opcode, A_CVI_SCATTER_RELEASE)) {
            int last_inst = pkt->num_insns - 1;
            uint16_t last_opcode = pkt->insn[last_inst].opcode;

            if ((last_opcode == J2_endloop0) ||
                (last_opcode == J2_endloop1) ||
                (last_opcode == J2_endloop01)) {
                last_inst--;
            }

            decode_send_insn_to(pkt, i, last_inst);
            break;
        }
    }
}

static void
check_for_vhist(Packet *pkt)
{
    pkt->vhist_insn = NULL;
    for (int i = 0; i < pkt->num_insns; i++) {
        Insn *insn = &pkt->insn[i];
        int opcode = insn->opcode;
        if (GET_ATTRIB(opcode, A_CVI) && GET_ATTRIB(opcode, A_CVI_4SLOT)) {
                pkt->vhist_insn = insn;
                return;
        }
    }
}

SlotMask mmvec_ext_decode_find_iclass_slots(int opcode)
{
    if (GET_ATTRIB(opcode, A_CVI_VM)) {
        if (GET_ATTRIB(opcode, A_RESTRICT_SLOT0ONLY)) {
            return SLOTS_0;
        } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT1ONLY)) {
            return SLOTS_1;
        }
        return SLOTS_01;
    } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT2ONLY)) {
        return SLOTS_2;
    } else if (GET_ATTRIB(opcode, A_CVI_VX)) {
        return SLOTS_23;
    } else if (GET_ATTRIB(opcode, A_CVI_VS_VX)) {
        return SLOTS_23;
    } else {
        return SLOTS_0123;
    }
}

void mmvec_ext_decode_checks(Packet *pkt, bool disas_only)
{
    check_new_value(pkt);
    if (!disas_only) {
        decode_shuffle_for_execution_vops(pkt);
    }
    check_for_vhist(pkt);
}
