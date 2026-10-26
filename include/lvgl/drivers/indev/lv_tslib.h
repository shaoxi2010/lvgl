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
 * Begin a touchscreen calibration sequence: pauses the indev feed and
 * swallows a still-held touch (e.g. the long-press that opened the
 * calibration UI). Sampling and UI stay separated: the application draws
 * its own crosshair scene between these calls.
 * @param indev tslib input device
 */
void lv_tslib_calibration_start(lv_indev_t * indev);

/**
 * Sample one raw (unfiltered) touch point: waits for a press (bounded by
 * timeout_ms), collects samples until release, returns the median of each
 * axis. Blocks; pumps lv_timer_handler() while waiting.
 * @param indev        tslib input device, in a calibration sequence
 * @param timeout_ms   give up when no touch within this time
 * @param x            raw x result
 * @param y            raw y result
 * @return true: sampled; false: timed out
 */
bool lv_tslib_calibration_sample(lv_indev_t * indev, uint32_t timeout_ms, int * x, int * y);

/**
 * Fit the 5 sampled raw points to the 5 screen points the UI displayed
 * (ts_calibrate-compatible least squares), write the result to
 * $TSLIB_CALIBFILE (default /etc/pointercal) and reconfigure the filter
 * stack, so the live device is recalibrated without a restart. Ends the
 * calibration sequence.
 * @param indev       tslib input device
 * @param screen_pts  the 5 points the UI asked the user to touch
 * @param raw_pts     the 5 raw points sampled at those locations
 * @return LV_RESULT_OK: saved and applied; LV_RESULT_INVALID: fit or I/O error
 */
lv_result_t lv_tslib_calibration_finish(lv_indev_t * indev, const lv_point_t screen_pts[5],
                                        const lv_point_t raw_pts[5]);

/**
 * Abandon the calibration sequence without saving (e.g. the UI timed out
 * waiting for a point). Ends the sequence and resumes the indev feed.
 * @param indev tslib input device
 */
void lv_tslib_calibration_cancel(lv_indev_t * indev);

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
