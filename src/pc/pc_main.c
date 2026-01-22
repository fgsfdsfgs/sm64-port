#include <stdlib.h>

#ifdef TARGET_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "sm64.h"

#include "game/memory.h"
#include "audio/external.h"

#include "gfx/gfx_pc.h"
#include "gfx/gfx_opengl.h"
#include "gfx/gfx_direct3d11.h"
#include "gfx/gfx_direct3d12.h"
#include "gfx/gfx_dxgi.h"
#include "gfx/gfx_glx.h"
#include "gfx/gfx_sdl.h"
#include "gfx/gfx_ps2.h"

#include "audio/audio_api.h"
#include "audio/audio_wasapi.h"
#include "audio/audio_pulse.h"
#include "audio/audio_alsa.h"
#include "audio/audio_sdl.h"
#include "audio/audio_ps2.h"
#include "audio/audio_null.h"

#include "controller/controller_keyboard.h"

#include "configfile.h"

#include "compat.h"

#define CONFIG_FILE "sm64config.txt"

#ifdef TARGET_PS2
# include <tamtypes.h>
# include <kernel.h>
# include <iopcontrol.h>
# include <sifrpc.h>
# include <loadfile.h>
# include <sbv_patches.h>
# include <ps2_filesystem_driver.h>
# include "ps2_memcard.h"
#endif

OSMesg D_80339BEC;
OSMesgQueue gSIEventMesgQueue;

s8 gResetTimer;
s8 D_8032C648;
s8 gDebugLevelSelect;
s8 gShowProfiler;
s8 gShowDebugText;

static struct AudioAPI *audio_api;
static struct GfxWindowManagerAPI *wm_api;
static struct GfxRenderingAPI *rendering_api;

extern void gfx_run(Gfx *commands);
extern void thread5_game_loop(void *arg);
extern void create_next_audio_buffer(s16 *samples, u32 num_samples);
void game_loop_one_iteration(void);

void dispatch_audio_sptask(UNUSED struct SPTask *spTask) {
}

void set_vblank_handler(UNUSED s32 index, UNUSED struct VblankHandler *handler, UNUSED OSMesgQueue *queue, UNUSED OSMesg *msg) {
}

static uint8_t inited = 0;

#include "game/game_init.h" // for gGlobalTimer
void send_display_list(struct SPTask *spTask) {
    if (!inited) {
        return;
    }
    gfx_run((Gfx *)spTask->task.t.data_ptr);
}

#ifdef VERSION_EU
#define SAMPLES_HIGH 656
#define SAMPLES_LOW 640
#else
#define SAMPLES_HIGH 544
#define SAMPLES_LOW 528
#endif

static s16 audio_buffer[SAMPLES_HIGH * 2 * 2];

static inline void audio_frame(void) {
    int samples_left = audio_api->buffered();
    u32 num_audio_samples = samples_left < audio_api->get_desired_buffered() ? SAMPLES_HIGH : SAMPLES_LOW;
    s16 audio_buffer[SAMPLES_HIGH * 2 * 2];
    for (int i = 0; i < 2; i++) {
        create_next_audio_buffer(audio_buffer + i * (num_audio_samples * 2), num_audio_samples);
    }
    audio_api->play((u8 *)audio_buffer, 2 * num_audio_samples * 4);
}

void produce_one_frame(void) {
    gfx_start_frame();
    game_loop_one_iteration();
    audio_frame();
    gfx_end_frame();
}

#ifdef TARGET_WEB
static void em_main_loop(void) {
}

static void request_anim_frame(void (*func)(double time)) {
    EM_ASM(requestAnimationFrame(function(time) {
        dynCall("vd", $0, [time]);
    }), func);
}

static void on_anim_frame(double time) {
    static double target_time;

    time *= 0.03; // milliseconds to frame count (33.333 ms -> 1)

    if (time >= target_time + 10.0) {
        // We are lagging 10 frames behind, probably due to coming back after inactivity,
        // so reset, with a small margin to avoid potential jitter later.
        target_time = time - 0.010;
    }

    for (int i = 0; i < 2; i++) {
        // If refresh rate is 15 Hz or something we might need to generate two frames
        if (time >= target_time) {
            produce_one_frame();
            target_time = target_time + 1.0;
        }
    }

    request_anim_frame(on_anim_frame);
}
#endif

