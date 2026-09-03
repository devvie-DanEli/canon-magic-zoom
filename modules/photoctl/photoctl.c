#include <dryos.h>
#include <module.h>
#include <config.h>
#include <menu.h>
#include <property.h>
#include <lens.h>
#include <shoot.h>
#include <stdint.h>

#ifdef CONFIG_EOSM

/*
 * Photo Live View touch control is implemented by the core EOS M/gui-common
 * path in the build workflow. Keep this symbol and CBR entry because the
 * workflow removes the old module-level touch callback before compiling.
 */
static unsigned int photoctl_handle_touch(unsigned int ctx)
{
    (void)ctx;
    return 1;
}

/* ----------------------- Automatic mode memory ------------------------- */

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

/* Persist the last-used profile for each camera mode. */
static CONFIG_INT("photoctl.last_photo.valid", photo_last_valid, 0);
static CONFIG_INT("photoctl.last_photo.iso", photo_last_iso, 0);
static CONFIG_INT("photoctl.last_photo.shutter", photo_last_shutter, 0);
static CONFIG_INT("photoctl.last_photo.aperture", photo_last_aperture, 0);
static CONFIG_INT("photoctl.last_photo.wb_mode", photo_last_wb_mode, WB_AUTO);
static CONFIG_INT("photoctl.last_photo.kelvin", photo_last_kelvin, 5500);

static CONFIG_INT("photoctl.last_video.valid", video_last_valid, 0);
static CONFIG_INT("photoctl.last_video.iso", video_last_iso, 0);
static CONFIG_INT("photoctl.last_video.shutter", video_last_shutter, 0);
static CONFIG_INT("photoctl.last_video.aperture", video_last_aperture, 0);
static CONFIG_INT("photoctl.last_video.wb_mode", video_last_wb_mode, WB_AUTO);
static CONFIG_INT("photoctl.last_video.kelvin", video_last_kelvin, 5500);

static int active_mode = -1;
static int restore_pending = 0;
static int restore_mode = -1;
static int restore_attempts = 0;

static void photoctl_profile_sync_from_config(int movie, struct exposure_profile *profile)
{
    if (!profile)
        return;

    if (movie)
    {
        profile->valid = video_last_valid;
        profile->iso = video_last_iso;
        profile->shutter = video_last_shutter;
        profile->aperture = video_last_aperture;
        profile->wb_mode = video_last_wb_mode;
        profile->kelvin = video_last_kelvin;
    }
    else
    {
        profile->valid = photo_last_valid;
        profile->iso = photo_last_iso;
        profile->shutter = photo_last_shutter;
        profile->aperture = photo_last_aperture;
        profile->wb_mode = photo_last_wb_mode;
        profile->kelvin = photo_last_kelvin;
    }
}

static void photoctl_profile_sync_to_config(int movie, const struct exposure_profile *profile)
{
    if (!profile)
        return;

    if (movie)
    {
        if (video_last_valid != profile->valid) video_last_valid = profile->valid;
        if (video_last_iso != profile->iso) video_last_iso = profile->iso;
        if (video_last_shutter != profile->shutter) video_last_shutter = profile->shutter;
        if (video_last_aperture != profile->aperture) video_last_aperture = profile->aperture;
        if (video_last_wb_mode != profile->wb_mode) video_last_wb_mode = profile->wb_mode;
        if (video_last_kelvin != profile->kelvin) video_last_kelvin = profile->kelvin;
    }
    else
    {
        if (photo_last_valid != profile->valid) photo_last_valid = profile->valid;
        if (photo_last_iso != profile->iso) photo_last_iso = profile->iso;
        if (photo_last_shutter != profile->shutter) photo_last_shutter = profile->shutter;
        if (photo_last_aperture != profile->aperture) photo_last_aperture = profile->aperture;
        if (photo_last_wb_mode != profile->wb_mode) photo_last_wb_mode = profile->wb_mode;
        if (photo_last_kelvin != profile->kelvin) photo_last_kelvin = profile->kelvin;
    }
}

static int photoctl_profile_capture(int movie, struct exposure_profile *profile)
{
    if (!profile || !lens_info.lens_exists)
        return 0;

    if (!lens_info.raw_iso && !lens_info.raw_iso_auto &&
        !lens_info.raw_shutter && !lens_info.raw_aperture)
        return 0;

    profile->iso = lens_info.raw_iso;
    profile->shutter = lens_info.raw_shutter;
    profile->aperture = lens_info.raw_aperture;
    profile->wb_mode = lens_info.wb_mode;
    profile->kelvin = lens_info.kelvin;
    profile->valid = 1;

    photoctl_profile_sync_to_config(movie, profile);
    return 1;
}

static int photoctl_profile_matches(const struct exposure_profile *profile)
{
    if (!profile || !profile->valid)
        return 0;

    if (profile->iso && lens_info.raw_iso != profile->iso)
        return 0;
    if (profile->shutter && lens_info.raw_shutter != profile->shutter)
        return 0;
    if (profile->aperture && lens_info.raw_aperture != profile->aperture)
        return 0;
    if (lens_info.wb_mode != profile->wb_mode)
        return 0;
    if (profile->wb_mode == WB_KELVIN &&
        profile->kelvin && lens_info.kelvin != profile->kelvin)
        return 0;

    return 1;
}

static void photoctl_profile_apply(const struct exposure_profile *profile)
{
    if (!profile || !profile->valid)
        return;

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

    lens_display_set_dirty();
}

static void photoctl_schedule_restore(int movie)
{
    struct exposure_profile *profile = movie ? &video_profile : &photo_profile;

    if (!profile->valid)
        photoctl_profile_sync_from_config(movie, profile);

    if (!profile->valid)
    {
        restore_pending = 0;
        photoctl_profile_capture(movie, profile);
        return;
    }

    restore_mode = movie;
    restore_attempts = 0;
    restore_pending = 1;
    delayed_call(350, photoctl_profile_apply_task, 0);
}

