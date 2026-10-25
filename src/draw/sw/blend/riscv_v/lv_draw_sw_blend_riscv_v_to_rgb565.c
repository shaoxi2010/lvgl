/**
 * @file lv_draw_sw_blend_riscv_v_to_rgb565.c
 * RGB565 blend implementation for RISC-V Vector Extension (RVV 1.0)
 *
 * Reference: lv_draw_sw_blend_neon_to_rgb565.c (structure)
 *
 * NOTE: All RVV blend logic is inlined to avoid passing vector types as
 * function parameters, which causes complex stack operations that can corrupt
 * the stack.
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_draw_sw_blend_riscv_v_to_rgb565.h"
#if LV_USE_DRAW_SW_ASM == LV_DRAW_SW_ASM_RISCV_V

#include "../lv_draw_sw_blend_private.h"
#include "lv_blend_riscv_v_private.h"

/*********************
 *      DEFINES
 *********************/

/**
 * Check if all 8-bit alpha/mask elements in the current strip are zero, i.e.
 * the per-pixel blend would leave the destination unchanged (mix == 0 selects
 * dst). Deliberately not "<= LV_OPA_MIN": the per-pixel selects only
 * special-case mix == 0, so a looser threshold would make the output depend on
 * the strip's composition.
 */
#define LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_mask8, vl) \
    (__riscv_vfirst_m_b8(__riscv_vmsgtu_vx_u8m1_b8((v_mask8), 0, (vl)), (vl)) < 0)

/**
 * Check if all 8-bit alpha/mask elements in the current strip are fully opaque
 * (>= LV_OPA_MAX), so the source can be stored directly. Matches the
 * mix >= LV_OPA_MAX select of LV_RVV_BLEND_565_SELECT_U16M2.
 */
#define LV_RVV_MASK_IS_ALL_OPAQUE_U8M1(v_mask8, vl) \
    (__riscv_vfirst_m_b8(__riscv_vmsltu_vx_u8m1_b8((v_mask8), LV_OPA_MAX, (vl)), (vl)) < 0)

/**
 * Select destination pixel when mix == 0. Only the zero case is needed: this
 * macro is used on the opa < LV_OPA_MAX paths, where the effective mix
 * (mask * opa) >> 8 can never reach LV_OPA_MAX.
 */
#define LV_RVV_BLEND_565_SELECT_ZERO_U16M2(v_res, v_dst, v_mix16, vl) \
    do { \
        vbool8_t _m_zero = __riscv_vmseq_vx_u16m2_b8((v_mix16), 0, (vl)); \
        (v_res) = __riscv_vmerge_vvm_u16m2((v_res), (v_dst), _m_zero, (vl)); \
    } while(0)

/**
 * Shift the 8-bit R, G, B channels down to 5/6/5 bits while still in e8m1,
 * before widening to e16m2, to halve the 16-bit vector work
 */
#define LV_RVV_RGB888_EXTRACT_565_U16M2(v_r8, v_g8, v_b8, v_r5, v_g6, v_b5, vl) \
    do { \
        (v_r5) = __riscv_vzext_vf2_u16m2(__riscv_vsrl_vx_u8m1((v_r8), 3, (vl)), (vl)); \
        (v_g6) = __riscv_vzext_vf2_u16m2(__riscv_vsrl_vx_u8m1((v_g8), 2, (vl)), (vl)); \
        (v_b5) = __riscv_vzext_vf2_u16m2(__riscv_vsrl_vx_u8m1((v_b8), 3, (vl)), (vl)); \
    } while(0)

/**
 * Pack already-reduced 5/6/5 channels into RGB565: the reductions have already
 * narrowed the channels to their final widths, so no extra masking is needed
 */
#define LV_RVV_PACK_565_U16M2(v_r5, v_g6, v_b5, v_src565, vl) \
    do { \
        vuint16m2_t _g_b = __riscv_vor_vv_u16m2(__riscv_vsll_vx_u16m2((v_g6), 5, (vl)), (v_b5), (vl)); \
        (v_src565) = __riscv_vor_vv_u16m2(__riscv_vsll_vx_u16m2((v_r5), 11, (vl)), _g_b, (vl)); \
    } while(0)

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Fill with solid color (no blending needed, opa >= LV_OPA_MAX)
 */
lv_result_t lv_draw_sw_blend_riscv_v_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa >= LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf == NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const uint16_t color16 = lv_color_to_u16(dsc->color);
    uint16_t * dest_buf = dsc->dest_buf;
    size_t vl;

    /* LMUL=8 for maximum store throughput */
    size_t vlmax = __riscv_vsetvlmax_e16m8();
    vuint16m8_t v_color = __riscv_vmv_v_x_u16m8(color16, vlmax);

    /* 1D contiguous fast path */
    if(dest_stride == w * 2) {
        int32_t total_px = w * h;
        for(int32_t i = 0; i < total_px; i += vl) {
            vl = __riscv_vsetvl_e16m8(total_px - i);
            __riscv_vse16_v_u16m8(&dest_buf[i], v_color, vl);
        }
        return LV_RESULT_OK;
    }

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e16m8(w - x);
            __riscv_vse16_v_u16m8(&dest_buf[x], v_color, vl);
        }
        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
    }

    return LV_RESULT_OK;
}

