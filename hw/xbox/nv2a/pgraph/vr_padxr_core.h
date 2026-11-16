/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VR_PADXR_CORE_H
#define HW_XBOX_NV2A_PGRAPH_VR_PADXR_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PADXR_MAX_SEATS    4
#define PADXR_MAX_PORTS    4
#define PADXR_MAX_AXES     16
#define PADXR_MAX_BUTTONS  64
#define PADXR_MAX_MEMBERS  16
#define PADXR_MAX_GATES    4
#define PADXR_MAX_CUES     4
#define PADXR_MAX_PRESSES  8
#define PADXR_STALE_US     500000

typedef enum {
    PADXR_FAM_PAD, PADXR_FAM_PER, PADXR_FAM_RF, PADXR_FAM_STREAM,
    PADXR_FAM_HOST, PADXR_FAM_CAM, PADXR_FAM__COUNT
} PadxrFamily;

typedef enum {
    PADXR_ROUTE_MERGE, PADXR_ROUTE_ADAPTER, PADXR_ROUTE_DEVICE,
    PADXR_ROUTE_STREAM, PADXR_ROUTE_HOST, PADXR_ROUTE_NONE,
    PADXR_ROUTE_CAMERA, PADXR_ROUTE__COUNT
} PadxrRoute;

typedef enum {
    PADXR_OUT_PRESS, PADXR_OUT_HOLD, PADXR_OUT_SCHEDULE, PADXR_OUT_SEQUENCE,
    PADXR_OUT_AXIS, PADXR_OUT_DEVICE, PADXR_OUT_STREAM, PADXR_OUT_HOST,
    PADXR_OUT_DISPLAY, PADXR_OUT_CAMERA, PADXR_OUT__COUNT
} PadxrOutKind;

typedef enum {
    PADXR_RES_MIC, PADXR_RES_LAYERS, PADXR_RES_HAPTICS, PADXR_RES_RIGHT_STICK,
    PADXR_RES_LEFT_STICK, PADXR_RES_SPEED, PADXR_RES__COUNT
} PadxrRes;

#define PADXR_CLASH_BUTTONS PADXR_RES__COUNT

typedef enum {
    PADXR_CLAIM_NONE, PADXR_CLAIM_SHARED, PADXR_CLAIM_TEE,
    PADXR_CLAIM_EXCLUSIVE,
    PADXR_CLAIM_ADD,
    PADXR_CLAIM_SET,
    PADXR_CLAIM_OWN,
    PADXR_CLAIM__COUNT
} PadxrClaim;

typedef enum { PADXR_ONLINE_ANY, PADXR_ONLINE_PRIVATE, PADXR_ONLINE_OFFLINE }
    PadxrOnline;
typedef enum { PADXR_CLOCK_GAME, PADXR_CLOCK_WALL } PadxrClock;

typedef enum { PADXR_PAD_NONE, PADXR_PAD_GAMEPAD, PADXR_PAD_SBC } PadxrPadKind;

#define PADXR_PRIM_R1   (1u << 0)
#define PADXR_PRIM_R2   (1u << 1)
#define PADXR_PRIM_R3   (1u << 2)
#define PADXR_PRIM_R4   (1u << 3)
#define PADXR_PRIM_R5   (1u << 4)
#define PADXR_PRIM_R6   (1u << 5)
#define PADXR_PRIM_R7   (1u << 6)
#define PADXR_PRIM_R8   (1u << 7)
#define PADXR_PRIM_R9P  (1u << 8)
#define PADXR_PRIM_R9D  (1u << 9)
#define PADXR_PRIM_R9R  (1u << 10)
#define PADXR_PRIM_R10  (1u << 11)
#define PADXR_PRIM_R11  (1u << 12)
#define PADXR_PRIM_R12  (1u << 13)

#define PADXR_NEED_HEAD_POSE       (1u << 0)
#define PADXR_NEED_HEAD_6DOF       (1u << 1)
#define PADXR_NEED_CONTROLLERS     (1u << 2)
#define PADXR_NEED_ANALOG_TRIGGER  (1u << 3)
#define PADXR_NEED_CAPACITIVE      (1u << 4)
#define PADXR_NEED_HANDS           (1u << 5)
#define PADXR_NEED_FINGER_CURL     (1u << 6)
#define PADXR_NEED_TWO_HANDS       (1u << 7)
#define PADXR_NEED_HAPTICS         (1u << 8)
#define PADXR_NEED_MIC             (1u << 9)
#define PADXR_NEED_EYE_GAZE        (1u << 10)
#define PADXR_NEED_ANGULAR_VEL     (1u << 11)
#define PADXR_NEED_SCREEN_HIT      (1u << 12)
#define PADXR_NEED_SEAT_CAL        (1u << 13)
#define PADXR_NEED_STANDING        (1u << 14)
#define PADXR_NEED_ROOMSCALE       (1u << 15)
#define PADXR_NEED_PAD             (1u << 16)

