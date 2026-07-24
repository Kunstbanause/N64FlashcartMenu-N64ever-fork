#include <string.h>
#include <libdragon.h>

#include "fonts.h"
#include "utils/fs.h"


#define FONT_MODERN  "rom:/PixelMplus12-Bold.font64"   /* default: modern pixel font */
#define FONT_CLASSIC "rom:/Firple-Bold.font64"         /* fallback / compatibility   */

static rdpq_font_t *current_font = NULL;
static char         custom_path[256] = "";   /* SD override (menu/custom.font64), if any */

static rdpq_font_t *build_font (bool use_legacy) {
    const char *font_path = use_legacy ? FONT_CLASSIC : FONT_MODERN;
    if (custom_path[0] && file_exists(custom_path)) {
        font_path = custom_path;   /* a user SD font always wins */
    }

    rdpq_font_t *font = rdpq_font_load(font_path);

    rdpq_font_style(font, STL_DEFAULT, &((rdpq_fontstyle_t) { .color = RGBA32(0xFF, 0xFF, 0xFF, 0xFF) }));
    rdpq_font_style(font, STL_GREEN,   &((rdpq_fontstyle_t) { .color = RGBA32(0x70, 0xFF, 0x70, 0xFF) }));
    rdpq_font_style(font, STL_BLUE,    &((rdpq_fontstyle_t) { .color = RGBA32(0x70, 0xBC, 0xFF, 0xFF) }));
    rdpq_font_style(font, STL_YELLOW,  &((rdpq_fontstyle_t) { .color = RGBA32(0xFF, 0xFF, 0x70, 0xFF) }));
    rdpq_font_style(font, STL_ORANGE,  &((rdpq_fontstyle_t) { .color = RGBA32(0xFF, 0x99, 0x00, 0xFF) }));
    rdpq_font_style(font, STL_RED,     &((rdpq_fontstyle_t) { .color = RGBA32(0xFF, 0x40, 0x40, 0xFF) }));
    rdpq_font_style(font, STL_GRAY,    &((rdpq_fontstyle_t) { .color = RGBA32(0xA0, 0xA0, 0xA0, 0xFF) }));

    /* Styles STL_RAINBOW_BASE..+6: animated rainbow slots for the empty-grid greeting
       signoff, recoloured every frame via fonts_set_style_color(). Registered here so
       the slots exist (rdpq_font auto-grows its style table to 16). */
    for (int i = 0; i < STL_RAINBOW_COUNT; i++) {
        rdpq_font_style(font, STL_RAINBOW_BASE + i, &((rdpq_fontstyle_t) { .color = RGBA32(0xFF, 0xFF, 0xFF, 0xFF) }));
    }

    return font;
}


void fonts_set_style_color (uint8_t style_id, color_t color) {
    if (current_font) {
        rdpq_font_style(current_font, style_id, &((rdpq_fontstyle_t) { .color = color }));
    }
}


void fonts_init (char *custom_font_path, bool use_legacy_font) {
    if (custom_font_path) {
        strncpy(custom_path, custom_font_path, sizeof(custom_path) - 1);
        custom_path[sizeof(custom_path) - 1] = '\0';
    }
    current_font = build_font(use_legacy_font);
    rdpq_text_register_font(FNT_DEFAULT, current_font);
}


void fonts_reload (bool use_legacy_font) {
    /* rdpq_text_register_font asserts if the id is still registered, so unregister
       the current font first, then register the replacement and free the old one. */
    rdpq_font_t *old = current_font;
    rdpq_text_unregister_font(FNT_DEFAULT);
    /* Free the outgoing font BEFORE loading the replacement. Loading first holds
       both fonts in RAM at once (~660 KB peak), which OOMs the asset loader under
       heavy-favorites memory pressure (the crash seen toggling at 946 favorites).
       No text is drawn between here and the re-register below, so FNT_DEFAULT may
       be briefly empty. */
    if (old) {
        rdpq_font_free(old);
    }
    current_font = build_font(use_legacy_font);
    rdpq_text_register_font(FNT_DEFAULT, current_font);
}