/**
 * Fill with color and opacity (opa < LV_OPA_MAX)
 * blend formula per channel: result = (fg * opa + bg * (255 - opa)) >> 8
 */
lv_result_t lv_draw_sw_blend_riscv_v_color_to_rgb565_with_opa(lv_draw_sw_blend_fill_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa < LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf == NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const uint8_t opa = dsc->opa;
    const uint16_t color16 = lv_color_to_u16(dsc->color);
    uint16_t * dest_buf = dsc->dest_buf;
    size_t vl;

    if(opa <= LV_OPA_MIN) return LV_RESULT_OK;

    /* Precompute the weighted foreground channels and the inverse opacity in
     * scalar registers, leaving one multiply-add per channel in the loop */
    const uint16_t r_fg = (uint16_t)((color16 >> 11) * opa);
    const uint16_t g_fg = (uint16_t)(((color16 >> 5) & 0x3F) * opa);
    const uint16_t b_fg = (uint16_t)((color16 & 0x1F) * opa);
    const uint16_t inv_opa = (uint16_t)(255 - opa);

    size_t vlmax = __riscv_vsetvlmax_e16m2();
    vuint16m2_t v_src = __riscv_vmv_v_x_u16m2(color16, vlmax);

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e16m2(w - x);

            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);
            vuint16m2_t v_dst_r5, v_dst_g6, v_dst_b5;
            LV_RVV_565_EXTRACT_U16M2(v_dst, v_dst_r5, v_dst_g6, v_dst_b5, vl);

            vuint16m2_t v_acc_r = __riscv_vadd_vx_u16m2(__riscv_vmul_vx_u16m2(v_dst_r5, inv_opa, vl), r_fg, vl);
            vuint16m2_t v_acc_g = __riscv_vadd_vx_u16m2(__riscv_vmul_vx_u16m2(v_dst_g6, inv_opa, vl), g_fg, vl);
            vuint16m2_t v_acc_b = __riscv_vadd_vx_u16m2(__riscv_vmul_vx_u16m2(v_dst_b5, inv_opa, vl), b_fg, vl);
            vuint16m2_t v_res_r = __riscv_vsrl_vx_u16m2(v_acc_r, 8, vl);
            vuint16m2_t v_res_g = __riscv_vsrl_vx_u16m2(v_acc_g, 8, vl);
            vuint16m2_t v_res_b = __riscv_vsrl_vx_u16m2(v_acc_b, 8, vl);

            vuint16m2_t v_res;
            LV_RVV_PACK_565_U16M2(v_res_r, v_res_g, v_res_b, v_res, vl);
            LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src, v_dst, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
    }

    return LV_RESULT_OK;
}

/**
 * Fill with color and per-pixel mask (opa >= LV_OPA_MAX)
 */
lv_result_t lv_draw_sw_blend_riscv_v_color_to_rgb565_with_mask(lv_draw_sw_blend_fill_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa >= LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf != NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t mask_stride = dsc->mask_stride;
    const uint16_t color16 = lv_color_to_u16(dsc->color);
    const uint8_t * mask_buf = dsc->mask_buf;
    uint16_t * dest_buf = dsc->dest_buf;
    size_t vl;

    size_t vlmax = __riscv_vsetvlmax_e16m2();
    vuint16m2_t v_src = __riscv_vmv_v_x_u16m2(color16, vlmax);
    vuint16m2_t v_src_r5 = __riscv_vmv_v_x_u16m2(color16 >> 11, vlmax);
    vuint16m2_t v_src_g6 = __riscv_vmv_v_x_u16m2((color16 >> 5) & 0x3F, vlmax);
    vuint16m2_t v_src_b5 = __riscv_vmv_v_x_u16m2(color16 & 0x1F, vlmax);

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e8m1(w - x);

            vuint8m1_t v_mask8 = __riscv_vle8_v_u8m1(&mask_buf[x], vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_mask8, vl)) continue;
            if(LV_RVV_MASK_IS_ALL_OPAQUE_U8M1(v_mask8, vl)) {
                __riscv_vse16_v_u16m2(&dest_buf[x], v_src, vl);
                continue;
            }

            vuint16m2_t v_mix16 = __riscv_vzext_vf2_u16m2(v_mask8, vl);
            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);

            vuint16m2_t v_res;
            LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl);
            LV_RVV_BLEND_565_SELECT_U16M2(v_res, v_src, v_dst, v_mix16, vl);
            LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src, v_dst, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        mask_buf += mask_stride;
    }

    return LV_RESULT_OK;
}

/**
 * Fill with color, opacity, and per-pixel mask
 * Effective mix = (mask * opa) >> 8
 */
