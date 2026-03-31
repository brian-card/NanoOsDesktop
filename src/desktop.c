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
 *   - Outline-box dragging/resizing (no real-time content reflow)
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

#define OUTLINE_BORDER_W    2

/* ------------------------------------------------------------------ */
/*  Grip / resize direction                                           */
/* ------------------------------------------------------------------ */

typedef enum {
    GRIP_N, GRIP_S, GRIP_E, GRIP_W,
    GRIP_NE, GRIP_NW, GRIP_SE, GRIP_SW,
    GRIP_COUNT
} GripDir;

typedef struct WinState WinState;

typedef struct {
    GripDir   dir;
    WinState *wst;
} GripData;

/* ------------------------------------------------------------------ */
/*  Taskbar group — one button per unique window title                */
/* ------------------------------------------------------------------ */

typedef struct {
    bool        inUse;
    char        title[MAX_TITLE];
    lv_obj_t   *btn;
    lv_obj_t   *label;
    int         count;
} TbGroup;

static TbGroup tbGroups[MAX_TASKBAR_GROUPS];

/* ------------------------------------------------------------------ */
/*  Window state                                                      */
/* ------------------------------------------------------------------ */

struct WinState {
    bool        inUse;
    lv_obj_t   *win;
    TbGroup    *groupTb;            /* Taskbar group this window belongs to */
    lv_obj_t   *btnMaximize;
    lv_group_t *inputGroup;
    char        title[MAX_TITLE];
    int         creationOrder;      /* For ordering in the popup */

    int32_t     origX, origY, origW, origH;

    int32_t     dragOffsetX, dragOffsetY;
    bool        dragging;

    int32_t     rzStartMx, rzStartMy;
    int32_t     rzStartWinX, rzStartWinY;
    int32_t     rzStartWinW, rzStartWinH;

    lv_obj_t   *grips[GRIP_COUNT];
    GripData    gripData[GRIP_COUNT];

    bool        maximized;
    bool        minimized;
};

static WinState windows[MAX_WINDOWS];
static int nextCreationOrder = 0;

