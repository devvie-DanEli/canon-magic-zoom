#include <dryos.h>
#include <property.h>
#include <propvalues.h>
#include <lens.h>
#include <config.h>
#include <bmp.h>
#include <lvinfo.h>
#include "expo_mode_sep.h"

/* --------------------------------------------------------------------------
 * Separate exposure memory for Photo vs Movie (EOS M and similar).
 *
 * Canon shares ISO/shutter/aperture/WB across the movie switch. We keep two
 * stashes and swap them when is_movie_mode() changes.
 *
 * Memory Recall (M1/M2/M3) only runs in movie LV and only changes live movie
 * settings. Those values are written into the movie stash on the next leave
 * from movie mode, so recall is preserved across mode flips without sharing
 * slots with photo mode.
 * -------------------------------------------------------------------------- */
static CONFIG_INT("expo.sep.mv.iso",      expo_mv_iso, 0);
static CONFIG_INT("expo.sep.mv.shutter",  expo_mv_shutter, 0);
static CONFIG_INT("expo.sep.mv.aperture", expo_mv_aperture, 0);
static CONFIG_INT("expo.sep.mv.wb",       expo_mv_wb, 0);
static CONFIG_INT("expo.sep.mv.kelvin",   expo_mv_kelvin, 5500);

static CONFIG_INT("expo.sep.ph.iso",      expo_ph_iso, 0);
static CONFIG_INT("expo.sep.ph.shutter",  expo_ph_shutter, 0);
static CONFIG_INT("expo.sep.ph.aperture", expo_ph_aperture, 0);
static CONFIG_INT("expo.sep.ph.wb",       expo_ph_wb, 0);
static CONFIG_INT("expo.sep.ph.kelvin",   expo_ph_kelvin, 5500);

static int expo_sep_prev_movie = -1; /* -1 = not primed */
static int expo_sep_apply_pending = 0;
static int expo_sep_apply_to_movie = 0;

static void expo_sep_capture_into(
    int *iso, int *shutter, int *aperture, int *wb, int *kelvin)
{
    if (lens_info.raw_iso)
        *iso = lens_info.raw_iso;
    if (lens_info.raw_shutter)
        *shutter = lens_info.raw_shutter;
    if (lens_info.raw_aperture && lens_info.lens_exists)
        *aperture = lens_info.raw_aperture;
    *wb = lens_info.wb_mode;
    if (lens_info.kelvin)
        *kelvin = lens_info.kelvin;
}

static void expo_sep_apply_from(
    int iso, int shutter, int aperture, int wb, int kelvin)
{
    if (RECORDING || RECORDING_RAW)
        return;

    if (iso > 0)
        lens_set_rawiso(iso);
    if (shutter > 0)
        lens_set_rawshutter(shutter);
    if (aperture > 0 && lens_info.lens_exists)
        lens_set_rawaperture(aperture);

    if (wb == WB_AUTO)
        lens_set_wb_mode(WB_AUTO);
    else if (wb == WB_KELVIN || (kelvin >= 1500 && kelvin <= 15000))
        lens_set_kelvin(kelvin ? kelvin : 5500);
    else if (wb > 0)
        lens_set_wb_mode(wb);

    lens_display_set_dirty();
}

static void expo_sep_capture_movie(void)
{
    expo_sep_capture_into(
        &expo_mv_iso, &expo_mv_shutter, &expo_mv_aperture,
        &expo_mv_wb, &expo_mv_kelvin);
}

static void expo_sep_capture_photo(void)
{
    expo_sep_capture_into(
        &expo_ph_iso, &expo_ph_shutter, &expo_ph_aperture,
        &expo_ph_wb, &expo_ph_kelvin);
}

static void expo_sep_apply_movie(void)
{
    if (!expo_mv_iso && !expo_mv_shutter)
        return; /* never saved yet */
    expo_sep_apply_from(
        expo_mv_iso, expo_mv_shutter, expo_mv_aperture,
        expo_mv_wb, expo_mv_kelvin);
}

static void expo_sep_apply_photo(void)
{
    if (!expo_ph_iso && !expo_ph_shutter)
        return;
    expo_sep_apply_from(
        expo_ph_iso, expo_ph_shutter, expo_ph_aperture,
        expo_ph_wb, expo_ph_kelvin);
}

/* Called from shoot_task. Detects photo <-> movie transitions. */
void expo_sep_update(void)
{
    int movie = is_movie_mode() ? 1 : 0;

    if (expo_sep_prev_movie < 0)
    {
        expo_sep_prev_movie = movie;
        /* Seed the active side so the first leave has something useful. */
        if (movie)
            expo_sep_capture_movie();
        else
            expo_sep_capture_photo();
        return;
    }

    if (movie == expo_sep_prev_movie)
        return;

    /* Mode changed: save the side we leave (includes any Memory Recall
     * values still active in movie LV), then restore the other side. */
    if (RECORDING || RECORDING_RAW)
        return; /* do not swap mid-recording */

    if (expo_sep_prev_movie)
        expo_sep_capture_movie();
    else
        expo_sep_capture_photo();

    expo_sep_prev_movie = movie;

    /* Defer apply a couple of ticks so PROP/lens state settles. */
    expo_sep_apply_pending = 3;
    expo_sep_apply_to_movie = movie;
}

void expo_sep_apply_deferred(void)
{
    if (!expo_sep_apply_pending)
        return;
    if (RECORDING || RECORDING_RAW)
        return;

    expo_sep_apply_pending--;
    if (expo_sep_apply_pending > 0)
        return;

    if (expo_sep_apply_to_movie && is_movie_mode())
        expo_sep_apply_movie();
    else if (!expo_sep_apply_to_movie && !is_movie_mode())
        expo_sep_apply_photo();
}
