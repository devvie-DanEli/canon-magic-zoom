#include <dryos.h>
#include <property.h>
#include <propvalues.h>
#include <lens.h>
#include <config.h>
#include <menu.h>
#include <lvinfo.h>
#include <module.h>
#include <shoot.h>
#include <raw.h>
#include <histogram.h>
#include <math.h>

/*
 * EOS M Auto ISO for Movie / RAW LiveView.
 *
 * Canon's native Movie Auto ISO path was found to hold one value while the
 * EOS M is recording RAW, so this module controls ISO directly. The meter is
 * derived from the existing RAW histogram percentile scanner, which already
 * operates on the LiveView RAW buffer used by the Crop Mood pipeline.
 *
 * Shutter and aperture remain user-controlled. ISO is the only parameter
 * changed by this controller.
 */

#define AUTO_ISO_MAX_CHOICES       5
static const uint8_t auto_iso_max_raw[AUTO_ISO_MAX_CHOICES] = { 88, 96, 104, 112, 120 };
static const char *auto_iso_max_labels[AUTO_ISO_MAX_CHOICES] = { "400", "800", "1600", "3200", "6400" };

#define AUTO_ISO_MIN_RAW           72       /* ISO 100 */
#define AUTO_ISO_TARGET_EV         (-2.50f) /* median target, relative to RAW white */
#define AUTO_ISO_PERCENTILE_X10    500      /* 50th percentile */
#define AUTO_ISO_SCAN_SPEED        8        /* fast RAW percentile scan */
#define AUTO_ISO_UPDATE_TICKS      8
#define AUTO_ISO_DEADBAND_RAW      2        /* 1/4 stop */
#define AUTO_ISO_MAX_STEP_RAW      8        /* never jump more than 1 EV/update */

static CONFIG_INT("eosm.auto_iso", eosm_auto_iso_enabled_config, 0);
static CONFIG_INT("eosm.auto_iso.max", eosm_auto_iso_max_index, 4);
static CONFIG_INT("eosm.auto_iso.last_manual_iso", eosm_auto_iso_last_manual_raw, 88);

static int dual_iso_prev = 0;
static int auto_iso_supported_cache = -1;
static int auto_iso_running = 0;
static int auto_iso_tick = 0;
static int auto_iso_last_target_raw = -1;
static int auto_iso_last_applied_raw = -1;

static int (*dual_iso_is_enabled)(void) = MODULE_FUNCTION(dual_iso_is_enabled);

static int auto_iso_set_enabled(int enabled);

static int auto_iso_supported(void)
{
    if (auto_iso_supported_cache < 0)
        auto_iso_supported_cache = is_camera("EOSM", "2.0.2");
    return auto_iso_supported_cache;
}

static int auto_iso_dual_enabled(void)
{
    return dual_iso_is_enabled && dual_iso_is_enabled();
}

static int auto_iso_max_raw_value(void)
{
    return auto_iso_max_raw[COERCE(eosm_auto_iso_max_index, 0, AUTO_ISO_MAX_CHOICES - 1)];
}

static int auto_iso_last_manual(void)
{
    int raw = eosm_auto_iso_last_manual_raw;
    if (raw < AUTO_ISO_MIN_RAW || raw > 120)
        raw = 88;
    return raw;
}

static void auto_iso_track_manual_iso(void)
{
    if (!eosm_auto_iso_enabled_config && lens_info.raw_iso)
        eosm_auto_iso_last_manual_raw = lens_info.raw_iso;
}

static void auto_iso_reset_controller(void)
{
    auto_iso_tick = 0;
    auto_iso_last_target_raw = -1;
    auto_iso_last_applied_raw = -1;
}

static void auto_iso_disable_and_restore_manual(void)
{
    if (lens_info.raw_iso)
        eosm_auto_iso_last_manual_raw = lens_info.raw_iso;

    eosm_auto_iso_enabled_config = 0;
    auto_iso_reset_controller();
    lens_set_rawiso(auto_iso_last_manual());
    lens_display_set_dirty();
}

/*
 * The RAW histogram API returns an actual sensor-code sample. Converting that
 * sample to EV lets us measure how far the median image brightness is from a
 * fixed target. Since shutter and aperture stay fixed, the correction can be
 * applied directly to ISO at 8 raw ISO codes per EV.
 */