lv_result_t lv_draw_sw_blend_riscv_v_color_to_rgb565_with_opa_mask(lv_draw_sw_blend_fill_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa < LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf != NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t mask_stride = dsc->mask_stride;
    const uint8_t opa = dsc->opa;
    const uint16_t color16 = lv_color_to_u16(dsc->color);
    const uint8_t * mask_buf = dsc->mask_buf;
    uint16_t * dest_buf = dsc->dest_buf;
    size_t vl;

    if(opa <= LV_OPA_MIN) return LV_RESULT_OK;

    size_t vlmax = __riscv_vsetvlmax_e16m2();
    vuint16m2_t v_src = __riscv_vmv_v_x_u16m2(color16, vlmax);
    vuint16m2_t v_src_r5 = __riscv_vmv_v_x_u16m2(color16 >> 11, vlmax);
    vuint16m2_t v_src_g6 = __riscv_vmv_v_x_u16m2((color16 >> 5) & 0x3F, vlmax);
    vuint16m2_t v_src_b5 = __riscv_vmv_v_x_u16m2(color16 & 0x1F, vlmax);

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e8m1(w - x);

            vuint8m1_t v_mask8 = __riscv_vle8_v_u8m1(&mask_buf[x], vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_mask8, vl)) continue;

            /* Widen and shift in one go: (mask * opa) >> 8 */
            vuint16m2_t v_mix16 = __riscv_vsrl_vx_u16m2(__riscv_vwmulu_vx_u16m2(v_mask8, opa, vl), 8, vl);
            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);

            vuint16m2_t v_res;
            LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl);
            LV_RVV_BLEND_565_SELECT_ZERO_U16M2(v_res, v_dst, v_mix16, vl);
            LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src, v_dst, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        mask_buf += mask_stride;
    }

    return LV_RESULT_OK;
}

/**********************
 *  RGB565 TO RGB565 BLEND FUNCTIONS
 **********************/

/**
 * RGB565 to RGB565 simple copy (no blending, opa >= LV_OPA_MAX)
 */
lv_result_t lv_draw_sw_blend_riscv_v_rgb565_to_rgb565(lv_draw_sw_blend_image_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa >= LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf == NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint16_t * src_buf = dsc->src_buf;
    size_t vl;

    const int32_t row_bytes = w * 2;

    /* 1D contiguous fast path */
    if(dest_stride == row_bytes && src_stride == row_bytes) {
        const int32_t total_bytes = row_bytes * h;
        const uint8_t * s8 = (const uint8_t *)src_buf;
        uint8_t * d8 = (uint8_t *)dest_buf;
        for(int32_t i = 0; i < total_bytes; i += vl) {
            vl = __riscv_vsetvl_e8m8(total_bytes - i);
            vuint8m8_t v_data = __riscv_vle8_v_u8m8(s8 + i, vl);
            __riscv_vse8_v_u8m8(d8 + i, v_data, vl);
        }
        return LV_RESULT_OK;
    }

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < row_bytes; x += vl) {
            vl = __riscv_vsetvl_e8m8(row_bytes - x);
            vuint8m8_t v_data = __riscv_vle8_v_u8m8((const uint8_t *)src_buf + x, vl);
            __riscv_vse8_v_u8m8((uint8_t *)dest_buf + x, v_data, vl);
        }
        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        src_buf = drawbuf_next_row(src_buf, src_stride);
    }

    return LV_RESULT_OK;
}

/**
 * RGB565 to RGB565 with opacity
 * blend formula per channel: result = (src * opa + dst * (255 - opa)) >> 8
 */
lv_result_t lv_draw_sw_blend_riscv_v_rgb565_to_rgb565_with_opa(lv_draw_sw_blend_image_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa < LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf == NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    const uint8_t opa = dsc->opa;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint16_t * src_buf = dsc->src_buf;
    size_t vl;

    if(opa <= LV_OPA_MIN) return LV_RESULT_OK;

    size_t vlmax = __riscv_vsetvlmax_e16m2();
    vuint16m2_t v_mix16 = __riscv_vmv_v_x_u16m2(opa, vlmax);

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e16m2(w - x);

            vuint16m2_t v_src = __riscv_vle16_v_u16m2(&src_buf[x], vl);
            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);

            vuint16m2_t v_src_r5, v_src_g6, v_src_b5;
            LV_RVV_565_EXTRACT_U16M2(v_src, v_src_r5, v_src_g6, v_src_b5, vl);

            vuint16m2_t v_res;
            LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl);
            LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src, v_dst, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        src_buf = drawbuf_next_row(src_buf, src_stride);
    }

    return LV_RESULT_OK;
}

/**
 * RGB565 to RGB565 with per-pixel mask
 * blend formula per channel: result = (src * mask + dst * (255 - mask)) >> 8
 */
