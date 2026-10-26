/**
 * @file lv_stb_image.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "../lvgl_public.h"

#if LV_USE_STB_IMAGE

#include "lv_image_decoder_private.h"
#include "../core/lv_global.h"

/* stb_image is configured to decode from memory or LVGL's fs callbacks only,
 * and to allocate through LVGL's allocator. Each LV_STB_IMAGE_* config option
 * maps to the corresponding STBI_NO_* macro, so only the enabled formats are
 * compiled in and claimed by the decoder.
 */
#define STBI_NO_STDIO
#define STBI_ASSERT(e)       LV_ASSERT(e)
#define STBI_MALLOC(sz)      lv_malloc(sz)
#define STBI_REALLOC(p, sz)  lv_realloc((p), (sz))
#define STBI_FREE(p)         lv_free(p)

#if !LV_STB_IMAGE_JPEG
#define STBI_NO_JPEG
#endif
#if !LV_STB_IMAGE_PNG
#define STBI_NO_PNG
#endif
#if !LV_STB_IMAGE_BMP
#define STBI_NO_BMP
#endif
#if !LV_STB_IMAGE_GIF
#define STBI_NO_GIF
#endif
#if !LV_STB_IMAGE_PSD
#define STBI_NO_PSD
#endif
#if !LV_STB_IMAGE_PIC
#define STBI_NO_PIC
#endif
#if !LV_STB_IMAGE_PNM
#define STBI_NO_PNM
#endif
#if !LV_STB_IMAGE_HDR
#define STBI_NO_HDR
#endif
#if !LV_STB_IMAGE_TGA
#define STBI_NO_TGA
#endif

/* The SIMD choice is fully explicit: stb_image is prevented from enabling
 * any SIMD path on its own (STBI_NO_SIMD by default). Each choice is only
 * honored when it matches the target architecture, otherwise the build
 * falls back to scalar code with a warning.
 */
#define LV_STB_IMAGE_SIMD_SCALAR 1
#define LV_STB_IMAGE_SIMD_SSE2   2
#define LV_STB_IMAGE_SIMD_NEON   3
#define LV_STB_IMAGE_SIMD_RVV    4

/* Target architecture detection */
#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#define STB_IMAGE_X86_TARGET
#endif
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
#define STB_IMAGE_NEON_TARGET
#endif
#if defined(__riscv_v_intrinsic) || defined(__riscv_vector_071)
#define STB_IMAGE_RVV_TARGET
#endif

#if LV_STB_IMAGE_SIMD == LV_STB_IMAGE_SIMD_SCALAR
#define STBI_NO_SIMD
#elif LV_STB_IMAGE_SIMD == LV_STB_IMAGE_SIMD_SSE2
#if defined(STB_IMAGE_X86_TARGET)
#define STBI_SSE2
#else
#warning "LV_STB_IMAGE_SIMD_SSE2 requires an x86/x64 target, falling back to scalar"
#define STBI_NO_SIMD
#endif
#elif LV_STB_IMAGE_SIMD == LV_STB_IMAGE_SIMD_NEON
#if defined(STB_IMAGE_NEON_TARGET)
#define STBI_NEON
#else
#warning "LV_STB_IMAGE_SIMD_NEON requires an ARM target built with NEON, falling back to scalar"
#define STBI_NO_SIMD
#endif
#elif LV_STB_IMAGE_SIMD == LV_STB_IMAGE_SIMD_RVV
#if defined(STB_IMAGE_RVV_TARGET)
#define STBI_RVV
#else
#warning "LV_STB_IMAGE_SIMD_RVV requires a RISC-V target built with the vector extension, falling back to scalar"
#define STBI_NO_SIMD
#endif
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "../libs/stb/stb_image.h"

/*********************
 *      DEFINES
 *********************/

#define DECODER_NAME    "STB_IMAGE"

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_result_t decoder_info(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc, lv_image_header_t * header);
static lv_result_t decoder_open(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc);
static void decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc);
static int fs_read_cb(void * user, char * data, int size);
static void fs_skip_cb(void * user, int n);
static int fs_eof_cb(void * user);
static void get_fs_callbacks(stbi_io_callbacks * cb);
static bool is_stb_image(const uint8_t * raw_data, size_t len);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Register the stb_image decoder functions in LVGL
 */
