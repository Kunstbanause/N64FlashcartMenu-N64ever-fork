/**
 * @file background.c
 * @brief Implementation of the background UI component.
 * @ingroup ui_components
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ui_components.h"
#include "../png_decoder.h"
#include "constants.h"
#include "utils/fs.h"

#define CACHE_METADATA_MAGIC    (0x424B4731)

#define BUILTIN_SPLASH_SPRITE   "rom:/splash.sprite"

/**
 * @brief Structure representing the background component.
 */
typedef struct {
    char *cache_location;      /**< Path to the cache file location. */
    surface_t *image;          /**< Pointer to the loaded image surface (Files background). */
    rspq_block_t *image_display_list; /**< Display list for rendering the image. */
    sprite_t  *splash_sprite;  /**< Built-in N64ever boot splash (rom:/splash.sprite). */
    surface_t *splash_override; /**< User /menu/n64ever/splash.png override (or NULL). */
} component_background_t;

/* ---- Synchronous PNG decode (drives the cooperative decoder to completion) ---- */
static volatile bool      sync_done;
static surface_t         *sync_surface;
static png_err_t          sync_err;

static void sync_png_cb(png_err_t err, surface_t *decoded, void *unused) {
    (void)unused;
    sync_err = err;
    sync_surface = decoded;
    sync_done = true;
}

/* Decode a PNG to a freshly-allocated surface, blocking until done. Returns NULL
   if the file is missing or decoding fails. Safe to call at init (the splash
   blocks input, so the decoder is otherwise idle). */
static surface_t *decode_png_sync(char *path, int max_w, int max_h) {
    if (!file_exists(path)) return NULL;
    sync_done = false; sync_surface = NULL; sync_err = PNG_OK;
    if (png_decoder_start(path, max_w, max_h, sync_png_cb, NULL) != PNG_OK) {
        return NULL;
    }
    int guard = 0;
    while (!sync_done && guard++ < 1000000) {
        png_decoder_poll();
    }
    return (sync_done && sync_err == PNG_OK) ? sync_surface : NULL;
}

/**
 * @brief Structure for background image cache metadata.
 */
typedef struct {
    uint32_t magic;    /**< Magic number for cache validation. */
    uint32_t width;    /**< Image width in pixels. */
    uint32_t height;   /**< Image height in pixels. */
    uint32_t size;     /**< Image buffer size in bytes. */
} cache_metadata_t;

static component_background_t *background = NULL;

/**
 * @brief Load background image from cache file if available.
 *
 * @param c Pointer to the background component structure.
 */
static void load_from_cache(component_background_t *c) {
    if (!c->cache_location) {
        return;
    }

    FILE *f;

    if ((f = fopen(c->cache_location, "rb")) == NULL) {
        return;
    }

    cache_metadata_t cache_metadata;

    if (fread(&cache_metadata, sizeof(cache_metadata), 1, f) != 1) {
        fclose(f);
        return;
    }

    if (cache_metadata.magic != CACHE_METADATA_MAGIC || cache_metadata.width > DISPLAY_WIDTH || cache_metadata.height > DISPLAY_HEIGHT) {
        fclose(f);
        return;
    }

    c->image = calloc(1, sizeof(surface_t));
    *c->image = surface_alloc(FMT_RGBA16, cache_metadata.width, cache_metadata.height);

    if (cache_metadata.size != (c->image->height * c->image->stride)) {
        surface_free(c->image);
        free(c->image);
        c->image = NULL;
        fclose(f);
        return;
    }

    if (fread(c->image->buffer, cache_metadata.size, 1, f) != 1) {
        surface_free(c->image);
        free(c->image);
        c->image = NULL;
    }

    fclose(f);
}

/**
 * @brief Save background image to cache file.
 *
 * @param c Pointer to the background component structure.
 */
static void save_to_cache(component_background_t *c) {
    if (!c->cache_location || !c->image) {
        return;
    }

    FILE *f;

    if ((f = fopen(c->cache_location, "wb")) == NULL) {
        return;
    }

    cache_metadata_t cache_metadata = {
        .magic = CACHE_METADATA_MAGIC,
        .width = c->image->width,
        .height = c->image->height,
        .size = (c->image->height * c->image->stride),
    };

    fwrite(&cache_metadata, sizeof(cache_metadata), 1, f);
    fwrite(c->image->buffer, cache_metadata.size, 1, f);

    fclose(f);
}

/**
 * @brief Prepare the background image for display (darken and center).
 *
 * @param c Pointer to the background component structure.
 */
static void prepare_background(component_background_t *c) {
    if (!c->image || c->image->width == 0 || c->image->height == 0) {
        return;
    }

    uint16_t image_center_x = (c->image->width / 2);
    uint16_t image_center_y = (c->image->height / 2);

    // Prepare display list
    rspq_block_begin();
    rdpq_mode_push();
        if ((c->image->width != DISPLAY_WIDTH) || (c->image->height != DISPLAY_HEIGHT)) {
            rdpq_set_mode_fill(BACKGROUND_EMPTY_COLOR);
        }
        if (c->image->width != DISPLAY_WIDTH) {
            rdpq_fill_rectangle(
                0,
                DISPLAY_CENTER_Y - image_center_y,
                DISPLAY_CENTER_X - image_center_x,
                DISPLAY_CENTER_Y + image_center_y
            );
            rdpq_fill_rectangle(
                DISPLAY_CENTER_X + image_center_x - (c->image->width % 2),
                DISPLAY_CENTER_Y - image_center_y,
                DISPLAY_WIDTH,
                DISPLAY_CENTER_Y + image_center_y
            );
        }
        if (c->image->height != DISPLAY_HEIGHT) {
            rdpq_fill_rectangle(
                0,
                0,
                DISPLAY_WIDTH,
                DISPLAY_CENTER_Y - image_center_y
            );
            rdpq_fill_rectangle(
                0,
                DISPLAY_CENTER_Y + image_center_y - (c->image->height % 2),
                DISPLAY_WIDTH,
                DISPLAY_HEIGHT
            );
        }
        rdpq_set_mode_copy(false);
        rdpq_tex_blit(c->image, DISPLAY_CENTER_X - image_center_x, DISPLAY_CENTER_Y - image_center_y, NULL);
    rdpq_mode_pop();
    c->image_display_list = rspq_block_end();
}

