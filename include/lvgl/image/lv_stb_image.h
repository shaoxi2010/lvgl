/**
 * @file lv_stb_image.h
 *
 */

#ifndef LV_STB_IMAGE_H
#define LV_STB_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../config/lv_conf_internal.h"
#if LV_USE_STB_IMAGE

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Register the stb_image decoder functions in LVGL.
 * Supports JPEG, PNG, BMP, GIF (first frame), PSD, PNM (binary), HDR and TGA.
 */
void lv_stb_image_init(void);

void lv_stb_image_deinit(void);

/**********************
 *      MACROS
 **********************/

#endif /*LV_STB_IMAGE_H*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LV_STB_IMAGE_H*/