void lv_stb_image_init(void)
{
    lv_image_decoder_t * dec = lv_image_decoder_create();
    lv_image_decoder_set_info_cb(dec, decoder_info);
    lv_image_decoder_set_open_cb(dec, decoder_open);
    lv_image_decoder_set_close_cb(dec, decoder_close);

    /*LVGL's in-memory order is B,G,R(,A) (little-endian 0xAARRGGBB etc.),
     *so make every stb decoder emit that order; the SIMD kernels swap the
     *channels for free at the store*/
    stbi_set_output_order(STBI_ORDER_BGR);

    dec->name = DECODER_NAME;
}

void lv_stb_image_deinit(void)
{
    lv_image_decoder_t * dec = NULL;
    while((dec = lv_image_decoder_get_next(dec)) != NULL) {
        if(dec->info_cb == decoder_info) {
            lv_image_decoder_delete(dec);
            break;
        }
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Get info about an image supported by stb_image
 * @param decoder   pointer to the decoder where this function belongs
 * @param dsc       image descriptor containing the source and type of the image and other info.
 * @param header    image information is set in header parameter
 * @return          LV_RESULT_OK: no error; LV_RESULT_INVALID: can't get the info
 */
static lv_result_t decoder_info(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc, lv_image_header_t * header)
{
    LV_UNUSED(decoder);

    lv_image_src_t src_type = dsc->src_type;
    if(src_type != LV_IMAGE_SRC_FILE && src_type != LV_IMAGE_SRC_VARIABLE) return LV_RESULT_INVALID;

    const uint8_t * magic;
    uint8_t buf[8];
    if(src_type == LV_IMAGE_SRC_FILE) {
        uint32_t rn;
        lv_fs_read(&dsc->file, buf, sizeof(buf), &rn);
        if(rn != sizeof(buf)) return LV_RESULT_INVALID;

        /*Rewind for stbi_info_from_callbacks below*/
        lv_fs_seek(&dsc->file, 0, LV_FS_SEEK_SET);

        magic = buf;
    }
    else {
        const lv_image_dsc_t * img_dsc = dsc->src;
        if(img_dsc->data_size < sizeof(buf)) return LV_RESULT_INVALID;
        magic = img_dsc->data;
    }

    if(!is_stb_image(magic, sizeof(buf))) {
        bool is_tga = false;
#ifndef STBI_NO_TGA
        /*TGA has no magic bytes, fall back to the file extension. It's only
         *safe to do for files, a random C array could be mistaken for TGA.*/
        if(src_type == LV_IMAGE_SRC_FILE) {
            const char * ext = lv_fs_get_ext(dsc->src);
            is_tga = lv_strcmp(ext, "tga") == 0;
        }
#endif
        if(!is_tga) return LV_RESULT_INVALID;
    }

    int w, h, comp;
    int ok;
    if(src_type == LV_IMAGE_SRC_FILE) {
        stbi_io_callbacks cb;
        get_fs_callbacks(&cb);
        ok = stbi_info_from_callbacks(&cb, &dsc->file, &w, &h, &comp);
    }
    else {
        const lv_image_dsc_t * img_dsc = dsc->src;
        ok = stbi_info_from_memory(img_dsc->data, (int)img_dsc->data_size, &w, &h, &comp);
    }

    if(!ok) return LV_RESULT_INVALID;

    /*Avoid attempting to load images LVGL cannot use*/
    if(w <= 0 || h <= 0 || w > UINT16_MAX || h > UINT16_MAX) return LV_RESULT_INVALID;

    /*images with an alpha channel decode to ARGB8888, everything else
     *(JPEG, 24-bit BMP/PNG, ...) to RGB888*/
    header->cf = (comp == 4) ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB888;
    header->w = w;
    header->h = h;

    return LV_RESULT_OK;
}

/**
 * Open an image and decode it into dsc.decoded
 * @param decoder   pointer to the decoder where this function belongs
 * @param dsc       decoded image descriptor
 * @return          LV_RESULT_OK: no error; LV_RESULT_INVALID: can't open the image
 */
static lv_result_t decoder_open(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);
    LV_PROFILER_DECODER_BEGIN_TAG("lv_stb_decoder_open");

    int w, h, comp;
    lv_draw_buf_t * decoded = NULL;

    /*Read the header to size the draw buffer, then decode straight into it:
     *JPEG writes its rows in place, every other format is row-copied in by
     *stb itself -- either way no adapter-side copy*/
    if(dsc->src_type == LV_IMAGE_SRC_FILE) {
        lv_fs_file_t f;
        if(lv_fs_open(&f, dsc->src, LV_FS_MODE_RD) != LV_FS_RES_OK) {
            LV_LOG_WARN("File open failed");
            LV_PROFILER_DECODER_END_TAG("lv_stb_decoder_open");
            return LV_RESULT_INVALID;
        }

        stbi_io_callbacks cb;
        get_fs_callbacks(&cb);
        if(stbi_info_from_callbacks(&cb, &f, &w, &h, &comp) && w > 0 && h > 0) {
            decoded = lv_draw_buf_create(w, h, (comp == 4) ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB888, 0);
            if(decoded) {
                int req = (comp == 4) ? 4 : 3;
                lv_fs_seek(&f, 0, LV_FS_SEEK_SET);
                if(stbi_load_from_callbacks_into(&cb, &f, decoded->data, (int)decoded->header.stride,
                                                 &w, &h, &comp, req) == NULL) {
                    lv_draw_buf_destroy(decoded);
                    decoded = NULL;
                }
            }
        }
        lv_fs_close(&f);
    }
    else if(dsc->src_type == LV_IMAGE_SRC_VARIABLE) {
        const lv_image_dsc_t * img_dsc = dsc->src;
        if(stbi_info_from_memory(img_dsc->data, (int)img_dsc->data_size, &w, &h, &comp) && w > 0 && h > 0) {
            decoded = lv_draw_buf_create(w, h, (comp == 4) ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB888, 0);
            if(decoded) {
                int req = (comp == 4) ? 4 : 3;
                if(stbi_load_from_memory_into(img_dsc->data, (int)img_dsc->data_size, decoded->data,
                                              (int)decoded->header.stride, &w, &h, &comp, req) == NULL) {
                    lv_draw_buf_destroy(decoded);
                    decoded = NULL;
                }
            }
        }
    }
    else {
        LV_PROFILER_DECODER_END_TAG("lv_stb_decoder_open");
        return LV_RESULT_INVALID;
    }

    if(decoded == NULL) {
        LV_LOG_WARN("stb_image error: %s", stbi_failure_reason());
        LV_PROFILER_DECODER_END_TAG("lv_stb_decoder_open");
        return LV_RESULT_INVALID;
    }

    lv_draw_buf_t * adjusted = lv_image_decoder_post_process(dsc, decoded);
    if(adjusted == NULL) {
        lv_draw_buf_destroy(decoded);
        LV_PROFILER_DECODER_END_TAG("lv_stb_decoder_open");
        return LV_RESULT_INVALID;
    }

    /*The adjusted draw buffer is newly allocated.*/
    if(adjusted != decoded) {
        lv_draw_buf_destroy(decoded);
        decoded = adjusted;
    }

    dsc->decoded = decoded;

    if(dsc->args.no_cache) {
        LV_PROFILER_DECODER_END_TAG("lv_stb_decoder_open");
        return LV_RESULT_OK;
    }

    /*If the image cache is disabled, just return the decoded image*/
    if(!lv_image_cache_is_enabled()) {
        LV_PROFILER_DECODER_END_TAG("lv_stb_decoder_open");
        return LV_RESULT_OK;
    }

    /*Add the decoded image to the cache*/
    lv_image_cache_data_t search_key;
    search_key.src_type = dsc->src_type;
    search_key.src = dsc->src;
    search_key.slot.size = decoded->data_size;

    lv_cache_entry_t * entry = lv_image_decoder_add_to_cache(decoder, &search_key, decoded, NULL);

    if(entry == NULL) {
        lv_draw_buf_destroy(decoded);
        dsc->decoded = NULL;
        LV_PROFILER_DECODER_END_TAG("lv_stb_decoder_open");
        return LV_RESULT_INVALID;
    }
    dsc->cache_entry = entry;

    LV_PROFILER_DECODER_END_TAG("lv_stb_decoder_open");
    return LV_RESULT_OK;    /*If not returned earlier then it failed*/
}

/**
 * Free the allocated resources
 * @param decoder   pointer to the decoder where this function belongs
 * @param dsc       pointer to a descriptor which describes this decoding session
 */
static void decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);

    if(dsc->args.no_cache ||
       !lv_image_cache_is_enabled()) lv_draw_buf_destroy((lv_draw_buf_t *)dsc->decoded);
}

