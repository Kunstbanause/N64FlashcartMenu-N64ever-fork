/**
 * @file common.c
 * @brief Common UI components implementation
 * @ingroup ui_components
 */

#include <stdarg.h>

#include "../ui_components.h"
#include "../fonts.h"
#include "constants.h"

/* Shared animated rainbow effects (defined alongside the dialog code below). */
static void draw_rainbow_rim(int x0, int y0, int x1, int y1, uint8_t hue);
static void draw_rainbow_border_inset(int x0, int y0, int x1, int y1, uint8_t hue);
static uint8_t border_glow_hue = 0;

/**
 * @brief Draw a box with the specified color.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 * @param color The color of the box.
 */
void ui_components_box_draw (int x0, int y0, int x1, int y1, color_t color) {
    rdpq_mode_push();
        rdpq_set_mode_fill(color);
        rdpq_fill_rectangle(x0, y0, x1, y1);
    rdpq_mode_pop();
}

/**
 * @brief Draw a stylised grey N64 cartridge as a placeholder when no art exists.
 *        Composed from filled rectangles: body, top grip ridges, recessed label,
 *        and the darker connector lip — a recognisable silhouette centred in the
 *        given box.
 */
/* Baked cartridge photo (rom:/placeholder-cart.sprite), loaded once on first use.
   Falls back to the procedural silhouette below if the sprite can't be loaded. */
static sprite_t *cart_ph_sprite = NULL;
static surface_t cart_ph_surf;
static bool      cart_ph_tried  = false;

/* Baked 64DD-disc placeholder (rom:/placeholder-disc.sprite), loaded once on first use.
   Falls back to the cart placeholder if it can't be loaded. */
static sprite_t *disc_ph_sprite = NULL;
static surface_t disc_ph_surf;
static bool      disc_ph_tried  = false;

static void cart_placeholder_procedural (int x0, int y0, int x1, int y1) {
    const color_t body    = RGBA32(0x9A, 0x9A, 0x9A, 0xFF);
    const color_t edge    = RGBA32(0x5E, 0x5E, 0x5E, 0xFF);
    const color_t label   = RGBA32(0xD2, 0xD2, 0xD2, 0xFF);
    const color_t label_b = RGBA32(0x80, 0x80, 0x80, 0xFF);
    const color_t ridge   = RGBA32(0x77, 0x77, 0x77, 0xFF);

    int bw = x1 - x0, bh = y1 - y0;
    /* N64 cartridge front aspect is roughly 0.72 (w:h); fit it into the box. */
    int ch = (int)(bh * 0.92f);
    int cw = (int)(ch * 0.72f);
    if (cw > (int)(bw * 0.92f)) { cw = (int)(bw * 0.92f); ch = (int)(cw / 0.72f); if (ch > bh) ch = bh; }
    int cx0 = (x0 + x1) / 2 - cw / 2;
    int cy0 = (y0 + y1) / 2 - ch / 2;
    int cx1 = cx0 + cw, cy1 = cy0 + ch;

    /* Body with a darker outline for definition. */
    ui_components_box_draw(cx0 - 1, cy0 - 1, cx1 + 1, cy1 + 1, edge);
    ui_components_box_draw(cx0, cy0, cx1, cy1, body);

    /* Top grip ridges: four short vertical bars across the upper band. */
    int rb_y0 = cy0 + (int)(ch * 0.05f);
    int rb_y1 = cy0 + (int)(ch * 0.16f);
    for (int r = 0; r < 4; r++) {
        int rx = cx0 + (int)(cw * (0.17f + r * 0.18f));
        ui_components_box_draw(rx, rb_y0, rx + (int)(cw * 0.08f), rb_y1, ridge);
    }

    /* Recessed label area (lighter, bordered) in the upper-middle. */
    int lx0 = cx0 + (int)(cw * 0.14f), lx1 = cx1 - (int)(cw * 0.14f);
    int ly0 = cy0 + (int)(ch * 0.22f), ly1 = cy0 + (int)(ch * 0.62f);
    ui_components_box_draw(lx0 - 1, ly0 - 1, lx1 + 1, ly1 + 1, label_b);
    ui_components_box_draw(lx0, ly0, lx1, ly1, label);

    /* Connector lip: darker band along the bottom edge. */
    ui_components_box_draw(cx0, cy1 - (int)(ch * 0.08f), cx1, cy1, edge);
}