static int auto_iso_calculate_target_raw(int current_iso_raw)
{
    int raw_level = raw_hist_get_percentile_level(
        AUTO_ISO_PERCENTILE_X10,
        GRAY_PROJECTION_GREEN,
        AUTO_ISO_SCAN_SPEED
    );

    if (raw_level <= 0)
        return -1;

    float measured_ev = raw_to_ev(raw_level);
    float correction_ev = AUTO_ISO_TARGET_EV - measured_ev;
    int delta_raw = (int)roundf(correction_ev * 8.0f);

    /* Limit a single controller step so one noisy frame cannot cause a large jump. */
    delta_raw = COERCE(delta_raw, -AUTO_ISO_MAX_STEP_RAW, AUTO_ISO_MAX_STEP_RAW);

    return current_iso_raw + delta_raw;
}

static int auto_iso_choose_raw(int target_raw, int current_iso_raw)
{
    int max_raw = auto_iso_max_raw_value();
    target_raw = COERCE(target_raw, AUTO_ISO_MIN_RAW, max_raw);

    if (auto_iso_last_applied_raw >= 0 &&
        ABS(target_raw - auto_iso_last_applied_raw) < AUTO_ISO_DEADBAND_RAW)
        return auto_iso_last_applied_raw;

    if (ABS(target_raw - current_iso_raw) < AUTO_ISO_DEADBAND_RAW)
        return current_iso_raw;

    return target_raw;
}

static void auto_iso_task(void *unused)
{
    (void)unused;
    auto_iso_running = 1;

    if (!auto_iso_supported() || !eosm_auto_iso_enabled_config ||
        !is_movie_mode() || auto_iso_dual_enabled() ||
        shooting_mode != SHOOTMODE_M || !lv || !RECORDING_RAW ||
        !lens_info.raw_shutter || !lens_info.raw_aperture)
        goto cleanup;

    {
        int current_iso_raw = lens_info.raw_iso;
        if (current_iso_raw < AUTO_ISO_MIN_RAW || current_iso_raw > 120)
            current_iso_raw = auto_iso_last_manual();

        /* If the ISO state is somehow still zero/invalid, establish a valid
         * manual gain before trying to meter. */
        if (current_iso_raw < AUTO_ISO_MIN_RAW || current_iso_raw > 120)
            goto cleanup;

        int target_raw = auto_iso_calculate_target_raw(current_iso_raw);
        if (target_raw < 0)
            goto cleanup;

        int chosen_raw = auto_iso_choose_raw(target_raw, current_iso_raw);
        int max_raw = auto_iso_max_raw_value();

        /* Hard gain ceiling. Applied immediately before every ISO write. */
        chosen_raw = COERCE(chosen_raw, AUTO_ISO_MIN_RAW, max_raw);
        auto_iso_last_target_raw = target_raw;

        if (chosen_raw != current_iso_raw)
        {
            if (lens_set_rawiso(chosen_raw))
                auto_iso_last_applied_raw = chosen_raw;
            else
                auto_iso_last_applied_raw = -1;
        }
        else
        {
            auto_iso_last_applied_raw = current_iso_raw;
        }
    }

cleanup:
    auto_iso_running = 0;
}

static unsigned int auto_iso_shoot_task(unsigned int unused)
{
    (void)unused;

    if (!auto_iso_supported() || !is_movie_mode())
        return CBR_RET_CONTINUE;

    if (!eosm_auto_iso_enabled_config)
    {
        auto_iso_track_manual_iso();
        return CBR_RET_CONTINUE;
    }

    if (auto_iso_dual_enabled())
    {
        if (!dual_iso_prev)
            auto_iso_disable_and_restore_manual();
        dual_iso_prev = 1;
        return CBR_RET_CONTINUE;
    }

    dual_iso_prev = 0;

    if (!RECORDING_RAW || !lv || shooting_mode != SHOOTMODE_M)
        return CBR_RET_CONTINUE;

    if (++auto_iso_tick < AUTO_ISO_UPDATE_TICKS)
        return CBR_RET_CONTINUE;
    auto_iso_tick = 0;

    if (!auto_iso_running)
        task_create("eosm_auto_iso", 0x1c, 0x1000, auto_iso_task, (void *)0);

    return CBR_RET_CONTINUE;
}

int eosm_auto_iso_is_enabled(void)
{
    return auto_iso_supported() && eosm_auto_iso_enabled_config &&
           is_movie_mode() && !auto_iso_dual_enabled();
}

