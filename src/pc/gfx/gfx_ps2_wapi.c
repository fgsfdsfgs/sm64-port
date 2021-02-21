#ifdef TARGET_PS2

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>

#include <kernel.h>
#include <gsKit.h>
#include <dmaKit.h>

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"

#define FRAMERATE_SHIFT 1
#define FRAMESKIP 10

struct VidMode {
    const char *name;
    s16 mode;
    s16 interlace;
    s16 field;
    int max_width;
    int max_height;
    int width;
    int height;
    int vck;
    int x_off;
    int y_off;
};

static const struct VidMode vid_modes[] = {
    // NTSC
    { "480i", GS_MODE_NTSC,      GS_INTERLACED,    GS_FIELD,  704,  480,  704,  452, 4, 0, 0 },
    { "480p", GS_MODE_DTV_480P,  GS_NONINTERLACED, GS_FRAME,  704,  480,  704,  452, 2, 0, 0 },
    // PAL
    { "576i", GS_MODE_PAL,       GS_INTERLACED,    GS_FIELD,  704,  576,  704,  536, 4, 0, 0 },
    { "576p", GS_MODE_DTV_576P,  GS_NONINTERLACED, GS_FRAME,  704,  576,  704,  536, 2, 0, 0 },
    // HDTV
    { "720p", GS_MODE_DTV_720P,  GS_NONINTERLACED, GS_FRAME, 1280,  720, 1280,  698, 1, 0, 0 },
    {"1080i", GS_MODE_DTV_1080I, GS_INTERLACED,    GS_FIELD, 1920, 1080, 1920, 1080, 1, 0, 0 },
};

GSGLOBAL *gs_global;

static const struct VidMode *vid_mode;

static unsigned int window_width = DESIRED_SCREEN_WIDTH;
static unsigned int window_height = DESIRED_SCREEN_HEIGHT;

static unsigned int last = 0;
static bool do_render = true;

static volatile unsigned int vblank_count = 0;
static int vsync_callback_id = -1;

static int vsync_callback(void) {
    gsKit_display_buffer(gs_global); // working buffer gets displayed

    ++vblank_count;

    ExitHandler();

    return 0;
}

static void gfx_ps2_init(const char *game_name, bool start_in_fullscreen) {
    gs_global = gsKit_init_global();

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

    dmaKit_chan_init(DMA_CHANNEL_GIF);

    vsync_callback_id = gsKit_add_vsync_handler(&vsync_callback);

    if (gsKit_detect_signal() == GS_MODE_NTSC)
        vid_mode = &vid_modes[0];
    else
        vid_mode = &vid_modes[2];

    gs_global->ZBuffering = GS_SETTING_ON;
    gs_global->DoubleBuffering = GS_SETTING_ON;
    gs_global->PrimAAEnable = GS_SETTING_OFF;
    gs_global->PSM = GS_PSM_CT16; // RGB565 color buffer
    gs_global->PSMZ = GS_PSMZ_16; // 16-bit unsigned zbuffer

    gsKit_init_screen(gs_global);

    window_width = gs_global->Width;
    window_height = gs_global->Height;
}

static void gfx_ps2_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {

}

static void gfx_ps2_set_fullscreen(bool enable) {

}

static void gfx_ps2_set_keyboard_callbacks(bool (*on_key_down)(int scancode), bool (*on_key_up)(int scancode), void (*on_all_keys_up)(void)) {

}

static void gfx_ps2_main_loop(void (*run_one_game_iter)(void)) {
    const unsigned int now = vblank_count >> FRAMERATE_SHIFT;
    if (!last) last = now;

    const unsigned int frames = now - last;

    if (frames) {
        // catch up but skip the first FRAMESKIP frames
        int skip = (frames > FRAMESKIP) ? FRAMESKIP : (frames - 1);
        for (unsigned int f = 0; f < frames; ++f, --skip) {
            do_render = (skip <= 0);
            run_one_game_iter();
        }
        last = now;
    }
}

static void gfx_ps2_get_dimensions(uint32_t *width, uint32_t *height) {
    *width = window_width;
    *height = window_height;
}

static void gfx_ps2_handle_events(void) {

}

static bool gfx_ps2_start_frame(void) {
    if (do_render) gsKit_sync_flip(gs_global);
    return do_render;
}

static void gfx_ps2_swap_buffers_begin(void) {
}

static void gfx_ps2_swap_buffers_end(void) {

}

static double gfx_ps2_get_time(void) {
    return 0.0;
}

struct GfxWindowManagerAPI gfx_ps2_wapi = {
    gfx_ps2_init,
    gfx_ps2_set_keyboard_callbacks,
    gfx_ps2_set_fullscreen_changed_callback,
    gfx_ps2_set_fullscreen,
    gfx_ps2_main_loop,
    gfx_ps2_get_dimensions,
    gfx_ps2_handle_events,
    gfx_ps2_start_frame,
    gfx_ps2_swap_buffers_begin,
    gfx_ps2_swap_buffers_end,
    gfx_ps2_get_time
};

#endif // TARGET_PS2
