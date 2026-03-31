/**
 * NanoOS Desktop - Pure LVGL Desktop UI
 *
 * This module contains only LVGL API calls.  It knows nothing about
 * SDL, hardware drivers, tick sources, or event loops.  The platform
 * layer is responsible for:
 *   - Initializing LVGL (lv_init)
 *   - Registering display and input drivers
 *   - Creating an input group and passing it here
 *   - Running the LVGL timer handler
 */

#ifndef DESKTOP_H
#define DESKTOP_H

#include "lvgl.h"

/**
 * Build the desktop UI on the active screen.
 *
 * @param group  Input group that the platform layer has already bound
 *               to a keyboard/keypad input device.  Interactive widgets
 *               (text areas, buttons, etc.) will be added to this group
 *               so they can receive key events.
 */
void desktopCreate(lv_group_t *group);

#endif /* DESKTOP_H */
