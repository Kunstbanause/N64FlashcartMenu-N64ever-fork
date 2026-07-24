/**
 * @file fonts.h
 * @brief Menu fonts
 * @ingroup menu 
 */

#ifndef FONTS_H__
#define FONTS_H__

#include <stdbool.h>
#include <stdint.h>
#include <libdragon.h>   /* color_t (fonts_set_style_color) */

/**
 * @brief Font type enumeration.
 * 
 * This enumeration defines the different types of fonts that can be used
 * in the menu system.
 */
typedef enum {
    FNT_DEFAULT = 1, /**< Default font type */
} menu_font_type_t;

/**
 * @brief Font style enumeration.
 * 
 * This enumeration defines the different styles of fonts that can be used
 * in the menu system.
 */
typedef enum {
    STL_DEFAULT = 0, /**< Default font style */
    STL_GREEN,       /**< Green font style */
    STL_BLUE,        /**< Blue font style */
    STL_YELLOW,      /**< Yellow font style */
    STL_ORANGE,      /**< Orange font style */
    STL_RED,         /**< Red font style */
    STL_GRAY,        /**< Gray font style */
} menu_font_style_t;

/* Spare style slots recoloured at runtime to animate the rainbow greeting signoff.
   rdpq_font auto-grows its style table to 16, so 7..13 are safe to use. */
#define STL_RAINBOW_BASE   7
#define STL_RAINBOW_COUNT  7

/**
 * @brief Initialize fonts.
 *
 * Loads the default UI font: the modern PixelMplus12 pixel font, or the classic
 * Firple-Bold when @p use_legacy_font is true. An SD override (menu/custom.font64)
 * at @p custom_font_path takes precedence over both.
 *
 * @param custom_font_path Path to the optional SD custom font file.
 * @param use_legacy_font  Use the classic Firple-Bold font instead of PixelMplus12.
 */
void fonts_init(char *custom_font_path, bool use_legacy_font);

/**
 * @brief Swap the UI font live (after the font setting is toggled).
 *
 * @param use_legacy_font Use the classic Firple-Bold font instead of PixelMplus12.
 */
void fonts_reload(bool use_legacy_font);

/**
 * @brief Recolour a font style slot at runtime (used to animate the rainbow
 *        greeting signoff). @p style_id should be in the STL_RAINBOW_BASE range.
 */
void fonts_set_style_color(uint8_t style_id, color_t color);

#endif /* FONTS_H__ */