lv_result_t lv_draw_sw_blend_riscv_v_rgb565_to_rgb565_with_mask(lv_draw_sw_blend_image_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa >= LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf != NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    const int32_t mask_stride = dsc->mask_stride;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint16_t * src_buf = dsc->src_buf;
    const uint8_t * mask_buf = dsc->mask_buf;
    size_t vl;

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e8m1(w - x);

            vuint8m1_t v_mask8 = __riscv_vle8_v_u8m1(&mask_buf[x], vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_mask8, vl)) continue;

            vuint16m2_t v_src = __riscv_vle16_v_u16m2(&src_buf[x], vl);
            if(LV_RVV_MASK_IS_ALL_OPAQUE_U8M1(v_mask8, vl)) {
                __riscv_vse16_v_u16m2(&dest_buf[x], v_src, vl);
                continue;
            }

            vuint16m2_t v_mix16 = __riscv_vzext_vf2_u16m2(v_mask8, vl);
            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);

            vuint16m2_t v_src_r5, v_src_g6, v_src_b5;
            LV_RVV_565_EXTRACT_U16M2(v_src, v_src_r5, v_src_g6, v_src_b5, vl);

            vuint16m2_t v_res;
            LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl);
            LV_RVV_BLEND_565_SELECT_U16M2(v_res, v_src, v_dst, v_mix16, vl);
            LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src, v_dst, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        src_buf = drawbuf_next_row(src_buf, src_stride);
        mask_buf += mask_stride;
    }

    return LV_RESULT_OK;
}

/**
 * RGB565 to RGB565 with opacity and per-pixel mask
 * effective mix = (mask * opa) >> 8
 */
lv_result_t lv_draw_sw_blend_riscv_v_rgb565_to_rgb565_with_opa_mask(lv_draw_sw_blend_image_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa < LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf != NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    const int32_t mask_stride = dsc->mask_stride;
    const uint8_t opa = dsc->opa;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint16_t * src_buf = dsc->src_buf;
    const uint8_t * mask_buf = dsc->mask_buf;
    size_t vl;

    if(opa <= LV_OPA_MIN) return LV_RESULT_OK;

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e8m1(w - x);

            vuint8m1_t v_mask8 = __riscv_vle8_v_u8m1(&mask_buf[x], vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_mask8, vl)) continue;

            vuint16m2_t v_mix16 = __riscv_vsrl_vx_u16m2(__riscv_vwmulu_vx_u16m2(v_mask8, opa, vl), 8, vl);
            vuint16m2_t v_src = __riscv_vle16_v_u16m2(&src_buf[x], vl);
            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);

            vuint16m2_t v_src_r5, v_src_g6, v_src_b5;
            LV_RVV_565_EXTRACT_U16M2(v_src, v_src_r5, v_src_g6, v_src_b5, vl);

            vuint16m2_t v_res;
            LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl);
            LV_RVV_BLEND_565_SELECT_ZERO_U16M2(v_res, v_dst, v_mix16, vl);
            LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src, v_dst, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        src_buf = drawbuf_next_row(src_buf, src_stride);
        mask_buf += mask_stride;
    }

    return LV_RESULT_OK;
}

/**********************
 *  RGB888/XRGB8888 TO RGB565 BLEND FUNCTIONS
 **********************/

#define LV_RVV_RGB888_COPY_LOOP(LOAD_MACRO) \
    do { \
        for(int32_t y = 0; y < h; y++) { \
            for(int32_t x = 0; x < w; x += vl) { \
                vl = __riscv_vsetvl_e8m1(w - x); \
                vuint8m1_t v_src_b, v_src_g, v_src_r; \
                LOAD_MACRO(src_buf, x, v_src_b, v_src_g, v_src_r, vl); \
                vuint16m2_t v_src_r5, v_src_g6, v_src_b5, v_res; \
                LV_RVV_RGB888_EXTRACT_565_U16M2(v_src_r, v_src_g, v_src_b, v_src_r5, v_src_g6, v_src_b5, vl); \
                LV_RVV_PACK_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_res, vl); \
                __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl); \
            } \
            dest_buf = drawbuf_next_row(dest_buf, dest_stride); \
            src_buf = drawbuf_next_row(src_buf, src_stride); \
        } \
    } while(0)

/**
 * RGB888/XRGB8888 to RGB565 simple copy (no blending, opa >= LV_OPA_MAX)
 */
