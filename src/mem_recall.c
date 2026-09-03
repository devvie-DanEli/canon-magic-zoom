#include <dryos.h>
#include <property.h>
#include <propvalues.h>
#include <lens.h>
#include <config.h>
#include <bmp.h>
#include <lvinfo.h>
#include <module.h>
#include <notify_box.h>
#include <mem_recall.h>

#define MEM_SLOTS 3

/* Panel layout (same vertical band as other LV touch editors). */
#define MEM_BOX_X       80
#define MEM_BOX_Y       120
#define MEM_BOX_W       560
#define MEM_BOX_H       240
#define MEM_SLOT_CX     360
#define MEM_SLOT_Y      150
#define MEM_UP_Y        (MEM_SLOT_Y - 10)
#define MEM_DOWN_Y      (MEM_SLOT_Y + 70)
#define MEM_BTN_Y       280
#define MEM_BTN_H       56
#define MEM_SAVE_X      120
#define MEM_LOAD_X      380
#define MEM_BTN_W       200

static CONFIG_INT("mem.m0.valid", m0_valid, 0);
static CONFIG_INT("mem.m0.iso", m0_iso, 0);
static CONFIG_INT("mem.m0.shutter", m0_shutter, 0);
static CONFIG_INT("mem.m0.aperture", m0_aperture, 0);
static CONFIG_INT("mem.m0.wb_mode", m0_wb_mode, 9);
static CONFIG_INT("mem.m0.kelvin", m0_kelvin, 5500);
static CONFIG_INT("mem.m0.crop_mode", m0_crop_mode, 0);
static CONFIG_INT("mem.m0.ar", m0_ar, 0);
static CONFIG_INT("mem.m0.res", m0_res, 0);
static CONFIG_INT("mem.m0.fps", m0_fps, 0);
static CONFIG_INT("mem.m0.bit", m0_bit, 2);

static CONFIG_INT("mem.m1.valid", m1_valid, 0);
static CONFIG_INT("mem.m1.iso", m1_iso, 0);
static CONFIG_INT("mem.m1.shutter", m1_shutter, 0);
static CONFIG_INT("mem.m1.aperture", m1_aperture, 0);
static CONFIG_INT("mem.m1.wb_mode", m1_wb_mode, 9);
static CONFIG_INT("mem.m1.kelvin", m1_kelvin, 5500);
static CONFIG_INT("mem.m1.crop_mode", m1_crop_mode, 0);
static CONFIG_INT("mem.m1.ar", m1_ar, 0);
static CONFIG_INT("mem.m1.res", m1_res, 0);
static CONFIG_INT("mem.m1.fps", m1_fps, 0);
static CONFIG_INT("mem.m1.bit", m1_bit, 2);

static CONFIG_INT("mem.m2.valid", m2_valid, 0);
static CONFIG_INT("mem.m2.iso", m2_iso, 0);
static CONFIG_INT("mem.m2.shutter", m2_shutter, 0);
static CONFIG_INT("mem.m2.aperture", m2_aperture, 0);
static CONFIG_INT("mem.m2.wb_mode", m2_wb_mode, 9);
static CONFIG_INT("mem.m2.kelvin", m2_kelvin, 5500);
static CONFIG_INT("mem.m2.crop_mode", m2_crop_mode, 0);
static CONFIG_INT("mem.m2.ar", m2_ar, 0);
static CONFIG_INT("mem.m2.res", m2_res, 0);
static CONFIG_INT("mem.m2.fps", m2_fps, 0);
static CONFIG_INT("mem.m2.bit", m2_bit, 2);

static CONFIG_INT("mem.last_slot", mem_last_slot, -1);

static int (*crop_rec_memory_capture)(int *, int *, int *, int *, int *) =
    MODULE_FUNCTION(crop_rec_memory_capture);
static int (*crop_rec_memory_apply)(int, int, int, int, int) =
    MODULE_FUNCTION(crop_rec_memory_apply);

static int mem_panel_open;
static int mem_ui_slot;          /* 0..2 */
static int mem_ui_confirm;       /* 0 = main, 1 = overwrite? */

struct mem_fields {
    int *valid, *iso, *shutter, *aperture, *wb_mode, *kelvin;
    int *crop_mode, *ar, *res, *fps, *bit;
};

