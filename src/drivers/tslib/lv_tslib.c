/**
 * @file lv_tslib.c
 *
 */

/**********************
 *      INCLUDES
 **********************/
#include "lv_tslib_private.h"
#if LV_USE_TSLIB

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tslib.h>

#include "../../display/lv_display_private.h"

/*********************
 *      DEFINES
 *********************/

/*ts_calibrate-compatible calibration support*/
#define LV_TSLIB_CALIBRATION_SAMPLES 128
#define LV_TSLIB_CALIBFILE_DEFAULT "/etc/pointercal"

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    struct tsdev * ts;
    /*Config*/
    bool swap_axes;
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    /*State*/
    int last_x;
    int last_y;
    lv_indev_state_t state;
    bool deleting;
} lv_tslib_t;

typedef struct {
    int x[5], xfb[5];
    int y[5], yfb[5];
    int a[7];
} lv_tslib_calibration_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static int _tslib_calibrate(int v, int in_min, int in_max, int out_min, int out_max);

static lv_point_t _tslib_process_pointer(lv_indev_t * indev, int x, int y);
static void _tslib_async_delete_cb(void * user_data);
static void _tslib_read(lv_indev_t * indev, lv_indev_data_t * data);
static void _tslib_indev_delete_cb(lv_event_t * e);
static int _tslib_sort_by_x(const void * lhs, const void * rhs);
static int _tslib_sort_by_y(const void * lhs, const void * rhs);
static void _tslib_delay_and_refresh(void);
static void _tslib_wait_release(struct tsdev * ts);
static bool _tslib_getxy(struct tsdev * ts, uint32_t timeout_ms, int * x, int * y);
static void _tslib_clearbuf(struct tsdev * ts);
static void _tslib_calibration_leave(lv_indev_t * indev, lv_tslib_t * dsc);
static int _tslib_perform_calibration(lv_tslib_calibration_t * cal);

/**********************
 *   STATIC FUNCTIONS
 **********************/

static int _tslib_calibrate(int v, int in_min, int in_max, int out_min, int out_max)
{
    if(in_min != in_max) v = (v - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    return LV_CLAMP(out_min, v, out_max);
}

static lv_point_t _tslib_process_pointer(lv_indev_t * indev, int x, int y)
{
    LV_ASSERT(indev != NULL);
    lv_display_t * disp = lv_indev_get_display(indev);
    lv_tslib_t * dsc = lv_indev_get_driver_data(indev);
    LV_ASSERT(dsc != NULL);

    int swapped_x = dsc->swap_axes ? y : x;
    int swapped_y = dsc->swap_axes ? x : y;

    int offset_x = disp->offset_x;
    int offset_y = disp->offset_y;
    int width = disp->hor_res;
    int height = disp->ver_res;

    lv_point_t p;
    p.x = _tslib_calibrate(swapped_x, dsc->min_x, dsc->max_x, offset_x, offset_x + width - 1);
    p.y = _tslib_calibrate(swapped_y, dsc->min_y, dsc->max_y, offset_y, offset_y + height - 1);
    return p;
}

static void _tslib_async_delete_cb(void * user_data)
{
    lv_indev_t * indev = user_data;
    lv_indev_delete(indev);
}

static void _tslib_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_ASSERT(indev != NULL);
    LV_ASSERT(data != NULL);
    lv_tslib_t * dsc = lv_indev_get_driver_data(indev);
    LV_ASSERT(dsc != NULL);

    /*Update dsc with buffered samples. tslib already applies the filters and
     *the calibration from ts.conf, so the coordinates are screen coordinates
     *when the touchscreen has been calibrated with ts_calibrate.*/
    struct ts_sample samp = {0};
    int nr;
    while((nr = ts_read(dsc->ts, &samp, 1)) > 0) {
        if(samp.pressure > 0) {
            /*Keep the last pressed position: pen-up samples may carry garbage*/
            dsc->last_x = samp.x;
            dsc->last_y = samp.y;
            dsc->state = LV_INDEV_STATE_PRESSED;
        }
        else {
            dsc->state = LV_INDEV_STATE_RELEASED;
        }
    }

    if(!dsc->deleting && nr < 0 && errno != EAGAIN) {
        LV_LOG_ERROR("ts_read failed: %s", strerror(errno));
        lv_async_call(_tslib_async_delete_cb, indev);
        dsc->deleting = true;
    }

    data->state = dsc->state;
    data->point = _tslib_process_pointer(indev, dsc->last_x, dsc->last_y);
}

