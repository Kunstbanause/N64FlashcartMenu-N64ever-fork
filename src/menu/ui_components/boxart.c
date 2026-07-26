/**
 * @file boxart.c
 * @brief Implementation of the boxart UI component.
 * @ingroup ui_components
 */

#include <stdlib.h>
#include <string.h>

#include "../ui_components.h"
#include "../path.h"
#include "../png_decoder.h"
#include "../settings.h"
#include "../game_special.h"
#include "constants.h"
#include "utils/fs.h"

#define OLD_BOXART_DIRECTORY       "menu/boxart"
#define METADATA_BASE_DIRECTORY    "menu/metadata"
#define HOMEBREW_ID_SUBDIRECTORY   "homebrew"

/* Path and source-PNG size for the cache write pending after a decode completes */
static char     pending_cache_path[256];
static uint32_t pending_png_size;

/* Cache file header written/read before the raw pixel data.
   png_size is the byte length of the source PNG; if it changes the cache is stale. */
typedef struct {
    uint32_t png_size;
    uint16_t width;
    uint16_t height;
    uint32_t stride;
} boxart_cache_header_t;

/* Derive cache path by replacing trailing ".png" with ".cache" */
static void make_cache_path(char *out, size_t out_size, const char *png_path) {
    strncpy(out, png_path, out_size - 1);
    out[out_size - 1] = '\0';
    size_t len = strlen(out);
    if (len > 4 && memcmp(out + len - 4, ".png", 4) == 0) {
        if (len - 4 + 6 < out_size) {
            memcpy(out + len - 4, ".cache", 7); /* includes '\0' */
        }
    }
}

/* Get file size in bytes (returns 0 on error) */
static uint32_t get_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long s = ftell(f);
    fclose(f);
    return (s > 0) ? (uint32_t)s : 0;
}

/* Load a previously cached raw surface.
   png_size is the current PNG byte count — cache is rejected if it differs. */
static surface_t *load_cache(const char *cache_path, uint32_t png_size) {
    FILE *f = fopen(cache_path, "rb");
    if (!f) return NULL;

    boxart_cache_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return NULL; }

    /* Reject stale cache (PNG was replaced) */
    if (hdr.png_size != png_size) { fclose(f); return NULL; }

    surface_t *img = calloc(1, sizeof(surface_t));
    if (!img) { fclose(f); return NULL; }

    *img = surface_alloc(FMT_RGBA16, hdr.width, hdr.height);
    if (!img->buffer) { free(img); fclose(f); return NULL; }

    if (img->stride != hdr.stride) {
        surface_free(img); free(img); fclose(f); return NULL;
    }

    size_t data_size = (size_t)hdr.stride * hdr.height;
    if (fread(img->buffer, 1, data_size, f) != data_size) {
        surface_free(img); free(img); fclose(f); return NULL;
    }

    fclose(f);
    return img;
}

/* Write decoded surface to cache file alongside source PNG size for future validation */
static void write_cache(const char *cache_path, const surface_t *img, uint32_t png_size) {
    FILE *f = fopen(cache_path, "wb");
    if (!f) return;

    boxart_cache_header_t hdr = {
        .png_size = png_size,
        .width    = img->width,
        .height   = img->height,
        .stride   = img->stride,
    };
    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(img->buffer, 1, (size_t)img->stride * img->height, f);
    fclose(f);
}

/**
 * @brief PNG decoder callback for boxart image loading.
 */
static void png_decoder_callback(png_err_t err, surface_t *decoded_image, void *callback_data) {
    component_boxart_t *b = (component_boxart_t *)(callback_data);
    b->loading = false;
    b->image = decoded_image;

    if (err == PNG_OK && decoded_image && pending_cache_path[0]) {
        write_cache(pending_cache_path, decoded_image, pending_png_size);
        pending_cache_path[0] = '\0';
        pending_png_size = 0;
    }
}

#define DFS_BOXART_DIRECTORY    "rom:/boxart"

