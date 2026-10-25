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

int win_width;
int win_height;

static void gfx_xbox_wapi_init(const char *game_name, bool start_in_fullscreen) {
    int status;

    // try 720p first
    status = XVideoSetMode(1280, 720, 32, REFRESH_DEFAULT);

    // fall back to 640x480
    if (!status)
        XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    if ((status = pb_init())) {
        debugPrint("gfx_xbox_wapi_init: pb_init failed: %d\n", status);
        while (1) Sleep(100);
    }

    win_width = pb_back_buffer_width();
    win_height = pb_back_buffer_height();

    pb_reset();
    pb_target_back_buffer();
    pb_erase_depth_stencil_buffer(0, 0, win_width, win_height);
    pb_fill(0, 0, win_width, win_height, 0x00000000);
    while (pb_busy())
        ;

    pb_show_front_screen();

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
    pb_wait_for_vbl(); // comment this one out if porting 60fps patch
    pb_wait_for_vbl();
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
