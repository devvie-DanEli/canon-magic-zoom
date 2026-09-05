#include <dryos.h>
#include <lvinfo.h>
#include <module.h>

#ifdef CONFIG_SLIM_MENUS

/* The Auto ISO controller remains a module. This core-side LVInfo item only
 * mirrors its state and gives the user a visible LiveView control surface. */
static int (*eosm_auto_iso_is_enabled)(void) =
    MODULE_FUNCTION(eosm_auto_iso_is_enabled);
static int (*eosm_auto_iso_is_locked)(void) =
    MODULE_FUNCTION(eosm_auto_iso_is_locked);

static LVINFO_UPDATE_FUNC(eosm_auto_iso_lv_update)
{
    LVINFO_BUFFER(16);

    if (!lv || !is_movie_mode())
    {
        item->value = 0;
        item->width = 0;
        return;
    }

    if (eosm_auto_iso_is_locked && eosm_auto_iso_is_locked())
    {
        item->color_fg = COLOR_GRAY(50);
        snprintf(buffer, sizeof(buffer), "AUTO OFF");
        return;
    }

    if (!eosm_auto_iso_is_enabled)
    {
        item->color_fg = COLOR_GRAY(50);
        snprintf(buffer, sizeof(buffer), "AUTO OFF");
        return;
    }

    item->color_fg = COLOR_WHITE;
    snprintf(buffer, sizeof(buffer), "%s",
             eosm_auto_iso_is_enabled() ? "AUTO ON" : "AUTO OFF");
}

static struct lvinfo_item eosm_auto_iso_lv_item = {
    .name = "Auto ISO",
    .which_bar = LV_BOTTOM_BAR_ONLY,
    .update = eosm_auto_iso_lv_update,
    .preferred_position = 1,
    .priority = 2,
};

static void eosm_auto_iso_lv_init(void)
{
    lvinfo_add_item(&eosm_auto_iso_lv_item);
}

INIT_FUNC("eosm_auto_iso_lv", eosm_auto_iso_lv_init)

#endif /* CONFIG_SLIM_MENUS */