lv_result_t lv_draw_sw_blend_riscv_v_rgb888_to_rgb565(lv_draw_sw_blend_image_dsc_t * dsc,
                                                      uint8_t src_px_size)
{
    LV_ASSERT(src_px_size == 3 || src_px_size == 4);
    LV_ASSERT(dsc->opa >= LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf == NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint8_t * src_buf = dsc->src_buf;
    size_t vl;

    if(src_px_size == 3) {
        LV_RVV_RGB888_COPY_LOOP(LV_RVV_LOAD_RGB888_U8M1);
    }
    else {
        LV_RVV_RGB888_COPY_LOOP(LV_RVV_LOAD_XRGB8888_U8M1);
    }

    return LV_RESULT_OK;
}

#define LV_RVV_RGB888_OPA_LOOP(LOAD_MACRO) \
    do { \
        for(int32_t y = 0; y < h; y++) { \
            for(int32_t x = 0; x < w; x += vl) { \
                vl = __riscv_vsetvl_e8m1(w - x); \
                vuint8m1_t v_src_b, v_src_g, v_src_r; \
                LOAD_MACRO(src_buf, x, v_src_b, v_src_g, v_src_r, vl); \
                vuint16m2_t v_src_r5, v_src_g6, v_src_b5, v_src565; \
                LV_RVV_RGB888_EXTRACT_565_U16M2(v_src_r, v_src_g, v_src_b, v_src_r5, v_src_g6, v_src_b5, vl); \
                LV_RVV_PACK_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_src565, vl); \
                vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl); \
                vuint16m2_t v_res; \
                LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl); \
                LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src565, v_dst, vl); \
                __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl); \
            } \
            dest_buf = drawbuf_next_row(dest_buf, dest_stride); \
            src_buf = drawbuf_next_row(src_buf, src_stride); \
        } \
    } while(0)

/**
 * RGB888/XRGB8888 to RGB565 with opacity
 */
lv_result_t lv_draw_sw_blend_riscv_v_rgb888_to_rgb565_with_opa(lv_draw_sw_blend_image_dsc_t * dsc,
                                                               uint8_t src_px_size)
{
    LV_ASSERT(src_px_size == 3 || src_px_size == 4);
    LV_ASSERT(dsc->opa < LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf == NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    const uint8_t opa = dsc->opa;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint8_t * src_buf = dsc->src_buf;
    size_t vl;

    if(opa <= LV_OPA_MIN) return LV_RESULT_OK;

    size_t vlmax = __riscv_vsetvlmax_e16m2();
    vuint16m2_t v_mix16 = __riscv_vmv_v_x_u16m2(opa, vlmax);

    if(src_px_size == 3) {
        LV_RVV_RGB888_OPA_LOOP(LV_RVV_LOAD_RGB888_U8M1);
    }
    else {
        LV_RVV_RGB888_OPA_LOOP(LV_RVV_LOAD_XRGB8888_U8M1);
    }

    return LV_RESULT_OK;
}

#define LV_RVV_RGB888_MASK_LOOP(LOAD_MACRO) \
    do { \
        for(int32_t y = 0; y < h; y++) { \
            for(int32_t x = 0; x < w; x += vl) { \
                vl = __riscv_vsetvl_e8m1(w - x); \
                vuint8m1_t v_mask8 = __riscv_vle8_v_u8m1(&mask_buf[x], vl); \
                if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_mask8, vl)) continue; \
                vuint8m1_t v_src_b, v_src_g, v_src_r; \
                LOAD_MACRO(src_buf, x, v_src_b, v_src_g, v_src_r, vl); \
                vuint16m2_t v_src_r5, v_src_g6, v_src_b5, v_src565; \
                LV_RVV_RGB888_EXTRACT_565_U16M2(v_src_r, v_src_g, v_src_b, v_src_r5, v_src_g6, v_src_b5, vl); \
                LV_RVV_PACK_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_src565, vl); \
                if(LV_RVV_MASK_IS_ALL_OPAQUE_U8M1(v_mask8, vl)) { \
                    __riscv_vse16_v_u16m2(&dest_buf[x], v_src565, vl); \
                    continue; \
                } \
                vuint16m2_t v_mix16 = __riscv_vzext_vf2_u16m2(v_mask8, vl); \
                vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl); \
                vuint16m2_t v_res; \
                LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl); \
                LV_RVV_BLEND_565_SELECT_U16M2(v_res, v_src565, v_dst, v_mix16, vl); \
                LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src565, v_dst, vl); \
                __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl); \
            } \
            dest_buf = drawbuf_next_row(dest_buf, dest_stride); \
            src_buf = drawbuf_next_row(src_buf, src_stride); \
            mask_buf += mask_stride; \
        } \
    } while(0)

/**
 * RGB888/XRGB8888 to RGB565 with per-pixel mask
 */