static WinState *winStateAlloc(void)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].inUse) {
            memset(&windows[i], 0, sizeof(WinState));
            windows[i].inUse = true;
            windows[i].creationOrder = nextCreationOrder++;
            return &windows[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Outline box — shared by drag and resize operations                */
/*                                                                    */
/*  The target rect is tracked in plain integers so we never have to  */
/*  read coordinates back from the LVGL object (theme defaults,       */
/*  layout constraints, or padding could silently alter them).        */
/* ------------------------------------------------------------------ */

static lv_obj_t  *desktopArea;          /* forward — defined below   */

static lv_obj_t  *outlineBox = NULL;
static lv_style_t styleOutline;
static int32_t    outlineX, outlineY, outlineW, outlineH;

static void createOutline(int32_t x, int32_t y, int32_t w, int32_t h)
{
    outlineX = x;  outlineY = y;
    outlineW = w;  outlineH = h;

    if (outlineBox) {
        lv_obj_set_pos(outlineBox, x, y);
        lv_obj_set_size(outlineBox, w, h);
        lv_obj_remove_flag(outlineBox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_to_index(outlineBox, -1);
        return;
    }

    outlineBox = lv_obj_create(desktopArea);
    lv_obj_add_style(outlineBox, &styleOutline, 0);
    /* Force-override any theme defaults that might shift the box */
    lv_obj_set_style_pad_all(outlineBox, 0, 0);
    lv_obj_set_style_margin_all(outlineBox, 0, 0);
    lv_obj_set_style_max_width(outlineBox, SCREEN_WIDTH, 0);
    lv_obj_set_style_max_height(outlineBox, SCREEN_HEIGHT, 0);
    lv_obj_set_style_min_width(outlineBox, 0, 0);
    lv_obj_set_style_min_height(outlineBox, 0, 0);
    lv_obj_set_pos(outlineBox, x, y);
    lv_obj_set_size(outlineBox, w, h);
    lv_obj_remove_flag(outlineBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(outlineBox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(outlineBox, -1);
}

static void moveOutline(int32_t x, int32_t y, int32_t w, int32_t h)
{
    outlineX = x;  outlineY = y;
    outlineW = w;  outlineH = h;
    if (outlineBox) {
        lv_obj_set_pos(outlineBox, x, y);
        lv_obj_set_size(outlineBox, w, h);
    }
}

static void hideOutline(void)
{
    if (outlineBox) {
        lv_obj_add_flag(outlineBox, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                              */
/* ------------------------------------------------------------------ */

static lv_obj_t *createWindow(lv_obj_t *parent, const char *title,
                               int32_t x, int32_t y,
                               int32_t w, int32_t h,
                               lv_group_t *group);
static void createEditor(lv_group_t *group, int32_t x, int32_t y);
static void closeStartMenu(void);
static void updateTbGroupLabel(TbGroup *g);

/* ------------------------------------------------------------------ */
/*  Styles                                                            */
/* ------------------------------------------------------------------ */

static lv_style_t styleDesktop;
static lv_style_t styleTaskbar;
static lv_style_t styleTaskbarBtn;
static lv_style_t styleTaskbarBtnActive;
static lv_style_t styleGrip;
static lv_style_t styleGripHover;
static lv_style_t styleMenu;
static lv_style_t styleMenuItem;
static lv_style_t styleMenuItemHover;

static void initStyles(void)
{
    lv_style_init(&styleDesktop);
    lv_style_set_bg_color(&styleDesktop, lv_color_hex(0x2B4570));
    lv_style_set_bg_opa(&styleDesktop, LV_OPA_COVER);
    lv_style_set_border_width(&styleDesktop, 0);
    lv_style_set_radius(&styleDesktop, 0);
    lv_style_set_pad_all(&styleDesktop, 0);

    lv_style_init(&styleTaskbar);
    lv_style_set_bg_color(&styleTaskbar, lv_color_hex(0x1A1A2E));
    lv_style_set_bg_opa(&styleTaskbar, LV_OPA_COVER);
    lv_style_set_border_width(&styleTaskbar, 0);
    lv_style_set_radius(&styleTaskbar, 0);
    lv_style_set_pad_left(&styleTaskbar, 4);
    lv_style_set_pad_right(&styleTaskbar, 4);
    lv_style_set_pad_top(&styleTaskbar, 2);
    lv_style_set_pad_bottom(&styleTaskbar, 2);
    lv_style_set_pad_column(&styleTaskbar, 4);

    lv_style_init(&styleTaskbarBtn);
    lv_style_set_bg_color(&styleTaskbarBtn, lv_color_hex(0x3A3A5C));
    lv_style_set_bg_opa(&styleTaskbarBtn, LV_OPA_COVER);
    lv_style_set_text_color(&styleTaskbarBtn, lv_color_white());
    lv_style_set_radius(&styleTaskbarBtn, 4);
    lv_style_set_pad_left(&styleTaskbarBtn, 10);
    lv_style_set_pad_right(&styleTaskbarBtn, 10);
    lv_style_set_pad_top(&styleTaskbarBtn, 4);
    lv_style_set_pad_bottom(&styleTaskbarBtn, 4);

    lv_style_init(&styleTaskbarBtnActive);
    lv_style_set_bg_color(&styleTaskbarBtnActive, lv_color_hex(0x5A5A8C));

    lv_style_init(&styleGrip);
    lv_style_set_bg_opa(&styleGrip, LV_OPA_TRANSP);
    lv_style_set_border_width(&styleGrip, 0);
    lv_style_set_radius(&styleGrip, 0);
    lv_style_set_pad_all(&styleGrip, 0);

    lv_style_init(&styleGripHover);
    lv_style_set_bg_color(&styleGripHover, lv_color_hex(0x4488FF));
    lv_style_set_bg_opa(&styleGripHover, LV_OPA_50);

    /* Outline box: transparent fill, bright dashed-look border */
    lv_style_init(&styleOutline);
    lv_style_set_bg_opa(&styleOutline, LV_OPA_TRANSP);
    lv_style_set_border_color(&styleOutline, lv_color_hex(0xCCCCFF));
    lv_style_set_border_width(&styleOutline, OUTLINE_BORDER_W);
    lv_style_set_border_opa(&styleOutline, LV_OPA_70);
    lv_style_set_radius(&styleOutline, 0);
    lv_style_set_pad_all(&styleOutline, 0);

    lv_style_init(&styleMenu);
    lv_style_set_bg_color(&styleMenu, lv_color_hex(0x222244));
    lv_style_set_bg_opa(&styleMenu, LV_OPA_COVER);
    lv_style_set_border_color(&styleMenu, lv_color_hex(0x444466));
    lv_style_set_border_width(&styleMenu, 1);
    lv_style_set_radius(&styleMenu, 4);
    lv_style_set_pad_all(&styleMenu, 2);
    lv_style_set_pad_row(&styleMenu, 0);

    lv_style_init(&styleMenuItem);
    lv_style_set_bg_opa(&styleMenuItem, LV_OPA_TRANSP);
    lv_style_set_text_color(&styleMenuItem, lv_color_white());
    lv_style_set_pad_left(&styleMenuItem, 10);
    lv_style_set_pad_right(&styleMenuItem, 10);
    lv_style_set_pad_top(&styleMenuItem, 6);
    lv_style_set_pad_bottom(&styleMenuItem, 6);
    lv_style_set_radius(&styleMenuItem, 2);
    lv_style_set_border_width(&styleMenuItem, 0);

    lv_style_init(&styleMenuItemHover);
    lv_style_set_bg_color(&styleMenuItemHover, lv_color_hex(0x4466AA));
    lv_style_set_bg_opa(&styleMenuItemHover, LV_OPA_COVER);
}

/* ------------------------------------------------------------------ */
/*  Desktop + Taskbar                                                 */
/* ------------------------------------------------------------------ */

static lv_obj_t *taskbar;

static void createDesktopAndTaskbar(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    desktopArea = lv_obj_create(scr);
    lv_obj_add_style(desktopArea, &styleDesktop, 0);
    lv_obj_set_size(desktopArea, SCREEN_WIDTH,
                    SCREEN_HEIGHT - TASKBAR_HEIGHT);
    lv_obj_align(desktopArea, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_remove_flag(desktopArea, LV_OBJ_FLAG_SCROLLABLE);

    taskbar = lv_obj_create(scr);
    lv_obj_add_style(taskbar, &styleTaskbar, 0);
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

static lv_obj_t *instancePopup  = NULL;
static lv_obj_t *dismissOverlay = NULL;   /* Click-away overlay */
static lv_obj_t *startMenu      = NULL;
static lv_obj_t *subPrograms    = NULL;
static lv_obj_t *subDocuments   = NULL;
static lv_obj_t *shutdownDlg    = NULL;


static void closeInstancePopup(void)
{
    if (instancePopup) {
        lv_obj_delete(instancePopup);
        instancePopup = NULL;
    }
    if (dismissOverlay) {
        lv_obj_delete(dismissOverlay);
        dismissOverlay = NULL;
    }
}

/* Forward: called when the taskbar group button is clicked */
static void onTbGroupClick(lv_event_t *e);

static TbGroup *findTbGroup(const char *title)
{
    for (int i = 0; i < MAX_TASKBAR_GROUPS; i++) {
        if (tbGroups[i].inUse &&
            strcmp(tbGroups[i].title, title) == 0)
            return &tbGroups[i];
    }
    return NULL;
}

static TbGroup *createTbGroup(const char *title)
{
    for (int i = 0; i < MAX_TASKBAR_GROUPS; i++) {
        if (!tbGroups[i].inUse) {
            TbGroup *g = &tbGroups[i];
            memset(g, 0, sizeof(TbGroup));
            g->inUse = true;
            strncpy(g->title, title, MAX_TITLE - 1);
            g->title[MAX_TITLE - 1] = '\0';

            g->btn = lv_button_create(taskbar);
            lv_obj_add_style(g->btn, &styleTaskbarBtn, 0);
            lv_obj_add_style(g->btn, &styleTaskbarBtnActive, 0);
            lv_obj_set_height(g->btn, LV_SIZE_CONTENT);

            g->label = lv_label_create(g->btn);
            lv_label_set_text(g->label, title);
            lv_obj_center(g->label);

            lv_obj_add_event_cb(g->btn, onTbGroupClick,
                                LV_EVENT_CLICKED, g);
            return g;
        }
    }
    return NULL;
}

static TbGroup *getOrCreateTbGroup(const char *title)
{
    TbGroup *g = findTbGroup(title);
    if (g) return g;
    return createTbGroup(title);
}

static void updateTbGroupLabel(TbGroup *g)
{
    if (!g || !g->inUse) return;

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

static void addToTbGroup(WinState *st)
{
    TbGroup *g = getOrCreateTbGroup(st->title);
    if (!g) return;
    st->groupTb = g;
    g->count++;
    updateTbGroupLabel(g);
}

static void removeFromTbGroup(WinState *st)
{
    TbGroup *g = st->groupTb;
    if (!g) return;
    g->count--;
    if (g->count <= 0) {
        lv_obj_delete(g->btn);
        g->btn   = NULL;
        g->label = NULL;
        g->inUse = false;
    } else {
        updateTbGroupLabel(g);
    }
    st->groupTb = NULL;
}

/* ------------------------------------------------------------------ */
/*  Grip helpers                                                      */
/* ------------------------------------------------------------------ */

static void updateGrips(WinState *st)
{
    /* Force layout so get_x/y/width/height return up-to-date values.
     * Without this, grips are positioned at stale coordinates when
     * called right after lv_obj_set_pos / lv_obj_set_size. */
    lv_obj_update_layout(st->win);

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

static void showGrips(WinState *st, bool visible)
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

static void raiseWindow(WinState *st)
{
    lv_obj_move_to_index(st->win, -1);
    for (int i = 0; i < GRIP_COUNT; i++)
        lv_obj_move_to_index(st->grips[i], -1);
}

static void onWinPressed(lv_event_t *e)
{
    WinState *st = (WinState *)lv_event_get_user_data(e);
    raiseWindow(st);
}

/* ------------------------------------------------------------------ */
/*  Resize grip callbacks  (outline mode)                             */
/* ------------------------------------------------------------------ */

static void onGripPressed(lv_event_t *e)
{
    GripData *gd = (GripData *)lv_event_get_user_data(e);
    WinState *st = gd->wst;
    raiseWindow(st);

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    st->rzStartMx    = p.x;
    st->rzStartMy    = p.y;
    st->rzStartWinX = lv_obj_get_x(st->win);
    st->rzStartWinY = lv_obj_get_y(st->win);
    st->rzStartWinW = lv_obj_get_width(st->win);
    st->rzStartWinH = lv_obj_get_height(st->win);

    /* Show outline at current window position */
    createOutline(st->rzStartWinX, st->rzStartWinY,
                  st->rzStartWinW, st->rzStartWinH);
}

static void onGripPressing(lv_event_t *e)
{
    GripData *gd = (GripData *)lv_event_get_user_data(e);
    WinState *st = gd->wst;
    GripDir   dir = gd->dir;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    int32_t dx = p.x - st->rzStartMx;
    int32_t dy = p.y - st->rzStartMy;
    int32_t nx = st->rzStartWinX, ny = st->rzStartWinY;
    int32_t nw = st->rzStartWinW, nh = st->rzStartWinH;

    if (dir == GRIP_E || dir == GRIP_NE || dir == GRIP_SE) nw += dx;
    if (dir == GRIP_W || dir == GRIP_NW || dir == GRIP_SW) { nx += dx; nw -= dx; }
    if (dir == GRIP_S || dir == GRIP_SE || dir == GRIP_SW) nh += dy;
    if (dir == GRIP_N || dir == GRIP_NE || dir == GRIP_NW) { ny += dy; nh -= dy; }

    if (nw < MIN_WIN_W) {
        if (dir == GRIP_W || dir == GRIP_NW || dir == GRIP_SW)
            nx = st->rzStartWinX + st->rzStartWinW - MIN_WIN_W;
        nw = MIN_WIN_W;
    }
    if (nh < MIN_WIN_H) {
        if (dir == GRIP_N || dir == GRIP_NE || dir == GRIP_NW)
            ny = st->rzStartWinY + st->rzStartWinH - MIN_WIN_H;
        nh = MIN_WIN_H;
    }

    /* Move the outline only — don't touch the real window */
    moveOutline(nx, ny, nw, nh);
}

static void onGripReleased(lv_event_t *e)
{
    GripData *gd = (GripData *)lv_event_get_user_data(e);
    WinState *st = gd->wst;

    /* Apply the tracked outline rect to the real window */
    lv_obj_set_pos(st->win, outlineX, outlineY);
    lv_obj_set_size(st->win, outlineW, outlineH);

    st->origX = outlineX;
    st->origY = outlineY;
    st->origW = outlineW;
    st->origH = outlineH;

    hideOutline();
    updateGrips(st);
}

/* ------------------------------------------------------------------ */
/*  Title bar drag callbacks  (outline mode)                          */
/* ------------------------------------------------------------------ */

static void onHeaderPressed(lv_event_t *e)
{
    WinState *st = (WinState *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    raiseWindow(st);

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (st->maximized) {
        /* Restore from maximized on drag — compute proportional offset,
         * but don't move the window yet; just set up for outline drag */
        int32_t deskW  = lv_obj_get_width(desktopArea);
        int32_t ratioX = (point.x * st->origW) / deskW;
        int32_t newX = point.x - ratioX;
        int32_t newY = point.y;

        st->maximized = false;
        lv_obj_t *lbl = lv_obj_get_child(st->btnMaximize, 0);
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PLUS);

        /* Restore real window to pre-maximize size at computed position */
        lv_obj_set_size(st->win, st->origW, st->origH);
        lv_obj_set_pos(st->win, newX, newY);
        updateGrips(st);
        showGrips(st, true);

        st->dragOffsetX = point.x - newX;
        st->dragOffsetY = 0;

        /* Show outline at restored position */
        createOutline(newX, newY, st->origW, st->origH);
    } else {
        int32_t wx = lv_obj_get_x(st->win);
        int32_t wy = lv_obj_get_y(st->win);
        st->dragOffsetX = point.x - wx;
        st->dragOffsetY = point.y - wy;

        /* Show outline at current window position */
        createOutline(wx, wy,
                       lv_obj_get_width(st->win),
                       lv_obj_get_height(st->win));
    }
    st->dragging = true;
}

static void onHeaderPressing(lv_event_t *e)
{
    WinState *st = (WinState *)lv_event_get_user_data(e);
    if (!st->dragging) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);

    /* Move the outline only — don't touch the real window */
    moveOutline(point.x - st->dragOffsetX,
                point.y - st->dragOffsetY,
                outlineW, outlineH);
}

static void onHeaderReleased(lv_event_t *e)
{
    WinState *st = (WinState *)lv_event_get_user_data(e);
    st->dragging = false;

    /* Apply the tracked outline position to the real window */
    lv_obj_set_pos(st->win, outlineX, outlineY);
    st->origX = outlineX;
    st->origY = outlineY;

    hideOutline();
    updateGrips(st);
}

/* ------------------------------------------------------------------ */
/*  Window button callbacks                                           */
/* ------------------------------------------------------------------ */

static void onMinimize(lv_event_t *e)
{
    WinState *st = (WinState *)lv_event_get_user_data(e);
    st->minimized = true;
    lv_obj_add_flag(st->win, LV_OBJ_FLAG_HIDDEN);
    showGrips(st, false);
}

static void onMaximize(lv_event_t *e)
{
    WinState *st = (WinState *)lv_event_get_user_data(e);
    if (!st->maximized) {
        st->origX = lv_obj_get_x(st->win);
        st->origY = lv_obj_get_y(st->win);
        st->origW = lv_obj_get_width(st->win);
        st->origH = lv_obj_get_height(st->win);
        lv_obj_set_pos(st->win, 0, 0);
        lv_obj_set_size(st->win,
                        lv_obj_get_width(desktopArea),
                        lv_obj_get_height(desktopArea));
        st->maximized = true;
        lv_obj_t *lbl = lv_obj_get_child(st->btnMaximize, 0);
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_COPY);
        showGrips(st, false);
    } else {
        lv_obj_set_pos(st->win, st->origX, st->origY);
        lv_obj_set_size(st->win, st->origW, st->origH);
        st->maximized = false;
        lv_obj_t *lbl = lv_obj_get_child(st->btnMaximize, 0);
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PLUS);
        showGrips(st, true);
        updateGrips(st);
    }
}

static void onClose(lv_event_t *e)
{
    WinState *st = (WinState *)lv_event_get_user_data(e);
    for (int i = 0; i < GRIP_COUNT; i++) {
        if (st->grips[i]) {
            lv_obj_delete(st->grips[i]);
            st->grips[i] = NULL;
        }
    }
    lv_obj_delete(st->win);
    removeFromTbGroup(st);
    st->win    = NULL;
    st->inUse = false;
}

/* ------------------------------------------------------------------ */
/*  Taskbar group click + instance popup                              */
/* ------------------------------------------------------------------ */

/* Helper to add a clickable menu item */
static lv_obj_t *addMenuItem(lv_obj_t *parent, const char *text)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_add_style(item, &styleMenuItem, 0);
    lv_obj_add_style(item, &styleMenuItemHover, LV_STATE_HOVERED);
    lv_obj_set_size(item, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, text);
    return item;
}

/* Called when an instance in the popup is clicked */
static void onInstanceClick(lv_event_t *e)
{
    WinState *st = (WinState *)lv_event_get_user_data(e);
    closeInstancePopup();

    if (st->minimized) {
        st->minimized = false;
        lv_obj_remove_flag(st->win, LV_OBJ_FLAG_HIDDEN);
        if (!st->maximized) {
            showGrips(st, true);
            updateGrips(st);
        }
    }
    raiseWindow(st);
}

/* Collect windows belonging to a group, sorted by creation order */
static int collectGroupWindows(TbGroup *g, WinState **out, int max)
{
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS && n < max; i++) {
        if (windows[i].inUse && windows[i].groupTb == g)
            out[n++] = &windows[i];
    }
    /* Simple insertion sort by creationOrder */
    for (int i = 1; i < n; i++) {
        WinState *key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j]->creationOrder > key->creationOrder) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}

/* Called when the transparent overlay behind a menu is clicked.
 * We must not delete dismissOverlay from inside its own event
 * handler — use lv_obj_delete_async so it's deferred. */
static void onDismissOverlay(lv_event_t *e)
{
    (void)e;

    /* Close popups (these don't touch dismissOverlay since we
     * clear the pointer before deleting) */
    if (instancePopup) { lv_obj_delete(instancePopup); instancePopup = NULL; }
    if (subPrograms)   { lv_obj_delete(subPrograms);   subPrograms   = NULL; }
    if (subDocuments)  { lv_obj_delete(subDocuments);   subDocuments  = NULL; }
    if (startMenu)     { lv_obj_delete(startMenu);     startMenu     = NULL; }

    /* Defer deletion of ourselves */
    if (dismissOverlay) {
        lv_obj_t *ov = dismissOverlay;
        dismissOverlay = NULL;
        lv_obj_delete_async(ov);
    }
}

/* Create a full-screen transparent clickable overlay behind popups */
static void createDismissOverlay(void)
{
    if (dismissOverlay) return;

    dismissOverlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(dismissOverlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(dismissOverlay, 0, 0);
    lv_obj_set_style_bg_opa(dismissOverlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dismissOverlay, 0, 0);
    lv_obj_set_style_radius(dismissOverlay, 0, 0);
    lv_obj_set_style_pad_all(dismissOverlay, 0, 0);
    lv_obj_remove_flag(dismissOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dismissOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(dismissOverlay, onDismissOverlay,
                        LV_EVENT_CLICKED, NULL);
}

static void onTbGroupClick(lv_event_t *e)
{
    TbGroup *g = (TbGroup *)lv_event_get_user_data(e);

    /* Close the start menu if it's open */
    closeStartMenu();

    /* If popup is already open for this group, close it */
    if (instancePopup) {
        closeInstancePopup();
        return;
    }

    /* Single instance — toggle minimize/restore directly */
    if (g->count == 1) {
        WinState *st = NULL;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].inUse && windows[i].groupTb == g) {
                st = &windows[i];
                break;
            }
        }
        if (!st) return;

        if (st->minimized) {
            st->minimized = false;
            lv_obj_remove_flag(st->win, LV_OBJ_FLAG_HIDDEN);
            raiseWindow(st);
            if (!st->maximized) {
                showGrips(st, true);
                updateGrips(st);
            }
        } else {
            st->minimized = true;
            lv_obj_add_flag(st->win, LV_OBJ_FLAG_HIDDEN);
            showGrips(st, false);
        }
        return;
    }

    /* Multiple instances — show a popup listing them */
    WinState *sorted[MAX_WINDOWS];
    int n = collectGroupWindows(g, sorted, MAX_WINDOWS);
    if (n == 0) return;

    /* Transparent overlay catches clicks outside the popup */
    createDismissOverlay();

    instancePopup = lv_obj_create(lv_screen_active());
    lv_obj_add_style(instancePopup, &styleMenu, 0);
    lv_obj_set_size(instancePopup, 180, LV_SIZE_CONTENT);
    lv_obj_remove_flag(instancePopup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(instancePopup, LV_FLEX_FLOW_COLUMN);

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

        lv_obj_t *item = addMenuItem(instancePopup, buf);
        lv_obj_add_event_cb(item, onInstanceClick,
                            LV_EVENT_CLICKED, sorted[i]);
    }

    /* Position above the taskbar button */
    lv_obj_update_layout(instancePopup);
    int32_t popupH = lv_obj_get_height(instancePopup);
    int32_t btnX   = lv_obj_get_x(g->btn);
    lv_obj_set_pos(instancePopup, btnX,
                   SCREEN_HEIGHT - TASKBAR_HEIGHT - popupH - 4);
}

/* ------------------------------------------------------------------ */
/*  Grip creation                                                     */
/* ------------------------------------------------------------------ */

static void createGrips(WinState *st)
{
    for (int i = 0; i < GRIP_COUNT; i++) {
        lv_obj_t *g = lv_obj_create(desktopArea);
        lv_obj_add_style(g, &styleGrip, 0);
        lv_obj_add_style(g, &styleGripHover, LV_STATE_HOVERED);
        lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
        st->gripData[i].dir = (GripDir)i;
        st->gripData[i].wst = st;
        lv_obj_add_event_cb(g, onGripPressed,  LV_EVENT_PRESSED,  &st->gripData[i]);
        lv_obj_add_event_cb(g, onGripPressing,  LV_EVENT_PRESSING, &st->gripData[i]);
        lv_obj_add_event_cb(g, onGripReleased, LV_EVENT_RELEASED, &st->gripData[i]);
        st->grips[i] = g;
    }
    updateGrips(st);
}

/* ------------------------------------------------------------------ */
/*  Window creation                                                   */
/* ------------------------------------------------------------------ */

static lv_obj_t *createWindow(lv_obj_t *parent, const char *title,
                               int32_t x, int32_t y,
                               int32_t w, int32_t h,
                               lv_group_t *group)
{
    WinState *st = winStateAlloc();
    if (!st) return NULL;

    st->inputGroup = group;
    st->origX = x;  st->origY = y;
    st->origW = w;  st->origH = h;
    strncpy(st->title, title, MAX_TITLE - 1);
    st->title[MAX_TITLE - 1] = '\0';

    lv_obj_t *win = lv_win_create(parent);
    st->win = win;
    lv_win_add_title(win, title);

    lv_obj_t *header = lv_win_get_header(win);
    lv_obj_set_height(header, 30);
    lv_obj_add_event_cb(header, onHeaderPressed,  LV_EVENT_PRESSED,  st);
    lv_obj_add_event_cb(header, onHeaderPressing,  LV_EVENT_PRESSING, st);
    lv_obj_add_event_cb(header, onHeaderReleased, LV_EVENT_RELEASED, st);

    lv_obj_t *btnMin = lv_win_add_button(win, LV_SYMBOL_MINUS, 30);
    lv_obj_add_event_cb(btnMin, onMinimize, LV_EVENT_CLICKED, st);

    lv_obj_t *btnMax = lv_win_add_button(win, LV_SYMBOL_PLUS, 30);
    lv_obj_add_event_cb(btnMax, onMaximize, LV_EVENT_CLICKED, st);
    st->btnMaximize = btnMax;

    lv_obj_t *btnClose = lv_win_add_button(win, LV_SYMBOL_CLOSE, 30);
    lv_obj_set_style_bg_color(btnClose, lv_color_hex(0xCC3333), 0);
    lv_obj_set_style_bg_opa(btnClose, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(btnClose, onClose, LV_EVENT_CLICKED, st);

    lv_obj_set_pos(win, x, y);
    lv_obj_set_size(win, w, h);
    lv_obj_set_style_anim_duration(win, 0, 0);

    lv_obj_add_event_cb(win, onWinPressed, LV_EVENT_PRESSED, st);
    lv_obj_t *winContent = lv_win_get_content(win);
    lv_obj_add_flag(winContent, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* Add to taskbar group */
    addToTbGroup(st);

    createGrips(st);
    return win;
}

/* ------------------------------------------------------------------ */
/*  Editor creation helper                                            */
/* ------------------------------------------------------------------ */

static lv_group_t *appGroup;

static void createEditor(lv_group_t *group, int32_t x, int32_t y)
{
    int32_t winW = 370;
    int32_t winH = 256;

    lv_obj_t *win = createWindow(desktopArea, "Editor",
                                  x, y, winW, winH, group);
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

static void closeStartMenu(void)
{
    if (subPrograms)    { lv_obj_delete(subPrograms);    subPrograms    = NULL; }
    if (subDocuments)   { lv_obj_delete(subDocuments);   subDocuments   = NULL; }
    if (startMenu)      { lv_obj_delete(startMenu);      startMenu      = NULL; }
    if (dismissOverlay) { lv_obj_delete(dismissOverlay); dismissOverlay = NULL; }
}

static void closeSubmenus(void)
{
    if (subPrograms)  { lv_obj_delete(subPrograms);  subPrograms  = NULL; }
    if (subDocuments) { lv_obj_delete(subDocuments); subDocuments = NULL; }
}

/* --- Programs submenu ---------------------------------------------- */

static void onEditorClick(lv_event_t *e)
{
    (void)e;
    closeStartMenu();
    static int spawnCount = 0;
    int32_t off = (spawnCount++ % 5) * 20;
    createEditor(appGroup, 40 + off, 20 + off);
}

static void showProgramsSubmenu(void)
{
    closeSubmenus();
    int32_t menuX = lv_obj_get_x(startMenu) + lv_obj_get_width(startMenu);
    int32_t menuY = lv_obj_get_y(startMenu);

    subPrograms = lv_obj_create(lv_screen_active());
    lv_obj_add_style(subPrograms, &styleMenu, 0);
    lv_obj_set_size(subPrograms, 140, LV_SIZE_CONTENT);
    lv_obj_set_pos(subPrograms, menuX, menuY);
    lv_obj_remove_flag(subPrograms, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(subPrograms, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *item = addMenuItem(subPrograms, LV_SYMBOL_EDIT " Editor");
    lv_obj_add_event_cb(item, onEditorClick, LV_EVENT_CLICKED, NULL);
}

static void onProgramsHover(lv_event_t *e)
{
    (void)e;
    showProgramsSubmenu();
}

/* --- Documents submenu --------------------------------------------- */

static void showDocumentsSubmenu(void)
{
    closeSubmenus();
    int32_t menuX = lv_obj_get_x(startMenu) + lv_obj_get_width(startMenu);
    int32_t menuY = lv_obj_get_y(startMenu);

    subDocuments = lv_obj_create(lv_screen_active());
    lv_obj_add_style(subDocuments, &styleMenu, 0);
    lv_obj_set_size(subDocuments, 160, LV_SIZE_CONTENT);
    lv_obj_set_pos(subDocuments, menuX, menuY + 30);
    lv_obj_remove_flag(subDocuments, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(subDocuments, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *item = addMenuItem(subDocuments, "(empty)");
    lv_obj_set_style_text_color(item, lv_color_hex(0x888888), 0);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_CLICKABLE);
}

static void onDocumentsHover(lv_event_t *e)
{
    (void)e;
    showDocumentsSubmenu();
}

/* --- Shutdown ------------------------------------------------------ */

static void onShutdownHover(lv_event_t *e)
{
    (void)e;
    closeSubmenus();
}

static int shutdownSelection = 0;

static void onShutdownOption(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target_obj(e);
    int32_t idx = (int32_t)(intptr_t)lv_event_get_user_data(e);
    shutdownSelection = idx;

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

static void onShutdownOk(lv_event_t *e)
{
    (void)e;
    if (shutdownDlg) { lv_obj_delete(shutdownDlg); shutdownDlg = NULL; }
}

static void onShutdownCancel(lv_event_t *e)
{
    (void)e;
    if (shutdownDlg) { lv_obj_delete(shutdownDlg); shutdownDlg = NULL; }
}

static void showShutdownDialog(void)
{
    if (shutdownDlg) return;

    int32_t dlgW = 260, dlgH = 180;
    int32_t dlgX = (SCREEN_WIDTH - dlgW) / 2;
    int32_t dlgY = (SCREEN_HEIGHT - TASKBAR_HEIGHT - dlgH) / 2;

    shutdownDlg = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(shutdownDlg, dlgX, dlgY);
    lv_obj_set_size(shutdownDlg, dlgW, dlgH);
    lv_obj_set_style_bg_color(shutdownDlg, lv_color_hex(0x222244), 0);
    lv_obj_set_style_bg_opa(shutdownDlg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(shutdownDlg, lv_color_hex(0x444466), 0);
    lv_obj_set_style_border_width(shutdownDlg, 2, 0);
    lv_obj_set_style_radius(shutdownDlg, 6, 0);
    lv_obj_set_style_pad_all(shutdownDlg, 15, 0);
    lv_obj_remove_flag(shutdownDlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(shutdownDlg);
    lv_label_set_text(title, "Shut Down");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *opts = lv_obj_create(shutdownDlg);
    lv_obj_set_size(opts, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(opts, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(opts, 0, 0);
    lv_obj_set_style_pad_all(opts, 0, 0);
    lv_obj_set_style_pad_row(opts, 4, 0);
    lv_obj_set_flex_flow(opts, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(opts, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_remove_flag(opts, LV_OBJ_FLAG_SCROLLABLE);

    static const char *labels[] = { "Power Off", "Suspend", "Restart" };
    shutdownSelection = 0;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *cb = lv_checkbox_create(opts);
        lv_checkbox_set_text(cb, labels[i]);
        lv_obj_set_style_text_color(cb, lv_color_white(), 0);
        if (i == 0) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_add_event_cb(cb, onShutdownOption, LV_EVENT_VALUE_CHANGED,
                            (void *)(intptr_t)i);
    }

    lv_obj_t *btnOk = lv_button_create(shutdownDlg);
    lv_obj_set_size(btnOk, 70, 32);
    lv_obj_align(btnOk, LV_ALIGN_BOTTOM_RIGHT, -78, 0);
    lv_obj_t *lblOk = lv_label_create(btnOk);
    lv_label_set_text(lblOk, "Ok");
    lv_obj_center(lblOk);
    lv_obj_add_event_cb(btnOk, onShutdownOk, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btnCancel = lv_button_create(shutdownDlg);
    lv_obj_set_size(btnCancel, 70, 32);
    lv_obj_align(btnCancel, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *lblCancel = lv_label_create(btnCancel);
    lv_label_set_text(lblCancel, "Cancel");
    lv_obj_center(lblCancel);
    lv_obj_add_event_cb(btnCancel, onShutdownCancel, LV_EVENT_CLICKED, NULL);
}

static void onShutdownClick(lv_event_t *e)
{
    (void)e;
    closeStartMenu();
    showShutdownDialog();
}

/* ------------------------------------------------------------------ */
/*  Start button + menu                                               */
/* ------------------------------------------------------------------ */

static void onStartClick(lv_event_t *e)
{
    (void)e;
    closeInstancePopup();

    if (startMenu) {
        closeStartMenu();
        return;
    }

    /* Transparent overlay catches clicks outside the menu */
    createDismissOverlay();

    startMenu = lv_obj_create(lv_screen_active());
    lv_obj_add_style(startMenu, &styleMenu, 0);
    lv_obj_set_size(startMenu, 160, LV_SIZE_CONTENT);
    lv_obj_remove_flag(startMenu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(startMenu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_pos(startMenu, 4,
                   SCREEN_HEIGHT - TASKBAR_HEIGHT - 4);

    lv_obj_update_layout(startMenu);

    lv_obj_t *miProg = addMenuItem(startMenu,
                                      LV_SYMBOL_DIRECTORY " Programs  "
                                      LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(miProg, onProgramsHover, LV_EVENT_HOVER_OVER, NULL);

    lv_obj_t *miDocs = addMenuItem(startMenu,
                                      LV_SYMBOL_FILE " Documents  "
                                      LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(miDocs, onDocumentsHover, LV_EVENT_HOVER_OVER, NULL);

    lv_obj_t *sep = lv_obj_create(startMenu);
    lv_obj_set_size(sep, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x444466), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);
    lv_obj_remove_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *miShut = addMenuItem(startMenu,
                                      LV_SYMBOL_POWER " Shutdown");
    lv_obj_add_event_cb(miShut, onShutdownHover, LV_EVENT_HOVER_OVER, NULL);
    lv_obj_add_event_cb(miShut, onShutdownClick, LV_EVENT_CLICKED, NULL);

    lv_obj_update_layout(startMenu);
    int32_t menuH = lv_obj_get_height(startMenu);
    lv_obj_set_pos(startMenu, 4,
                   SCREEN_HEIGHT - TASKBAR_HEIGHT - menuH - 4);
}

/* ------------------------------------------------------------------ */
/*  Public entry point                                                */
/* ------------------------------------------------------------------ */

void desktopCreate(lv_group_t *group)
{
    appGroup = group;
    initStyles();
    createDesktopAndTaskbar();

    lv_obj_t *startBtn = lv_button_create(taskbar);
    lv_obj_add_style(startBtn, &styleTaskbarBtn, 0);
    lv_obj_set_height(startBtn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(startBtn, lv_color_hex(0x2266AA), 0);
    lv_obj_set_style_bg_opa(startBtn, LV_OPA_COVER, 0);

    lv_obj_t *startLbl = lv_label_create(startBtn);
    lv_label_set_text(startLbl, LV_SYMBOL_LIST " Start");
    lv_obj_center(startLbl);

    lv_obj_add_event_cb(startBtn, onStartClick, LV_EVENT_CLICKED, NULL);
}