#ifdef TARGET_PS2
void reset_IOP() {
    SifInitRpc(0);
    while (!SifIopReset(NULL, 0)) {} // Comment this line if you want to "debug" through ps2link
    while (!SifIopSync()) {} 
}

static void prepare_IOP() {
    reset_IOP();
    SifInitRpc(0);

    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
}

static void init_drivers() {
    // This will try to load only the drivers from the unit where the game is running
    init_only_boot_ps2_filesystem_driver();
    // But also require to load manually the memcard driver, as maybe the game is running on a different unit
    // and we're using the memory card to save/load game data
    init_memcard_driver(true);
    ps2_memcard_init();
}

static void deinit_drivers() {
    deinit_memcard_driver(true);
    deinit_only_boot_ps2_filesystem_driver();
}
#endif

static void save_config(void) {
    configfile_save(CONFIG_FILE);
}

static void on_fullscreen_changed(bool is_now_fullscreen) {
    configFullscreen = is_now_fullscreen;
}

void main_func(void) {
    static u64 pool[0x165000/8 / 4 * sizeof(void *)];

#ifdef TARGET_PS2
    prepare_IOP();
    init_drivers();
#endif

    main_pool_init(pool, pool + sizeof(pool) / sizeof(pool[0]));
    gEffectsMemoryPool = mem_pool_init(0x4000, MEMORY_POOL_LEFT);

    configfile_load(CONFIG_FILE);
    atexit(save_config);

#ifdef TARGET_WEB
    emscripten_set_main_loop(em_main_loop, 0, 0);
    request_anim_frame(on_anim_frame);
#endif

#if defined(ENABLE_DX12)
    rendering_api = &gfx_direct3d12_api;
    wm_api = &gfx_dxgi_api;
#elif defined(ENABLE_DX11)
    rendering_api = &gfx_direct3d11_api;
    wm_api = &gfx_dxgi_api;
#elif defined(TARGET_PS2)
    rendering_api = &gfx_ps2_rapi;
    wm_api = &gfx_ps2_wapi;
#elif defined(ENABLE_OPENGL)
    rendering_api = &gfx_opengl_api;
    #if defined(__linux__) || defined(__BSD__)
        wm_api = &gfx_glx;
    #else
        wm_api = &gfx_sdl;
    #endif
#endif

    gfx_init(wm_api, rendering_api, "Super Mario 64 PC-Port", configFullscreen);
    
    wm_api->set_fullscreen_changed_callback(on_fullscreen_changed);
    wm_api->set_keyboard_callbacks(keyboard_on_key_down, keyboard_on_key_up, keyboard_on_all_keys_up);

#if HAVE_WASAPI
    if (audio_api == NULL && audio_wasapi.init()) {
        audio_api = &audio_wasapi;
    }
#endif
#if HAVE_PULSE_AUDIO
    if (audio_api == NULL && audio_pulse.init()) {
        audio_api = &audio_pulse;
    }
#endif
#if HAVE_ALSA
    if (audio_api == NULL && audio_alsa.init()) {
        audio_api = &audio_alsa;
    }
#endif
#ifdef TARGET_WEB
    if (audio_api == NULL && audio_sdl.init()) {
        audio_api = &audio_sdl;
    }
#endif
#ifdef TARGET_PS2
    if (audio_api == NULL && audio_ps2.init()) {
        audio_api = &audio_ps2;
    }
#endif
    if (audio_api == NULL) {
        audio_api = &audio_null;
    }

    audio_init();
    sound_init();

    thread5_game_loop(NULL);
#ifdef TARGET_WEB
    /*for (int i = 0; i < atoi(argv[1]); i++) {
        game_loop_one_iteration();
    }*/
    inited = 1;
#else
    inited = 1;
    while (1) {
        wm_api->main_loop(produce_one_frame);
    }
#endif
#ifdef TARGET_PS2
    deinit_drivers();
#endif
}

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
int WINAPI WinMain(UNUSED HINSTANCE hInstance, UNUSED HINSTANCE hPrevInstance, UNUSED LPSTR pCmdLine, UNUSED int nCmdShow) {
    main_func();
    return 0;
}
#else
int main(UNUSED int argc, UNUSED char *argv[]) {
    main_func();
    return 0;
}
#endif