static struct mem_fields mem_slot_fields(int slot)
{
    struct mem_fields f = {0};
    switch (slot)
    {
        case 0:
            f.valid = &m0_valid; f.iso = &m0_iso; f.shutter = &m0_shutter;
            f.aperture = &m0_aperture; f.wb_mode = &m0_wb_mode; f.kelvin = &m0_kelvin;
            f.crop_mode = &m0_crop_mode; f.ar = &m0_ar; f.res = &m0_res;
            f.fps = &m0_fps; f.bit = &m0_bit;
            break;
        case 1:
            f.valid = &m1_valid; f.iso = &m1_iso; f.shutter = &m1_shutter;
            f.aperture = &m1_aperture; f.wb_mode = &m1_wb_mode; f.kelvin = &m1_kelvin;
            f.crop_mode = &m1_crop_mode; f.ar = &m1_ar; f.res = &m1_res;
            f.fps = &m1_fps; f.bit = &m1_bit;
            break;
        default:
            f.valid = &m2_valid; f.iso = &m2_iso; f.shutter = &m2_shutter;
            f.aperture = &m2_aperture; f.wb_mode = &m2_wb_mode; f.kelvin = &m2_kelvin;
            f.crop_mode = &m2_crop_mode; f.ar = &m2_ar; f.res = &m2_res;
            f.fps = &m2_fps; f.bit = &m2_bit;
            break;
    }
    return f;
}

int mem_recall_is_available(void)
{
    return is_movie_mode() && !RECORDING;
}

int mem_recall_current_slot(void)
{
    if (mem_last_slot < 0 || mem_last_slot > 2)
        return -1;
    return mem_last_slot;
}

const char *mem_recall_slot_label(int slot)
{
    static const char *labels[] = { "M1", "M2", "M3" };
    if (slot < 0 || slot > 2)
        return "Mem";
    return labels[slot];
}

int mem_recall_slot_has_data(int slot)
{
    if (slot < 0 || slot > 2)
        return 0;
    return *mem_slot_fields(slot).valid != 0;
}

int mem_recall_save(int slot)
{
    int mode, ar, res, fps, bit;
    struct mem_fields f;

    if (!mem_recall_is_available())
        return 0;
    if (slot < 0 || slot > 2)
        return 0;

    f = mem_slot_fields(slot);
    *f.iso = lens_info.raw_iso;
    *f.shutter = lens_info.raw_shutter;
    *f.aperture = lens_info.raw_aperture;
    *f.wb_mode = lens_info.wb_mode;
    *f.kelvin = lens_info.kelvin;

    if (crop_rec_memory_capture &&
        crop_rec_memory_capture(&mode, &ar, &res, &fps, &bit))
    {
        *f.crop_mode = mode;
        *f.ar = ar;
        *f.res = res;
        *f.fps = fps;
        *f.bit = bit;
    }
    else
    {
        *f.crop_mode = 0;
        *f.ar = 0;
        *f.res = 0;
        *f.fps = 0;
        *f.bit = 2;
    }

    *f.valid = 1;
    mem_last_slot = slot;
    NotifyBox(1500, "Saved %s", mem_recall_slot_label(slot));
    return 1;
}

int mem_recall_load(int slot)
{
    struct mem_fields f;

    if (!mem_recall_is_available())
        return 0;
    if (slot < 0 || slot > 2)
        return 0;

    f = mem_slot_fields(slot);
    if (!*f.valid)
    {
        NotifyBox(1500, "%s empty", mem_recall_slot_label(slot));
        return 0;
    }

    if (*f.iso)
        lens_set_rawiso(*f.iso);
    if (*f.shutter)
        lens_set_rawshutter(*f.shutter);
    if (*f.aperture && lens_info.lens_exists)
        lens_set_rawaperture(*f.aperture);

    if (*f.wb_mode == WB_AUTO)
        lens_set_wb_mode(WB_AUTO);
    else if (*f.wb_mode == WB_KELVIN || *f.kelvin)
        lens_set_kelvin(*f.kelvin ? *f.kelvin : 5500);
    else
        lens_set_wb_mode(*f.wb_mode);

    if (crop_rec_memory_apply)
        crop_rec_memory_apply(*f.crop_mode, *f.ar, *f.res, *f.fps, *f.bit);

    mem_last_slot = slot;
    NotifyBox(1500, "Loaded %s", mem_recall_slot_label(slot));
    lens_display_set_dirty();
    return 1;
}

void mem_recall_panel_open(void)
{
    mem_panel_open = 1;
    mem_ui_confirm = 0;
    if (mem_last_slot >= 0 && mem_last_slot <= 2)
        mem_ui_slot = mem_last_slot;
    else
        mem_ui_slot = 0;
    lens_display_set_dirty();
}

void mem_recall_panel_close(void)
{
    if (!mem_panel_open)
        return;
    mem_panel_open = 0;
    mem_ui_confirm = 0;
    bmp_fill(COLOR_EMPTY, MEM_BOX_X - 4, MEM_BOX_Y - 4,
             MEM_BOX_W + 8, MEM_BOX_H + 8);
    lens_display_set_dirty();
}

int mem_recall_panel_is_open(void)
{
    return mem_panel_open;
}

