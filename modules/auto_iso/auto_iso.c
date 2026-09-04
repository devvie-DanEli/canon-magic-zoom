#include <dryos.h>
#include <property.h>
#include <propvalues.h>
#include <lens.h>
#include <config.h>
#include <menu.h>
#include <lvinfo.h>
#include <module.h>
#include <shoot.h>

#ifdef CONFIG_EOSM

#define AUTO_ISO_MAX_CHOICES 5
static const uint8_t auto_iso_max_raw[AUTO_ISO_MAX_CHOICES] = { 88, 96, 104, 112, 120 };
static const char *auto_iso_max_labels[AUTO_ISO_MAX_CHOICES] = { "400", "800", "1600", "3200", "6400" };

/* Auto ISO state and ceiling are deliberately independent of M1/M2/M3. */
static CONFIG_INT("eosm.auto_iso", eosm_auto_iso_enabled_config, 0);
static CONFIG_INT("eosm.auto_iso.max", eosm_auto_iso_max_index, 4);
static CONFIG_INT("eosm.auto_iso.last_manual_iso", eosm_auto_iso_last_manual_raw, 88);

static uint8_t auto_iso_range_min_raw = 72;
static int dual_iso_prev = 0;

static int (*dual_iso_is_enabled_fn)(void) = MODULE_FUNCTION(dual_iso_is_enabled);

PROP_INT(PROP_ISO_AUTO, eosm_auto_iso_calculated_raw);

PROP_HANDLER(PROP_AUTO_ISO_RANGE)
{
    if (len >= 2)
    {
        uint8_t *p = (uint8_t *)buf;
        auto_iso_range_min_raw = p[1];
    }
}

static int auto_iso_dual_enabled(void)
{
    return dual_iso_is_enabled_fn && dual_iso_is_enabled_fn();
}

static int auto_iso_max_raw_value(void)
{
    return auto_iso_max_raw[COERCE(eosm_auto_iso_max_index, 0, AUTO_ISO_MAX_CHOICES - 1)];
}

static void auto_iso_apply_limit(void)
{
    if (!is_movie_mode()) return;

    uint8_t range[2];
    range[0] = auto_iso_max_raw_value();
    range[1] = auto_iso_range_min_raw ? auto_iso_range_min_raw : 72;
    if (range[0] < range[1]) range[0] = range[1];
    prop_request_change(PROP_AUTO_ISO_RANGE, range, 2);
}

static int auto_iso_last_manual(void)
{
    int raw = eosm_auto_iso_last_manual_raw;
    if (raw <= 0 || raw >= 255) raw = 88;
    return raw;
}

static void auto_iso_track_manual_iso(void)
{
    if (!eosm_auto_iso_enabled_config && lens_info.raw_iso)
        eosm_auto_iso_last_manual_raw = lens_info.raw_iso;
}

static void auto_iso_disable_and_restore_manual(void)
{
    if (lens_info.raw_iso)
        eosm_auto_iso_last_manual_raw = lens_info.raw_iso;

    eosm_auto_iso_enabled_config = 0;
    lens_set_rawiso(auto_iso_last_manual());
    lens_display_set_dirty();
}

static int auto_iso_set_enabled(int enabled)
{
    if (!is_movie_mode()) return 0;

    if (auto_iso_dual_enabled())
    {
        auto_iso_disable_and_restore_manual();
        return 0;
    }

    if (enabled)
    {
        auto_iso_track_manual_iso();
        auto_iso_apply_limit();
        eosm_auto_iso_enabled_config = 1;
        lens_set_rawiso(0);
    }
    else
    {
        auto_iso_disable_and_restore_manual();
    }

    lens_display_set_dirty();
    return 1;
}

int eosm_auto_iso_is_enabled(void)
{
    return eosm_auto_iso_enabled_config && is_movie_mode() && !auto_iso_dual_enabled();
}

int eosm_auto_iso_is_locked(void)
{
    return auto_iso_dual_enabled();
}