lv_result_t lv_draw_sw_blend_riscv_v_rgb888_to_rgb565_with_mask(lv_draw_sw_blend_image_dsc_t * dsc,
                                                                uint8_t src_px_size)
{
    LV_ASSERT(src_px_size == 3 || src_px_size == 4);
    LV_ASSERT(dsc->opa >= LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf != NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    const int32_t mask_stride = dsc->mask_stride;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint8_t * src_buf = dsc->src_buf;
    const uint8_t * mask_buf = dsc->mask_buf;
    size_t vl;

    if(src_px_size == 3) {
        LV_RVV_RGB888_MASK_LOOP(LV_RVV_LOAD_RGB888_U8M1);
    }
    else {
        LV_RVV_RGB888_MASK_LOOP(LV_RVV_LOAD_XRGB8888_U8M1);
    }

    return LV_RESULT_OK;
}

#define LV_RVV_RGB888_OPA_MASK_LOOP(LOAD_MACRO) \
    do { \
        for(int32_t y = 0; y < h; y++) { \
            for(int32_t x = 0; x < w; x += vl) { \
                vl = __riscv_vsetvl_e8m1(w - x); \
                vuint8m1_t v_mask8 = __riscv_vle8_v_u8m1(&mask_buf[x], vl); \
                if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_mask8, vl)) continue; \
                vuint8m1_t v_src_b, v_src_g, v_src_r; \
                LOAD_MACRO(src_buf, x, v_src_b, v_src_g, v_src_r, vl); \
                vuint16m2_t v_src_r5, v_src_g6, v_src_b5, v_src565; \
                LV_RVV_RGB888_EXTRACT_565_U16M2(v_src_r, v_src_g, v_src_b, v_src_r5, v_src_g6, v_src_b5, vl); \
                LV_RVV_PACK_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_src565, vl); \
                vuint16m2_t v_mix16 = __riscv_vsrl_vx_u16m2( \
                                                             __riscv_vwmulu_vx_u16m2(v_mask8, opa, vl), 8, vl); \
                vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl); \
                vuint16m2_t v_res; \
                LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl); \
                LV_RVV_BLEND_565_SELECT_ZERO_U16M2(v_res, v_dst, v_mix16, vl); \
                LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src565, v_dst, vl); \
                __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl); \
            } \
            dest_buf = drawbuf_next_row(dest_buf, dest_stride); \
            src_buf = drawbuf_next_row(src_buf, src_stride); \
            mask_buf += mask_stride; \
        } \
    } while(0)

/**
 * RGB888/XRGB8888 to RGB565 with opacity and per-pixel mask
 */
lv_result_t lv_draw_sw_blend_riscv_v_rgb888_to_rgb565_with_opa_mask(lv_draw_sw_blend_image_dsc_t * dsc,
                                                                    uint8_t src_px_size)
{
    LV_ASSERT(src_px_size == 3 || src_px_size == 4);
    LV_ASSERT(dsc->opa < LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf != NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    const int32_t mask_stride = dsc->mask_stride;
    const uint8_t opa = dsc->opa;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint8_t * src_buf = dsc->src_buf;
    const uint8_t * mask_buf = dsc->mask_buf;
    size_t vl;

    if(opa <= LV_OPA_MIN) return LV_RESULT_OK;

    if(src_px_size == 3) {
        LV_RVV_RGB888_OPA_MASK_LOOP(LV_RVV_LOAD_RGB888_U8M1);
    }
    else {
        LV_RVV_RGB888_OPA_MASK_LOOP(LV_RVV_LOAD_XRGB8888_U8M1);
    }

    return LV_RESULT_OK;
}

#undef LV_RVV_RGB888_COPY_LOOP
#undef LV_RVV_RGB888_OPA_LOOP
#undef LV_RVV_RGB888_MASK_LOOP
#undef LV_RVV_RGB888_OPA_MASK_LOOP

/**********************
 *  ARGB8888 TO RGB565 BLEND FUNCTIONS
 **********************/

/**
 * ARGB8888 to RGB565 blend using source alpha
 */
lv_result_t lv_draw_sw_blend_riscv_v_argb8888_to_rgb565(lv_draw_sw_blend_image_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa >= LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf == NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint8_t * src_buf = dsc->src_buf;
    size_t vl;

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e8m1(w - x);

            vuint8m1_t v_src_b, v_src_g, v_src_r, v_src_a;
            LV_RVV_LOAD_ARGB8888_U8M1(src_buf, x, v_src_b, v_src_g, v_src_r, v_src_a, vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_src_a, vl)) continue;

            vuint16m2_t v_src_r5, v_src_g6, v_src_b5, v_src565;
            LV_RVV_RGB888_EXTRACT_565_U16M2(v_src_r, v_src_g, v_src_b, v_src_r5, v_src_g6, v_src_b5, vl);
            LV_RVV_PACK_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_src565, vl);

            if(LV_RVV_MASK_IS_ALL_OPAQUE_U8M1(v_src_a, vl)) {
                __riscv_vse16_v_u16m2(&dest_buf[x], v_src565, vl);
                continue;
            }

            vuint16m2_t v_mix16 = __riscv_vzext_vf2_u16m2(v_src_a, vl);
            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);

            vuint16m2_t v_res;
            LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl);
            LV_RVV_BLEND_565_SELECT_U16M2(v_res, v_src565, v_dst, v_mix16, vl);
            LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src565, v_dst, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        src_buf = drawbuf_next_row(src_buf, src_stride);
    }

    return LV_RESULT_OK;
}

/**
 * ARGB8888 to RGB565 with global opacity
 * effective_alpha = (src_alpha * opa) >> 8
 */
