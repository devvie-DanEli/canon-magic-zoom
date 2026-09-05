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

/* Centered panel — black field, orange accents (matches user mockup). */
#define MEM_BOX_X       60
#define MEM_BOX_Y       100
#define MEM_BOX_W       600
#define MEM_BOX_H       280
#define MEM_CX          360
#define MEM_SLOT_Y      175
#define MEM_STATUS_Y    230
#define MEM_BTN_Y       300
#define MEM_BTN_H       50
#define MEM_BTN_W       160
#define MEM_SAVE_X      140
#define MEM_LOAD_X      420
#define MEM_CHEV_W      36
#define MEM_CHEV_H      48

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
static int mem_ui_slot;
static int mem_ui_confirm;

static int mem_loaded_slot = -1;
static int mem_loaded_iso, mem_loaded_shutter, mem_loaded_aperture;
static int mem_loaded_wb_mode, mem_loaded_kelvin;
static int mem_loaded_crop_mode, mem_loaded_ar, mem_loaded_res;
static int mem_loaded_fps, mem_loaded_bit;

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
        case 2:
            f.valid = &m2_valid; f.iso = &m2_iso; f.shutter = &m2_shutter;
            f.aperture = &m2_aperture; f.wb_mode = &m2_wb_mode; f.kelvin = &m2_kelvin;
            f.crop_mode = &m2_crop_mode; f.ar = &m2_ar; f.res = &m2_res;
            f.fps = &m2_fps; f.bit = &m2_bit;
            break;
        default:
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
    struct mem_fields f;
    if (slot < 0 || slot > 2)
        return 0;
    f = mem_slot_fields(slot);
    return f.valid && *f.valid != 0;
}

static void mem_snapshot_from_slot(int slot)
{
    struct mem_fields f = mem_slot_fields(slot);
    if (!f.valid || !*f.valid)
    {
        mem_loaded_slot = -1;
        return;
    }
    mem_loaded_iso = *f.iso;
    mem_loaded_shutter = *f.shutter;
    mem_loaded_aperture = *f.aperture;
    mem_loaded_wb_mode = *f.wb_mode;
    mem_loaded_kelvin = *f.kelvin;
    mem_loaded_crop_mode = *f.crop_mode;
    mem_loaded_ar = *f.ar;
    mem_loaded_res = *f.res;
    mem_loaded_fps = *f.fps;
    mem_loaded_bit = *f.bit;
}

static int mem_live_matches_loaded(void)
{
    int mode = 0, ar = 0, res = 0, fps = 0, bit = 2;
    if (mem_loaded_slot < 0)
        return 0;
    if (lens_info.raw_iso != mem_loaded_iso) return 0;
    if (lens_info.raw_shutter != mem_loaded_shutter) return 0;
    if (lens_info.raw_aperture != mem_loaded_aperture) return 0;
    if (lens_info.wb_mode != mem_loaded_wb_mode) return 0;
    if (lens_info.wb_mode == WB_KELVIN &&
        lens_info.kelvin != mem_loaded_kelvin) return 0;
    if (!crop_rec_memory_capture)
        return 0;
    if (!crop_rec_memory_capture(&mode, &ar, &res, &fps, &bit))
        return 0;
    if (mode != mem_loaded_crop_mode) return 0;
    if (ar != mem_loaded_ar) return 0;
    if (res != mem_loaded_res) return 0;
    if (fps != mem_loaded_fps) return 0;
    if (bit != mem_loaded_bit) return 0;
    return 1;
}

static const char *mem_slot_status_text(int slot)
{
    if (!mem_recall_slot_has_data(slot))
        return "empty";
    if (slot == mem_loaded_slot && mem_live_matches_loaded())
        return "loaded";
    return "saved";
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
    if (!f.valid)
        return 0;

    /* Capture the complete video state first. Do not modify the destination
     * slot until every part of the capture succeeded. */
    if (!crop_rec_memory_capture ||
        !crop_rec_memory_capture(&mode, &ar, &res, &fps, &bit))
    {
        NotifyBox(2000, "Cannot save %s: Crop Rec unavailable",
                  mem_recall_slot_label(slot));
        return 0;
    }

    *f.iso = lens_info.raw_iso;
    *f.shutter = lens_info.raw_shutter;
    *f.aperture = lens_info.raw_aperture;
    *f.wb_mode = lens_info.wb_mode;
    *f.kelvin = lens_info.kelvin;
    *f.crop_mode = mode;
    *f.ar = ar;
    *f.res = res;
    *f.fps = fps;
    *f.bit = bit;

    *f.valid = 1;
    mem_last_slot = slot;
    mem_loaded_slot = slot;
    mem_snapshot_from_slot(slot);

    /* Persist immediately so each M1/M2/M3 save is independent of the next
     * camera shutdown or a later overwrite. */
    config_save();

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
    if (!f.valid || !*f.valid)
    {
        NotifyBox(1500, "%s empty", mem_recall_slot_label(slot));
        return 0;
    }

    /* Do not partially load a memory if Crop Rec is unavailable or rejects
     * the preset. Exposure and crop state should move together. */
    if (!crop_rec_memory_apply)
    {
        NotifyBox(2000, "Cannot load %s: Crop Rec unavailable",
                  mem_recall_slot_label(slot));
        return 0;
    }

    if (!crop_rec_memory_apply(*f.crop_mode, *f.ar, *f.res, *f.fps, *f.bit))
    {
        NotifyBox(2000, "Cannot load %s: Crop Rec rejected memory",
                  mem_recall_slot_label(slot));
        return 0;
    }

    if (*f.iso)
        lens_set_rawiso(*f.iso);
    if (*f.shutter)
        hdr_set_rawshutter(*f.shutter);
    if (*f.aperture && lens_info.lens_exists)
        lens_set_rawaperture(*f.aperture);

    if (*f.wb_mode == WB_AUTO)
        lens_set_wb_mode(WB_AUTO);
    else if (*f.wb_mode == WB_KELVIN)
        lens_set_kelvin(*f.kelvin ? *f.kelvin : 5500);
    else
        lens_set_wb_mode(*f.wb_mode);

    mem_last_slot = slot;
    mem_loaded_slot = slot;
    mem_snapshot_from_slot(slot);
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

/* Filled chevron: tip_x is the tip. dir < 0 = ◀, dir > 0 = ▶
 * Height grows away from the tip so the point faces the correct way. */
static void mem_draw_chev(int tip_x, int cy, int dir)
{
    for (int i = 0; i < MEM_CHEV_W; i++)
    {
        int half = (MEM_CHEV_H / 2) * (i + 1) / MEM_CHEV_W;
        if (half < 1) half = 1;
        int x = (dir < 0) ? (tip_x + i) : (tip_x - i - 1);
        bmp_fill(COLOR_ORANGE, x, cy - half, 2, half * 2);
    }
}

static void mem_draw_button(int x, int y, int w, int h, const char *label)
{
    bmp_fill(COLOR_ORANGE, x, y, w, h);
    /* Approximate center for MED font (~10 px/char). */
    int tw = 0;
    for (const char *p = label; *p; p++)
        tw += 10;
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_ORANGE),
               x + (w - tw) / 2, y + (h - 18) / 2, "%s", label);
}

