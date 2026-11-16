/*
 * PenguinBox — the real Steel Battalion controller as a source of a Sim Configurator port (see the header)
 *
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 *
 * Plain C: no QEMU, no SDL, no glib. tests/xbox/simsbc runs it whole.
 */
#include "xemu-sim-merge.h"
#include "xemu-sim-sbc-source.h"

double sim_sbc_source_value(int row, const SimInput *in, const SbcFrame *f)
{
    if (row < 0 || row >= SIM_SBC_SOURCE_NIDS || !f) {
        return 0;
    }
    const SimSbcId *t = &sim_sbc_ids()[row];
#if defined(SIM_SBC_SOURCE_DRILL) && SIM_SBC_SOURCE_DRILL == 1
    if (t->field == SIM_SBC_F_GEAR && f->gear_lever == 0) {
        return -1;
    }
#elif defined(SIM_SBC_SOURCE_DRILL) && SIM_SBC_SOURCE_DRILL == 2
    if (t->field == SIM_SBC_F_LEFT_PEDAL || t->field == SIM_SBC_F_RIGHT_PEDAL) {
        return t->field == SIM_SBC_F_LEFT_PEDAL ? f->right_pedal : f->left_pedal;
    }
#endif
    switch (t->field) {
    case SIM_SBC_F_BUTTON:
        return (double)((f->buttons >> t->arg) & 1);
    case SIM_SBC_F_TOGGLE:
        return (double)((f->toggle_levels >> (t->arg - 32)) & 1);
    case SIM_SBC_F_AIMING_X:
        return f->aiming_x;
    case SIM_SBC_F_AIMING_Y:
        return f->aiming_y;
    case SIM_SBC_F_ROTATION:
        return f->rotation_lever;
    case SIM_SBC_F_SIGHT_X:
        return f->sight_change_x;
    case SIM_SBC_F_SIGHT_Y:
        return f->sight_change_y;
    case SIM_SBC_F_LEFT_PEDAL:
        return f->left_pedal;
    case SIM_SBC_F_MIDDLE_PEDAL:
        return f->middle_pedal;
    case SIM_SBC_F_RIGHT_PEDAL:
        return f->right_pedal;
    case SIM_SBC_F_TUNER:
        return in ? sim_merge_detent_int(in, f->tuner_dial) : -1;
    case SIM_SBC_F_GEAR:
        return in ? sim_merge_detent_int(in, f->gear_lever) : -1;
    }
    return 0;
}

void sim_sbc_source_fill(const SimPortDevice *dev, const SbcFrame *f, double *vals)
{
    for (int i = 0; i < dev->ninputs; i++) {
        vals[i] = sim_sbc_source_value(sim_sbc_id_index(dev->inputs[i].id), &dev->inputs[i], f);
    }
}

static const char *field_name(const SimSbcId *t, char *buf, size_t len)
{
    static const char *const names[] = {
        [SIM_SBC_F_AIMING_X] = "aiming_x (u16)", [SIM_SBC_F_AIMING_Y] = "aiming_y (u16)",
        [SIM_SBC_F_ROTATION] = "rotation_lever (s16)", [SIM_SBC_F_SIGHT_X] = "sight_change_x (s16)",
        [SIM_SBC_F_SIGHT_Y] = "sight_change_y (s16)", [SIM_SBC_F_LEFT_PEDAL] = "left_pedal (u16)",
        [SIM_SBC_F_MIDDLE_PEDAL] = "middle_pedal (u16)", [SIM_SBC_F_RIGHT_PEDAL] = "right_pedal (u16)",
        [SIM_SBC_F_TUNER] = "tuner_dial (0..15)", [SIM_SBC_F_GEAR] = "gear_lever (the wire byte)",
    };
    if (t->field == SIM_SBC_F_BUTTON) {
        snprintf(buf, len, "buttons bit %d (level)", t->arg);
    } else if (t->field == SIM_SBC_F_TOGGLE) {
        snprintf(buf, len, "toggle_levels bit %d (report bit %d, level)", t->arg - 32, t->arg);
    } else {
        snprintf(buf, len, "%s", names[t->field]);
    }
    return buf;
}