lv_result_t lv_draw_sw_blend_riscv_v_argb8888_to_rgb565_with_opa(lv_draw_sw_blend_image_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa < LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf == NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    const uint8_t opa = dsc->opa;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint8_t * src_buf = dsc->src_buf;
    size_t vl;

    if(opa <= LV_OPA_MIN) return LV_RESULT_OK;

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e8m1(w - x);

            vuint8m1_t v_src_b, v_src_g, v_src_r, v_src_a;
            LV_RVV_LOAD_ARGB8888_U8M1(src_buf, x, v_src_b, v_src_g, v_src_r, v_src_a, vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_src_a, vl)) continue;

            vuint16m2_t v_src_r5, v_src_g6, v_src_b5, v_src565;
            LV_RVV_RGB888_EXTRACT_565_U16M2(v_src_r, v_src_g, v_src_b, v_src_r5, v_src_g6, v_src_b5, vl);
            LV_RVV_PACK_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_src565, vl);

            vuint16m2_t v_mix16 = __riscv_vsrl_vx_u16m2(__riscv_vwmulu_vx_u16m2(v_src_a, opa, vl), 8, vl);
            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);

            vuint16m2_t v_res;
            LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl);
            LV_RVV_BLEND_565_SELECT_ZERO_U16M2(v_res, v_dst, v_mix16, vl);
            LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src565, v_dst, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        src_buf = drawbuf_next_row(src_buf, src_stride);
    }

    return LV_RESULT_OK;
}

/**
 * ARGB8888 to RGB565 with per-pixel mask
 * effective_alpha = (src_alpha * mask) >> 8
 */
lv_result_t lv_draw_sw_blend_riscv_v_argb8888_to_rgb565_with_mask(lv_draw_sw_blend_image_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa >= LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf != NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    const int32_t mask_stride = dsc->mask_stride;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint8_t * src_buf = dsc->src_buf;
    const uint8_t * mask_buf = dsc->mask_buf;
    size_t vl;

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e8m1(w - x);

            /* Check the mask before loading the 4-channel source */
            vuint8m1_t v_mask8 = __riscv_vle8_v_u8m1(&mask_buf[x], vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_mask8, vl)) continue;

            vuint8m1_t v_src_b, v_src_g, v_src_r, v_src_a;
            LV_RVV_LOAD_ARGB8888_U8M1(src_buf, x, v_src_b, v_src_g, v_src_r, v_src_a, vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_src_a, vl)) continue;

            vuint16m2_t v_src_r5, v_src_g6, v_src_b5, v_src565;
            LV_RVV_RGB888_EXTRACT_565_U16M2(v_src_r, v_src_g, v_src_b, v_src_r5, v_src_g6, v_src_b5, vl);
            LV_RVV_PACK_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_src565, vl);

            /* Widening multiply straight from the 8-bit operands */
            vuint16m2_t v_mix16 = __riscv_vsrl_vx_u16m2(__riscv_vwmulu_vv_u16m2(v_src_a, v_mask8, vl), 8, vl);
            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);

            vuint16m2_t v_res;
            LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl);
            LV_RVV_BLEND_565_SELECT_U16M2(v_res, v_src565, v_dst, v_mix16, vl);
            LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src565, v_dst, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        src_buf = drawbuf_next_row(src_buf, src_stride);
        mask_buf += mask_stride;
    }

    return LV_RESULT_OK;
}

/**
 * ARGB8888 to RGB565 with opacity and per-pixel mask
 * effective_alpha = (src_alpha * mask * opa) >> 16
 */
lv_result_t lv_draw_sw_blend_riscv_v_argb8888_to_rgb565_with_opa_mask(lv_draw_sw_blend_image_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa < LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf != NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    const int32_t mask_stride = dsc->mask_stride;
    const uint8_t opa = dsc->opa;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint8_t * src_buf = dsc->src_buf;
    const uint8_t * mask_buf = dsc->mask_buf;
    size_t vl;

    if(opa <= LV_OPA_MIN) return LV_RESULT_OK;

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e8m1(w - x);

            vuint8m1_t v_mask8 = __riscv_vle8_v_u8m1(&mask_buf[x], vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_mask8, vl)) continue;

            vuint8m1_t v_src_b, v_src_g, v_src_r, v_src_a;
            LV_RVV_LOAD_ARGB8888_U8M1(src_buf, x, v_src_b, v_src_g, v_src_r, v_src_a, vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_src_a, vl)) continue;

            vuint16m2_t v_src_r5, v_src_g6, v_src_b5, v_src565;
            LV_RVV_RGB888_EXTRACT_565_U16M2(v_src_r, v_src_g, v_src_b, v_src_r5, v_src_g6, v_src_b5, vl);
            LV_RVV_PACK_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_src565, vl);

            /* (src_alpha * mask * opa) >> 16 in two multiplies: the widened
             * product fits 16 bits (255 * 255 = 65025), so the second one only
             * needs its high half */
            vuint16m2_t v_prod16 = __riscv_vwmulu_vv_u16m2(v_src_a, v_mask8, vl);
            vuint16m2_t v_mix16 = __riscv_vmulhu_vx_u16m2(v_prod16, opa, vl);

            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);

            vuint16m2_t v_res;
            LV_RVV_BLEND_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_dst, v_mix16, v_res, vl);
            LV_RVV_BLEND_565_SELECT_ZERO_U16M2(v_res, v_dst, v_mix16, vl);
            LV_RVV_BLEND_565_SELECT_EQ_U16M2(v_res, v_src565, v_dst, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        src_buf = drawbuf_next_row(src_buf, src_stride);
        mask_buf += mask_stride;
    }

    return LV_RESULT_OK;
}

