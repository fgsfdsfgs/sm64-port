#include <ultra64.h>

#include "sm64.h"

#include "gfx_dimensions.h"

#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/segment2.h"
#include "game/save_file.h"

#include "audio/external.h"

#include "pc/controller/controller_ps2.h"
#include "pc/gfx/gfx_ps2.h"
#include "pc/gfx/gfx_pc.h"
#include "pc/ps2_vid_mode_select.h"


// stolen from `ingame_menu.c`
static u8 gMenuHoldKeyIndex = 0;
static u8 gMenuHoldKeyTimer = 0;
static s8 gDialogLineNum = 0;

u8 gShowVidModeSelect = FALSE;

// Used for signalling a special n64 controller combination when
// some buttons are held down for a number of frames
static int specialInputHoldTimer = 0;
static u32 coolOffTimer = FALSE;

// NTSC (480i) or PAL (576i)
#define DEFAULT_VID_MODE 1

#define TEXT_SELECT_VIDEO_MODE \
    0x1C,0x28,0x2F,0x28,0x26,0x37, DIALOG_CHAR_SPACE, \
    0x39,0x2C,0x27,0x28,0x32, DIALOG_CHAR_SPACE, \
    0x30,0x32,0x27,0x28, DIALOG_CHAR_SPACE, \
    0x37,0x32, DIALOG_CHAR_TERMINATOR

#define TEXT_TO_SAVE_YOUR_PREFERENCE \
    0x36,0x24,0x39,0x28, DIALOG_CHAR_SPACE, \
    0x3C,0x32,0x38,0x35, DIALOG_CHAR_SPACE, \
    0x33,0x35,0x28,0x29,0x28,0x35,0x28,0x31,0x26,0x28, DIALOG_CHAR_SPACE, \
    DIALOG_CHAR_TERMINATOR

extern void adjust_analog_stick(struct Controller *controller);

static void shade_screen(void) {
    create_dl_translation_matrix(MENU_MTX_PUSH, GFX_DIMENSIONS_FROM_LEFT_EDGE(0), SCREEN_HEIGHT, 0);

    create_dl_scale_matrix(MENU_MTX_NOPUSH,
                           GFX_DIMENSIONS_ASPECT_RATIO * SCREEN_HEIGHT / 130.0f, 3.0f, 1.0f);

    gDPSetEnvColor(gDisplayListHead++, 0, 0, 0, 110);
    gSPDisplayList(gDisplayListHead++, dl_draw_text_bg_box);
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

static void read_stick_input(struct Controller* controller) {
    OSContPad pads[4];
    osContGetReadData(&pads[0]);
    controller->rawStickX = pads[0].stick_x;
    controller->rawStickY = pads[0].stick_y;
    adjust_analog_stick(controller);
} 

static void handle_vertical_menu_scrolling(s8 scrollDirection, s8 *currentIndex, s8 minIndex, s8 maxIndex) {
    u8 index = 0;
    struct Controller controller;
    read_stick_input(&controller);

    if (controller.rawStickY > 60) {
        index++;
    }

    if (controller.rawStickY < -60) {
        index += 2;
    }

    if (((index ^ gMenuHoldKeyIndex) & index) == 2) {
        if (currentIndex[0] == maxIndex) {
            //! Probably originally a >=, but later replaced with an == and an else statement.
            currentIndex[0] = maxIndex;
        } else {
            play_sound(SOUND_MENU_CHANGE_SELECT, gDefaultSoundArgs);
            currentIndex[0]++;
        }
    }

    if (((index ^ gMenuHoldKeyIndex) & index) == 1) {
        if (currentIndex[0] == minIndex) {
            // Same applies to here as above
        } else {
            play_sound(SOUND_MENU_CHANGE_SELECT, gDefaultSoundArgs);
            currentIndex[0]--;
        }
    }

    if (gMenuHoldKeyTimer == 10) {
        gMenuHoldKeyTimer = 8;
        gMenuHoldKeyIndex = 0;
    } else {
        gMenuHoldKeyTimer++;
        gMenuHoldKeyIndex = index;
    }

    if ((index & 3) == 0) {
        gMenuHoldKeyTimer = 0;
    }
}


void render_ps2_vid_mode_options(s16 x, s16 y, s8 *index, s16 yIndex) {
    u8 text240p[] = { 0x02,0x04,0x00,0x33,0xFF };
    u8 text480i[] = { 0x04,0x08,0x00,0x2C,0xFF };
    u8 text480p[] = { 0x04,0x08,0x00,0x33,0xFF };
    u8 text720p[] = { 0x07,0x02,0x00,0x33,0xFF };
    u8 text1080i[] = { 0x01,0x00,0x08,0x00,0x2C,0xFF };
    u8* options[] = { text240p, text480i, text480p, text720p, text1080i };
    u8 infoText1[] = { TEXT_SELECT_VIDEO_MODE };
    u8 infoText2[] = { TEXT_TO_SAVE_YOUR_PREFERENCE };

    handle_vertical_menu_scrolling(MENU_SCROLL_VERTICAL, index, 1, 5);

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, gDialogTextAlpha);

    print_generic_string(x, y + 30, infoText1);
    print_generic_string(x, y + 15, infoText2);

    u8 i;
    for (i = 0; i < ARRAY_COUNT(options); i++) {
        print_generic_string(x + 10, y - 2 - (i * 15), options[i]);
    }

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);

    create_dl_translation_matrix(MENU_MTX_PUSH, x - 4, (y - ((index[0] - 1) * yIndex)) - 2, 0);

    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, gDialogTextAlpha);
    gSPDisplayList(gDisplayListHead++, dl_draw_triangle);
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