/* Map a grid image-view setting (settings.h grid_image_view_t) to a loader type. */
file_image_type_t ui_components_boxart_view_to_type(int view) {
    switch (view) {
        case GRID_IMAGE_BOX_BACK:   return IMAGE_BOXART_BACK;
        case GRID_IMAGE_BOX_3D:     return IMAGE_BOXART_3D;
        case GRID_IMAGE_CART_FRONT: return IMAGE_GAMEPAK_FRONT;
        case GRID_IMAGE_CART_3D:    return IMAGE_GAMEPAK_3D;
        case GRID_IMAGE_LOGO:       return IMAGE_LOGO;
        default:                    return IMAGE_BOXART_FRONT;
    }
}

/* Logical media type -> ROM DFS sprite basename (NULL = no baked art for type). */
static const char *dfs_type_name(file_image_type_t t) {
    switch (t) {
        case IMAGE_BOXART_FRONT:  return "front";
        case IMAGE_BOXART_BACK:   return "back";
        case IMAGE_BOXART_3D:     return "box3d";
        case IMAGE_GAMEPAK_FRONT: return "cart";
        case IMAGE_GAMEPAK_3D:    return "cart3d";
        case IMAGE_LOGO:          return "logo";
        default:                  return NULL;
    }
}

/* Custom-folder PNG filename suffix per type (see /menu/n64ever/gameconfigs/). */
static const char *custom_type_suffix(file_image_type_t t) {
    switch (t) {
        case IMAGE_BOXART_FRONT:  return "-front";
        case IMAGE_BOXART_BACK:   return "-back";
        case IMAGE_BOXART_3D:     return "-3dbox";
        case IMAGE_GAMEPAK_FRONT: return "-cart";
        case IMAGE_GAMEPAK_3D:    return "-3dcart";
        case IMAGE_LOGO:          return "-logo";
        default:                  return NULL;
    }
}

/* SD-card metadata-folder PNG filename per type (NULL = no SD equivalent). */
static const char *metadata_type_filename(file_image_type_t t) {
    switch (t) {
        case IMAGE_GAMEPAK_FRONT: return "gamepak_front.png";
        case IMAGE_GAMEPAK_BACK:  return "gamepak_back.png";
        case IMAGE_BOXART_BACK:   return "boxart_back.png";
        case IMAGE_BOXART_LEFT:   return "boxart_left.png";
        case IMAGE_BOXART_RIGHT:  return "boxart_right.png";
        case IMAGE_BOXART_BOTTOM: return "boxart_bottom.png";
        case IMAGE_BOXART_TOP:    return "boxart_top.png";
        case IMAGE_BOXART_FRONT:  return "boxart_front.png";
        default:                  return NULL; /* 3d box, 3d cart, logo: DFS/custom only */
    }
}

/* Fallback cascade per requested type. Product spec:
   box types prefer front > 3D box > back, then cart > 3D cart, then logo;
   cart types prefer cart > 3D cart > logo, then box; logo prefers
   logo > front > 3D box > back > cart > 3D cart. */
static const file_image_type_t *fallback_chain(file_image_type_t t, int *n) {
    static const file_image_type_t FRONT[]  = { IMAGE_BOXART_FRONT, IMAGE_BOXART_3D, IMAGE_BOXART_BACK, IMAGE_GAMEPAK_FRONT, IMAGE_GAMEPAK_3D, IMAGE_LOGO };
    static const file_image_type_t BACK[]   = { IMAGE_BOXART_BACK, IMAGE_BOXART_FRONT, IMAGE_BOXART_3D, IMAGE_GAMEPAK_FRONT, IMAGE_GAMEPAK_3D, IMAGE_LOGO };
    static const file_image_type_t BOX3D[]  = { IMAGE_BOXART_3D, IMAGE_BOXART_FRONT, IMAGE_BOXART_BACK, IMAGE_GAMEPAK_FRONT, IMAGE_GAMEPAK_3D, IMAGE_LOGO };
    static const file_image_type_t CART[]   = { IMAGE_GAMEPAK_FRONT, IMAGE_GAMEPAK_3D, IMAGE_LOGO, IMAGE_BOXART_FRONT, IMAGE_BOXART_3D, IMAGE_BOXART_BACK };
    static const file_image_type_t CART3D[] = { IMAGE_GAMEPAK_3D, IMAGE_GAMEPAK_FRONT, IMAGE_LOGO, IMAGE_BOXART_FRONT, IMAGE_BOXART_3D, IMAGE_BOXART_BACK };
    static const file_image_type_t LOGO[]   = { IMAGE_LOGO, IMAGE_BOXART_FRONT, IMAGE_BOXART_3D, IMAGE_BOXART_BACK, IMAGE_GAMEPAK_FRONT, IMAGE_GAMEPAK_3D };
    *n = 6;
    switch (t) {
        case IMAGE_BOXART_BACK:   return BACK;
        case IMAGE_BOXART_3D:     return BOX3D;
        case IMAGE_GAMEPAK_FRONT: return CART;
        case IMAGE_GAMEPAK_3D:    return CART3D;
        case IMAGE_LOGO:          return LOGO;
        default:                  return FRONT;
    }
}