/**
 * ARGB8888 premultiplied to RGB565
 * result = min(src_premul + ((dst * (255 - src_alpha)) >> 8), max_channel_val)
 */
lv_result_t lv_draw_sw_blend_riscv_v_argb8888_premultiplied_to_rgb565(lv_draw_sw_blend_image_dsc_t * dsc)
{
    LV_ASSERT(dsc->opa >= LV_OPA_MAX);
    LV_ASSERT(dsc->mask_buf == NULL);

    const int32_t w = dsc->dest_w;
    const int32_t h = dsc->dest_h;
    const int32_t dest_stride = dsc->dest_stride;
    const int32_t src_stride = dsc->src_stride;
    uint16_t * dest_buf = dsc->dest_buf;
    const uint8_t * src_buf = dsc->src_buf;
    size_t vl;

    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x += vl) {
            vl = __riscv_vsetvl_e8m1(w - x);

            vuint8m1_t v_src_b, v_src_g, v_src_r, v_src_a;
            LV_RVV_LOAD_ARGB8888_U8M1(src_buf, x, v_src_b, v_src_g, v_src_r, v_src_a, vl);
            if(LV_RVV_MASK_IS_ALL_ZERO_U8M1(v_src_a, vl)) continue;

            vuint16m2_t v_src_r5, v_src_g6, v_src_b5, v_src565;
            LV_RVV_RGB888_EXTRACT_565_U16M2(v_src_r, v_src_g, v_src_b, v_src_r5, v_src_g6, v_src_b5, vl);
            LV_RVV_PACK_565_U16M2(v_src_r5, v_src_g6, v_src_b5, v_src565, vl);

            if(LV_RVV_MASK_IS_ALL_OPAQUE_U8M1(v_src_a, vl)) {
                __riscv_vse16_v_u16m2(&dest_buf[x], v_src565, vl);
                continue;
            }

            vuint16m2_t v_dst = __riscv_vle16_v_u16m2(&dest_buf[x], vl);
            vuint16m2_t v_dst_r5, v_dst_g6, v_dst_b5;
            LV_RVV_565_EXTRACT_U16M2(v_dst, v_dst_r5, v_dst_g6, v_dst_b5, vl);

            vuint16m2_t v_a16 = __riscv_vzext_vf2_u16m2(v_src_a, vl);
            vuint16m2_t v_inv_a = __riscv_vrsub_vx_u16m2(v_a16, 255, vl);

            /* Clamp to the channel width: a no-op on valid premultiplied data,
             * it only prevents channel bleed on malformed input */
            vuint16m2_t v_bg_r = __riscv_vsrl_vx_u16m2(__riscv_vmul_vv_u16m2(v_dst_r5, v_inv_a, vl), 8, vl);
            vuint16m2_t v_bg_g = __riscv_vsrl_vx_u16m2(__riscv_vmul_vv_u16m2(v_dst_g6, v_inv_a, vl), 8, vl);
            vuint16m2_t v_bg_b = __riscv_vsrl_vx_u16m2(__riscv_vmul_vv_u16m2(v_dst_b5, v_inv_a, vl), 8, vl);
            vuint16m2_t v_res_r = __riscv_vminu_vx_u16m2(__riscv_vadd_vv_u16m2(v_src_r5, v_bg_r, vl), 31, vl);
            vuint16m2_t v_res_g = __riscv_vminu_vx_u16m2(__riscv_vadd_vv_u16m2(v_src_g6, v_bg_g, vl), 63, vl);
            vuint16m2_t v_res_b = __riscv_vminu_vx_u16m2(__riscv_vadd_vv_u16m2(v_src_b5, v_bg_b, vl), 31, vl);

            vuint16m2_t v_res;
            LV_RVV_PACK_565_U16M2(v_res_r, v_res_g, v_res_b, v_res, vl);
            LV_RVV_BLEND_565_SELECT_U16M2(v_res, v_src565, v_dst, v_a16, vl);

            __riscv_vse16_v_u16m2(&dest_buf[x], v_res, vl);
        }

        dest_buf = drawbuf_next_row(dest_buf, dest_stride);
        src_buf = drawbuf_next_row(src_buf, src_stride);
    }

    return LV_RESULT_OK;
}

#endif /* LV_USE_DRAW_SW_ASM == LV_DRAW_SW_ASM_RISCV_V */