static void _tslib_indev_delete_cb(lv_event_t * e)
{
    LV_ASSERT(e != NULL);
    lv_indev_t * indev = lv_event_get_target(e);
    lv_tslib_t * dsc = lv_indev_get_driver_data(indev);
    LV_ASSERT(dsc != NULL);
    lv_async_call_cancel(_tslib_async_delete_cb, indev);
    ts_close(dsc->ts);
    lv_free(dsc);
}

/*The functions below are the sampling and math of tslib's ts_calibrate,
 *kept in the driver because only the driver can read raw (unfiltered)
 *samples from the tsdev and reconfigure the filter stack afterwards.*/

static int _tslib_sort_by_x(const void * lhs, const void * rhs)
{
    return ((const struct ts_sample *)lhs)->x - ((const struct ts_sample *)rhs)->x;
}

static int _tslib_sort_by_y(const void * lhs, const void * rhs)
{
    return ((const struct ts_sample *)lhs)->y - ((const struct ts_sample *)rhs)->y;
}

/*Give the rest of the system a turn while waiting for a sample: LVGL's own
 *delay and timer primitives keep this driver OS-independent. Note that
 *lv_delay_ms() busy-waits unless the application installs lv_delay_set_cb().*/
static void _tslib_delay_and_refresh(void)
{
    lv_delay_ms(5);
    lv_timer_handler();
}

/*The long-press that opened the calibration is usually still held down
 *when this runs: wait for that touch to be released (bounded) so it does
 *not register as the first calibration point. On timeout assume it is
 *gone already -- a still-held touch then waits for its release and the
 *next press in _tslib_getxy(), which is also correct.*/
static void _tslib_wait_release(struct tsdev * ts)
{
    struct ts_sample s;
    uint32_t start = lv_tick_get();

    while(lv_tick_elaps(start) < 500) {
        if(ts_read_raw(ts, &s, 1) == 1 && s.pressure <= 0) break;
        _tslib_delay_and_refresh();
    }
    _tslib_clearbuf(ts);
}

/*Sample one point: wait for a press (bounded by timeout_ms), collect
 *samples until release, take the median of each axis (more robust than a
 *mean against touch jitter). Returns false if nobody pressed in time.*/
static bool _tslib_getxy(struct tsdev * ts, uint32_t timeout_ms, int * x, int * y)
{
    struct ts_sample samp[LV_TSLIB_CALIBRATION_SAMPLES];
    int index = 0;
    uint32_t start = lv_tick_get();

    while(ts_read_raw(ts, &samp[0], 1) != 1 || samp[0].pressure <= 0) {
        if(lv_tick_elaps(start) > timeout_ms) return false;
        _tslib_delay_and_refresh();
    }

    for(;;) {
        if(index < LV_TSLIB_CALIBRATION_SAMPLES - 1) index++;
        while(ts_read_raw(ts, &samp[index], 1) != 1) _tslib_delay_and_refresh();
        if(samp[index].pressure <= 0) break;
    }

    qsort(samp, index, sizeof(samp[0]), _tslib_sort_by_x);
    *x = (index & 1) ? samp[index / 2].x : (samp[index / 2 - 1].x + samp[index / 2].x) / 2;
    qsort(samp, index, sizeof(samp[0]), _tslib_sort_by_y);
    *y = (index & 1) ? samp[index / 2].y : (samp[index / 2 - 1].y + samp[index / 2].y) / 2;
    LV_LOG_INFO("calibration point: %d samples -> raw (%d, %d)", index, *x, *y);
    return true;
}

static void _tslib_clearbuf(struct tsdev * ts)
{
    while(ts_read_raw(ts, &(struct ts_sample) {0}, 1) == 1) {
    }
}

/*Least-squares fit of raw (x, y) to screen (xfb, yfb), coefficients scaled
 *by 65536 -- the exact format the tslib linear filter reads back.*/