void ui_components_cart_placeholder_draw (int x0, int y0, int x1, int y1) {
    if (!cart_ph_tried) {
        cart_ph_tried  = true;
        cart_ph_sprite = sprite_load("rom:/placeholder-cart.sprite");
        if (cart_ph_sprite) cart_ph_surf = sprite_get_pixels(cart_ph_sprite);
    }
    if (!cart_ph_sprite) { cart_placeholder_procedural(x0, y0, x1, y1); return; }

    /* Aspect-fit the cart photo into the box (92% to leave a little breathing room). */
    int bw = x1 - x0, bh = y1 - y0;
    float sx = (bw * 0.92f) / cart_ph_surf.width;
    float sy = (bh * 0.92f) / cart_ph_surf.height;
    float scale = (sx < sy) ? sx : sy;
    int draw_w = (int)(cart_ph_surf.width  * scale);
    int draw_h = (int)(cart_ph_surf.height * scale);
    int dx = x0 + (bw - draw_w) / 2;
    int dy = y0 + (bh - draw_h) / 2;
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        /* Alpha-blend rather than alpha-compare so the soft (anti-aliased) cart
           edge composites smoothly over the background instead of a hard, jagged
           1-bit cut-out. */
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_tex_blit(&cart_ph_surf, dx, dy,
                      &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
    rdpq_mode_pop();
}

void ui_components_disc_placeholder_draw (int x0, int y0, int x1, int y1) {
    if (!disc_ph_tried) {
        disc_ph_tried  = true;
        disc_ph_sprite = sprite_load("rom:/placeholder-disc.sprite");
        if (disc_ph_sprite) disc_ph_surf = sprite_get_pixels(disc_ph_sprite);
    }
    /* No baked disc placeholder -> fall back to the cart placeholder. */
    if (!disc_ph_sprite) { ui_components_cart_placeholder_draw(x0, y0, x1, y1); return; }

    int bw = x1 - x0, bh = y1 - y0;
    float sx = (bw * 0.92f) / disc_ph_surf.width;
    float sy = (bh * 0.92f) / disc_ph_surf.height;
    float scale = (sx < sy) ? sx : sy;
    int draw_w = (int)(disc_ph_surf.width  * scale);
    int draw_h = (int)(disc_ph_surf.height * scale);
    int dx = x0 + (bw - draw_w) / 2;
    int dy = y0 + (bh - draw_h) / 2;
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_tex_blit(&disc_ph_surf, dx, dy,
                      &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
    rdpq_mode_pop();
}

static void ui_components_border_draw_internal (int x0, int y0, int x1, int y1, color_t color) {
    rdpq_mode_push();
        rdpq_set_mode_fill(color);
        rdpq_fill_rectangle(x0 - BORDER_THICKNESS, y0 - BORDER_THICKNESS, x1 + BORDER_THICKNESS, y0);
        rdpq_fill_rectangle(x0 - BORDER_THICKNESS, y1, x1 + BORDER_THICKNESS, y1 + BORDER_THICKNESS);
        rdpq_fill_rectangle(x0 - BORDER_THICKNESS, y0, x0, y1);
        rdpq_fill_rectangle(x1, y0, x1 + BORDER_THICKNESS, y1);
    rdpq_mode_pop();
}

/**
 * @brief Draw a border with the specified color.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 * @param color The color of the border.
 */
void ui_components_border_draw (int x0, int y0, int x1, int y1) {
    /* Rainbow frame drawn just INSIDE the edge so it stays in the visible area
       (an outside halo would land in the TV overscan and be cut off). */
    border_glow_hue += 1;   /* halved for the 60fps render (same glow cycle speed as 30fps) */
    draw_rainbow_border_inset(x0, y0, x1, y1, border_glow_hue);
}

/**
 * @brief Draw the layout with tabs.
 */
void ui_components_layout_draw_tabbed (void) {
    ui_components_border_draw(
        VISIBLE_AREA_X0,
        VISIBLE_AREA_Y0 + TAB_HEIGHT + BORDER_THICKNESS,
        VISIBLE_AREA_X1,
        VISIBLE_AREA_Y1
    );
    /* (white actions separator bar removed) */
}

/**
 * @brief Draw the layout.
 */
void ui_components_layout_draw (void) {
    ui_components_border_draw(
        VISIBLE_AREA_X0,
        VISIBLE_AREA_Y0,
        VISIBLE_AREA_X1,
        VISIBLE_AREA_Y1
    );
    /* (white actions separator bar removed) */
}

/**
 * @brief Draw a progress bar.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 * @param progress The progress value (0.0 to 1.0).
 */
void ui_components_progressbar_draw (int x0, int y0, int x1, int y1, float progress) {
    float progress_width = progress * (x1 - x0);

    ui_components_box_draw(x0, y0, x0 + progress_width, y1, PROGRESSBAR_DONE_COLOR);
    ui_components_box_draw(x0 + progress_width, y0, x1, y1, PROGRESSBAR_BG_COLOR);
}

/**
 * @brief Draw a rainbow progress bar: a fixed hue gradient across the width, with the
 *        filled portion bright and the rest dimmed. Matches the ROM-launch loading bar.
 */
void ui_components_progressbar_draw_rainbow (int x0, int y0, int x1, int y1, float progress) {
    int W = x1 - x0;
    if (W <= 0) return;
    int filled_px = (int)(progress * W);
    int N = W / 4;
    if (N > 96) N = 96;
    if (N < 1)  N = 1;
    for (int i = 0; i < N; i++) {
        int sx0 = x0 + W * i / N;
        int sx1 = x0 + W * (i + 1) / N;
        int sc  = (sx0 + sx1) / 2 - x0;
        uint8_t hue = (uint8_t)(255 * i / N);
        uint8_t bri = (sc < filled_px) ? 220 : 28;   /* filled bright, unfilled dim */
        ui_components_box_draw(sx0, y0, sx1, y1, ui_components_rainbow_color(hue, bri));
    }
}

/**
 * @brief Draw a seek bar.
 * 
 * @param position The position value (0.0 to 1.0).
 */
void ui_components_seekbar_draw (float position) {
    int x0 = SEEKBAR_X;
    int y0 = SEEKBAR_Y;
    int x1 = SEEKBAR_X + SEEKBAR_WIDTH;
    int y1 = SEEKBAR_Y + SEEKBAR_HEIGHT;

    ui_components_border_draw(x0, y0, x1, y1);
    ui_components_progressbar_draw(x0, y0, x1, y1, position);
}

static color_t rainbow_bar_color(uint8_t hue, uint8_t brightness) {
    uint8_t sector = hue / 43;
    uint8_t frac   = (uint8_t)((hue % 43) * 6);
    uint8_t inv    = 255 - frac;
    uint8_t r, g, b;
    switch (sector) {
        case 0: r=255; g=frac; b=0;    break;
        case 1: r=inv; g=255;  b=0;    break;
        case 2: r=0;   g=255;  b=frac; break;
        case 3: r=0;   g=inv;  b=255;  break;
        case 4: r=frac;g=0;    b=255;  break;
        default:r=255; g=0;    b=inv;  break;
    }
    r = (uint8_t)((uint16_t)r * brightness / 255);
    g = (uint8_t)((uint16_t)g * brightness / 255);
    b = (uint8_t)((uint16_t)b * brightness / 255);
    return RGBA32(r, g, b, 255);
}

/* Public accessor for the shared rainbow palette (used by the file browser, etc.). */
color_t ui_components_rainbow_color (uint8_t hue, uint8_t brightness) {
    return rainbow_bar_color(hue, brightness);
}

static uint8_t loader_hue = 0;

/**
 * @brief Draw a rainbow shimmer loading bar.
 *
 * @param progress The progress value (0.0 to 1.0).
 * @param msg The message to display truncated to 30 characters.
 */
void ui_components_loader_draw (float progress, const char *msg) {
    /* Wider and taller than the original; no border */
    int bar_w = LOADER_WIDTH + 120;
    int bar_h = 18;
    int x0 = DISPLAY_CENTER_X - bar_w / 2;
    int y0 = DISPLAY_CENTER_Y - bar_h / 2;
    int x1 = x0 + bar_w;
    int y1 = y0 + bar_h;
    int W  = x1 - x0;

    int filled_px = (int)(progress * W);
    /* Fine segments for a smooth gradient (matches the other rainbow effects),
       capped so the per-frame fill count stays reasonable. */
    int N = W / 4;
    if (N > 96) N = 96;
    if (N < 1)  N = 1;

    rdpq_mode_push();
    rdpq_set_mode_fill(RGBA32(0x08, 0x08, 0x08, 0xFF));
    rdpq_fill_rectangle(x0, y0, x1, y1);
    rdpq_mode_pop();

    for (int i = 0; i < N; i++) {
        int sx0 = x0 + W * i / N;
        int sx1 = x0 + W * (i + 1) / N;
        int sc  = (sx0 + sx1) / 2 - x0;
        uint8_t hue = (uint8_t)(loader_hue + 255 * i / N);
        uint8_t bri = (sc < filled_px) ? 220 : 28;
        rdpq_mode_push();
        rdpq_set_mode_fill(rainbow_bar_color(hue, bri));
        rdpq_fill_rectangle(sx0, y0, sx1, y1);
        rdpq_mode_pop();
    }
    loader_hue += 1;   /* halved for the 60fps render */
    (void)msg;
}

/**
 * @brief Draw a scrollbar.
 * 
 * @param x The x-coordinate of the top-left corner.
 * @param y The y-coordinate of the top-left corner.
 * @param width The width of the scrollbar.
 * @param height The height of the scrollbar.
 * @param position The current position.
 * @param items The total number of items.
 * @param visible_items The number of visible items.
 */
void ui_components_scrollbar_draw (int x, int y, int width, int height, int position, int items, int visible_items) {
    if (items <= 1 || items <= visible_items) {
        ui_components_box_draw(x, y, x + width, y + height, SCROLLBAR_INACTIVE_COLOR);
    } else {
        int scroll_height = (int) ((visible_items / (float) (items)) * height);
        float scroll_position = ((position / (float) (items - 1)) * (height - scroll_height));

        ui_components_box_draw(x, y, x + width, y + height, SCROLLBAR_BG_COLOR);
        ui_components_box_draw(x, y + scroll_position, x + width, y + scroll_position + scroll_height, SCROLLBAR_POSITION_COLOR);
    }
}

/**
 * @brief Draw a list scrollbar.
 * 
 * @param position The current position.
 * @param items The total number of items.
 * @param visible_items The number of visible items.
 */
void ui_components_list_scrollbar_draw (int position, int items, int visible_items) {
    ui_components_scrollbar_draw(
        LIST_SCROLLBAR_X,
        LIST_SCROLLBAR_Y,
        LIST_SCROLLBAR_WIDTH,
        LIST_SCROLLBAR_HEIGHT,
        position,
        items,
        visible_items
    );
}

/**
 * @brief Draw a dialog box.
 * 
 * @param width The width of the dialog box.
 * @param height The height of the dialog box.
 */
/* Animated rainbow glow rim shared by every dialog/menu/messagebox. */
#define DIALOG_GLOW_SEGS  10
static uint8_t dialog_glow_hue = 0;

static void dialog_glow_layer (int x0, int y0, int x1, int y1,
                               int pad, uint8_t base_hue, uint8_t brightness) {
    int lx0 = x0 - pad, ly0 = y0 - pad;
    int lx1 = x1 + pad, ly1 = y1 + pad;
    int W = lx1 - lx0;
    int H = y1  - y0;
    int N = DIALOG_GLOW_SEGS;
    for (int i = 0; i < N; i++) {
        ui_components_box_draw(lx0 + W * i / N, ly0, lx0 + W * (i + 1) / N, y0,
            rainbow_bar_color((uint8_t)(base_hue + 64 * i / N), brightness));
    }
    for (int i = 0; i < N; i++) {
        ui_components_box_draw(x1, y0 + H * i / N, lx1, y0 + H * (i + 1) / N,
            rainbow_bar_color((uint8_t)(base_hue + 64 + 64 * i / N), brightness));
    }
    for (int i = 0; i < N; i++) {
        ui_components_box_draw(lx1 - W * (i + 1) / N, y1, lx1 - W * i / N, ly1,
            rainbow_bar_color((uint8_t)(base_hue + 128 + 64 * i / N), brightness));
    }
    for (int i = 0; i < N; i++) {
        ui_components_box_draw(lx0, y1 - H * (i + 1) / N, x0, y1 - H * i / N,
            rainbow_bar_color((uint8_t)(base_hue + 192 + 64 * i / N), brightness));
    }
}

/* Two-layer animated rainbow rim (outside halo), used for centered dialogs. */
static void draw_rainbow_rim(int x0, int y0, int x1, int y1, uint8_t hue) {
    dialog_glow_layer(x0, y0, x1, y1, 4, hue, 70);
    dialog_glow_layer(x0, y0, x1, y1, 2, hue, 170);
}

/* Rainbow frame drawn just INSIDE the rect edge (always within the visible
   area), as a continuous clockwise gradient. Used for full-screen layouts. */
static void draw_rainbow_border_inset(int x0, int y0, int x1, int y1, uint8_t hue) {
    const int T = 3;            /* frame thickness */
    int W = x1 - x0;
    int H = y1 - y0;
    int N = DIALOG_GLOW_SEGS;
    for (int i = 0; i < N; i++) {   /* top: left → right */
        ui_components_box_draw(x0 + W * i / N, y0, x0 + W * (i + 1) / N, y0 + T,
            rainbow_bar_color((uint8_t)(hue + 64 * i / N), 200));
    }
    for (int i = 0; i < N; i++) {   /* right: top → bottom */
        ui_components_box_draw(x1 - T, y0 + H * i / N, x1, y0 + H * (i + 1) / N,
            rainbow_bar_color((uint8_t)(hue + 64 + 64 * i / N), 200));
    }
    for (int i = 0; i < N; i++) {   /* bottom: right → left */
        ui_components_box_draw(x1 - W * (i + 1) / N, y1 - T, x1 - W * i / N, y1,
            rainbow_bar_color((uint8_t)(hue + 128 + 64 * i / N), 200));
    }
    for (int i = 0; i < N; i++) {   /* left: bottom → top */
        ui_components_box_draw(x0, y1 - H * (i + 1) / N, x0 + T, y1 - H * i / N,
            rainbow_bar_color((uint8_t)(hue + 192 + 64 * i / N), 200));
    }
}

void ui_components_dialog_draw (int width, int height) {
    int x0 = DISPLAY_CENTER_X - (width / 2);
    int y0 = DISPLAY_CENTER_Y - (height / 2);
    int x1 = DISPLAY_CENTER_X + (width / 2);
    int y1 = DISPLAY_CENTER_Y + (height / 2);

    dialog_glow_hue += 1;   /* halved for the 60fps render */
    draw_rainbow_rim(x0, y0, x1, y1, dialog_glow_hue);

    ui_components_box_draw(x0, y0, x1, y1, DIALOG_BG_COLOR);
}

/**
 * @brief Draw a message box with formatted text.
 * 
 * @param fmt The format string.
 * @param ... The format arguments.
 */
void ui_components_messagebox_draw (char *fmt, ...) {
    char buffer[512];
    size_t nbytes = sizeof(buffer);

    va_list va;
    va_start(va, fmt);
    char *formatted = vasnprintf(buffer, &nbytes, fmt, va);
    va_end(va);

    int paragraph_nbytes = nbytes;

    rdpq_paragraph_t *paragraph = rdpq_paragraph_build(&(rdpq_textparms_t) {
        .width = MESSAGEBOX_MAX_WIDTH,
        .height = VISIBLE_AREA_HEIGHT,
        .align = ALIGN_CENTER,
        .valign = VALIGN_CENTER,
        .wrap = WRAP_WORD,
        .line_spacing = TEXT_LINE_SPACING_ADJUST,
    }, FNT_DEFAULT, formatted, &paragraph_nbytes);

    if (formatted != buffer) {
        free(formatted);
    }

    ui_components_dialog_draw(
        paragraph->bbox.x1 - paragraph->bbox.x0 + MESSAGEBOX_MARGIN,
        paragraph->bbox.y1 - paragraph->bbox.y0 + MESSAGEBOX_MARGIN
    );

    rdpq_paragraph_render(paragraph, DISPLAY_CENTER_X - (MESSAGEBOX_MAX_WIDTH / 2), VISIBLE_AREA_Y0);

    rdpq_paragraph_free(paragraph);
}

/**
 * @brief Draw the main text with formatted content.
 * 
 * @param style The font style.
 * @param align The horizontal alignment.
 * @param valign The vertical alignment.
 * @param fmt The format string.
 * @param ... The format arguments.
 */
void ui_components_main_text_draw (menu_font_type_t style, rdpq_align_t align, rdpq_valign_t valign, char *fmt, ...) {
    char buffer[1024];
    size_t nbytes = sizeof(buffer);

    va_list va;
    va_start(va, fmt);
    char *formatted = vasnprintf(buffer, &nbytes, fmt, va);
    va_end(va);

    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .style_id = style,
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = LAYOUT_ACTIONS_SEPARATOR_Y - OVERSCAN_HEIGHT - (TEXT_MARGIN_VERTICAL * 2),
            .align = align,
            .valign = valign,
            .wrap = WRAP_WORD,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL,
        formatted,
        nbytes
    );

    if (formatted != buffer) {
        free(formatted);
    }
}

/**
 * @brief Draw the actions bar text with formatted content.
 * 
 * @param style The font style.
 * @param align The horizontal alignment.
 * @param valign The vertical alignment.
 * @param fmt The format string.
 * @param ... The format arguments.
 */
void ui_components_actions_bar_text_draw (menu_font_type_t style, rdpq_align_t align, rdpq_valign_t valign, char *fmt, ...) {
    char buffer[256];
    size_t nbytes = sizeof(buffer);

    va_list va;
    va_start(va, fmt);
    char *formatted = vasnprintf(buffer, &nbytes, fmt, va);
    va_end(va);

    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .style_id = style,
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = VISIBLE_AREA_Y1 - LAYOUT_ACTIONS_SEPARATOR_Y - BORDER_THICKNESS - (TEXT_MARGIN_VERTICAL * 2),
            .align = align,
            .valign = valign,
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        LAYOUT_ACTIONS_SEPARATOR_Y + BORDER_THICKNESS + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL,
        formatted,
        nbytes
    );

    if (formatted != buffer) {
        free(formatted);
    }
}

