#include <dryos.h>
#include <module.h>
#include <config.h>
#include <menu.h>
#include <bmp.h>
#include <property.h>
#include <lvinfo.h>
#include <lens.h>
#include <shoot.h>

#ifdef CONFIG_EOSM

/*
 * Photo-mode controls are deliberately kept out of gui-common.c.
 * handle_common_events_by_feature() calls handle_module_keys() before the
 * event reaches Canon, so this module can consume only the Photo-mode touch
 * events while leaving the existing Movie-mode touch router unchanged.
 */

static uint32_t photoctl_touch_word(void *obj, int index)
{
    uint32_t p = (uint32_t)obj;

    if (!p || (p & 0xF0000000))
        return index == 0 ? p : 0;

    return ((volatile uint32_t *)p)[index];
}

static uint32_t photoctl_touch_nested_word(uint32_t p, int index)
{
    if (p < 0x00900000 || p >= 0x00A00000 || (p & 3))
        return 0;
    return ((volatile uint32_t *)p)[index];
}

static int photoctl_touch_get_xy(struct event *event, int *x, int *y)
{
    uint32_t w1 = photoctl_touch_word(event->obj, 1);
    uint32_t packed = photoctl_touch_nested_word(w1, 1);

    *x = packed & 0xFFFF;
    *y = (packed >> 16) & 0xFFFF;
    return *x < 720 && *y < 480;
}

static int photoctl_context_ok(void)
{
    return lv && !is_movie_mode() && !RECORDING && !gui_menu_shown() && lv_dispsize != 10;
}

static int (*dual_iso_is_enabled)() = MODULE_FUNCTION(dual_iso_is_enabled);
static int (*dual_iso_slim_step_recovery)(int) =
    MODULE_FUNCTION(dual_iso_slim_step_recovery);

static void photoctl_refresh_wb_editor(void)
{
    char value[32];
    int awb = (lens_info.wb_mode == WB_AUTO);

    if (awb)
        snprintf(value, sizeof(value), "AWB");
    else if (lens_info.wb_mode == WB_KELVIN)
        snprintf(value, sizeof(value), "%dK", lens_info.kelvin);
    else
        snprintf(value, sizeof(value), "%s",
            lens_info.wb_mode == WB_SUNNY ? "Sunny" :
            lens_info.wb_mode == WB_CLOUDY ? "Cloudy" :
            lens_info.wb_mode == WB_TUNGSTEN ? "Tungsten" :
            lens_info.wb_mode == WB_FLUORESCENT ? "Fluor." :
            lens_info.wb_mode == WB_FLASH ? "Flash" :
            lens_info.wb_mode == WB_CUSTOM ? "Custom" :
            lens_info.wb_mode == WB_SHADE ? "Shade" : "WB");

    lvinfo_touch_editor_set_item(0, value, !awb);
    lvinfo_touch_editor_set_item(1, awb ? "AWB ON" : "AWB OFF", 1);
}

static void photoctl_change_field(enum lvinfo_touch_field field, int slot, int sign)
{
    if (!lvinfo_touch_editor_item_enabled(slot))
        return;

    switch (field)
    {
        case LVINFO_TOUCH_APERTURE:
            if (lens_info.lens_exists && lens_info.raw_aperture)
                aperture_toggle((void *)-1, sign);
            break;

        case LVINFO_TOUCH_SHUTTER:
            if (lens_info.raw_shutter)
                shutter_toggle((void *)-1, sign);
            break;

        case LVINFO_TOUCH_ISO:
            if (dual_iso_is_enabled && dual_iso_is_enabled())
            {
                if (dual_iso_slim_step_recovery)
                    dual_iso_slim_step_recovery(sign > 0 ? 1 : -1);
            }
            else
            {
                iso_toggle((void *)-1, sign);
            }
            break;

        case LVINFO_TOUCH_WB:
            if (slot == 1)
            {
                if (sign > 0)
                    lens_set_wb_mode(WB_AUTO);
                else if (lens_info.wb_mode == WB_AUTO)
                    lens_set_kelvin(5500);
            }
            else if (lens_info.wb_mode != WB_AUTO)
            {
                kelvin_toggle((void *)-1, sign);
            }
            photoctl_refresh_wb_editor();
            break;

        default:
            break;
    }

    lens_display_set_dirty();
}

