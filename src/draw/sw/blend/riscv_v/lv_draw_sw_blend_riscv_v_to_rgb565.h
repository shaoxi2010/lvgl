/**
 * @file lv_draw_sw_blend_riscv_v_to_rgb565.h
 */

#ifndef LV_DRAW_SW_BLEND_RISCV_V_TO_RGB565_H
#define LV_DRAW_SW_BLEND_RISCV_V_TO_RGB565_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "../../../../lvgl_public.h"
#if LV_USE_DRAW_SW_ASM == LV_DRAW_SW_ASM_RISCV_V

/*********************
 *      DEFINES
 *********************/

/* Color fill to RGB565 */
#ifndef LV_DRAW_SW_COLOR_BLEND_TO_RGB565
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB565(dsc) \
    lv_draw_sw_blend_riscv_v_color_to_rgb565(dsc)
#endif

#ifndef LV_DRAW_SW_COLOR_BLEND_TO_RGB565_WITH_OPA
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB565_WITH_OPA(dsc) \
    lv_draw_sw_blend_riscv_v_color_to_rgb565_with_opa(dsc)
#endif

#ifndef LV_DRAW_SW_COLOR_BLEND_TO_RGB565_WITH_MASK
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB565_WITH_MASK(dsc) \
    lv_draw_sw_blend_riscv_v_color_to_rgb565_with_mask(dsc)
#endif

#ifndef LV_DRAW_SW_COLOR_BLEND_TO_RGB565_MIX_MASK_OPA
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB565_MIX_MASK_OPA(dsc) \
    lv_draw_sw_blend_riscv_v_color_to_rgb565_with_opa_mask(dsc)
#endif

/* RGB565 image blend to RGB565 */
#ifndef LV_DRAW_SW_RGB565_BLEND_NORMAL_TO_RGB565
#define LV_DRAW_SW_RGB565_BLEND_NORMAL_TO_RGB565(dsc) \
    lv_draw_sw_blend_riscv_v_rgb565_to_rgb565(dsc)
#endif

#ifndef LV_DRAW_SW_RGB565_BLEND_NORMAL_TO_RGB565_WITH_OPA
#define LV_DRAW_SW_RGB565_BLEND_NORMAL_TO_RGB565_WITH_OPA(dsc) \
    lv_draw_sw_blend_riscv_v_rgb565_to_rgb565_with_opa(dsc)
#endif

#ifndef LV_DRAW_SW_RGB565_BLEND_NORMAL_TO_RGB565_WITH_MASK
#define LV_DRAW_SW_RGB565_BLEND_NORMAL_TO_RGB565_WITH_MASK(dsc) \
    lv_draw_sw_blend_riscv_v_rgb565_to_rgb565_with_mask(dsc)
#endif

#ifndef LV_DRAW_SW_RGB565_BLEND_NORMAL_TO_RGB565_MIX_MASK_OPA
#define LV_DRAW_SW_RGB565_BLEND_NORMAL_TO_RGB565_MIX_MASK_OPA(dsc) \
    lv_draw_sw_blend_riscv_v_rgb565_to_rgb565_with_opa_mask(dsc)
#endif

/* RGB888/XRGB8888 image blend to RGB565 */
#ifndef LV_DRAW_SW_RGB888_BLEND_NORMAL_TO_RGB565
#define LV_DRAW_SW_RGB888_BLEND_NORMAL_TO_RGB565(dsc, src_px_size) \
    lv_draw_sw_blend_riscv_v_rgb888_to_rgb565(dsc, src_px_size)
#endif

#ifndef LV_DRAW_SW_RGB888_BLEND_NORMAL_TO_RGB565_WITH_OPA
#define LV_DRAW_SW_RGB888_BLEND_NORMAL_TO_RGB565_WITH_OPA(dsc, src_px_size) \
    lv_draw_sw_blend_riscv_v_rgb888_to_rgb565_with_opa(dsc, src_px_size)
#endif

#ifndef LV_DRAW_SW_RGB888_BLEND_NORMAL_TO_RGB565_WITH_MASK
#define LV_DRAW_SW_RGB888_BLEND_NORMAL_TO_RGB565_WITH_MASK(dsc, src_px_size) \
    lv_draw_sw_blend_riscv_v_rgb888_to_rgb565_with_mask(dsc, src_px_size)
#endif

#ifndef LV_DRAW_SW_RGB888_BLEND_NORMAL_TO_RGB565_MIX_MASK_OPA
#define LV_DRAW_SW_RGB888_BLEND_NORMAL_TO_RGB565_MIX_MASK_OPA(dsc, src_px_size) \
    lv_draw_sw_blend_riscv_v_rgb888_to_rgb565_with_opa_mask(dsc, src_px_size)
#endif