/* Draw one button hint at a fixed slot in the actions bar. */
static void actions_bar_slot_draw (const char *txt, int x, int width, rdpq_align_t align) {
    if (!txt || !txt[0]) {
        return;
    }
    rdpq_text_print(
        &(rdpq_textparms_t) {
            .style_id = STL_DEFAULT,
            .width = width,
            .height = VISIBLE_AREA_Y1 - LAYOUT_ACTIONS_SEPARATOR_Y - BORDER_THICKNESS - (TEXT_MARGIN_VERTICAL * 2),
            .align = align,
            .valign = VALIGN_BOTTOM,
            .wrap = WRAP_NONE,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        x,
        LAYOUT_ACTIONS_SEPARATOR_Y + BORDER_THICKNESS + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL,
        txt
    );
}

/**
 * @brief Draw the standardized actions bar with up to five button hints.
 *
 * The five buttons always occupy the same equidistant slots so a given button
 * means the same screen position in every view: A is anchored to the left edge,
 * R to the right edge, and B/S/C are distributed equidistantly between them.
 * Pass NULL (or "") for any slot a view does not use.
 *
 * @param a Label for the A slot (left-anchored), e.g. "A: Open".
 * @param b Label for the B slot.
 * @param s Label for the S (Start) slot.
 * @param c Label for the C slot.
 * @param r Label for the R slot (right-anchored), e.g. "R: Menu".
 */
void ui_components_actions_bar_buttons_draw (const char *a, const char *b, const char *s, const char *c, const char *r) {
    int x0 = VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL;
    int w  = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2);
    int q  = w / 4;

    /* A anchored left, R anchored right (each given the full width to align in). */
    actions_bar_slot_draw(a, x0, w, ALIGN_LEFT);
    actions_bar_slot_draw(r, x0, w, ALIGN_RIGHT);

    /* B/S/C centered on the 1/4, 2/4, 3/4 slots (box of width w/2 centered on slot). */
    actions_bar_slot_draw(b, x0,         2 * q, ALIGN_CENTER);
    actions_bar_slot_draw(s, x0 + q,     2 * q, ALIGN_CENTER);
    actions_bar_slot_draw(c, x0 + 2 * q, 2 * q, ALIGN_CENTER);
}

