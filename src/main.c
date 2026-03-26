/**
 * NanoOS Desktop - Platform Entry Point (LVGL v9 SDL backend)
 *
 * This file uses ONLY the LVGL API.  The SDL display, mouse, and
 * keyboard drivers are built into LVGL v9 and accessed through
 * lv_sdl_*() functions — no #include <SDL2/SDL.h> needed here.
 *
 * To port to a different target, replace the lv_sdl_*_create()
 * calls with your hardware's display/input setup and provide
 * an appropriate lv_conf.h.  desktop.c stays the same.
 */

#include "desktop.h"

#ifdef __linux__
#include <SDL2/SDL.h>
#endif

#define SCREEN_WIDTH   640
#define SCREEN_HEIGHT  480

/* ------------------------------------------------------------------ */
/*  Arrow cursor image (12 x 18, ARGB8888)                            */
/*                                                                    */
/*  Classic pointer arrow with black outline and white fill.          */
/*  The hotspot is (0,0) — the top-left pixel.                       */
/* ------------------------------------------------------------------ */

#define B 0x00,0x00,0x00,0xFF   /* black  (border)      */
#define W 0xFF,0xFF,0xFF,0xFF   /* white  (fill)        */
#define _ 0x00,0x00,0x00,0x00   /* transparent          */

static const uint8_t cursor_pixels[] = {
 /* 0    1    2    3    4    5    6    7    8    9   10   11  */
    B,   _,   _,   _,   _,   _,   _,   _,   _,   _,   _,   _,
    B,   B,   _,   _,   _,   _,   _,   _,   _,   _,   _,   _,
    B,   W,   B,   _,   _,   _,   _,   _,   _,   _,   _,   _,
    B,   W,   W,   B,   _,   _,   _,   _,   _,   _,   _,   _,
    B,   W,   W,   W,   B,   _,   _,   _,   _,   _,   _,   _,
    B,   W,   W,   W,   W,   B,   _,   _,   _,   _,   _,   _,
    B,   W,   W,   W,   W,   W,   B,   _,   _,   _,   _,   _,
    B,   W,   W,   W,   W,   W,   W,   B,   _,   _,   _,   _,
    B,   W,   W,   W,   W,   W,   W,   W,   B,   _,   _,   _,
    B,   W,   W,   W,   W,   W,   W,   W,   W,   B,   _,   _,
    B,   W,   W,   W,   W,   W,   B,   B,   B,   B,   _,   _,
    B,   W,   W,   B,   W,   W,   B,   _,   _,   _,   _,   _,
    B,   W,   B,   _,   B,   W,   W,   B,   _,   _,   _,   _,
    B,   B,   _,   _,   B,   W,   W,   B,   _,   _,   _,   _,
    B,   _,   _,   _,   _,   B,   W,   W,   B,   _,   _,   _,
    _,   _,   _,   _,   _,   B,   W,   W,   B,   _,   _,   _,
    _,   _,   _,   _,   _,   _,   B,   W,   B,   _,   _,   _,
    _,   _,   _,   _,   _,   _,   _,   B,   _,   _,   _,   _,
};

#undef B
#undef W
#undef _

static const lv_image_dsc_t cursor_img = {
    .header = {
        .cf     = LV_COLOR_FORMAT_ARGB8888,
        .w      = 12,
        .h      = 18,
    },
    .data      = cursor_pixels,
    .data_size = sizeof(cursor_pixels),
};

int main(void)
{
    lv_init();

    /* Create the display — LVGL's built-in SDL driver handles
     * window creation, draw buffers, and the flush callback. */
    lv_display_t *disp = lv_sdl_window_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    (void)disp;

    /* Create input devices — LVGL's SDL drivers handle all
     * event polling, key mapping, and text input internally. */
    lv_indev_t *mouse = lv_sdl_mouse_create();

    lv_indev_t *keyboard = lv_sdl_keyboard_create();

    /* Attach an arrow cursor image to the mouse device.
     * LVGL will move this object to follow the pointer. */
    lv_obj_t *cursor_obj = lv_image_create(lv_screen_active());
    lv_image_set_src(cursor_obj, &cursor_img);
    lv_indev_set_cursor(mouse, cursor_obj);

    /* Hide the system cursor so only the LVGL cursor is visible */
#ifdef __linux__
    SDL_ShowCursor(SDL_DISABLE);
#endif

    /* Create an input group and bind it to the keyboard */
    lv_group_t *group = lv_group_create();
    lv_indev_set_group(keyboard, group);

    /* Build the desktop UI (pure LVGL, no platform knowledge) */
    desktop_create(group);

    /* Main loop — lv_timer_handler() drives rendering and
     * internally pumps SDL events via the registered drivers. */
    while (1) {
        lv_timer_handler();
        lv_delay_ms(5);
    }

    return 0;
}