/* Attempt to load a PNG (cache-first, then decode). Returns true if the component
   was satisfied (image cached, or async decode started). */
static bool try_png(component_boxart_t *b, char *png_path, int max_w, int max_h) {
    uint32_t png_sz = get_file_size(png_path);
    if (png_sz == 0) return false;
    char cache_path[400];
    make_cache_path(cache_path, sizeof(cache_path), png_path);
    surface_t *cached = load_cache(cache_path, png_sz);
    if (cached) { b->loading = false; b->image = cached; return true; }
    if (png_decoder_start(png_path, max_w, max_h, png_decoder_callback, b) == PNG_OK) {
        strncpy(pending_cache_path, cache_path, sizeof(pending_cache_path) - 1);
        pending_cache_path[sizeof(pending_cache_path) - 1] = '\0';
        pending_png_size = png_sz;
        return true;
    }
    return false;
}

/* Attempt to load a built-in ROM DFS sprite for the given code+type.
   The baked art library is keyed mostly by US (E) game codes, but users often run
   PAL/JP dumps (same game, different 4th region byte). So if the ROM's exact code
   has no baked art, fall back to other region variants of the same title (the first
   three code chars identify the game). A region-mismatched cover beats no cover. */
static bool try_dfs_sprite(component_boxart_t *b, const char *game_code, file_image_type_t t) {
    const char *tn = dfs_type_name(t);
    if (!tn) return false;

    /* Attempt order: the ROM's own region first, then the common region bytes. */
    static const char region_candidates[] = { 'E', 'P', 'J', 'U', 'A', 'X', 'F', 'D', 'I', 'S' };
    char order[1 + sizeof(region_candidates)];
    int no = 0;
    order[no++] = game_code[3];
    for (size_t i = 0; i < sizeof(region_candidates); i++) {
        if (region_candidates[i] != game_code[3]) order[no++] = region_candidates[i];
    }

    char path[64];
    for (int i = 0; i < no; i++) {
        snprintf(path, sizeof(path), DFS_BOXART_DIRECTORY"/%c%c%c%c/%s.sprite",
                 game_code[0], game_code[1], game_code[2], order[i], tn);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fclose(f);
        sprite_t *spr = sprite_load(path);
        if (!spr) return false;
        surface_t *s = calloc(1, sizeof(surface_t));
        if (!s) { sprite_free(spr); return false; }
        *s = sprite_get_pixels(spr);
        b->loading = false;
        b->sprite = spr;
        b->image = s; /* surface struct only; pixel buffer is owned by the sprite */
        return true;
    }
    return false;
}

/* Attempt to load an SD-card metadata-folder PNG for the given code+type. `stem` (the ROM's own
   filename, no extension/path) is the fallback identity for ROMs with no matching code-keyed art
   directory -- homebrew and ROM hacks routinely share a generic/duplicated header code, so a
   per-code folder either collides or never matches; keying by the ROM's own filename instead
   gives every such ROM a stable, distinct art slot under the same homebrew/ directory. */