static int _tslib_perform_calibration(lv_tslib_calibration_t * cal)
{
    int j;
    float n, x, y, x2, y2, xy, z, zx, zy;
    float det, a, b, c, e, f, i;
    float scaling = 65536.0;

    n = x = y = x2 = y2 = xy = 0;
    for(j = 0; j < 5; j++) {
        n += 1.0;
        x += (float)cal->x[j];
        y += (float)cal->y[j];
        x2 += (float)(cal->x[j] * cal->x[j]);
        y2 += (float)(cal->y[j] * cal->y[j]);
        xy += (float)(cal->x[j] * cal->y[j]);
    }

    det = n * (x2 * y2 - xy * xy) + x * (xy * y - x * y2) + y * (x * xy - y * x2);
    if(det < 0.1 && det > -0.1) {
        LV_LOG_WARN("calibration: singular matrix, not solvable");
        return 0;
    }

    a = (x2 * y2 - xy * xy) / det;
    b = (xy * y - x * y2) / det;
    c = (x * xy - y * x2) / det;
    e = (n * y2 - y * y) / det;
    f = (x * y - n * xy) / det;
    i = (n * x2 - x * x) / det;

    z = zx = zy = 0;
    for(j = 0; j < 5; j++) {
        z += (float)cal->xfb[j];
        zx += (float)(cal->xfb[j] * cal->x[j]);
        zy += (float)(cal->xfb[j] * cal->y[j]);
    }
    cal->a[0] = (int)((a * z + b * zx + c * zy) * scaling);
    cal->a[1] = (int)((b * z + e * zx + f * zy) * scaling);
    cal->a[2] = (int)((c * z + f * zx + i * zy) * scaling);

    z = zx = zy = 0;
    for(j = 0; j < 5; j++) {
        z += (float)cal->yfb[j];
        zx += (float)(cal->yfb[j] * cal->x[j]);
        zy += (float)(cal->yfb[j] * cal->y[j]);
    }
    cal->a[3] = (int)((a * z + b * zx + c * zy) * scaling);
    cal->a[4] = (int)((b * z + e * zx + f * zy) * scaling);
    cal->a[5] = (int)((c * z + f * zx + i * zy) * scaling);
    cal->a[6] = (int)scaling;
    return 1;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_indev_t * lv_tslib_create(const char * dev_path)
{
    LV_CHECK_ARG(dev_path != NULL, return NULL);

    lv_tslib_t * dsc = lv_malloc_zeroed(sizeof(lv_tslib_t));
    LV_ASSERT_MALLOC(dsc);
    if(dsc == NULL) return NULL;

    dsc->ts = ts_open(dev_path, 1 /*nonblocking*/);
    if(dsc->ts == NULL) {
        LV_LOG_WARN("ts_open failed: %s", strerror(errno));
        lv_free(dsc);
        return NULL;
    }

    if(0 != ts_config(dsc->ts)) {
        LV_LOG_ERROR("ts_config failed. Check that TSLIB_CONFFILE, TSLIB_CALIBFILE and "
                     "TSLIB_PLUGINDIR are set, e.g. via `ts_uenv`.");
        ts_close(dsc->ts);
        lv_free(dsc);
        return NULL;
    }

    lv_indev_t * indev = lv_indev_create();
    if(indev == NULL) {
        ts_close(dsc->ts);
        lv_free(dsc);
        return NULL;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, _tslib_read);
    lv_indev_set_driver_data(indev, dsc);
    lv_indev_add_event_cb(indev, _tslib_indev_delete_cb, LV_EVENT_DELETE, NULL);

    return indev;
}

void lv_tslib_set_swap_axes(lv_indev_t * indev, bool swap_axes)
{
    LV_CHECK_ARG(indev != NULL, return);
    lv_tslib_t * dsc = lv_indev_get_driver_data(indev);
    LV_CHECK_ARG(dsc != NULL, return);
    dsc->swap_axes = swap_axes;
}

void lv_tslib_set_calibration(lv_indev_t * indev, int min_x, int min_y, int max_x, int max_y)
{
    LV_CHECK_ARG(indev != NULL, return);
    lv_tslib_t * dsc = lv_indev_get_driver_data(indev);
    LV_CHECK_ARG(dsc != NULL, return);
    dsc->min_x = min_x;
    dsc->min_y = min_y;
    dsc->max_x = max_x;
    dsc->max_y = max_y;
}

/*Leave raw sampling mode: the pen-up sample may have been consumed by the
 *raw reads and the filters were rebuilt anyway, so restart from a
 *released, drained state and hand the device back to the indev.*/
static void _tslib_calibration_leave(lv_indev_t * indev, lv_tslib_t * dsc)
{
    dsc->state = LV_INDEV_STATE_RELEASED;
    _tslib_clearbuf(dsc->ts);
    lv_indev_enable(indev, true);
}

void lv_tslib_calibration_start(lv_indev_t * indev)
{
    LV_CHECK_ARG(indev != NULL, return);
    lv_tslib_t * dsc = lv_indev_get_driver_data(indev);
    LV_CHECK_ARG(dsc != NULL, return);

    /*The indev read callback must not run during calibration: it reads
     *filtered samples from the same tsdev the calibration samples raw,
     *and both would race for the events of one file descriptor.*/
    lv_indev_enable(indev, false);

    /*A long-press is what typically opens a calibration UI, and that
     *finger is usually still held: swallow it so it does not register
     *as the first point (bounded, see _tslib_wait_release).*/
    _tslib_wait_release(dsc->ts);
}

bool lv_tslib_calibration_sample(lv_indev_t * indev, uint32_t timeout_ms, int * x, int * y)
{
    LV_CHECK_ARG(indev != NULL, return false);
    lv_tslib_t * dsc = lv_indev_get_driver_data(indev);
    LV_CHECK_ARG(dsc != NULL, return false);

    _tslib_clearbuf(dsc->ts);
    return _tslib_getxy(dsc->ts, timeout_ms, x, y);
}

lv_result_t lv_tslib_calibration_finish(lv_indev_t * indev, const lv_point_t screen_pts[5],
                                        const lv_point_t raw_pts[5])
{
    LV_CHECK_ARG(indev != NULL && screen_pts != NULL && raw_pts != NULL, return LV_RESULT_INVALID);
    lv_tslib_t * dsc = lv_indev_get_driver_data(indev);
    LV_CHECK_ARG(dsc != NULL, return LV_RESULT_INVALID);

    lv_display_t * disp = lv_indev_get_display(indev);
    int width = disp->hor_res;
    int height = disp->ver_res;

    lv_tslib_calibration_t cal = {0};
    int j;
    for(j = 0; j < 5; j++) {
        cal.x[j] = raw_pts[j].x;
        cal.y[j] = raw_pts[j].y;
        cal.xfb[j] = screen_pts[j].x;
        cal.yfb[j] = screen_pts[j].y;
    }

    lv_result_t res = LV_RESULT_INVALID;
    if(_tslib_perform_calibration(&cal)) {
        /*Same file and field order ts_calibrate writes, so the file stays
         *interchangeable with the standard tslib tools.*/
        const char * calfile = getenv("TSLIB_CALIBFILE");
        if(calfile == NULL) calfile = LV_TSLIB_CALIBFILE_DEFAULT;

        FILE * f = fopen(calfile, "w");
        if(f == NULL) {
            LV_LOG_ERROR("cannot write %s: %s", calfile, strerror(errno));
        }
        else {
            fprintf(f, "%d %d %d %d %d %d %d %d %d %d\n",
                    cal.a[1], cal.a[2], cal.a[0], cal.a[4], cal.a[5],
                    cal.a[3], cal.a[6], width, height, 0);
            fclose(f);
            res = LV_RESULT_OK;
        }
    }

    /*Rebuild the module stack so the linear filter picks up the calfile
     *just written -- the live tsdev is recalibrated without a restart.*/
    if(res == LV_RESULT_OK) {
        if(ts_reconfig(dsc->ts) == 0) {
            LV_LOG_INFO("calibration saved and applied (%d %d %d %d %d %d)",
                        cal.a[1], cal.a[2], cal.a[0], cal.a[4], cal.a[5], cal.a[3]);
        }
        else {
            LV_LOG_ERROR("ts_reconfig failed: %s", strerror(errno));
            res = LV_RESULT_INVALID;
        }
    }

    _tslib_calibration_leave(indev, dsc);
    return res;
}

void lv_tslib_calibration_cancel(lv_indev_t * indev)
{
    LV_CHECK_ARG(indev != NULL, return);
    lv_tslib_t * dsc = lv_indev_get_driver_data(indev);
    LV_CHECK_ARG(dsc != NULL, return);

    _tslib_calibration_leave(indev, dsc);
}

void lv_tslib_delete(lv_indev_t * indev)
{
    lv_indev_delete(indev);
}

#endif /*LV_USE_TSLIB*/