#define PADXR_SG_LIVE        (1u << 0)
#define PADXR_SG_ACTIONS     (1u << 1)
#define PADXR_SG_NOT_STALE   (1u << 2)
#define PADXR_SG_ONLINE_LOCK (1u << 3)
#define PADXR_SG_ALL         0x0Fu

typedef struct PadxrMemberDesc {
    const char *id, *kind;
    uint16_t primitives;
    uint32_t needs[4];
    uint8_t res[PADXR_RES__COUNT];
    uint8_t layers;
    uint8_t family, route, out_kind;
    uint8_t online, clock;
    uint8_t session_gates;
    uint8_t own_pad;
    uint64_t own_buttons;
    bool harness;
} PadxrMemberDesc;

bool padxr_needs_met(const PadxrMemberDesc *d, uint32_t caps, uint32_t *missing);
bool padxr_claim_pair_compatible(int a, int b);
bool padxr_claims_compatible(const PadxrMemberDesc *a, const PadxrMemberDesc *b,
                             int *clash);
bool padxr_layers_fit(unsigned claimed, const PadxrMemberDesc *d,
                      unsigned budget);

const char *padxr_claim_name(int c);
const char *padxr_res_name(int r);
const char *padxr_family_name(int f);
const char *padxr_route_name(int r);
const char *padxr_out_kind_name(int k);

int padxr_manifest_line(const PadxrMemberDesc *d, char *buf, size_t n);

typedef struct PadxrRange { int32_t lo, hi, add_scale; bool unipolar; } PadxrRange;
extern const PadxrRange PADXR_RANGE_S16;
extern const PadxrRange PADXR_RANGE_U15;
extern const PadxrRange PADXR_RANGE_U8;

int32_t padxr_axis_value(const PadxrRange *r, int claim, float v);
int32_t padxr_axis_merge(const PadxrRange *r, int32_t real, int claim, float v);

typedef struct PadxrAxisCell {
    bool has_own, has_set;
    int32_t own, set;
    int32_t add;
} PadxrAxisCell;

void padxr_axis_cell_write(PadxrAxisCell *c, const PadxrRange *r, int claim,
                           float v);
bool padxr_axis_cell_empty(const PadxrAxisCell *c);
int32_t padxr_axis_cell_apply(const PadxrAxisCell *c, const PadxrRange *r,
                              int32_t real);

typedef struct PadxrButtonCell {
    uint64_t press;
    uint64_t own_mask;
    uint64_t own_down;
} PadxrButtonCell;

uint64_t padxr_merge_buttons(uint64_t real, const PadxrButtonCell *c);

typedef struct PadxrEdgeSched {
    uint32_t remaining[PADXR_MAX_BUTTONS];
    uint64_t fresh;
    uint64_t down_last;
} PadxrEdgeSched;

void padxr_sched_press(PadxrEdgeSched *s, int bit, uint32_t count);
void padxr_sched_reset(PadxrEdgeSched *s);
uint64_t padxr_sched_step(PadxrEdgeSched *s);
bool padxr_sched_idle(const PadxrEdgeSched *s);

typedef struct PadxrCue { uint8_t hand, locus; float amplitude; uint16_t ms; } PadxrCue;

#define PADXR_LOCUS_HAND        0xFFu
#define PADXR_LOCUS_FINGERTIPS  0x1Fu
#define PADXR_LOCUS_POINTER     0x02u

bool padxr_cue_valid(const PadxrCue *c);
bool padxr_cue_stronger(const PadxrCue *a, const PadxrCue *b);

size_t padxr_glove_encode_v2(uint8_t out[8], int hand, uint8_t spot_mask,
                             int intensity, int ms);
size_t padxr_glove_encode_cue(uint8_t out[8], const PadxrCue *c);

typedef struct PadxrPose {
    bool valid;
    float px, py, pz;
    float qx, qy, qz, qw;
} PadxrPose;

typedef struct PadxrHand {
    PadxrPose aim;
    bool select, select_changed;
} PadxrHand;

typedef struct PadxrInput {
    PadxrHand hands[2];
    PadxrPose head;
    bool actions_active;
    bool stale;
    uint32_t generation;
    uint32_t caps;
    int64_t published_us;
} PadxrInput;

uint32_t padxr_caps_from_input(const PadxrInput *in);
void padxr_input_read_rule(const PadxrInput *pub, int64_t now_us,
                           PadxrInput *out);