void mem_recall_panel_draw(void)
{
    const char *slot_name;
    const char *status;
    int slot_cx = MEM_CX;
    int chev_cy = MEM_SLOT_Y + 16;

    if (!mem_panel_open)
        return;

    bmp_fill(COLOR_BLACK, MEM_BOX_X, MEM_BOX_Y, MEM_BOX_W, MEM_BOX_H);

    if (mem_ui_confirm)
    {
        bmp_printf(FONT(FONT_LARGE, COLOR_WHITE, COLOR_BLACK),
                   slot_cx - 90, MEM_SLOT_Y, "Overwrite?");
        mem_draw_button(MEM_SAVE_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H, "Yes");
        mem_draw_button(MEM_LOAD_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H, "No");
        return;
    }

    slot_name = mem_recall_slot_label(mem_ui_slot);
    status = mem_slot_status_text(mem_ui_slot);

    /* Left / right orange chevrons around centered slot name. */
    /* Left control shows ◀, right shows ▶ (geometry corrected). */
    mem_draw_chev(slot_cx - 100, chev_cy, -1);
    mem_draw_chev(slot_cx + 100, chev_cy, +1);

    /* Slot label — white, large, centered (M1 ~ 2 chars). */
    bmp_printf(FONT(FONT_LARGE, COLOR_WHITE, COLOR_BLACK),
               slot_cx - 28, MEM_SLOT_Y, "%s", slot_name);

    /* Status under slot */
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK),
               slot_cx - 28, MEM_STATUS_Y, "%s", status);

    mem_draw_button(MEM_SAVE_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H, "Save");
    mem_draw_button(MEM_LOAD_X, MEM_BTN_Y, MEM_BTN_W, MEM_BTN_H, "Load");
}

int mem_recall_panel_touch(int x, int y)
{
    int min_y = MEM_BOX_Y;
    int max_y = MEM_BOX_Y + MEM_BOX_H;

    if (!mem_panel_open)
        return 0;
    if (x < MEM_BOX_X || x > MEM_BOX_X + MEM_BOX_W || y < min_y || y > max_y)
        return 1;

    if (mem_ui_confirm)
    {
        if (x >= MEM_SAVE_X && x < MEM_SAVE_X + MEM_BTN_W &&
            y >= MEM_BTN_Y && y < MEM_BTN_Y + MEM_BTN_H)
        {
            mem_recall_save(mem_ui_slot);
            mem_ui_confirm = 0;
            lens_display_set_dirty();
            return 1;
        }
        if (x >= MEM_LOAD_X && x < MEM_LOAD_X + MEM_BTN_W &&
            y >= MEM_BTN_Y && y < MEM_BTN_Y + MEM_BTN_H)
        {
            mem_ui_confirm = 0;
            lens_display_set_dirty();
            return 1;
        }
        return 1;
    }

    if (x >= MEM_SAVE_X && x < MEM_SAVE_X + MEM_BTN_W &&
        y >= MEM_BTN_Y && y < MEM_BTN_Y + MEM_BTN_H)
    {
        if (mem_recall_slot_has_data(mem_ui_slot))
            mem_ui_confirm = 1;
        else
            mem_recall_save(mem_ui_slot);
        lens_display_set_dirty();
        return 1;
    }

    if (x >= MEM_LOAD_X && x < MEM_LOAD_X + MEM_BTN_W &&
        y >= MEM_BTN_Y && y < MEM_BTN_Y + MEM_BTN_H)
    {
        mem_recall_load(mem_ui_slot);
        return 1;
    }

    if (x >= MEM_CX - 145 && x < MEM_CX - 55 &&
        y >= MEM_SLOT_Y - 10 && y < MEM_SLOT_Y + MEM_CHEV_H + 10)
    {
        mem_ui_slot = (mem_ui_slot + MEM_SLOTS - 1) % MEM_SLOTS;
        lens_display_set_dirty();
        return 1;
    }

    if (x > MEM_CX + 55 && x <= MEM_CX + 145 &&
        y >= MEM_SLOT_Y - 10 && y < MEM_SLOT_Y + MEM_CHEV_H + 10)
    {
        mem_ui_slot = (mem_ui_slot + 1) % MEM_SLOTS;
        lens_display_set_dirty();
        return 1;
    }

    return 1;
}