static bool try_metadata_png(component_boxart_t *b, const char *storage_prefix,
                             const char *game_code, const char *rom_title, const char *stem,
                             file_image_type_t t) {
    const char *filename = metadata_type_filename(t);
    if (!filename) return false;

    char boxart_path[300];
    path_t *path = path_init(storage_prefix, METADATA_BASE_DIRECTORY);

    if (game_code[1] == 'E' && game_code[2] == 'D' && rom_title) {
        char safe_title[21];
        memcpy(safe_title, rom_title, 20);
        safe_title[20] = '\0';
        snprintf(boxart_path, sizeof(boxart_path), HOMEBREW_ID_SUBDIRECTORY"/%s", safe_title);
        path_push(path, boxart_path);
    } else {
        snprintf(boxart_path, sizeof(boxart_path), "%c/%c/%c/%c", game_code[0], game_code[1], game_code[2], game_code[3]);
        path_push(path, boxart_path);
        if (!directory_exists(path_get(path))) path_pop(path);
        if (!directory_exists(path_get(path))) {
            path_free(path);
            path = path_init(storage_prefix, OLD_BOXART_DIRECTORY);
            path_push(path, boxart_path);
            if (!directory_exists(path_get(path))) path_pop(path);
        }
        /* No code-keyed directory anywhere -- fall back to a filename-keyed homebrew slot. */
        if (!directory_exists(path_get(path)) && stem && stem[0]) {
            path_free(path);
            path = path_init(storage_prefix, METADATA_BASE_DIRECTORY);
            snprintf(boxart_path, sizeof(boxart_path), HOMEBREW_ID_SUBDIRECTORY"/%s", stem);
            path_push(path, boxart_path);
        }
    }

    bool ok = false;
    if (directory_exists(path_get(path))) {
        path_push(path, (char *)filename);
        ok = try_png(b, path_get(path), BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX);
    }
    path_free(path);
    return ok;
}

/**
 * @brief Initialize and load the boxart component for a game.
 *
 * For the requested view, walks the fallback chain; for each candidate type it
 * tries, in priority order: a custom-folder PNG override, the built-in ROM DFS
 * sprite, then an SD-card metadata PNG. Returns the first that loads, or NULL if
 * no media exists for the game at all (caller shows the title text instead).
 */
int ui_components_boxart_alpha_threshold(const component_boxart_t *b) {
    /* N64 3D-cart AND logo art have soft anti-aliased edges on a transparent field; a 1-bit cutout
       draws that faint fringe opaque (the "eaten/haloed" look). A higher threshold crops the fringe
       for a clean edge. Other types -- and all 64DD disk/expansion art -- stay at 1. */
    return (b && (b->type == IMAGE_GAMEPAK_3D || b->type == IMAGE_LOGO) && !b->is_dd) ? 128 : 1;
}

