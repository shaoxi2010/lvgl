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
#include <string.h>

#include <tslib.h>

#include "../../display/lv_display_private.h"

/*********************
 *      DEFINES
 *********************/

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

/**********************
 *  STATIC PROTOTYPES
 **********************/

static int _tslib_calibrate(int v, int in_min, int in_max, int out_min, int out_max);

static lv_point_t _tslib_process_pointer(lv_indev_t * indev, int x, int y);
static void _tslib_async_delete_cb(void * user_data);
static void _tslib_read(lv_indev_t * indev, lv_indev_data_t * data);
static void _tslib_indev_delete_cb(lv_event_t * e);

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

void lv_tslib_delete(lv_indev_t * indev)
{
    lv_indev_delete(indev);
}

#endif /*LV_USE_TSLIB*/