static void ps2_vid_mode_select_open(void) {
    gShowVidModeSelect = TRUE;    
    
    // reset vid mode
    u8 vidMode = DEFAULT_VID_MODE;
    if (gfx_ps2_wapi.set_vid_mode(vidMode)) {
        save_file_set_ps2_vid_mode(vidMode);
    }

    gDialogTextAlpha = 0;
    gDialogLineNum = vidMode + 1;

    // prevent a mode being selected by the X
    // button after the menu opens
    coolOffTimer = 60;
}

static void ps2_vid_mode_select_detect_open(u32 btns) {
    if ((btns & (PAD_CROSS | PAD_TRIANGLE)) == (PAD_CROSS | PAD_TRIANGLE) && !gShowVidModeSelect) {
        specialInputHoldTimer++;
    } else {
        specialInputHoldTimer = 0;
    }

    if (specialInputHoldTimer > 15) {
        ps2_vid_mode_select_open();
        specialInputHoldTimer = 0;
    }

    if (coolOffTimer > 0) {
        coolOffTimer--;
    }
}

void ps2_vid_mode_select_init(void) {
    u8 vidMode = DEFAULT_VID_MODE; // 480i;
    u16 savedVidMode = save_file_get_ps2_vid_mode();

    // Vid mode has never been saved
    if (savedVidMode != (u16)-1) {
        vidMode = savedVidMode;
    }
    gfx_ps2_wapi.set_vid_mode(vidMode);
}


void handle_ps2_vid_mode_select(void) {
    // Read raw PS2 controls so that we can read triangle
    u32 btns = controller_ps2.read_btns();
    // Look forthe special button combo
    ps2_vid_mode_select_detect_open(btns);

    if (gShowVidModeSelect) {
        shade_screen();
        render_ps2_vid_mode_options(99, 93, &gDialogLineNum, 15);
        // Preview the vid mode
        u8 vidMode = gDialogLineNum - 1;
        // This will do nothing unless the vid mode has changed
        // so it's safe to call every frame
        if (gfx_ps2_wapi.set_vid_mode(vidMode)) {
            // Reset the texture cache since we've changed 
            // video mode so VRAM might get a little weird
            // This atm does not work (causes more VRAM issues)
            // gfx_clear_texture_cache();
        };


        if ((btns & PAD_CROSS) && coolOffTimer == 0)
        {
            save_file_set_ps2_vid_mode(vidMode);
            play_sound(SOUND_MENU_PAUSE_2, gDefaultSoundArgs);
            gShowVidModeSelect = FALSE;
        }
        

        if (gDialogTextAlpha < 250) {
            gDialogTextAlpha += 25;
        }
        gDialogColorFadeTimer = (s16) gDialogColorFadeTimer + 0x1000;
    }
}