component_boxart_t *ui_components_boxart_init(const char *storage_prefix, const char *game_code, const char *rom_title, const char *rom_path_hint, file_image_type_t current_image_view, bool use_custom) {
    component_boxart_t *b;

    if ((b = calloc(1, sizeof(component_boxart_t))) == NULL) {
        return NULL;
    }
    b->loading = true;
    b->is_dd = (game_code && (game_code[0] == 'D' || game_code[0] == 'E'));  /* 64DD disk/expansion */

    /* Derive the SD prefix and ROM filename stem for custom-folder lookups. */
    char sd_prefix[16] = "sd:";
    char stem[256] = "";
    if (rom_path_hint && rom_path_hint[0]) {
        const char *colon = strchr(rom_path_hint, ':');
        if (colon) {
            size_t plen = (size_t)(colon - rom_path_hint + 1);
            if (plen < sizeof(sd_prefix)) { memcpy(sd_prefix, rom_path_hint, plen); sd_prefix[plen] = '\0'; }
        }
        const char *slash = strrchr(rom_path_hint, '/');
        const char *fname = slash ? slash + 1 : rom_path_hint;
        strncpy(stem, fname, sizeof(stem) - 1); stem[sizeof(stem) - 1] = '\0';
        char *dot = strrchr(stem, '.'); if (dot) *dot = '\0';
    }

    /* Special-edition art redirect: a ROM that reuses another game's NUS code (e.g. OoT
       Master Quest, which ships under the regular OoT codes CZLE/NZLP) gets its own baked
       art under a distinct DFS key, matched on the filename rather than the colliding code. */
    const char *dfs_code = game_code;
    if (stem[0]) {
        int si = game_special_match(stem);
        if (si >= 0) {
            const game_special_t *sp = game_special_get(si);
            if (sp && sp->art_code) dfs_code = sp->art_code;
        }
    }

    int chain_n;
    const file_image_type_t *chain = fallback_chain(current_image_view, &chain_n);

    for (int i = 0; i < chain_n; i++) {
        file_image_type_t t = chain[i];
        b->type = t;   /* record the type actually loaded (a success returns from this iteration) */

        /* 1. Custom-folder PNG override (beats built-in art). Gated by the
           "Use custom files" setting — skipping these SD probes when off is a big
           speed win, since they almost always miss. */
        if (use_custom && stem[0]) {
            char cpath[400];
            const char *suf = custom_type_suffix(t);
            if (suf) {
                snprintf(cpath, sizeof(cpath), "%s/menu/n64ever/gameconfigs/%s%s.png", sd_prefix, stem, suf);
                if (try_png(b, cpath, 1024, 1024)) return b;
            }
            /* (Legacy /menu/custom PNG probes dropped: the boot migration relocates all custom
               art to /menu/n64ever, so they were wasted SD opens on every cover load.) */
        }

        /* 2. Built-in ROM DFS sprite (special-edition redirect applied). */
        if (try_dfs_sprite(b, dfs_code, t)) return b;

        /* 3. SD-card metadata-folder PNG. */
        if (try_metadata_png(b, storage_prefix, game_code, rom_title, stem, t)) return b;
    }

    free(b);
    return NULL;
}

/**
 * @brief Free the boxart component and its resources.
 */
void ui_components_boxart_free(component_boxart_t *b) {
    if (b) {
        if (b->loading) {
            png_decoder_abort();
        }
        if (b->sprite) {
            /* Sprite-backed: the surface struct references the sprite's buffer,
               so free only the struct here and release the sprite separately. */
            if (b->image) free(b->image);
            sprite_free(b->sprite);
        } else if (b->image) {
            surface_free(b->image);
            free(b->image);
        }
        free(b);
    }
}

/**
 * @brief Draw the boxart image or a loading placeholder.
 */
void ui_components_boxart_draw(component_boxart_t *b) {
    if (b && b->image) {
        int box_x = BOXART_X;
        int box_y = BOXART_Y;
        rdpq_mode_push();
        rdpq_set_mode_copy(false);
        if (b->image->width <= BOXART_WIDTH_MAX && b->image->height <= BOXART_HEIGHT_MAX) {
            /* Pre-scaled database images: snap to their designated display positions */
            if (b->image->height == BOXART_HEIGHT_MAX) {
                box_x = BOXART_X_JP;
                box_y = BOXART_Y_JP;
            } else if (b->image->width == BOXART_WIDTH_DD && b->image->height == BOXART_HEIGHT_DD) {
                box_x = BOXART_X_DD;
                box_y = BOXART_Y_DD;
            }
            rdpq_tex_blit(b->image, box_x, box_y, NULL);
        } else {
            /* Large sidecar image: scale to fit the standard boxart display area */
            float sx = (float)BOXART_WIDTH / b->image->width;
            float sy = (float)BOXART_HEIGHT / b->image->height;
            float scale = sx < sy ? sx : sy;
            int draw_w = (int)(b->image->width  * scale);
            int draw_h = (int)(b->image->height * scale);
            int off_x  = (BOXART_WIDTH  - draw_w) / 2;
            int off_y  = (BOXART_HEIGHT - draw_h) / 2;
            rdpq_tex_blit(b->image,
                BOXART_X + off_x, BOXART_Y + off_y,
                &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
        }
        rdpq_mode_pop();
    } else {
        ui_components_box_draw(
            BOXART_X,
            BOXART_Y,
            BOXART_X + BOXART_WIDTH,
            BOXART_Y + BOXART_HEIGHT,
            BOXART_LOADING_COLOR
        );
    }
}