typedef bool (*PadxrMemRead)(void *ctx, uint32_t addr, int width,
                             uint32_t *value);
typedef bool (*PadxrModArmed)(void *ctx, const char *slug);

typedef enum { PADXR_GATE_NONE, PADXR_GATE_MEM, PADXR_GATE_MOD } PadxrGateKind;

typedef struct PadxrGate {
    uint8_t kind;
    uint8_t width;
    uint8_t n_chain;
    bool not_equals;
    uint32_t addr;
    int32_t chain[4];
    uint32_t mask;
    uint32_t value;
    const char *mod_slug;
} PadxrGate;

typedef struct PadxrGameMem {
    PadxrMemRead read;
    PadxrModArmed mod_armed;
    void *ctx;
} PadxrGameMem;

bool padxr_gate_eval(const PadxrGate *g, const PadxrGameMem *mem);

typedef struct PadxrPortOut {
    PadxrButtonCell buttons;
    uint64_t hold;
    struct { uint8_t bit; uint32_t count; } press[PADXR_MAX_PRESSES];
    uint8_t n_press;
    PadxrAxisCell axis[PADXR_MAX_AXES];
} PadxrPortOut;

typedef struct PadxrOutputs {
    PadxrPortOut port[PADXR_MAX_PORTS];
    PadxrCue cue[PADXR_MAX_CUES];
    uint8_t n_cue;
    bool host_recenter;
    uint32_t refused;
    const PadxrMemberDesc *writer;
} PadxrOutputs;

typedef struct PadxrPortCells {
    bool live;
    PadxrButtonCell buttons;
    PadxrAxisCell axis[PADXR_MAX_AXES];
} PadxrPortCells;

void padxr_port_cells_from_out(const PadxrPortOut *o, PadxrPortCells *c);
void padxr_merge_port(const PadxrPortCells *c, uint64_t sched_down,
                      uint64_t *buttons, int16_t *axis, int n_axis,
                      const PadxrRange *const *ranges);

void padxr_out_press(PadxrOutputs *o, int port, int bit, uint32_t count);
void padxr_out_hold(PadxrOutputs *o, int port, int bit);
void padxr_out_own(PadxrOutputs *o, int port, int bit, bool down);
void padxr_out_axis(PadxrOutputs *o, int port, int axis, const PadxrRange *r,
                    int claim, float v);
void padxr_out_cue(PadxrOutputs *o, const PadxrCue *c);

typedef struct PadxrTickCtx {
    const PadxrInput *input;
    uint64_t tick;
    int64_t wall_us;
    const PadxrGameMem *mem;
} PadxrTickCtx;

typedef struct PadxrMemberOps {
    const PadxrMemberDesc *desc;
    bool (*arm)(void *self, const char **why);
    void (*tick)(void *self, const PadxrTickCtx *ctx, PadxrOutputs *out);
    void (*disarm)(void *self, int reason);
    void (*state_loaded)(void *self);
} PadxrMemberOps;

typedef enum {
    PADXR_DISARM_GATE, PADXR_DISARM_NEED, PADXR_DISARM_PROFILE,
    PADXR_DISARM_SESSION, PADXR_DISARM_ONLINE
} PadxrDisarmReason;

typedef struct PadxrMemberStatus {
    bool armed, gated_open;
    uint64_t ticks, presses, axis_writes, cues_sent, cues_dropped, refusals;
} PadxrMemberStatus;

typedef struct PadxrSlot {
    const PadxrMemberOps *ops;
    void *self;
    PadxrGate gates[PADXR_MAX_GATES];
    uint8_t n_gates;
    PadxrMemberStatus st;
} PadxrSlot;

typedef void (*PadxrLog)(void *ctx, const char *line);

typedef struct PadxrSeat {
    PadxrSlot slot[PADXR_MAX_MEMBERS];
    int n;
    unsigned layer_budget;
    bool online;
    uint64_t tick;
    PadxrLog log;
    void *log_ctx;
} PadxrSeat;

void padxr_seat_init(PadxrSeat *s, unsigned layer_budget, PadxrLog log,
                     void *log_ctx);
int padxr_seat_arm(PadxrSeat *s, const PadxrMemberOps *ops, void *self,
                   const PadxrGate *gates, int n_gates, uint32_t caps);
void padxr_seat_disarm_all(PadxrSeat *s, int reason);
void padxr_seat_state_loaded(PadxrSeat *s);
void padxr_seat_tick(PadxrSeat *s, const PadxrInput *in,
                     const PadxrGameMem *mem, int64_t wall_us,
                     PadxrOutputs *out);
bool padxr_seat_any_armed(const PadxrSeat *s);

#ifdef __cplusplus
}
#endif

#endif
