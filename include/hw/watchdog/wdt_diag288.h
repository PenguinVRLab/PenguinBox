#ifndef WDT_DIAG288_H
#define WDT_DIAG288_H

#include "hw/qdev-core.h"
#include "qom/object.h"

#define TYPE_WDT_DIAG288 "diag288"
typedef struct DIAG288Class DIAG288Class;
typedef struct DIAG288State DIAG288State;
DECLARE_OBJ_CHECKERS(DIAG288State, DIAG288Class,
                     DIAG288, TYPE_WDT_DIAG288)

#define WDT_DIAG288_INIT      0
#define WDT_DIAG288_CHANGE    1
#define WDT_DIAG288_CANCEL    2

struct DIAG288State {
    DeviceState parent_obj;
    QEMUTimer *timer;
    bool enabled;

};

struct DIAG288Class {
    DeviceClass parent_class;

    int (*handle_timer)(DIAG288State *dev,
                        uint64_t func, uint64_t timeout);
};

#endif
