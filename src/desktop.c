/**
 * NanoOS Desktop - Pure LVGL Desktop UI (LVGL v9)
 *
 * Features:
 *   - Taskbar with Start menu and grouped window buttons
 *   - Taskbar buttons group same-titled windows as "Title +N"
 *   - Start menu with Programs, Documents, and Shutdown
 *   - Draggable, resizable windows with min/max/close
 *   - Click-to-raise z-ordering
 *   - Shutdown dialog with Power Off / Suspend / Restart
 *
 * This file includes ONLY lvgl.h.  Pure LVGL, no platform code.
 */

#include "desktop.h"
#include <string.h>

#define SCREEN_WIDTH   640
#define SCREEN_HEIGHT  480
#define TASKBAR_HEIGHT  36

#define MAX_WINDOWS         8
#define MAX_TASKBAR_GROUPS  8
#define GRIP_SIZE           6
#define MIN_WIN_W         120
#define MIN_WIN_H          80
#define MAX_TITLE          32

/* ------------------------------------------------------------------ */
/*  Grip / resize direction                                           */
/* ------------------------------------------------------------------ */

typedef enum {
    GRIP_N, GRIP_S, GRIP_E, GRIP_W,
    GRIP_NE, GRIP_NW, GRIP_SE, GRIP_SW,
    GRIP_COUNT
} grip_dir_t;

typedef struct win_state win_state_t;

typedef struct {
    grip_dir_t   dir;
    win_state_t *wst;
} grip_data_t;

/* ------------------------------------------------------------------ */
/*  Taskbar group — one button per unique window title                */
/* ------------------------------------------------------------------ */

typedef struct {
    bool        in_use;
    char        title[MAX_TITLE];
    lv_obj_t   *btn;
    lv_obj_t   *label;
    int         count;
} tb_group_t;

static tb_group_t tb_groups[MAX_TASKBAR_GROUPS];

/* ------------------------------------------------------------------ */
/*  Window state                                                      */
/* ------------------------------------------------------------------ */

struct win_state {
    bool        in_use;
    lv_obj_t   *win;
    tb_group_t *group_tb;           /* Taskbar group this window belongs to */
    lv_obj_t   *btn_maximize;
    lv_group_t *input_group;
    char        title[MAX_TITLE];
    int         creation_order;     /* For ordering in the popup */

    int32_t     orig_x, orig_y, orig_w, orig_h;

    int32_t     drag_offset_x, drag_offset_y;
    bool        dragging;

    int32_t     rz_start_mx, rz_start_my;
    int32_t     rz_start_win_x, rz_start_win_y;
    int32_t     rz_start_win_w, rz_start_win_h;

    lv_obj_t   *grips[GRIP_COUNT];
    grip_data_t grip_data[GRIP_COUNT];

    bool        maximized;
    bool        minimized;
};

static win_state_t windows[MAX_WINDOWS];
static int next_creation_order = 0;