static int photoctl_handle_touch(unsigned int ctx)
{
    struct event *event = (struct event *)ctx;
    int x, y;

    if (!event || !photoctl_context_ok())
        return 1;

    switch (event->param)
    {
        case BGMT_TOUCH_1_FINGER:
            if (!photoctl_touch_get_xy(event, &x, &y))
                return 1;

            if (lvinfo_touch_editor_is_open())
            {
                int slot = -1;
                int sign = 0;

                if (!lvinfo_touch_editor_hit_test(x, y, &slot, &sign))
                {
                    lvinfo_touch_editor_close();
                    return 0;
                }

                if (sign && slot >= 0)
                    photoctl_change_field(lvinfo_touch_editor_field(), slot, sign);

                return 0;
            }

            {
                enum lvinfo_touch_field field = lvinfo_touch_field_at(x, y);
                if (field == LVINFO_TOUCH_NONE)
                    return 1;

                /* Photo mode deliberately exposes only exposure/WB controls.
                 * Crop/FPS/bit-depth/video-memory fields remain movie-only. */
                if (field != LVINFO_TOUCH_APERTURE &&
                    field != LVINFO_TOUCH_SHUTTER &&
                    field != LVINFO_TOUCH_ISO &&
                    field != LVINFO_TOUCH_WB)
                    return 1;

                if ((field == LVINFO_TOUCH_APERTURE &&
                     (!lens_info.lens_exists || !lens_info.raw_aperture)) ||
                    (field == LVINFO_TOUCH_SHUTTER && !lens_info.raw_shutter))
                    return 0;

                lvinfo_touch_editor_open(field);
                if (field == LVINFO_TOUCH_WB)
                    photoctl_refresh_wb_editor();
                return 0;
            }

        case BGMT_UNTOUCH_1_FINGER:
            if (lvinfo_touch_editor_is_open())
                return 0;
            return 1;

        case BGMT_TOUCH_2_FINGER:
        case BGMT_UNTOUCH_2_FINGER:
            if (lvinfo_touch_editor_is_open())
                return 0;
            return 1;

        default:
            return 1;
    }
}

/* ------------------------- Mode-separated settings -------------------- */

struct exposure_profile
{
    int valid;
    int iso;
    int shutter;
    int aperture;
    int wb_mode;
    int kelvin;
};

static struct exposure_profile photo_profile;
static struct exposure_profile video_profile;
static int active_mode = -1;
static int restore_pending = 0;
static int restore_mode = -1;

static int photoctl_profile_capture(struct exposure_profile *profile)
{
    if (!profile || !lens_info.lens_exists)
        return 0;

    if (!lens_info.raw_iso && !lens_info.raw_iso_auto)
        return 0;

    profile->iso = lens_info.raw_iso;
    profile->shutter = lens_info.raw_shutter;
    profile->aperture = lens_info.raw_aperture;
    profile->wb_mode = lens_info.wb_mode;
    profile->kelvin = lens_info.kelvin;
    profile->valid = 1;
    return 1;
}

static void photoctl_profile_apply_task(int timer, void *opaque)
{
    struct exposure_profile *profile;
    int movie;

    (void)timer;
    (void)opaque;

    if (!restore_pending)
        return;

    movie = is_movie_mode();
    if (movie != restore_mode)
    {
        delayed_call(100, photoctl_profile_apply_task, 0);
        return;
    }

    if (!lv || RECORDING || gui_menu_shown())
    {
        delayed_call(100, photoctl_profile_apply_task, 0);
        return;
    }

    profile = movie ? &video_profile : &photo_profile;
    if (!profile->valid)
    {
        photoctl_profile_capture(profile);
        restore_pending = 0;
        return;
    }

    if (profile->iso || profile->shutter || profile->aperture)
    {
        if (profile->iso)
            lens_set_rawiso(profile->iso);
        if (profile->shutter)
            lens_set_rawshutter(profile->shutter);
        if (profile->aperture && lens_info.lens_exists)
            lens_set_rawaperture(profile->aperture);

        if (profile->wb_mode == WB_AUTO)
            lens_set_wb_mode(WB_AUTO);
        else if (profile->wb_mode == WB_KELVIN)
            lens_set_kelvin(profile->kelvin ? profile->kelvin : 5500);
        else
            lens_set_wb_mode(profile->wb_mode);
    }

    restore_pending = 0;
    lens_display_set_dirty();
}

