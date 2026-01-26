#ifndef CONTROLLER_API
#define CONTROLLER_API

#include <ultra64.h>

struct ControllerAPI {
    void (*init)(void);
    void (*read)(OSContPad *pad);
    // returns the raw button values for this controller
    u32 (*read_btns)(void);
};

#endif