/**
 * @brief Draw the tabs.
 * 
 * @param text Array of tab text.
 * @param count Number of tabs.
 * @param selected Index of the selected tab.
 * @param width Width of each tab.
 */
void ui_components_tabs_draw(const char **text, int count, int selected, float width ) {
    float starting_x = VISIBLE_AREA_X0;

    float x = starting_x;
    float y = OVERSCAN_HEIGHT;    
    float height = TAB_HEIGHT;

    // first draw the tabs that are not selected
    for(int i=0;i< count;i++) {
        if(i != selected) {
            ui_components_box_draw(
                x,
                y,
                x + width,
                y + height,
                TAB_INACTIVE_BACKGROUND_COLOR
            );

            ui_components_border_draw_internal(
                x,
                y,
                x + width,
                y + height,
                TAB_INACTIVE_BORDER_COLOR
            );
        }
        x += width;
    }
    
    // draw the selected tab (so it shows up on top of the others)
    if(selected >= 0 && selected < count) {
        x = starting_x + (width * selected);

        ui_components_box_draw(
            x,
            y,
            x + width,
            y + height,
            TAB_ACTIVE_BACKGROUND_COLOR
        );

        ui_components_border_draw_internal(
            x,
            y,
            x + width,
            y + height,
            TAB_ACTIVE_BORDER_COLOR
        );
    }

    // write the text on the tabs
    rdpq_textparms_t tab_textparms = {
        .width = width,
        .height = 24,
        .align = ALIGN_CENTER,
        .wrap = WRAP_NONE
    };
    x = starting_x;
    for(int i=0;i< count;i++) {
        rdpq_text_print(
            &tab_textparms,
            FNT_DEFAULT,
            x,
            y,
            text[i]
        );
        x += width;
    }
}

