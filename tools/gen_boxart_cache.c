/*
 * Host-side pre-generator for N64ever boxart .cache files.
 *
 * Reproduces, byte-for-byte, what the firmware writes on-device:
 *   - decode PNG with the SAME libspng, to SPNG_FMT_RGB8   (png_decoder.c)
 *   - surface_alloc(FMT_RGBA16, w, h): stride = w*2          (surface.c / TEX_FORMAT_PIX2BYTES)
 *   - per pixel: (r>>3)<<11 | (g>>3)<<6 | (b>>3)<<1 | 1     (png_decoder_poll)
 *   - cache file = { u32 png_size, u16 w, u16 h, u32 stride } then w*h*2 bytes  (boxart.c)
 * The N64 is big-endian, so header fields and each 16-bit pixel are written big-endian.
 * The metadata loader decodes with limits 158x158 (BOXART_*_MAX); larger images are skipped.
 *
 * Build (against the fork's own libspng submodule, so output matches the firmware exactly):
 *   gcc -O2 -o gen_boxart_cache tools/gen_boxart_cache.c \
 *       src/libs/libspng/spng/spng.c -I src/libs/libspng/spng -lz -lm
 *
 * Usage: gen_boxart_cache <file.png> [more.png ...]      -> writes <file>.cache beside each PNG
 * Normally invoked via tools/make_sdcard_boxart.sh; see README section 2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "spng.h"

#define LIMIT_W 158
#define LIMIT_H 158

static void put_be32(FILE *f, uint32_t v) { fputc(v>>24,f); fputc(v>>16,f); fputc(v>>8,f); fputc(v,f); }
static void put_be16(FILE *f, uint16_t v) { fputc(v>>8,f); fputc(v,f); }

/* returns: 0 ok, 1 skip (too big / not decodable), 2 io error */
static int gen_one(const char *png_path) {
    FILE *f = fopen(png_path, "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long png_size = ftell(f); fseek(f, 0, SEEK_SET);
    if (png_size <= 0) { fclose(f); return 2; }

    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_IGNORE_ADLER32);
    if (!ctx) { fclose(f); return 2; }
    spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
    spng_set_image_limits(ctx, LIMIT_W, LIMIT_H);
    spng_set_png_file(ctx, f);

    size_t image_size;
    if (spng_decoded_image_size(ctx, SPNG_FMT_RGB8, &image_size) != SPNG_OK) { spng_ctx_free(ctx); fclose(f); return 1; }
    uint8_t *rgb = malloc(image_size);
    if (!rgb) { spng_ctx_free(ctx); fclose(f); return 2; }
    if (spng_decode_image(ctx, rgb, image_size, SPNG_FMT_RGB8, 0) != SPNG_OK) { free(rgb); spng_ctx_free(ctx); fclose(f); return 1; }
    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) != SPNG_OK) { free(rgb); spng_ctx_free(ctx); fclose(f); return 1; }
    spng_ctx_free(ctx);
    fclose(f);

    int w = ihdr.width, h = ihdr.height;
    if (w > LIMIT_W || h > LIMIT_H) { free(rgb); return 1; }
    uint32_t stride = (uint32_t)w * 2; /* TEX_FORMAT_PIX2BYTES(FMT_RGBA16, w) */

    char cache[1024];
    size_t L = strlen(png_path);
    if (L < 4 || L + 3 >= sizeof(cache) || memcmp(png_path + L - 4, ".png", 4) != 0) { free(rgb); return 2; }
    memcpy(cache, png_path, L - 4);
    memcpy(cache + L - 4, ".cache", 7); /* incl NUL */

    FILE *o = fopen(cache, "wb");
    if (!o) { free(rgb); return 2; }
    put_be32(o, (uint32_t)png_size);
    put_be16(o, (uint16_t)w);
    put_be16(o, (uint16_t)h);
    put_be32(o, stride);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = rgb + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            uint8_t r = row[x*3+0] >> 3;
            uint8_t g = row[x*3+1] >> 3;
            uint8_t b = row[x*3+2] >> 3;
            put_be16(o, (uint16_t)((r << 11) | (g << 6) | (b << 1) | 1));
        }
    }
    fclose(o);
    free(rgb);
    return 0;
}

int main(int argc, char **argv) {
    int ok = 0, skip = 0, err = 0;
    for (int i = 1; i < argc; i++) {
        int r = gen_one(argv[i]);
        if (r == 0) ok++; else if (r == 1) { skip++; fprintf(stderr, "SKIP %s\n", argv[i]); }
        else { err++; fprintf(stderr, "ERR  %s\n", argv[i]); }
    }
    printf("cache: ok=%d skip=%d err=%d\n", ok, skip, err);
    return err ? 1 : 0;
}
