#if defined(TARGET_XBOX)

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <hal/xbox.h>
#include <SDL.h>

#include <ultra64.h>

#include "controller_api.h"

#define STICK_DEADZONE 8000
#define BUTTON_DEADZONE 0x20

/*
 * nxdk SDL converts an Xbox trigger value 0..255 to:
 *
 *   ((value << 8) | value) - 0x8000
 *
 * Preserve the original port's strict:
 *
 *   value > 0x20
 */
#define TRIGGER_AXIS_DEADZONE ((BUTTON_DEADZONE * SDL_JOYSTICK_AXIS_MAX) / 0xFF)

static SDL_GameController *sXboxController = NULL;
static SDL_JoystickID sXboxControllerInstance = -1;
static bool sXboxControllerInitialized = false;

static inline bool xbox_button_pressed(SDL_GameController *pad, SDL_GameControllerButton button) {
    return SDL_GameControllerGetButton(pad, button) != 0;
}

static inline bool xbox_trigger_pressed(SDL_GameController *pad, SDL_GameControllerAxis axis) {
    return SDL_GameControllerGetAxis(pad, axis) > TRIGGER_AXIS_DEADZONE;
}

static void controller_xbox_close_current(void) {
    if (sXboxController != NULL) {
        SDL_GameControllerClose(sXboxController);

        sXboxController = NULL;
    }

    sXboxControllerInstance = -1;
}

static void controller_xbox_open_first(void) {
    if (sXboxController != NULL) {
        return;
    }

    const int count = SDL_NumJoysticks();

    for (int i = 0; i < count; ++i) {
        if (!SDL_IsGameController(i)) {
            continue;
        }

        SDL_GameController *controller = SDL_GameControllerOpen(i);

        if (controller == NULL) {
            continue;
        }

        SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller);

        if (joystick == NULL) {
            SDL_GameControllerClose(controller);

            continue;
        }

        sXboxController = controller;

        sXboxControllerInstance = SDL_JoystickInstanceID(joystick);

        return;
    }
}

static void controller_xbox_update_device(void) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_CONTROLLERDEVICEREMOVED && sXboxController != NULL
            && event.cdevice.which == sXboxControllerInstance) {
            controller_xbox_close_current();
        }
    }

    if (sXboxController == NULL) {
        controller_xbox_open_first();
    }

    SDL_GameControllerUpdate();
}

static void controller_xbox_init(void) {
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        return;
    }

    SDL_GameControllerEventState(SDL_ENABLE);

    sXboxControllerInitialized = true;

    controller_xbox_open_first();
}

static void controller_xbox_read(OSContPad *pad) {
    if (!sXboxControllerInitialized) {
        return;
    }

    controller_xbox_update_device();

    SDL_GameController *xpad = sXboxController;

    if (xpad == NULL) {
        return;
    }

    const bool xpad_black = xbox_button_pressed(xpad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);

    const bool xpad_ltrig = xbox_trigger_pressed(xpad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);

    const bool xpad_rtrig = xbox_trigger_pressed(xpad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

    /*
     * Preserve the original dashboard reboot:
     *
     * Back + Black + left trigger + right trigger
     */
    if (xbox_button_pressed(xpad, SDL_CONTROLLER_BUTTON_BACK) && xpad_black && xpad_ltrig
        && xpad_rtrig) {
        XReboot();
    }

    /*
     * Preserve Baseline-B mappings.
     */

    if (xbox_button_pressed(xpad, SDL_CONTROLLER_BUTTON_A)) {
        pad->button |= A_BUTTON;
    }

    if (xbox_button_pressed(xpad, SDL_CONTROLLER_BUTTON_X)) {
        pad->button |= B_BUTTON;
    }

    if (xbox_button_pressed(xpad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
        pad->button |= L_TRIG;
    }

    if (xpad_black || xpad_rtrig) {
        pad->button |= R_TRIG;
    }

    if (xpad_ltrig) {
        pad->button |= Z_TRIG;
    }

    if (xbox_button_pressed(xpad, SDL_CONTROLLER_BUTTON_START)) {
        pad->button |= START_BUTTON;
    }

    /*
     * nxdk SDL changes Xbox Y from its original signed
     * orientation to SDL orientation using bitwise NOT.
     *
     * Undo that transformation so the existing SM64 Xbox
     * behavior sees the same values as Baseline B.
     */

    const int16_t lx = SDL_GameControllerGetAxis(xpad, SDL_CONTROLLER_AXIS_LEFTX);

    const int16_t ly = (int16_t) ~SDL_GameControllerGetAxis(xpad, SDL_CONTROLLER_AXIS_LEFTY);

    const int16_t rx = SDL_GameControllerGetAxis(xpad, SDL_CONTROLLER_AXIS_RIGHTX);

    const int16_t ry = (int16_t) ~SDL_GameControllerGetAxis(xpad, SDL_CONTROLLER_AXIS_RIGHTY);

    if (rx < -0x4000) {
        pad->button |= L_CBUTTONS;
    }

    if (rx > 0x4000) {
        pad->button |= R_CBUTTONS;
    }

    if (ry < -0x4000) {
        pad->button |= D_CBUTTONS;
    }

    if (ry > 0x4000) {
        pad->button |= U_CBUTTONS;
    }

    const uint32_t magnitude_sq = (uint32_t) (lx * lx) + (uint32_t) (ly * ly);

    if (magnitude_sq > (uint32_t) (STICK_DEADZONE * STICK_DEADZONE)) {
        pad->stick_x = lx / 0x100;

        pad->stick_y = ly / 0x100;
    }
}

struct ControllerAPI controller_xbox = {
    controller_xbox_init,
    controller_xbox_read
};

#endif