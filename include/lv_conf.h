/**
 * LVGL v9 configuration for NanoOS desktop simulator.
 *
 * Only settings that differ from the template defaults are listed here.
 * Everything else falls through to lv_conf_internal.h defaults.
 */

#if 1 /* Enable this file's content */

#ifndef LV_CONF_H
#define LV_CONF_H

/* =====================
 *  COLOR SETTINGS
 * ===================== */
#define LV_COLOR_DEPTH          32

/* =====================
 *  MEMORY
 * ===================== */
#define LV_MEM_SIZE             (256U * 1024U)

/* =====================
 *  TICK (let LVGL read SDL's clock directly)
 * ===================== */
#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  <SDL2/SDL.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR  (SDL_GetTicks())

/* =====================
 *  DISPLAY
 * ===================== */
#define LV_DPI_DEF              130

/* =====================
 *  LOGGING
 * ===================== */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

/* =====================
 *  FONTS
 * ===================== */
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1

/* =====================
 *  SDL DRIVER (built into LVGL v9)
 * ===================== */
#define LV_USE_SDL              1
#define LV_SDL_INCLUDE_PATH     <SDL2/SDL.h>
#define LV_SDL_BUF_COUNT        2
#define LV_SDL_FULLSCREEN       0

/* SDL resolution — used by lv_sdl_window_create() as defaults
 * if you pass 0, but we pass explicit values in main.c anyway. */
#define SDL_HOR_RES             640
#define SDL_VER_RES             480

/* =====================
 *  WIDGETS (most are enabled by default in v9;
 *  just making our dependencies explicit)
 * ===================== */
#define LV_USE_LABEL            1
#define LV_USE_BUTTON           1
#define LV_USE_BUTTONMATRIX     1
#define LV_USE_TEXTAREA         1
#define LV_USE_WIN              1
#define LV_USE_KEYBOARD         1

/* =====================
 *  LAYOUTS
 * ===================== */
#define LV_USE_FLEX             1
#define LV_USE_GRID             1

/* =====================
 *  THEMES
 * ===================== */
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1

/* =====================
 *  MISC
 * ===================== */
#define LV_BUILD_EXAMPLES       0

#endif /* LV_CONF_H */
#endif /* Enable this file's content */