/**
 * stbi_io_callbacks read callback. A negative size requests a skip of -size bytes.
 */
static int fs_read_cb(void * user, char * data, int size)
{
    lv_fs_file_t * fp = user;

    if(size < 0) {
        lv_fs_seek(fp, (uint32_t)(-size), LV_FS_SEEK_CUR);
        return 1;
    }

    uint32_t rn = 0;
    lv_fs_read(fp, data, (uint32_t)size, &rn);
    return (int)rn;
}

static void fs_skip_cb(void * user, int n)
{
    lv_fs_seek((lv_fs_file_t *)user, (uint32_t)n, LV_FS_SEEK_CUR);
}

static int fs_eof_cb(void * user)
{
    lv_fs_file_t * fp = user;
    uint32_t pos, size;

    lv_fs_tell(fp, &pos);
    lv_fs_seek(fp, 0, LV_FS_SEEK_END);
    lv_fs_tell(fp, &size);
    lv_fs_seek(fp, pos, LV_FS_SEEK_SET);

    return pos >= size;
}

/**
 * Wrap LVGL's fs into the callbacks stb_image requires. All three callbacks
 * must be set, stb_image calls them without NULL checks.
 */
static void get_fs_callbacks(stbi_io_callbacks * cb)
{
    cb->read = fs_read_cb;
    cb->skip = fs_skip_cb;
    cb->eof  = fs_eof_cb;
}