static win_state_t *win_state_alloc(void)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].in_use) {
            memset(&windows[i], 0, sizeof(win_state_t));
            windows[i].in_use = true;
            windows[i].creation_order = next_creation_order++;
            return &windows[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                              */
/* ------------------------------------------------------------------ */

static lv_obj_t *create_window(lv_obj_t *parent, const char *title,
                               int32_t x, int32_t y,
                               int32_t w, int32_t h,
                               lv_group_t *group);
static void create_editor(lv_group_t *group, int32_t x, int32_t y);
static void close_start_menu(void);
static void update_tb_group_label(tb_group_t *g);

/* ------------------------------------------------------------------ */
/*  Styles                                                            */
/* ------------------------------------------------------------------ */

static lv_style_t style_desktop;
static lv_style_t style_taskbar;
static lv_style_t style_taskbar_btn;
static lv_style_t style_taskbar_btn_active;
static lv_style_t style_grip;
static lv_style_t style_grip_hover;
static lv_style_t style_menu;
static lv_style_t style_menu_item;
static lv_style_t style_menu_item_hover;

static void init_styles(void)
{
    lv_style_init(&style_desktop);
    lv_style_set_bg_color(&style_desktop, lv_color_hex(0x2B4570));
    lv_style_set_bg_opa(&style_desktop, LV_OPA_COVER);
    lv_style_set_border_width(&style_desktop, 0);
    lv_style_set_radius(&style_desktop, 0);
    lv_style_set_pad_all(&style_desktop, 0);

    lv_style_init(&style_taskbar);
    lv_style_set_bg_color(&style_taskbar, lv_color_hex(0x1A1A2E));
    lv_style_set_bg_opa(&style_taskbar, LV_OPA_COVER);
    lv_style_set_border_width(&style_taskbar, 0);
    lv_style_set_radius(&style_taskbar, 0);
    lv_style_set_pad_left(&style_taskbar, 4);
    lv_style_set_pad_right(&style_taskbar, 4);
    lv_style_set_pad_top(&style_taskbar, 2);
    lv_style_set_pad_bottom(&style_taskbar, 2);
    lv_style_set_pad_column(&style_taskbar, 4);

    lv_style_init(&style_taskbar_btn);
    lv_style_set_bg_color(&style_taskbar_btn, lv_color_hex(0x3A3A5C));
    lv_style_set_bg_opa(&style_taskbar_btn, LV_OPA_COVER);
    lv_style_set_text_color(&style_taskbar_btn, lv_color_white());
    lv_style_set_radius(&style_taskbar_btn, 4);
    lv_style_set_pad_left(&style_taskbar_btn, 10);
    lv_style_set_pad_right(&style_taskbar_btn, 10);
    lv_style_set_pad_top(&style_taskbar_btn, 4);
    lv_style_set_pad_bottom(&style_taskbar_btn, 4);

    lv_style_init(&style_taskbar_btn_active);
    lv_style_set_bg_color(&style_taskbar_btn_active, lv_color_hex(0x5A5A8C));

    lv_style_init(&style_grip);
    lv_style_set_bg_opa(&style_grip, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_grip, 0);
    lv_style_set_radius(&style_grip, 0);
    lv_style_set_pad_all(&style_grip, 0);

    lv_style_init(&style_grip_hover);
    lv_style_set_bg_color(&style_grip_hover, lv_color_hex(0x4488FF));
    lv_style_set_bg_opa(&style_grip_hover, LV_OPA_50);

    lv_style_init(&style_menu);
    lv_style_set_bg_color(&style_menu, lv_color_hex(0x222244));
    lv_style_set_bg_opa(&style_menu, LV_OPA_COVER);
    lv_style_set_border_color(&style_menu, lv_color_hex(0x444466));
    lv_style_set_border_width(&style_menu, 1);
    lv_style_set_radius(&style_menu, 4);
    lv_style_set_pad_all(&style_menu, 2);
    lv_style_set_pad_row(&style_menu, 0);

    lv_style_init(&style_menu_item);
    lv_style_set_bg_opa(&style_menu_item, LV_OPA_TRANSP);
    lv_style_set_text_color(&style_menu_item, lv_color_white());
    lv_style_set_pad_left(&style_menu_item, 10);
    lv_style_set_pad_right(&style_menu_item, 10);
    lv_style_set_pad_top(&style_menu_item, 6);
    lv_style_set_pad_bottom(&style_menu_item, 6);
    lv_style_set_radius(&style_menu_item, 2);
    lv_style_set_border_width(&style_menu_item, 0);

    lv_style_init(&style_menu_item_hover);
    lv_style_set_bg_color(&style_menu_item_hover, lv_color_hex(0x4466AA));
    lv_style_set_bg_opa(&style_menu_item_hover, LV_OPA_COVER);
}

/* ------------------------------------------------------------------ */
/*  Desktop + Taskbar                                                 */
/* ------------------------------------------------------------------ */

static lv_obj_t *desktop_area;
static lv_obj_t *taskbar;

static void create_desktop_and_taskbar(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    desktop_area = lv_obj_create(scr);
    lv_obj_add_style(desktop_area, &style_desktop, 0);
    lv_obj_set_size(desktop_area, SCREEN_WIDTH,
                    SCREEN_HEIGHT - TASKBAR_HEIGHT);
    lv_obj_align(desktop_area, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_remove_flag(desktop_area, LV_OBJ_FLAG_SCROLLABLE);

    taskbar = lv_obj_create(scr);
    lv_obj_add_style(taskbar, &style_taskbar, 0);
    lv_obj_set_size(taskbar, SCREEN_WIDTH, TASKBAR_HEIGHT);
    lv_obj_align(taskbar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_remove_flag(taskbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(taskbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(taskbar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

/* ------------------------------------------------------------------ */
/*  Taskbar group management                                          */
/* ------------------------------------------------------------------ */

static lv_obj_t *instance_popup = NULL;

static void close_instance_popup(void)
{
    if (instance_popup) {
        lv_obj_delete(instance_popup);
        instance_popup = NULL;
    }
}

/* Forward: called when the taskbar group button is clicked */
static void on_tb_group_click(lv_event_t *e);

static tb_group_t *find_tb_group(const char *title)
{
    for (int i = 0; i < MAX_TASKBAR_GROUPS; i++) {
        if (tb_groups[i].in_use &&
            strcmp(tb_groups[i].title, title) == 0)
            return &tb_groups[i];
    }
    return NULL;
}

static tb_group_t *create_tb_group(const char *title)
{
    for (int i = 0; i < MAX_TASKBAR_GROUPS; i++) {
        if (!tb_groups[i].in_use) {
            tb_group_t *g = &tb_groups[i];
            memset(g, 0, sizeof(tb_group_t));
            g->in_use = true;
            strncpy(g->title, title, MAX_TITLE - 1);
            g->title[MAX_TITLE - 1] = '\0';

            g->btn = lv_button_create(taskbar);
            lv_obj_add_style(g->btn, &style_taskbar_btn, 0);
            lv_obj_add_style(g->btn, &style_taskbar_btn_active, 0);
            lv_obj_set_height(g->btn, LV_SIZE_CONTENT);

            g->label = lv_label_create(g->btn);
            lv_label_set_text(g->label, title);
            lv_obj_center(g->label);

            lv_obj_add_event_cb(g->btn, on_tb_group_click,
                                LV_EVENT_CLICKED, g);
            return g;
        }
    }
    return NULL;
}

static tb_group_t *get_or_create_tb_group(const char *title)
{
    tb_group_t *g = find_tb_group(title);
    if (g) return g;
    return create_tb_group(title);
}

static void update_tb_group_label(tb_group_t *g)
{
    if (!g || !g->in_use) return;

    if (g->count <= 1) {
        lv_label_set_text(g->label, g->title);
    } else {
        /* "Editor +2", "Editor +3", etc. */
        char buf[MAX_TITLE + 8];
        /* Build manually to avoid snprintf/stdio dependency */
        strncpy(buf, g->title, MAX_TITLE - 1);
        buf[MAX_TITLE - 1] = '\0';

        /* Append " +N" */
        int len = (int)strlen(buf);
        buf[len++] = ' ';
        buf[len++] = '+';
        if (g->count < 10) {
            buf[len++] = '0' + g->count;
        } else {
            buf[len++] = '0' + (g->count / 10);
            buf[len++] = '0' + (g->count % 10);
        }
        buf[len] = '\0';

        lv_label_set_text(g->label, buf);
    }
}

static void add_to_tb_group(win_state_t *st)
{
    tb_group_t *g = get_or_create_tb_group(st->title);
    if (!g) return;
    st->group_tb = g;
    g->count++;
    update_tb_group_label(g);
}

static void remove_from_tb_group(win_state_t *st)
{
    tb_group_t *g = st->group_tb;
    if (!g) return;
    g->count--;
    if (g->count <= 0) {
        lv_obj_delete(g->btn);
        g->btn   = NULL;
        g->label = NULL;
        g->in_use = false;
    } else {
        update_tb_group_label(g);
    }
    st->group_tb = NULL;
}

/* ------------------------------------------------------------------ */
/*  Grip helpers                                                      */
/* ------------------------------------------------------------------ */

static void update_grips(win_state_t *st)
{
    int32_t x = lv_obj_get_x(st->win);
    int32_t y = lv_obj_get_y(st->win);
    int32_t w = lv_obj_get_width(st->win);
    int32_t h = lv_obj_get_height(st->win);

    lv_obj_set_pos(st->grips[GRIP_N],  x + GRIP_SIZE, y);
    lv_obj_set_size(st->grips[GRIP_N], w - 2 * GRIP_SIZE, GRIP_SIZE);
    lv_obj_set_pos(st->grips[GRIP_S],  x + GRIP_SIZE, y + h - GRIP_SIZE);
    lv_obj_set_size(st->grips[GRIP_S], w - 2 * GRIP_SIZE, GRIP_SIZE);
    lv_obj_set_pos(st->grips[GRIP_W],  x, y + GRIP_SIZE);
    lv_obj_set_size(st->grips[GRIP_W], GRIP_SIZE, h - 2 * GRIP_SIZE);
    lv_obj_set_pos(st->grips[GRIP_E],  x + w - GRIP_SIZE, y + GRIP_SIZE);
    lv_obj_set_size(st->grips[GRIP_E], GRIP_SIZE, h - 2 * GRIP_SIZE);

    lv_obj_set_pos(st->grips[GRIP_NW], x, y);
    lv_obj_set_size(st->grips[GRIP_NW], GRIP_SIZE, GRIP_SIZE);
    lv_obj_set_pos(st->grips[GRIP_NE], x + w - GRIP_SIZE, y);
    lv_obj_set_size(st->grips[GRIP_NE], GRIP_SIZE, GRIP_SIZE);
    lv_obj_set_pos(st->grips[GRIP_SW], x, y + h - GRIP_SIZE);
    lv_obj_set_size(st->grips[GRIP_SW], GRIP_SIZE, GRIP_SIZE);
    lv_obj_set_pos(st->grips[GRIP_SE], x + w - GRIP_SIZE, y + h - GRIP_SIZE);
    lv_obj_set_size(st->grips[GRIP_SE], GRIP_SIZE, GRIP_SIZE);
}

static void show_grips(win_state_t *st, bool visible)
{
    for (int i = 0; i < GRIP_COUNT; i++) {
        if (visible)
            lv_obj_remove_flag(st->grips[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(st->grips[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ------------------------------------------------------------------ */
/*  Window z-order                                                    */
/* ------------------------------------------------------------------ */

static void raise_window(win_state_t *st)
{
    lv_obj_move_to_index(st->win, -1);
    for (int i = 0; i < GRIP_COUNT; i++)
        lv_obj_move_to_index(st->grips[i], -1);
}

static void on_win_pressed(lv_event_t *e)
{
    win_state_t *st = (win_state_t *)lv_event_get_user_data(e);
    raise_window(st);
}

/* ------------------------------------------------------------------ */
/*  Resize grip callbacks                                             */
/* ------------------------------------------------------------------ */

static void on_grip_pressed(lv_event_t *e)
{
    grip_data_t *gd = (grip_data_t *)lv_event_get_user_data(e);
    win_state_t *st = gd->wst;
    raise_window(st);

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    st->rz_start_mx    = p.x;
    st->rz_start_my    = p.y;
    st->rz_start_win_x = lv_obj_get_x(st->win);
    st->rz_start_win_y = lv_obj_get_y(st->win);
    st->rz_start_win_w = lv_obj_get_width(st->win);
    st->rz_start_win_h = lv_obj_get_height(st->win);
}

static void on_grip_pressing(lv_event_t *e)
{
    grip_data_t *gd = (grip_data_t *)lv_event_get_user_data(e);
    win_state_t *st = gd->wst;
    grip_dir_t   dir = gd->dir;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    int32_t dx = p.x - st->rz_start_mx;
    int32_t dy = p.y - st->rz_start_my;
    int32_t nx = st->rz_start_win_x, ny = st->rz_start_win_y;
    int32_t nw = st->rz_start_win_w, nh = st->rz_start_win_h;

    if (dir == GRIP_E || dir == GRIP_NE || dir == GRIP_SE) nw += dx;
    if (dir == GRIP_W || dir == GRIP_NW || dir == GRIP_SW) { nx += dx; nw -= dx; }
    if (dir == GRIP_S || dir == GRIP_SE || dir == GRIP_SW) nh += dy;
    if (dir == GRIP_N || dir == GRIP_NE || dir == GRIP_NW) { ny += dy; nh -= dy; }

    if (nw < MIN_WIN_W) {
        if (dir == GRIP_W || dir == GRIP_NW || dir == GRIP_SW)
            nx = st->rz_start_win_x + st->rz_start_win_w - MIN_WIN_W;
        nw = MIN_WIN_W;
    }
    if (nh < MIN_WIN_H) {
        if (dir == GRIP_N || dir == GRIP_NE || dir == GRIP_NW)
            ny = st->rz_start_win_y + st->rz_start_win_h - MIN_WIN_H;
        nh = MIN_WIN_H;
    }

    lv_obj_set_pos(st->win, nx, ny);
    lv_obj_set_size(st->win, nw, nh);
    update_grips(st);
}

static void on_grip_released(lv_event_t *e)
{
    grip_data_t *gd = (grip_data_t *)lv_event_get_user_data(e);
    win_state_t *st = gd->wst;
    st->orig_x = lv_obj_get_x(st->win);
    st->orig_y = lv_obj_get_y(st->win);
    st->orig_w = lv_obj_get_width(st->win);
    st->orig_h = lv_obj_get_height(st->win);
}

/* ------------------------------------------------------------------ */
/*  Title bar drag callbacks                                          */
/* ------------------------------------------------------------------ */

static void on_header_pressed(lv_event_t *e)
{
    win_state_t *st = (win_state_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    raise_window(st);

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (st->maximized) {
        int32_t desk_w  = lv_obj_get_width(desktop_area);
        int32_t ratio_x = (point.x * st->orig_w) / desk_w;
        lv_obj_set_size(st->win, st->orig_w, st->orig_h);
        int32_t new_x = point.x - ratio_x;
        lv_obj_set_pos(st->win, new_x, point.y);
        st->maximized = false;
        lv_obj_t *lbl = lv_obj_get_child(st->btn_maximize, 0);
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PLUS);
        st->drag_offset_x = point.x - new_x;
        st->drag_offset_y = point.y - point.y;
        update_grips(st);
        show_grips(st, true);
    } else {
        st->drag_offset_x = point.x - lv_obj_get_x(st->win);
        st->drag_offset_y = point.y - lv_obj_get_y(st->win);
    }
    st->dragging = true;
}

static void on_header_pressing(lv_event_t *e)
{
    win_state_t *st = (win_state_t *)lv_event_get_user_data(e);
    if (!st->dragging) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    lv_obj_set_pos(st->win,
                   point.x - st->drag_offset_x,
                   point.y - st->drag_offset_y);
    update_grips(st);
}

static void on_header_released(lv_event_t *e)
{
    win_state_t *st = (win_state_t *)lv_event_get_user_data(e);
    st->dragging = false;
    st->orig_x = lv_obj_get_x(st->win);
    st->orig_y = lv_obj_get_y(st->win);
}

/* ------------------------------------------------------------------ */
/*  Window button callbacks                                           */
/* ------------------------------------------------------------------ */

static void on_minimize(lv_event_t *e)
{
    win_state_t *st = (win_state_t *)lv_event_get_user_data(e);
    st->minimized = true;
    lv_obj_add_flag(st->win, LV_OBJ_FLAG_HIDDEN);
    show_grips(st, false);
}

static void on_maximize(lv_event_t *e)
{
    win_state_t *st = (win_state_t *)lv_event_get_user_data(e);
    if (!st->maximized) {
        st->orig_x = lv_obj_get_x(st->win);
        st->orig_y = lv_obj_get_y(st->win);
        st->orig_w = lv_obj_get_width(st->win);
        st->orig_h = lv_obj_get_height(st->win);
        lv_obj_set_pos(st->win, 0, 0);
        lv_obj_set_size(st->win,
                        lv_obj_get_width(desktop_area),
                        lv_obj_get_height(desktop_area));
        st->maximized = true;
        lv_obj_t *lbl = lv_obj_get_child(st->btn_maximize, 0);
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_COPY);
        show_grips(st, false);
    } else {
        lv_obj_set_pos(st->win, st->orig_x, st->orig_y);
        lv_obj_set_size(st->win, st->orig_w, st->orig_h);
        st->maximized = false;
        lv_obj_t *lbl = lv_obj_get_child(st->btn_maximize, 0);
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PLUS);
        show_grips(st, true);
        update_grips(st);
    }
}

static void on_close(lv_event_t *e)
{
    win_state_t *st = (win_state_t *)lv_event_get_user_data(e);
    for (int i = 0; i < GRIP_COUNT; i++) {
        if (st->grips[i]) {
            lv_obj_delete(st->grips[i]);
            st->grips[i] = NULL;
        }
    }
    lv_obj_delete(st->win);
    remove_from_tb_group(st);
    st->win    = NULL;
    st->in_use = false;
}

/* ------------------------------------------------------------------ */
/*  Taskbar group click + instance popup                              */
/* ------------------------------------------------------------------ */

/* Helper to add a clickable menu item */
static lv_obj_t *add_menu_item(lv_obj_t *parent, const char *text)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_add_style(item, &style_menu_item, 0);
    lv_obj_add_style(item, &style_menu_item_hover, LV_STATE_HOVERED);
    lv_obj_set_size(item, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, text);
    return item;
}

/* Called when an instance in the popup is clicked */
static void on_instance_click(lv_event_t *e)
{
    win_state_t *st = (win_state_t *)lv_event_get_user_data(e);
    close_instance_popup();

    if (st->minimized) {
        st->minimized = false;
        lv_obj_remove_flag(st->win, LV_OBJ_FLAG_HIDDEN);
        if (!st->maximized) {
            show_grips(st, true);
            update_grips(st);
        }
    }
    raise_window(st);
}

/* Collect windows belonging to a group, sorted by creation order */
static int collect_group_windows(tb_group_t *g, win_state_t **out, int max)
{
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS && n < max; i++) {
        if (windows[i].in_use && windows[i].group_tb == g)
            out[n++] = &windows[i];
    }
    /* Simple insertion sort by creation_order */
    for (int i = 1; i < n; i++) {
        win_state_t *key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j]->creation_order > key->creation_order) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}

static void on_tb_group_click(lv_event_t *e)
{
    tb_group_t *g = (tb_group_t *)lv_event_get_user_data(e);

    /* If popup is already open for this group, close it */
    if (instance_popup) {
        close_instance_popup();
        return;
    }

    /* Single instance — toggle minimize/restore directly */
    if (g->count == 1) {
        win_state_t *st = NULL;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].in_use && windows[i].group_tb == g) {
                st = &windows[i];
                break;
            }
        }
        if (!st) return;

        if (st->minimized) {
            st->minimized = false;
            lv_obj_remove_flag(st->win, LV_OBJ_FLAG_HIDDEN);
            raise_window(st);
            if (!st->maximized) {
                show_grips(st, true);
                update_grips(st);
            }
        } else {
            st->minimized = true;
            lv_obj_add_flag(st->win, LV_OBJ_FLAG_HIDDEN);
            show_grips(st, false);
        }
        return;
    }

    /* Multiple instances — show a popup listing them */
    win_state_t *sorted[MAX_WINDOWS];
    int n = collect_group_windows(g, sorted, MAX_WINDOWS);
    if (n == 0) return;

    instance_popup = lv_obj_create(lv_screen_active());
    lv_obj_add_style(instance_popup, &style_menu, 0);
    lv_obj_set_size(instance_popup, 180, LV_SIZE_CONTENT);
    lv_obj_remove_flag(instance_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(instance_popup, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < n; i++) {
        /* Build label like "Editor (1)", "Editor (2)" */
        char buf[MAX_TITLE + 8];
        strncpy(buf, g->title, MAX_TITLE - 1);
        buf[MAX_TITLE - 1] = '\0';
        int len = (int)strlen(buf);
        buf[len++] = ' ';
        buf[len++] = '(';
        if ((i + 1) < 10) {
            buf[len++] = '0' + (i + 1);
        } else {
            buf[len++] = '0' + ((i + 1) / 10);
            buf[len++] = '0' + ((i + 1) % 10);
        }
        buf[len++] = ')';
        buf[len] = '\0';

        /* Mark minimized windows */
        if (sorted[i]->minimized) {
            buf[len++] = ' ';
            buf[len++] = '-';
            buf[len] = '\0';
        }

        lv_obj_t *item = add_menu_item(instance_popup, buf);
        lv_obj_add_event_cb(item, on_instance_click,
                            LV_EVENT_CLICKED, sorted[i]);
    }

    /* Position above the taskbar button */
    lv_obj_update_layout(instance_popup);
    int32_t popup_h = lv_obj_get_height(instance_popup);
    int32_t btn_x   = lv_obj_get_x(g->btn);
    lv_obj_set_pos(instance_popup, btn_x,
                   SCREEN_HEIGHT - TASKBAR_HEIGHT - popup_h - 4);
}

/* ------------------------------------------------------------------ */
/*  Grip creation                                                     */
/* ------------------------------------------------------------------ */

static void create_grips(win_state_t *st)
{
    for (int i = 0; i < GRIP_COUNT; i++) {
        lv_obj_t *g = lv_obj_create(desktop_area);
        lv_obj_add_style(g, &style_grip, 0);
        lv_obj_add_style(g, &style_grip_hover, LV_STATE_HOVERED);
        lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
        st->grip_data[i].dir = (grip_dir_t)i;
        st->grip_data[i].wst = st;
        lv_obj_add_event_cb(g, on_grip_pressed,  LV_EVENT_PRESSED,  &st->grip_data[i]);
        lv_obj_add_event_cb(g, on_grip_pressing,  LV_EVENT_PRESSING, &st->grip_data[i]);
        lv_obj_add_event_cb(g, on_grip_released, LV_EVENT_RELEASED, &st->grip_data[i]);
        st->grips[i] = g;
    }
    update_grips(st);
}

/* ------------------------------------------------------------------ */
/*  Window creation                                                   */
/* ------------------------------------------------------------------ */

static lv_obj_t *create_window(lv_obj_t *parent, const char *title,
                               int32_t x, int32_t y,
                               int32_t w, int32_t h,
                               lv_group_t *group)
{
    win_state_t *st = win_state_alloc();
    if (!st) return NULL;

    st->input_group = group;
    st->orig_x = x;  st->orig_y = y;
    st->orig_w = w;  st->orig_h = h;
    strncpy(st->title, title, MAX_TITLE - 1);
    st->title[MAX_TITLE - 1] = '\0';

    lv_obj_t *win = lv_win_create(parent);
    st->win = win;
    lv_win_add_title(win, title);

    lv_obj_t *header = lv_win_get_header(win);
    lv_obj_set_height(header, 30);
    lv_obj_add_event_cb(header, on_header_pressed,  LV_EVENT_PRESSED,  st);
    lv_obj_add_event_cb(header, on_header_pressing,  LV_EVENT_PRESSING, st);
    lv_obj_add_event_cb(header, on_header_released, LV_EVENT_RELEASED, st);

    lv_obj_t *btn_min = lv_win_add_button(win, LV_SYMBOL_MINUS, 30);
    lv_obj_add_event_cb(btn_min, on_minimize, LV_EVENT_CLICKED, st);

    lv_obj_t *btn_max = lv_win_add_button(win, LV_SYMBOL_PLUS, 30);
    lv_obj_add_event_cb(btn_max, on_maximize, LV_EVENT_CLICKED, st);
    st->btn_maximize = btn_max;

    lv_obj_t *btn_close = lv_win_add_button(win, LV_SYMBOL_CLOSE, 30);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0xCC3333), 0);
    lv_obj_set_style_bg_opa(btn_close, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(btn_close, on_close, LV_EVENT_CLICKED, st);

    lv_obj_set_pos(win, x, y);
    lv_obj_set_size(win, w, h);
    lv_obj_set_style_anim_duration(win, 0, 0);

    lv_obj_add_event_cb(win, on_win_pressed, LV_EVENT_PRESSED, st);
    lv_obj_t *win_content = lv_win_get_content(win);
    lv_obj_add_flag(win_content, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* Add to taskbar group */
    add_to_tb_group(st);

    create_grips(st);
    return win;
}

/* ------------------------------------------------------------------ */
/*  Editor creation helper                                            */
/* ------------------------------------------------------------------ */

static lv_group_t *app_group;

static void create_editor(lv_group_t *group, int32_t x, int32_t y)
{
    int32_t win_w = 370;
    int32_t win_h = 256;

    lv_obj_t *win = create_window(desktop_area, "Editor",
                                  x, y, win_w, win_h, group);
    if (!win) return;

    lv_obj_t *content = lv_win_get_content(win);
    lv_obj_set_style_pad_all(content, 0, 0);

    lv_obj_t *ta = lv_textarea_create(content);
    lv_obj_set_size(ta, LV_PCT(100), LV_PCT(100));
    lv_textarea_set_text(ta, "Hello, world!");
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(ta, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_group_add_obj(group, ta);
    lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
}

/* ------------------------------------------------------------------ */
/*  Start menu                                                        */
/* ------------------------------------------------------------------ */

static lv_obj_t *start_menu     = NULL;
static lv_obj_t *sub_programs   = NULL;
static lv_obj_t *sub_documents  = NULL;
static lv_obj_t *shutdown_dlg   = NULL;

static void close_start_menu(void)
{
    if (sub_programs)  { lv_obj_delete(sub_programs);  sub_programs  = NULL; }
    if (sub_documents) { lv_obj_delete(sub_documents); sub_documents = NULL; }
    if (start_menu)    { lv_obj_delete(start_menu);    start_menu    = NULL; }
}

static void close_submenus(void)
{
    if (sub_programs)  { lv_obj_delete(sub_programs);  sub_programs  = NULL; }
    if (sub_documents) { lv_obj_delete(sub_documents); sub_documents = NULL; }
}

/* --- Programs submenu ---------------------------------------------- */

static void on_editor_click(lv_event_t *e)
{
    (void)e;
    close_start_menu();
    static int spawn_count = 0;
    int32_t off = (spawn_count++ % 5) * 20;
    create_editor(app_group, 40 + off, 20 + off);
}

static void show_programs_submenu(void)
{
    close_submenus();
    int32_t menu_x = lv_obj_get_x(start_menu) + lv_obj_get_width(start_menu);
    int32_t menu_y = lv_obj_get_y(start_menu);

    sub_programs = lv_obj_create(lv_screen_active());
    lv_obj_add_style(sub_programs, &style_menu, 0);
    lv_obj_set_size(sub_programs, 140, LV_SIZE_CONTENT);
    lv_obj_set_pos(sub_programs, menu_x, menu_y);
    lv_obj_remove_flag(sub_programs, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(sub_programs, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *item = add_menu_item(sub_programs, LV_SYMBOL_EDIT " Editor");
    lv_obj_add_event_cb(item, on_editor_click, LV_EVENT_CLICKED, NULL);
}

static void on_programs_hover(lv_event_t *e)
{
    (void)e;
    show_programs_submenu();
}

/* --- Documents submenu --------------------------------------------- */

static void show_documents_submenu(void)
{
    close_submenus();
    int32_t menu_x = lv_obj_get_x(start_menu) + lv_obj_get_width(start_menu);
    int32_t menu_y = lv_obj_get_y(start_menu);

    sub_documents = lv_obj_create(lv_screen_active());
    lv_obj_add_style(sub_documents, &style_menu, 0);
    lv_obj_set_size(sub_documents, 160, LV_SIZE_CONTENT);
    lv_obj_set_pos(sub_documents, menu_x, menu_y + 30);
    lv_obj_remove_flag(sub_documents, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(sub_documents, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *item = add_menu_item(sub_documents, "(empty)");
    lv_obj_set_style_text_color(item, lv_color_hex(0x888888), 0);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_CLICKABLE);
}

static void on_documents_hover(lv_event_t *e)
{
    (void)e;
    show_documents_submenu();
}

/* --- Shutdown ------------------------------------------------------ */

static void on_shutdown_hover(lv_event_t *e)
{
    (void)e;
    close_submenus();
}

static int shutdown_selection = 0;

static void on_shutdown_option(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target_obj(e);
    int32_t idx = (int32_t)(intptr_t)lv_event_get_user_data(e);
    shutdown_selection = idx;

    lv_obj_t *parent = lv_obj_get_parent(cb);
    uint32_t cnt = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (child == cb)
            lv_obj_add_state(child, LV_STATE_CHECKED);
        else
            lv_obj_remove_state(child, LV_STATE_CHECKED);
    }
}

static void on_shutdown_ok(lv_event_t *e)
{
    (void)e;
    if (shutdown_dlg) { lv_obj_delete(shutdown_dlg); shutdown_dlg = NULL; }
}

static void on_shutdown_cancel(lv_event_t *e)
{
    (void)e;
    if (shutdown_dlg) { lv_obj_delete(shutdown_dlg); shutdown_dlg = NULL; }
}

static void show_shutdown_dialog(void)
{
    if (shutdown_dlg) return;

    int32_t dlg_w = 260, dlg_h = 180;
    int32_t dlg_x = (SCREEN_WIDTH - dlg_w) / 2;
    int32_t dlg_y = (SCREEN_HEIGHT - TASKBAR_HEIGHT - dlg_h) / 2;

    shutdown_dlg = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(shutdown_dlg, dlg_x, dlg_y);
    lv_obj_set_size(shutdown_dlg, dlg_w, dlg_h);
    lv_obj_set_style_bg_color(shutdown_dlg, lv_color_hex(0x222244), 0);
    lv_obj_set_style_bg_opa(shutdown_dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(shutdown_dlg, lv_color_hex(0x444466), 0);
    lv_obj_set_style_border_width(shutdown_dlg, 2, 0);
    lv_obj_set_style_radius(shutdown_dlg, 6, 0);
    lv_obj_set_style_pad_all(shutdown_dlg, 15, 0);
    lv_obj_remove_flag(shutdown_dlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(shutdown_dlg);
    lv_label_set_text(title, "Shut Down");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *opts = lv_obj_create(shutdown_dlg);
    lv_obj_set_size(opts, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(opts, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(opts, 0, 0);
    lv_obj_set_style_pad_all(opts, 0, 0);
    lv_obj_set_style_pad_row(opts, 4, 0);
    lv_obj_set_flex_flow(opts, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(opts, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_remove_flag(opts, LV_OBJ_FLAG_SCROLLABLE);

    static const char *labels[] = { "Power Off", "Suspend", "Restart" };
    shutdown_selection = 0;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *cb = lv_checkbox_create(opts);
        lv_checkbox_set_text(cb, labels[i]);
        lv_obj_set_style_text_color(cb, lv_color_white(), 0);
        if (i == 0) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_add_event_cb(cb, on_shutdown_option, LV_EVENT_VALUE_CHANGED,
                            (void *)(intptr_t)i);
    }

    lv_obj_t *btn_ok = lv_button_create(shutdown_dlg);
    lv_obj_set_size(btn_ok, 70, 32);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -78, 0);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "Ok");
    lv_obj_center(lbl_ok);
    lv_obj_add_event_cb(btn_ok, on_shutdown_ok, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_cancel = lv_button_create(shutdown_dlg);
    lv_obj_set_size(btn_cancel, 70, 32);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_center(lbl_cancel);
    lv_obj_add_event_cb(btn_cancel, on_shutdown_cancel, LV_EVENT_CLICKED, NULL);
}

static void on_shutdown_click(lv_event_t *e)
{
    (void)e;
    close_start_menu();
    show_shutdown_dialog();
}

/* ------------------------------------------------------------------ */
/*  Start button + menu                                               */
/* ------------------------------------------------------------------ */

static void on_start_click(lv_event_t *e)
{
    (void)e;
    close_instance_popup();

    if (start_menu) {
        close_start_menu();
        return;
    }

    start_menu = lv_obj_create(lv_screen_active());
    lv_obj_add_style(start_menu, &style_menu, 0);
    lv_obj_set_size(start_menu, 160, LV_SIZE_CONTENT);
    lv_obj_remove_flag(start_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(start_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_pos(start_menu, 4,
                   SCREEN_HEIGHT - TASKBAR_HEIGHT - 4);

    lv_obj_update_layout(start_menu);

    lv_obj_t *mi_prog = add_menu_item(start_menu,
                                      LV_SYMBOL_DIRECTORY " Programs  "
                                      LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(mi_prog, on_programs_hover, LV_EVENT_HOVER_OVER, NULL);
    lv_obj_add_event_cb(mi_prog, on_editor_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *mi_docs = add_menu_item(start_menu,
                                      LV_SYMBOL_FILE " Documents  "
                                      LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(mi_docs, on_documents_hover, LV_EVENT_HOVER_OVER, NULL);

    lv_obj_t *sep = lv_obj_create(start_menu);
    lv_obj_set_size(sep, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x444466), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);
    lv_obj_remove_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mi_shut = add_menu_item(start_menu,
                                      LV_SYMBOL_POWER " Shutdown");
    lv_obj_add_event_cb(mi_shut, on_shutdown_hover, LV_EVENT_HOVER_OVER, NULL);
    lv_obj_add_event_cb(mi_shut, on_shutdown_click, LV_EVENT_CLICKED, NULL);

    lv_obj_update_layout(start_menu);
    int32_t menu_h = lv_obj_get_height(start_menu);
    lv_obj_set_pos(start_menu, 4,
                   SCREEN_HEIGHT - TASKBAR_HEIGHT - menu_h - 4);
}

/* ------------------------------------------------------------------ */
/*  Public entry point                                                */
/* ------------------------------------------------------------------ */

void desktop_create(lv_group_t *group)
{
    app_group = group;
    init_styles();
    create_desktop_and_taskbar();

    lv_obj_t *start_btn = lv_button_create(taskbar);
    lv_obj_add_style(start_btn, &style_taskbar_btn, 0);
    lv_obj_set_height(start_btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x2266AA), 0);
    lv_obj_set_style_bg_opa(start_btn, LV_OPA_COVER, 0);

    lv_obj_t *start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, LV_SYMBOL_LIST " Start");
    lv_obj_center(start_lbl);

    lv_obj_add_event_cb(start_btn, on_start_click, LV_EVENT_CLICKED, NULL);
}