void sim_sbc_source_log(const SimPortDevice *dev, int port, FILE *to)
{
    for (int i = 0; i < dev->ninputs; i++) {
        int row = sim_sbc_id_index(dev->inputs[i].id);
        char buf[64];
        fprintf(to, "sim-port: port %d:   %s.%s <- %s\n", port, dev->device_id, dev->inputs[i].id,
                row < 0 ? "NOTHING (not in the controller's report)" : field_name(&sim_sbc_ids()[row], buf,
                                                                                 sizeof(buf)));
    }
}

void sim_sbc_lights_init(SimSbcLights *l)
{
    memset(l, 0, sizeof(*l));
}

void sim_sbc_lights_offer(SimSbcLights *l, const uint8_t *report, size_t len)
{
    if (l->stopped || !report || !len) {
        return;
    }
    len = len < SIM_SBC_LIGHTS_MAX ? len : SIM_SBC_LIGHTS_MAX;
    memcpy(l->want, report, len);
    l->want_len = len;
    l->have_want = true;
}

size_t sim_sbc_lights_due(const SimSbcLights *l, int64_t now_ms, uint8_t *out)
{
    if (l->stopped || !l->have_want) {
        return 0;
    }
    if (l->have_sent && l->sent_len == l->want_len && !memcmp(l->sent, l->want, l->want_len)) {
        return 0;
    }
    if (l->have_sent && now_ms - l->sent_ms < SIM_SBC_LIGHTS_MIN_MS) {
        return 0;
    }
    memcpy(out, l->want, l->want_len);
    return l->want_len;
}

void sim_sbc_lights_sent(SimSbcLights *l, int64_t now_ms)
{
    memcpy(l->sent, l->want, l->want_len);
    l->sent_len = l->want_len;
    l->have_sent = true;
    l->sent_ms = now_ms;
}

size_t sim_sbc_lights_off(SimSbcLights *l, uint8_t *out)
{
    size_t len = l->have_want ? l->want_len : 22;
    memset(out, 0, len);
    if (l->have_want && len >= 2) {
        out[0] = l->want[0];
        out[1] = l->want[1];
    } else {
        out[1] = 22;
    }
    l->stopped = true;
    return len;
}

bool sim_sbc_port_forwards_lights(const SimPort *p)
{
    if (!p || !p->target || strcmp(p->target, "steel_battalion")) {
        return false;
    }
    for (int d = 0; d < p->ndevices; d++) {
        if (p->devices[d].usable && sim_sbc_is_controller(&p->devices[d])) {
            return true;
        }
    }
    return false;
}

void sim_sbc_lights_store(SimSbcLightSlot *slot, const void *report, size_t len)
{
    len = len < SIM_SBC_LIGHTS_MAX ? len : SIM_SBC_LIGHTS_MAX;
    if (!report || !len || (slot->version && slot->len == len && !memcmp(slot->report, report, len))) {
        return;
    }
    memcpy(slot->report, report, len);
    slot->len = len;
    slot->version++;
    if (!slot->version) {
        slot->version = 1;
    }
}

void sim_sbc_lights_step(SimSbcLights *l, const SimSbcLightSlot *slot, uint32_t *seen, int64_t now_ms,
                         bool (*send)(const uint8_t *report, size_t len, void *ctx), void *ctx)
{
    if (slot->version && slot->version != *seen) {
        sim_sbc_lights_offer(l, slot->report, slot->len);
        *seen = slot->version;
    }
    uint8_t out[SIM_SBC_LIGHTS_MAX];
    size_t n = sim_sbc_lights_due(l, now_ms, out);
    if (n && send(out, n, ctx)) {
        sim_sbc_lights_sent(l, now_ms);
    }
}