/**
 * @brief Free the display list for the background image.
 *
 * @param arg Pointer to the display list (rspq_block_t *).
 */
static void display_list_free(void *arg) {
    rspq_block_free((rspq_block_t *) (arg));
}

/**
 * @brief Initialize the background component and load from cache.
 *
 * @param cache_location Path to the cache file location.
 */
void ui_components_background_init(char *cache_location, bool decode_custom_splash) {
    if (!background) {
        background = calloc(1, sizeof(component_background_t));
        background->cache_location = strdup(cache_location);

        /* Derive the storage prefix (e.g. "sd:") from the cache path for the
           /menu/custom/ overrides. */
        char prefix[16] = "sd:";
        const char *colon = cache_location ? strchr(cache_location, ':') : NULL;
        if (colon) {
            size_t plen = (size_t)(colon - cache_location + 1);
            if (plen < sizeof(prefix)) { memcpy(prefix, cache_location, plen); prefix[plen] = '\0'; }
        }

        /* A user background.png is no longer rendered by any normal view (the grid,
           browser, file info and credits all clear to a solid colour -- only the boot
           splash uses an image). So we deliberately do NOT decode menu/n64ever/background.png
           at boot: this gracefully ignores a leftover background.png from an older version
           and skips a slow 640x480 PNG decode on every boot. A runtime "set as background"
           still restores from the cache. */
        load_from_cache(background);
        prepare_background(background);

        /* Boot splash: built-in N64ever sprite, overridable by /menu/n64ever/splash.png. */
        background->splash_sprite = sprite_load(BUILTIN_SPLASH_SPRITE);
        /* Only decode the SD splash override when Custom Splash is actually enabled --
           a 640x480 synchronous PNG decode at boot is slow, and pointless if unused. */
        if (decode_custom_splash) {
            char splash_path[160];
            snprintf(splash_path, sizeof(splash_path), "%s/menu/n64ever/splash.png", prefix);
            background->splash_override = decode_png_sync(splash_path, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        }
    }
}

/**
 * @brief Free the background component and its resources.
 */
void ui_components_background_free(void) {
    if (background) {
        if (background->image) {
            surface_free(background->image);
            free(background->image);
            background->image = NULL;
        }
        if (background->image_display_list) {
            rdpq_call_deferred(display_list_free, background->image_display_list);
            background->image_display_list = NULL;
        }
        if (background->splash_sprite) {
            sprite_free(background->splash_sprite);
            background->splash_sprite = NULL;
        }
        if (background->splash_override) {
            surface_free(background->splash_override);
            free(background->splash_override);
            background->splash_override = NULL;
        }
        if (background->cache_location) {
            free(background->cache_location);
        }
        free(background);
        background = NULL;
    }
}

/**
 * @brief Replace the background image and update cache/display list.
 *
 * @param image Pointer to the new background image surface.
 */
void ui_components_background_replace_image(surface_t *image) {
    if (!background) {
        return;
    }

    if (background->image) {
        surface_free(background->image);
        free(background->image);
        background->image = NULL;
    }

    if (background->image_display_list) {
        rdpq_call_deferred(display_list_free, background->image_display_list);
        background->image_display_list = NULL;
    }

    background->image = image;
    save_to_cache(background);
    prepare_background(background);
}

/**
 * @brief Clear to the background color (background image is reserved for splash only).
 */
void ui_components_background_draw(void) {
    rdpq_clear(BACKGROUND_EMPTY_COLOR);
}

/**
 * @brief Draw the background image full-screen as a boot splash overlay.
 *        Falls back to clearing if no image is loaded.
 */
void ui_components_background_draw_splash(bool use_custom) {
    if (!background) {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
        return;
    }

    /* The user splash.png override is only used when Custom Splash is enabled;
       otherwise always fall back to the built-in N64ever sprite. */
    surface_t *img = use_custom ? background->splash_override : NULL;
    sprite_t  *spr = background->splash_sprite;

    if (img || spr) {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
        rdpq_mode_push();
            rdpq_set_mode_copy(false);
            if (img) {
                int x = DISPLAY_CENTER_X - img->width / 2;
                int y = DISPLAY_CENTER_Y - img->height / 2;
                rdpq_tex_blit(img, x, y, NULL);
            } else {
                surface_t s = sprite_get_pixels(spr);
                int x = DISPLAY_CENTER_X - s.width / 2;
                int y = DISPLAY_CENTER_Y - s.height / 2;
                rdpq_tex_blit(&s, x, y, NULL);
            }
        rdpq_mode_pop();
    } else if (background->image_display_list) {
        rspq_block_run(background->image_display_list);
    } else {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
    }
}

/**
 * @brief Draw the user background image full-screen behind a view (Files only).
 *        Falls back to clearing if no background image has been set.
 */
void ui_components_background_draw_image(void) {
    if (background && background->image_display_list) {
        rspq_block_run(background->image_display_list);
    } else {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
    }
}
