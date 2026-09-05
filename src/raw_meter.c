#include "dryos.h"
#include "vram.h"
#include "raw.h"
#include "histogram.h"
#include "math.h"

#ifdef FEATURE_RAW_HISTOGRAM

/*
 * Compact RAW percentile meter for control loops such as Auto ISO.
 *
 * The legacy raw histogram API uses a 16384-entry heap histogram. That is
 * useful when exact sensor-code percentiles are required, but excessive for
 * exposure control. This meter quantizes the same RAW codes into 512 bins,
 * requiring only 2 KB of static storage and no heap allocation.
 */
#define RAW_METER_BINS       512
#define RAW_METER_SHIFT      5
#define RAW_METER_RAW_MASK   16383

static uint32_t raw_meter_hist[RAW_METER_BINS];

int FAST raw_meter_get_percentile_level(int percentile_x10, int gray_projection, int speed)
{
    if (!raw_update_params())
        return -1;

    get_yuv422_vram();
    memset(raw_meter_hist, 0, sizeof(raw_meter_hist));

    speed = COERCE(speed, 1, 16);
    percentile_x10 = COERCE(percentile_x10, 1, 999);

    int off = get_y_skip_offset_for_histogram();
    uint32_t total = 0;

    for (int i = os.y0 + off; i < os.y_max - off; i += speed)
    {
        int y = BM2RAW_Y(i);
        for (int j = os.x0; j < os.x_max; j += speed)
        {
            int x = BM2RAW_X(j);
            int px = raw_get_gray_pixel(x, y, gray_projection) & RAW_METER_RAW_MASK;
            raw_meter_hist[px >> RAW_METER_SHIFT]++;
            total++;
        }
    }

    if (!total)
        return -1;

    uint32_t target = (uint64_t)total * percentile_x10 / 1000;
    target = MAX(target, 1);

    uint32_t accumulated = 0;
    for (int i = 0; i < RAW_METER_BINS; i++)
    {
        accumulated += raw_meter_hist[i];
        if (accumulated >= target)
        {
            /* Return the center of the bucket. The controller still receives
             * a real 14-bit RAW-space value suitable for raw_to_ev(). */
            int raw = (i << RAW_METER_SHIFT) + (1 << (RAW_METER_SHIFT - 1));
            return MIN(raw, RAW_METER_RAW_MASK);
        }
    }

    return RAW_METER_RAW_MASK;
}

#endif /* FEATURE_RAW_HISTOGRAM */
