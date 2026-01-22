#ifdef TARGET_PS2

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>

#include <kernel.h>
#include <gsKit.h>
#include <gsHires.h>
#include <dmaKit.h>

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"
#include "gfx_ps2.h"

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
    int iPassCount;
    int x_off;
    int y_off;
};

static const struct VidMode vid_modes[] = {
    // NTSC
    { "480i", GS_MODE_NTSC,      GS_INTERLACED,    GS_FIELD,  704,  480,  704,  452, 4, 1, 0, 0 },
    { "480p", GS_MODE_DTV_480P,  GS_NONINTERLACED, GS_FRAME,  704,  480,  704,  452, 2, 1, 0, 0 },
    // PAL
    { "576i", GS_MODE_PAL,       GS_INTERLACED,    GS_FIELD,  704,  576,  704,  536, 4, 1, 0, 0 },
    { "576p", GS_MODE_DTV_576P,  GS_NONINTERLACED, GS_FRAME,  704,  576,  704,  536, 2, 1, 0, 0 },
    // HDTV
    { "720p", GS_MODE_DTV_720P,  GS_NONINTERLACED, GS_FRAME, 1280,  720, 1280,  720, 1, 2, 0, 0 },
    {"1080i", GS_MODE_DTV_1080I, GS_INTERLACED,    GS_FIELD, 1920, 1080, 1920, 1080, 1, 2, 0, 0 },
};

GSGLOBAL *gs_global;

static int vsync_sema_1st_id;
static int vsync_sema_2nd_id;
static int vsync_sema_id = -1;

static const struct VidMode *vid_mode;
static bool use_hires = false;

/* Copy of gsKit_sync_flip, but without the 'flip' */
static void gsKit_sync(GSGLOBAL *gsGlobal)
{
    WaitSema(vsync_sema_1st_id);
    WaitSema(vsync_sema_2nd_id);
}

/* Copy of gsKit_sync_flip, but without the 'sync' */
static void gsKit_flip(GSGLOBAL *gsGlobal)
{
   if (!gsGlobal->FirstFrame)
   {
      if (gsGlobal->DoubleBuffering == GS_SETTING_ON)
      {
         GS_SET_DISPFB2( gsGlobal->ScreenBuffer[
               gsGlobal->ActiveBuffer & 1] / 8192,
               gsGlobal->Width / 64, gsGlobal->PSM, 0, 0 );

         gsGlobal->ActiveBuffer ^= 1;
      }

   }

   gsKit_setactive(gsGlobal);
}

/* PRIVATE METHODS */
static int vsync_handler()
{
   iSignalSema(vsync_sema_id ? vsync_sema_2nd_id : vsync_sema_1st_id);
   vsync_sema_id ^= 1;

   ExitHandler();
   return 0;
}

static void prepare_sema() {
    ee_sema_t sema_1st;
    sema_1st.init_count = 0;
    sema_1st.max_count = 1;
    sema_1st.option = 0;
    vsync_sema_1st_id = CreateSema(&sema_1st);

    ee_sema_t sema_2nd;
    sema_2nd.init_count = 0;
    sema_2nd.max_count = 1;
    sema_2nd.option = 0;
    vsync_sema_2nd_id = CreateSema(&sema_2nd);
}

static void gfx_ps2_init(const char *game_name, bool start_in_fullscreen) {
#if defined(VERSION_EU)
    vid_mode = &vid_modes[2]; // PAL
#else
    vid_mode = &vid_modes[0]; // NTCS
    // change to 5 for 1080i
    // vid_mode = &vid_modes[5];
#endif
    use_hires = (vid_mode->mode == GS_MODE_DTV_720P || vid_mode->mode == GS_MODE_DTV_1080I);

    if (use_hires) {
        gs_global = gsKit_hires_init_global();
    } else {
        gs_global = gsKit_init_global();
    }

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gs_global->Mode = vid_mode->mode;
    gs_global->Width = vid_mode->width;
    gs_global->Height = vid_mode->height;
    if (gs_global->Mode == GS_MODE_DTV_1080I) {
        gs_global->Height /= 2;
    }

    gs_global->Interlace = vid_mode->interlace;
    gs_global->Field = vid_mode->field;
    gs_global->ZBuffering = GS_SETTING_ON;
    gs_global->DoubleBuffering = GS_SETTING_ON;
    gs_global->PrimAAEnable = GS_SETTING_OFF;
    // this could be enabled for hires, but I don't like it
    gs_global->Dithering = GS_SETTING_OFF;
    // hires runs out of VRAM if using more than 16bit color
    gs_global->PSM = use_hires ? GS_PSM_CT16 : GS_PSM_CT24;
    gs_global->PSMZ = GS_PSMZ_16; // 16-bit unsigned zbuffer

    if (use_hires) {
        gsKit_hires_init_screen(gs_global, vid_mode->iPassCount);
    } else {
        gsKit_init_screen(gs_global);
    }
    // hires sets the texture pointer to the wrong location. Ensure it's correct.
    gs_global->TexturePointer = gs_global->CurrentPointer;
    gsKit_TexManager_init(gs_global);
}

static void gfx_ps2_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {

}

static void gfx_ps2_set_fullscreen(bool enable) {

}

static void gfx_ps2_set_keyboard_callbacks(bool (*on_key_down)(int scancode), bool (*on_key_up)(int scancode), void (*on_all_keys_up)(void)) {

}

static void gfx_ps2_main_loop(void (*run_one_game_iter)(void)) {
    run_one_game_iter();
}

static void gfx_ps2_get_dimensions(uint32_t *width, uint32_t *height) {
    *width = gs_global->Width;
    *height = gs_global->Height;
    // the game doesn't need to know that we are 
    // rendering at half height for 1080i
    if (gs_global->Mode == GS_MODE_DTV_1080I) {
        *height *= 2;
    }
}

static void gfx_ps2_handle_events(void) {

}

static bool gfx_ps2_start_frame(void) {
    return 1;
}

static void gfx_ps2_swap_buffers_begin(void) {
    if (vsync_sema_id != -1) return;

    prepare_sema();
    vsync_sema_id = 0;
    gsKit_add_vsync_handler(vsync_handler);
}

static void gfx_ps2_swap_buffers_end(void) {
    /* How SM64 expect to run at 30 PFS we need to wait for 2 vsync */
    gsKit_sync(gs_global);
    if (use_hires) {
        gsKit_hires_flip(gs_global);
    } else {
        gsKit_flip(gs_global);
    }

    gsKit_queue_exec(gs_global);
    gsKit_TexManager_nextFrame(gs_global);
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