static unsigned int photoctl_profile_cbr(unsigned int ctx)
{
    int movie;
    struct exposure_profile *current;

    (void)ctx;

    if (!lv)
        return 0;

    movie = is_movie_mode() ? 1 : 0;
    current = movie ? &video_profile : &photo_profile;

    if (active_mode < 0)
    {
        active_mode = movie;
        photoctl_profile_capture(current);
        return 0;
    }

    if (movie != active_mode)
    {
        /* Freeze the profile of the mode being left before Canon's new mode
         * has had time to replace the exposure properties. */
        if (!restore_pending)
        {
            struct exposure_profile *old = active_mode ? &video_profile : &photo_profile;
            photoctl_profile_capture(old);
        }

        active_mode = movie;

        if (current->valid)
        {
            restore_mode = movie;
            restore_pending = 1;
            delayed_call(250, photoctl_profile_apply_task, 0);
        }
        else
        {
            photoctl_profile_capture(current);
        }

        return 0;
    }

    if (!restore_pending)
        photoctl_profile_capture(current);

    return 0;
}

/* ----------------------------- P1/P2/P3 ------------------------------- */

static CONFIG_INT("photoctl.p1.valid", p1_valid, 0);
static CONFIG_INT("photoctl.p1.iso", p1_iso, 0);
static CONFIG_INT("photoctl.p1.shutter", p1_shutter, 0);
static CONFIG_INT("photoctl.p1.aperture", p1_aperture, 0);
static CONFIG_INT("photoctl.p1.wb_mode", p1_wb_mode, WB_AUTO);
static CONFIG_INT("photoctl.p1.kelvin", p1_kelvin, 5500);

static CONFIG_INT("photoctl.p2.valid", p2_valid, 0);
static CONFIG_INT("photoctl.p2.iso", p2_iso, 0);
static CONFIG_INT("photoctl.p2.shutter", p2_shutter, 0);
static CONFIG_INT("photoctl.p2.aperture", p2_aperture, 0);
static CONFIG_INT("photoctl.p2.wb_mode", p2_wb_mode, WB_AUTO);
static CONFIG_INT("photoctl.p2.kelvin", p2_kelvin, 5500);

static CONFIG_INT("photoctl.p3.valid", p3_valid, 0);
static CONFIG_INT("photoctl.p3.iso", p3_iso, 0);
static CONFIG_INT("photoctl.p3.shutter", p3_shutter, 0);
static CONFIG_INT("photoctl.p3.aperture", p3_aperture, 0);
static CONFIG_INT("photoctl.p3.wb_mode", p3_wb_mode, WB_AUTO);
static CONFIG_INT("photoctl.p3.kelvin", p3_kelvin, 5500);

static void photoctl_save_slot(int slot)
{
    struct exposure_profile p;
    if (!photoctl_context_ok())
        return;
    if (!photoctl_profile_capture(&p))
        return;

    switch (slot)
    {
        case 0: p1_valid=1; p1_iso=p.iso; p1_shutter=p.shutter; p1_aperture=p.aperture; p1_wb_mode=p.wb_mode; p1_kelvin=p.kelvin; break;
        case 1: p2_valid=1; p2_iso=p.iso; p2_shutter=p.shutter; p2_aperture=p.aperture; p2_wb_mode=p.wb_mode; p2_kelvin=p.kelvin; break;
        default:p3_valid=1; p3_iso=p.iso; p3_shutter=p.shutter; p3_aperture=p.aperture; p3_wb_mode=p.wb_mode; p3_kelvin=p.kelvin; break;
    }

    NotifyBox(1200, "Saved P%d", slot + 1);
}

