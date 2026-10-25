/**
 * @file lv_tslib.h
 *
 */

#ifndef LV_TSLIB_H
#define LV_TSLIB_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "../../indev/lv_indev.h"

#if LV_USE_TSLIB

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create tslib input device from a given path.
 *
 * The touchscreen must be configured for tslib, i.e. the TSLIB_TSDEVICE,
 * TSLIB_CONFFILE, TSLIB_CALIBFILE, TSLIB_PLUGINDIR and TSLIB_FBDEVICE
 * environment variables have to be set (usually by sourcing `ts_uenv`).
 * tslib then reads the touch events through the filters configured in
 * ts.conf, which also applies the calibration done with ts_calibrate.
 * @param dev_path device path, e.g., /dev/input/event0
 * @return pointer to input device or NULL if opening failed
 */
lv_indev_t * lv_tslib_create(const char * dev_path);

/**
 * Set whether coordinates of pointer device should be swapped. Defaults to
 * false.
 * @param indev tslib input device
 * @param swap_axes whether to swap x and y axes
 */
void lv_tslib_set_swap_axes(lv_indev_t * indev, bool swap_axes);

/**
 * Configure a coordinate transformation for pointer devices. Applied after
 * axis swap, if any. Defaults to apply no transformation, because tslib
 * usually already outputs calibrated screen coordinates.
 * @param indev tslib input device
 * @param min_x pointer coordinate mapped to min x of display
 * @param min_y pointer coordinate mapped to min y of display
 * @param max_x pointer coordinate mapped to max x of display
 * @param max_y pointer coordinate mapped to max y of display
 */
void lv_tslib_set_calibration(lv_indev_t * indev, int min_x, int min_y, int max_x, int max_y);

/**
 * Remove tslib input device.
 * @param indev tslib input device to close and free @nullable
 */
void lv_tslib_delete(lv_indev_t * indev);

#endif /*LV_USE_TSLIB*/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_TSLIB_H*/