/* ARGB8888 image blend to RGB565 */
#ifndef LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_RGB565
#define LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_RGB565(dsc) \
    lv_draw_sw_blend_riscv_v_argb8888_to_rgb565(dsc)
#endif

#ifndef LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_RGB565_WITH_OPA
#define LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_RGB565_WITH_OPA(dsc) \
    lv_draw_sw_blend_riscv_v_argb8888_to_rgb565_with_opa(dsc)
#endif

#ifndef LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_RGB565_WITH_MASK
#define LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_RGB565_WITH_MASK(dsc) \
    lv_draw_sw_blend_riscv_v_argb8888_to_rgb565_with_mask(dsc)
#endif

#ifndef LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_RGB565_MIX_MASK_OPA
#define LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_RGB565_MIX_MASK_OPA(dsc) \
    lv_draw_sw_blend_riscv_v_argb8888_to_rgb565_with_opa_mask(dsc)
#endif

#ifndef LV_DRAW_SW_ARGB8888_PREMULTIPLIED_BLEND_NORMAL_TO_RGB565
#define LV_DRAW_SW_ARGB8888_PREMULTIPLIED_BLEND_NORMAL_TO_RGB565(dsc) \
    lv_draw_sw_blend_riscv_v_argb8888_premultiplied_to_rgb565(dsc)
#endif

/*
 * The premultiplied variants with opa/mask need no division but are left to the
 * generic fallback for now, as are L8/AL88/I1 and RGB565_SWAPPED sources.
 */

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/* Color fill functions */
lv_result_t lv_draw_sw_blend_riscv_v_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t * dsc);
lv_result_t lv_draw_sw_blend_riscv_v_color_to_rgb565_with_opa(lv_draw_sw_blend_fill_dsc_t * dsc);
lv_result_t lv_draw_sw_blend_riscv_v_color_to_rgb565_with_mask(lv_draw_sw_blend_fill_dsc_t * dsc);
lv_result_t lv_draw_sw_blend_riscv_v_color_to_rgb565_with_opa_mask(lv_draw_sw_blend_fill_dsc_t * dsc);

/* RGB565 to RGB565 blend functions */
lv_result_t lv_draw_sw_blend_riscv_v_rgb565_to_rgb565(lv_draw_sw_blend_image_dsc_t * dsc);
lv_result_t lv_draw_sw_blend_riscv_v_rgb565_to_rgb565_with_opa(lv_draw_sw_blend_image_dsc_t * dsc);
lv_result_t lv_draw_sw_blend_riscv_v_rgb565_to_rgb565_with_mask(lv_draw_sw_blend_image_dsc_t * dsc);
lv_result_t lv_draw_sw_blend_riscv_v_rgb565_to_rgb565_with_opa_mask(lv_draw_sw_blend_image_dsc_t * dsc);

/* RGB888/XRGB8888 to RGB565 blend functions */
lv_result_t lv_draw_sw_blend_riscv_v_rgb888_to_rgb565(lv_draw_sw_blend_image_dsc_t * dsc, uint8_t src_px_size);
lv_result_t lv_draw_sw_blend_riscv_v_rgb888_to_rgb565_with_opa(lv_draw_sw_blend_image_dsc_t * dsc,
                                                               uint8_t src_px_size);
lv_result_t lv_draw_sw_blend_riscv_v_rgb888_to_rgb565_with_mask(lv_draw_sw_blend_image_dsc_t * dsc,
                                                                uint8_t src_px_size);
lv_result_t lv_draw_sw_blend_riscv_v_rgb888_to_rgb565_with_opa_mask(lv_draw_sw_blend_image_dsc_t * dsc,
                                                                    uint8_t src_px_size);

/* ARGB8888 to RGB565 blend functions */
lv_result_t lv_draw_sw_blend_riscv_v_argb8888_to_rgb565(lv_draw_sw_blend_image_dsc_t * dsc);
lv_result_t lv_draw_sw_blend_riscv_v_argb8888_to_rgb565_with_opa(lv_draw_sw_blend_image_dsc_t * dsc);
lv_result_t lv_draw_sw_blend_riscv_v_argb8888_to_rgb565_with_mask(lv_draw_sw_blend_image_dsc_t * dsc);
lv_result_t lv_draw_sw_blend_riscv_v_argb8888_to_rgb565_with_opa_mask(lv_draw_sw_blend_image_dsc_t * dsc);
lv_result_t lv_draw_sw_blend_riscv_v_argb8888_premultiplied_to_rgb565(lv_draw_sw_blend_image_dsc_t * dsc);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_DRAW_SW_ASM == LV_DRAW_SW_ASM_RISCV_V */

#ifdef __cplusplus
}
#endif

#endif /* LV_DRAW_SW_BLEND_RISCV_V_TO_RGB565_H */
