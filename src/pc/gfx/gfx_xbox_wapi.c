#ifdef TARGET_XBOX

#include <hal/video.h>
#include <hal/xbox.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <hal/debug.h>
#include <pbkit/pbkit.h>

#include "gfx_window_manager_api.h"
#include "gfx_xbox.h"
#include "macros.h"

#ifdef VERSION_EU
#define REFRESH_RATE REFRESH_50HZ
#else
#define REFRESH_RATE REFRESH_60HZ
#endif

int win_width;
int win_height;

static void gfx_xbox_wapi_init(const char *game_name, bool start_in_fullscreen) {
    int status;

    // try 720p first
    status = XVideoSetMode(1280, 720, 32, REFRESH_RATE);

    // fall back to 640x480
    if (!status) XVideoSetMode(640, 480, 32, REFRESH_RATE);

    if ((status = pb_init())) {
        debugPrint("gfx_xbox_wapi_init: pb_init failed: %d\n", status);
        while (1) Sleep(100);
    }

    pb_show_front_screen();

    win_width = pb_back_buffer_width();
    win_height = pb_back_buffer_height();

    debugPrint("gfx_xbox_wapi_init: resolution %dx%d\n", win_width, win_height);
}

static void gfx_xbox_wapi_set_keyboard_callbacks(bool (*on_key_down)(int scancode), bool (*on_key_up)(int scancode), void (*on_all_keys_up)(void)) {
}

static void gfx_xbox_wapi_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
}

static void gfx_xbox_wapi_set_fullscreen(bool enable) {
}

static void gfx_xbox_wapi_main_loop(void (*run_one_game_iter)(void)) {
    run_one_game_iter();
}

static void gfx_xbox_wapi_get_dimensions(uint32_t *width, uint32_t *height) {
    *width = win_width;
    *height = win_height;
}

static void gfx_xbox_wapi_handle_events(void) {
}

static bool gfx_xbox_wapi_start_frame(void) {
    static DWORD lastPacedVblank;
    DWORD currentVblank;
    DWORD targetVblank;

    /*
     * Pace from pbkit's VBlank counter rather than rounded millisecond
     * timing. SM64 advances one game frame per two 60 Hz refreshes.
     *
     * If rendering already consumed two or more VBlanks, start a new cadence
     * from the current display phase so a late frame is not delayed again.
     * PFIFO synchronization remains unchanged.
     */
    currentVblank = pb_get_vbl_counter();

    if (lastPacedVblank == 0) {
        targetVblank = currentVblank + 2;
    } else if ((DWORD) (currentVblank - lastPacedVblank) >= 2) {
        targetVblank = currentVblank;
    } else {
        targetVblank = lastPacedVblank + 2;
    }

    while ((int32_t) (pb_get_vbl_counter() - targetVblank) < 0) {
        pb_wait_for_vbl();
    }

    lastPacedVblank = pb_get_vbl_counter();

    pb_reset();
    pb_target_back_buffer();

    while (pb_busy());

    return true;
}

static void gfx_xbox_wapi_swap_buffers_begin(void) {
    while (pb_busy());
    while (pb_finished());
}

static void gfx_xbox_wapi_swap_buffers_end(void) {
}

static double gfx_xbox_wapi_get_time(void) {
    return 0.0;
}

struct GfxWindowManagerAPI gfx_xbox_wapi = {
    gfx_xbox_wapi_init,
    gfx_xbox_wapi_set_keyboard_callbacks,
    gfx_xbox_wapi_set_fullscreen_changed_callback,
    gfx_xbox_wapi_set_fullscreen,
    gfx_xbox_wapi_main_loop,
    gfx_xbox_wapi_get_dimensions,
    gfx_xbox_wapi_handle_events,
    gfx_xbox_wapi_start_frame,
    gfx_xbox_wapi_swap_buffers_begin,
    gfx_xbox_wapi_swap_buffers_end,
    gfx_xbox_wapi_get_time
};

#endif