static void photoctl_profile_apply_task(int timer, void *opaque)
{
    int movie;
    struct exposure_profile *profile;

    (void)timer;
    (void)opaque;

    if (!restore_pending)
        return;

    movie = is_movie_mode() ? 1 : 0;
    if (movie != restore_mode)
    {
        restore_pending = 0;
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
        restore_pending = 0;
        return;
    }

    photoctl_profile_apply(profile);

    if (photoctl_profile_matches(profile) || restore_attempts++ >= 12)
    {
        restore_pending = 0;
        photoctl_profile_sync_to_config(movie, profile);
        return;
    }

    delayed_call(120, photoctl_profile_apply_task, 0);
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
        photoctl_profile_sync_from_config(movie, current);

        if (current->valid)
        {
            restore_mode = movie;
            restore_pending = 1;
            restore_attempts = 0;
            delayed_call(350, photoctl_profile_apply_task, 0);
        }
        else
        {
            photoctl_profile_capture(movie, current);
        }
        return 0;
    }

    if (movie != active_mode)
    {
        active_mode = movie;
        photoctl_profile_sync_from_config(movie, current);

        if (current->valid)
        {
            restore_mode = movie;
            restore_pending = 1;
            restore_attempts = 0;
            delayed_call(350, photoctl_profile_apply_task, 0);
        }
        else
        {
            photoctl_profile_capture(movie, current);
        }
        return 0;
    }

    if (restore_pending)
        return 0;

    photoctl_profile_capture(movie, current);
    return 0;
}

/* ----------------------------- P1/P2/P3 ------------------------------- */

static CONFIG_INT("photoctl.p1.valid", p1_valid, 0);
static CONFIG_INT("photoctl.p1.iso", p1_iso, 0);
static CONFIG_INT("photoctl.p1.shutter", p1_shutter, 0);
static CONFIG_INT("photoctl.p1.aperture", p1_aperture, 0);

static CONFIG_INT("photoctl.p2.valid", p2_valid, 0);
static CONFIG_INT("photoctl.p2.iso", p2_iso, 0);
static CONFIG_INT("photoctl.p2.shutter", p2_shutter, 0);
static CONFIG_INT("photoctl.p2.aperture", p2_aperture, 0);

static CONFIG_INT("photoctl.p3.valid", p3_valid, 0);
static CONFIG_INT("photoctl.p3.iso", p3_iso, 0);
static CONFIG_INT("photoctl.p3.shutter", p3_shutter, 0);
static CONFIG_INT("photoctl.p3.aperture", p3_aperture, 0);

static int photoctl_settings_ok(void)
{
    return lv && !is_movie_mode() && !RECORDING && lv_dispsize != 10;
}

static void photoctl_save_slot(int slot)
{
    struct exposure_profile p;

    if (!photoctl_settings_ok())
        return;
    if (!photoctl_profile_capture(0, &p))
        return;

    switch (slot)
    {
        case 0:
            p1_valid = 1; p1_iso = p.iso; p1_shutter = p.shutter; p1_aperture = p.aperture;
            break;
        case 1:
            p2_valid = 1; p2_iso = p.iso; p2_shutter = p.shutter; p2_aperture = p.aperture;
            break;
        default:
            p3_valid = 1; p3_iso = p.iso; p3_shutter = p.shutter; p3_aperture = p.aperture;
            break;
    }

    NotifyBox(1200, "Saved P%d", slot + 1);
}

static void photoctl_load_slot(int slot)
{
    int valid, iso, shutter, aperture;

    if (!photoctl_settings_ok())
        return;

    switch (slot)
    {
        case 0:
            valid = p1_valid; iso = p1_iso; shutter = p1_shutter; aperture = p1_aperture;
            break;
        case 1:
            valid = p2_valid; iso = p2_iso; shutter = p2_shutter; aperture = p2_aperture;
            break;
        default:
            valid = p3_valid; iso = p3_iso; shutter = p3_shutter; aperture = p3_aperture;
            break;
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

    photoctl_profile_capture(0, &photo_profile);
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
    photoctl_profile_sync_from_config(0, &photo_profile);
    photoctl_profile_sync_from_config(1, &video_profile);
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
    MODULE_CONFIG(photo_last_valid)
    MODULE_CONFIG(photo_last_iso)
    MODULE_CONFIG(photo_last_shutter)
    MODULE_CONFIG(photo_last_aperture)
    MODULE_CONFIG(photo_last_wb_mode)
    MODULE_CONFIG(photo_last_kelvin)
    MODULE_CONFIG(video_last_valid)
    MODULE_CONFIG(video_last_iso)
    MODULE_CONFIG(video_last_shutter)
    MODULE_CONFIG(video_last_aperture)
    MODULE_CONFIG(video_last_wb_mode)
    MODULE_CONFIG(video_last_kelvin)
    MODULE_CONFIG(p1_valid)
    MODULE_CONFIG(p1_iso)
    MODULE_CONFIG(p1_shutter)
    MODULE_CONFIG(p1_aperture)
    MODULE_CONFIG(p2_valid)
    MODULE_CONFIG(p2_iso)
    MODULE_CONFIG(p2_shutter)
    MODULE_CONFIG(p2_aperture)
    MODULE_CONFIG(p3_valid)
    MODULE_CONFIG(p3_iso)
    MODULE_CONFIG(p3_shutter)
    MODULE_CONFIG(p3_aperture)
MODULE_CONFIGS_END()

#endif /* CONFIG_EOSM */