static void mem_draw_arrow_up(int cx, int cy, int color)
{
    for (int i = 0; i < 14; i++)
        bmp_draw_rect(color, cx - i, cy + i, 2 * i + 1, 2);
}

static void mem_draw_arrow_down(int cx, int cy, int color)
{
    for (int i = 0; i < 14; i++)
        bmp_draw_rect(color, cx - i, cy - i, 2 * i + 1, 2);
}

static void mem_draw_button(int x, int y, int w, int h, const char *label)
{
    bmp_fill(COLOR_GRAY(20), x, y, w, h);
    bmp_draw_rect(COLOR_WHITE, x, y, w, h);
    int tw = strlen(label) * 10;
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK),
               x + (w - tw) / 2, y + (h - 18) / 2, "%s", label);
}

void mem_recall_panel_draw(void)
{
    char label[16];
    int has;

    if (!mem_panel_open)
        return;

    bmp_fill(COLOR_BLACK, MEM_BOX_X, MEM_BOX_Y, MEM_BOX_W, MEM_BOX_H);

    if (mem_ui_confirm)
    {
        bmp_printf(FONT(FONT_LARGE, COLOR_WHITE, COLOR_BLACK),
                   MEM_SLOT_CX - 70, MEM_SLOT_Y + 10, "Overwrite?");
        mem_draw_button(MEM_SAVE_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H, "Yes");
        mem_draw_button(MEM_LOAD_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H, "No");
        return;
    }

    /* Slot navigator */
    has = mem_recall_slot_has_data(mem_ui_slot);
    snprintf(label, sizeof(label), "%s%s%s",
             mem_recall_slot_label(mem_ui_slot),
             has ? "" : "-",
             (mem_last_slot == mem_ui_slot) ? "*" : "");

    bmp_printf(FONT(FONT_LARGE, COLOR_GRAY(50), COLOR_BLACK),
               MEM_SLOT_CX - 10, MEM_UP_Y + 8, "^");
    bmp_printf(FONT(FONT_LARGE, COLOR_WHITE, COLOR_BLACK),
               MEM_SLOT_CX - 28, MEM_SLOT_Y + 28, "%s", label);
    bmp_printf(FONT(FONT_LARGE, COLOR_GRAY(50), COLOR_BLACK),
               MEM_SLOT_CX - 10, MEM_DOWN_Y + 8, "v");

    /* Hint under slot */
    bmp_printf(FONT(FONT_SMALL, COLOR_GRAY(40), COLOR_BLACK),
               MEM_SLOT_CX - 50, MEM_DOWN_Y + 50, has ? "has data" : "empty");

    mem_draw_button(MEM_SAVE_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H, "Save");
    mem_draw_button(MEM_LOAD_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H, "Load");
}

static int mem_in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

int mem_recall_panel_touch(int x, int y)
{
    if (!mem_panel_open)
        return 0;

    /* Outside panel → close */
    if (!mem_in_rect(x, y, MEM_BOX_X, MEM_BOX_Y, MEM_BOX_W, MEM_BOX_H))
    {
        mem_recall_panel_close();
        return 1;
    }

    if (mem_ui_confirm)
    {
        if (mem_in_rect(x, y, MEM_SAVE_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H))
        {
            /* Yes → overwrite */
            mem_ui_confirm = 0;
            mem_recall_save(mem_ui_slot);
            lens_display_set_dirty();
            return 1;
        }
        if (mem_in_rect(x, y, MEM_LOAD_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H))
        {
            /* No → cancel */
            mem_ui_confirm = 0;
            lens_display_set_dirty();
            return 1;
        }
        return 1; /* absorb taps inside dialog */
    }

    /* Up arrow region: previous slot */
    if (mem_in_rect(x, y, MEM_SLOT_CX - 60, MEM_BOX_Y, 120, 70))
    {
        mem_ui_slot = MOD(mem_ui_slot - 1, MEM_SLOTS);
        lens_display_set_dirty();
        return 1;
    }
    /* Down arrow region: next slot */
    if (mem_in_rect(x, y, MEM_SLOT_CX - 60, MEM_DOWN_Y, 120, 60))
    {
        mem_ui_slot = MOD(mem_ui_slot + 1, MEM_SLOTS);
        lens_display_set_dirty();
        return 1;
    }

    /* Save */
    if (mem_in_rect(x, y, MEM_SAVE_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H))
    {
        if (mem_recall_slot_has_data(mem_ui_slot))
        {
            mem_ui_confirm = 1;
            lens_display_set_dirty();
        }
        else
        {
            mem_recall_save(mem_ui_slot);
            lens_display_set_dirty();
        }
        return 1;
    }

    /* Load */
    if (mem_in_rect(x, y, MEM_LOAD_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H))
    {
        mem_recall_load(mem_ui_slot);
        lens_display_set_dirty();
        return 1;
    }

    return 1;
}