void ui_component_value_editor(const char **header_text, const char **value_text, int count, int selected, float width_adjustment ) {
    float field_width = (VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2)) / width_adjustment;
    float starting_x = DISPLAY_CENTER_X - (field_width * count / 2.0f);

    float x = starting_x;
    float y = DISPLAY_CENTER_Y;    
    float height = TAB_HEIGHT;

    // first draw the values that are not selected
    for(int i=0;i< count;i++) {
        if(i != selected) {
            ui_components_box_draw(
                x,
                y,
                x + field_width,
                y + height + 24,
                TAB_INACTIVE_BACKGROUND_COLOR
            );
        }
        x += field_width;
    }
    
    // draw the selected value (so it shows up on top of the others)
    if(selected >= 0 && selected < count) {
        x = starting_x + (field_width * selected);

        ui_components_box_draw(
            x,
            y,
            x + field_width,
            y + height + 24,
            TAB_ACTIVE_BACKGROUND_COLOR
        );
    }

    // write the text on the value boxes
    rdpq_textparms_t value_textparms = {
        .width = field_width,
        .height = 24,
        .align = ALIGN_CENTER,
        .wrap = WRAP_NONE
    };
    x = starting_x;
    for(int i=0;i< count;i++) {
        rdpq_text_print(
            &value_textparms,
            FNT_DEFAULT,
            x,
            y,
            header_text[i]
        );

        rdpq_text_print(
            &value_textparms,
            FNT_DEFAULT,
            x,
            y + 24,
            value_text[i]
        );
        x += field_width;
    }

    // draw the border around the value boxes
    ui_components_border_draw (starting_x, y, x, y + height + 24);
}