/**
 * Check if the data starts with a magic number of a format stb_image supports
 * in this build. Formats trimmed away by their STBI_NO_* macro are not
 * claimed. TGA is not checked, it has no magic bytes.
 * @param raw_data  pointer to the start of the data
 * @param len       length of raw_data in bytes
 * @return          true: the data looks like a supported image, false: otherwise
 */
static bool is_stb_image(const uint8_t * raw_data, size_t len)
{
    LV_UNUSED(len);

#ifndef STBI_NO_JPEG
    if(raw_data[0] == 0xFF && raw_data[1] == 0xD8 && raw_data[2] == 0xFF) return true;
#endif
#ifndef STBI_NO_PNG
    if(raw_data[0] == 0x89 && raw_data[1] == 0x50 && raw_data[2] == 0x4E && raw_data[3] == 0x47) return true;
#endif
#ifndef STBI_NO_BMP
    if(raw_data[0] == 'B' && raw_data[1] == 'M') return true;
#endif
#ifndef STBI_NO_GIF
    if(raw_data[0] == 'G' && raw_data[1] == 'I' && raw_data[2] == 'F') return true;
#endif
#ifndef STBI_NO_PSD
    if(raw_data[0] == '8' && raw_data[1] == 'B' && raw_data[2] == 'P' && raw_data[3] == 'S') return true;
#endif
#ifndef STBI_NO_HDR
    if(raw_data[0] == '#' && raw_data[1] == '?') return true;
#endif
#ifndef STBI_NO_PNM
    if(raw_data[0] == 'P' && (raw_data[1] == '5' || raw_data[1] == '6')) return true;
#endif

    return false;
}

#endif /*LV_USE_STB_IMAGE*/