int eosm_auto_iso_get_last_manual_iso(void)
{
    return auto_iso_last_manual();
}

int eosm_auto_iso_prepare_manual_iso(void)
{
    if (eosm_auto_iso_enabled_config)
    {
        auto_iso_disable_and_restore_manual();
        return 1;
    }
    return 0;
}

int eosm_auto_iso_set_from_touch(int sign)
{
    if (auto_iso_dual_enabled())
    {
        auto_iso_disable_and_restore_manual();
        return 1;
    }

    if (sign > 0) auto_iso_set_enabled(1);
    else if (sign < 0) auto_iso_set_enabled(0);
    return 1;
}

static LVINFO_UPDATE_FUNC(auto_iso_lv_update)
{
    LVINFO_BUFFER(16);
    if (!is_movie_mode() || auto_iso_dual_enabled())
    {
        item->color_fg = COLOR_GRAY(50);
        snprintf(buffer, sizeof(buffer), "AUTO OFF");
        return;
    }
    item->color_fg = COLOR_WHITE;
    snprintf(buffer, sizeof(buffer), "%s", eosm_auto_iso_enabled_config ? "AUTO ON" : "AUTO OFF");
}

static struct lvinfo_item auto_iso_lv_item = {
    .name = "Auto ISO",
    .which_bar = LV_BOTTOM_BAR_ONLY,
    .update = auto_iso_lv_update,
    .preferred_position = 1,
    .priority = 2,
};

static MENU_SELECT_FUNC(auto_iso_menu_toggle)
{
    auto_iso_set_enabled(!eosm_auto_iso_enabled_config);
}

static MENU_SELECT_FUNC(auto_iso_max_select)
{
    eosm_auto_iso_max_index = COERCE(eosm_auto_iso_max_index + sign, 0, AUTO_ISO_MAX_CHOICES - 1);
    auto_iso_apply_limit();
}

static MENU_UPDATE_FUNC(auto_iso_menu_update)
{
    if (!is_movie_mode() || auto_iso_dual_enabled())
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
        /* SET opens the child submenu; left/right changes ON/OFF. */
        .edit_mode = EM_INLINE_ADJUST,
        .depends_on = DEP_LIVEVIEW | DEP_MOVIE_MODE,
        .children = (struct menu_entry[]) {
            {
                .name = "Auto ISO Max",
                .priv = &eosm_auto_iso_max_index,
                .min = 0,
                .max = AUTO_ISO_MAX_CHOICES - 1,
                .choices = (const char **)auto_iso_max_labels,
                .select = auto_iso_max_select,
                .update = auto_iso_max_update,
                .edit_mode = EM_INLINE_ADJUST,
                .depends_on = DEP_LIVEVIEW | DEP_MOVIE_MODE,
                .help = "Maximum ISO allowed by Auto ISO.",
            },
            MENU_EOL,
        },
        .help = "Enable Canon Auto ISO in Movie mode.",
        .help2 = "SET opens Auto ISO Max. Dual ISO disables Auto ISO.",
    },
};

static void auto_iso_menu_init(void)
{
    if (is_movie_mode()) auto_iso_apply_limit();
    lvinfo_add_item(&auto_iso_lv_item);
    menu_add("Expo", auto_iso_menu, COUNT(auto_iso_menu));
}

static void auto_iso_dual_vsync(void)
{
    if (!is_movie_mode()) return;
    int dual = auto_iso_dual_enabled();
    if (dual && !dual_iso_prev && eosm_auto_iso_enabled_config)
        auto_iso_disable_and_restore_manual();
    if (eosm_auto_iso_enabled_config && !dual)
        auto_iso_apply_limit();
    if (!eosm_auto_iso_enabled_config)
        auto_iso_track_manual_iso();
    dual_iso_prev = dual;
}

static void auto_iso_vsync_cbr(int unused);
MODULE_CBR(CBR_VSYNC, auto_iso_vsync_cbr, 0);
static void auto_iso_vsync_cbr(int unused)
{
    (void)unused;
    auto_iso_dual_vsync();
}

#endif