static void photoctl_load_slot(int slot)
{
    int valid, iso, shutter, aperture, wb_mode, kelvin;

    if (!photoctl_context_ok())
        return;

    switch (slot)
    {
        case 0: valid=p1_valid; iso=p1_iso; shutter=p1_shutter; aperture=p1_aperture; wb_mode=p1_wb_mode; kelvin=p1_kelvin; break;
        case 1: valid=p2_valid; iso=p2_iso; shutter=p2_shutter; aperture=p2_aperture; wb_mode=p2_wb_mode; kelvin=p2_kelvin; break;
        default:valid=p3_valid; iso=p3_iso; shutter=p3_shutter; aperture=p3_aperture; wb_mode=p3_wb_mode; kelvin=p3_kelvin; break;
    }

    if (!valid)
    {
        NotifyBox(1200, "P%d empty", slot + 1);
        return;
    }

    if (iso)
        lens_set_rawiso(iso);
    if (shutter)
        lens_set_rawshutter(shutter);
    if (aperture && lens_info.lens_exists)
        lens_set_rawaperture(aperture);

    if (wb_mode == WB_AUTO)
        lens_set_wb_mode(WB_AUTO);
    else if (wb_mode == WB_KELVIN)
        lens_set_kelvin(kelvin ? kelvin : 5500);
    else
        lens_set_wb_mode(wb_mode);

    photoctl_profile_capture(&photo_profile);
    lens_display_set_dirty();
    NotifyBox(1200, "Loaded P%d", slot + 1);
}

static MENU_SELECT_FUNC(photoctl_memory_select)
{
    int op = (int)(intptr_t)priv;
    int slot = op / 2;
    int save = !(op & 1);

    (void)delta;

    if (save)
        photoctl_save_slot(slot);
    else
        photoctl_load_slot(slot);
}

static MENU_UPDATE_FUNC(photoctl_memory_status)
{
    int op = (int)(intptr_t)entry->priv;
    int slot = op / 2;
    int valid = slot == 0 ? p1_valid : slot == 1 ? p2_valid : p3_valid;
    MENU_SET_VALUE(valid ? "saved" : "empty");
}

static struct menu_entry photoctl_menu[] =
{
    {
        .name = "P1 Save",
        .priv = (void *)(intptr_t)0,
        .select = photoctl_memory_select,
        .icon_type = IT_ACTION,
        .update = photoctl_memory_status,
    },
    {
        .name = "P1 Load",
        .priv = (void *)(intptr_t)1,
        .select = photoctl_memory_select,
        .icon_type = IT_ACTION,
        .update = photoctl_memory_status,
    },
    {
        .name = "P2 Save",
        .priv = (void *)(intptr_t)2,
        .select = photoctl_memory_select,
        .icon_type = IT_ACTION,
        .update = photoctl_memory_status,
    },
    {
        .name = "P2 Load",
        .priv = (void *)(intptr_t)3,
        .select = photoctl_memory_select,
        .icon_type = IT_ACTION,
        .update = photoctl_memory_status,
    },
    {
        .name = "P3 Save",
        .priv = (void *)(intptr_t)4,
        .select = photoctl_memory_select,
        .icon_type = IT_ACTION,
        .update = photoctl_memory_status,
    },
    {
        .name = "P3 Load",
        .priv = (void *)(intptr_t)5,
        .select = photoctl_memory_select,
        .icon_type = IT_ACTION,
        .update = photoctl_memory_status,
    },
};

static unsigned int photoctl_init(void)
{
    menu_add("Photo", photoctl_menu, COUNT(photoctl_menu));
    return 0;
}

static unsigned int photoctl_deinit(void)
{
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(photoctl_init)
    MODULE_DEINIT(photoctl_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_KEYPRESS_RAW, photoctl_handle_touch, 0)
    MODULE_CBR(CBR_VSYNC, photoctl_profile_cbr, 0)
MODULE_CBRS_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(p1_valid)
    MODULE_CONFIG(p1_iso)
    MODULE_CONFIG(p1_shutter)
    MODULE_CONFIG(p1_aperture)
    MODULE_CONFIG(p1_wb_mode)
    MODULE_CONFIG(p1_kelvin)
    MODULE_CONFIG(p2_valid)
    MODULE_CONFIG(p2_iso)
    MODULE_CONFIG(p2_shutter)
    MODULE_CONFIG(p2_aperture)
    MODULE_CONFIG(p2_wb_mode)
    MODULE_CONFIG(p2_kelvin)
    MODULE_CONFIG(p3_valid)
    MODULE_CONFIG(p3_iso)
    MODULE_CONFIG(p3_shutter)
    MODULE_CONFIG(p3_aperture)
    MODULE_CONFIG(p3_wb_mode)
    MODULE_CONFIG(p3_kelvin)
MODULE_CONFIGS_END()

#endif /* CONFIG_EOSM */