int eosm_auto_iso_is_locked(void)
{
    return auto_iso_supported() && auto_iso_dual_enabled();
}

int eosm_auto_iso_get_last_manual_iso(void)
{
    return auto_iso_last_manual();
}

int eosm_auto_iso_prepare_manual_iso(void)
{
    if (!auto_iso_supported())
        return 0;

    if (eosm_auto_iso_enabled_config)
    {
        auto_iso_disable_and_restore_manual();
        return 1;
    }
    return 0;
}

int eosm_auto_iso_set_from_touch(int sign)
{
    if (!auto_iso_supported())
        return 0;

    if (auto_iso_dual_enabled())
    {
        auto_iso_disable_and_restore_manual();
        return 1;
    }

    if (sign > 0)
        auto_iso_set_enabled(1);
    else if (sign < 0)
        auto_iso_set_enabled(0);
    return 1;
}

static int auto_iso_set_enabled(int enabled)
{
    if (!auto_iso_supported() || !is_movie_mode())
        return 0;

    if (auto_iso_dual_enabled())
    {
        auto_iso_disable_and_restore_manual();
        return 0;
    }

    if (enabled)
    {
        auto_iso_track_manual_iso();
        if (auto_iso_last_manual() < AUTO_ISO_MIN_RAW)
            return 0;
        auto_iso_reset_controller();
        eosm_auto_iso_enabled_config = 1;
        lens_display_set_dirty();
    }
    else
    {
        auto_iso_disable_and_restore_manual();
    }

    return 1;
}

static MENU_SELECT_FUNC(auto_iso_menu_toggle)
{
    auto_iso_set_enabled(!eosm_auto_iso_enabled_config);
}

static MENU_UPDATE_FUNC(auto_iso_menu_update)
{
    if (!auto_iso_supported() || !is_movie_mode() || auto_iso_dual_enabled())
    {
        MENU_SET_ENABLED(0);
        MENU_SET_VALUE("AUTO OFF");
        return;
    }
    MENU_SET_ENABLED(1);
    MENU_SET_VALUE(eosm_auto_iso_enabled_config ? "AUTO ON" : "AUTO OFF");
}

static MENU_UPDATE_FUNC(auto_iso_max_update)
{
    int index = COERCE(eosm_auto_iso_max_index, 0, AUTO_ISO_MAX_CHOICES - 1);
    eosm_auto_iso_max_index = index;
    if (eosm_auto_iso_enabled_config)
        auto_iso_reset_controller();
    MENU_SET_VALUE("%s", auto_iso_max_labels[index]);
}

static struct menu_entry auto_iso_menu[] = {
    {
        .name = "Auto ISO",
        .priv = &eosm_auto_iso_enabled_config,
        .min = 0,
        .max = 1,
        .choices = CHOICES("AUTO OFF", "AUTO ON"),
        .select = auto_iso_menu_toggle,
        .update = auto_iso_menu_update,
        .edit_mode = EM_INLINE_ADJUST,
        .depends_on = DEP_LIVEVIEW | DEP_MOVIE_MODE,
        .children = (struct menu_entry[]) {
            {
                .name = "Auto ISO Max",
                .priv = &eosm_auto_iso_max_index,
                .min = 0,
                .max = AUTO_ISO_MAX_CHOICES - 1,
                .choices = (const char **)auto_iso_max_labels,
                .update = auto_iso_max_update,
                .edit_mode = EM_INLINE_ADJUST,
                .depends_on = DEP_LIVEVIEW | DEP_MOVIE_MODE,
                .help = "Hard maximum gain limit for Auto ISO.",
            },
            MENU_EOL,
        },
        .help = "RAW-histogram Auto ISO for EOS M Movie mode.",
        .help2 = "Adjusts ISO only during RAW recording; shutter and aperture stay fixed.",
    },
};

static unsigned int auto_iso_init(void)
{
    if (!auto_iso_supported())
        return 0;

    menu_add("Expo", auto_iso_menu, COUNT(auto_iso_menu));
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(auto_iso_init)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_SHOOT_TASK, auto_iso_shoot_task, 0)
MODULE_CBRS_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(eosm_auto_iso_enabled_config)
    MODULE_CONFIG(eosm_auto_iso_max_index)
    MODULE_CONFIG(eosm_auto_iso_last_manual_raw)
MODULE_CONFIGS_END()
