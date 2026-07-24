/**
 * @file rom_boot.c
 * @brief ROM-boot countdown view.
 *
 * A fully userland power-on convenience: when "ROM boot" is enabled and a ROM has been chosen,
 * startup hands control here instead of the grid. We show that ROM's Load art (or the cart
 * placeholder) with a short countdown. The user can:
 *   - press B to CANCEL (drop to the games grid as normal), or
 *   - press START to BOOT NOW (skip the countdown), or
 *   - do nothing -> when the countdown elapses we launch.
 *
 * Launching does exactly what "Launch" from the file browser / inspect popup does: it hands off
 * to MENU_MODE_LOAD_ROM with the ROM path already set and load_pending.launch_rom, so the normal
 * loading subroutine (art + progress bar) runs. No SC64 firmware autoboot / reset boot-mode is
 * touched -- the menu program is running the whole time.
 */

#include <libdragon.h>
#include <string.h>

#include "../fonts.h"
#include "../rom_info.h"
#include "../sound.h"
#include "../ui_components/constants.h"
#include "utils/fs.h"
#include "views.h"



static component_boxart_t *rb_boxart = NULL;
static rom_info_t          rb_info;
static bool                rb_info_loaded = false;
static uint32_t            rb_deadline_ms = 0;
static bool                rb_done        = false;   /* committed/aborted this run -- ignore further input */


static void rb_free_resources (void) {
    ui_components_boxart_free(rb_boxart);
    rb_boxart = NULL;
    if (rb_info_loaded) {
        rom_info_free_meta(&rb_info);
        rb_info_loaded = false;
    }
}

/* Boot the chosen ROM. Mirrors the browser's "Launch Game": clear the favorite/history ids and
   the browser entry so load_rom_init keeps the rom_path we already set, then request an immediate
   launch. */
static void rb_commit (menu_t *menu) {
    rb_free_resources();
    menu->load.load_history_id   = -1;
    menu->load.load_favorite_id  = -1;
    menu->browser.entry          = NULL;   /* force load_rom_init to keep our pre-set rom_path */
    menu->load.from_grid         = false;
    menu->load_pending.launch_rom = true;  /* boot straight through, skipping the detail view */
    sound_play_effect(SFX_LAUNCH);
    menu->next_mode = MENU_MODE_LOAD_ROM;
}

/* Cancel: drop to the grid as if the menu had booted normally. The chosen ROM is left configured
   (ROM boot stays enabled for next power-on); we just don't boot it this time. */
static void rb_cancel (menu_t *menu) {
    rb_free_resources();
    if (menu->load.rom_path) {
        path_free(menu->load.rom_path);
        menu->load.rom_path = NULL;
    }
    sound_play_effect(SFX_EXIT);
    menu->next_mode = MENU_MODE_GAMES_GRID;
}


void view_rom_boot_init (menu_t *menu) {
    rb_done = false;
    rb_info_loaded = false;
    rb_boxart = NULL;

    /* Defensive: startup should only route here with rom_path set, but never deref a NULL. */
    if (!menu->load.rom_path) {
        menu->next_mode = MENU_MODE_GAMES_GRID;
        return;
    }

    /* Read just enough ROM info to pick the Load-view art for this game (a per-game override
       wins, else the global Load image-view). A bad/corrupt ROM simply shows the placeholder
       here; the real MENU_MODE_LOAD_ROM step re-reads it and reports any error properly. */
    if (rom_config_load(menu->load.rom_path, &rb_info) == ROM_OK) {
        rb_info_loaded = true;
        char gc[5];
        memcpy(gc, rb_info.game_code, 4);
        gc[4] = '\0';
        int lv_ovr = rom_config_get_image_view(menu->load.rom_path, 2);   /* 2 = Load context */
        int lv = (lv_ovr >= 0 && lv_ovr < GRID_IMAGE_COUNT) ? lv_ovr : menu->settings.image_view_load;
        file_image_type_t img_type = ui_components_boxart_view_to_type(lv);
        rb_boxart = ui_components_boxart_init(
            menu->storage_prefix, gc, rb_info.title,
            path_get(menu->load.rom_path), img_type, menu->settings.use_custom_files
        );
    }

    int secs = menu->settings.rom_boot_countdown_sec;
    if (secs < 1 || secs > 15) secs = 5;     /* clamp to the offered range; default 5 */
    rb_deadline_ms = get_ticks_ms() + (uint32_t)secs * 1000;
}


static void draw (menu_t *menu, surface_t *d, int secs) {
    rdpq_attach(d, NULL);
    ui_components_background_draw();

    /* Cover art, shifted up to leave room for the countdown text below. Same blit treatment as
       the load screen (bilinear + alpha-compare so transparent cart/logo art shows the backdrop,
       not a green block); fall back to the cart placeholder when there's no usable art. */
    const int img_w = 160, img_h = 120;
    int img_x0 = DISPLAY_CENTER_X - img_w / 2;
    int img_y0 = DISPLAY_CENTER_Y - img_h / 2 - 26;
    int img_x1 = img_x0 + img_w;
    int img_y1 = img_y0 + img_h;

    surface_t *img = (rb_boxart && !rb_boxart->loading) ? rb_boxart->image : NULL;
    bool img_ok = img && img->width > 0 && img->height > 0 && img->width <= 1024 && img->height <= 1024;
    if (img_ok) {
        float sx = (float)img_w / img->width;
        float sy = (float)img_h / img->height;
        float scale = sx < sy ? sx : sy;
        int dw = (int)(img->width  * scale);
        int dh = (int)(img->height * scale);
        rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_alphacompare(ui_components_boxart_alpha_threshold(rb_boxart));   /* higher for N64 3D carts */
        rdpq_tex_blit(img, img_x0 + (img_w - dw) / 2, img_y0 + (img_h - dh) / 2,
                      &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
        rdpq_mode_pop();
    } else {
        ui_components_cart_placeholder_draw(img_x0, img_y0, img_x1, img_y1);
    }

    ui_components_main_text_draw(STL_DEFAULT, ALIGN_CENTER, VALIGN_BOTTOM,
        "Booting this game in %d...\n"
        "\n"
        "B: Cancel        S: Boot now\n",
        secs
    );

    rdpq_detach_show();
}


void view_rom_boot_display (menu_t *menu, surface_t *display) {
    int32_t remain = (int32_t)(rb_deadline_ms - get_ticks_ms());
    int secs = remain > 0 ? (remain + 999) / 1000 : 0;

    if (!rb_done) {
        if (menu->actions.back) {            /* B -> cancel to the grid */
            rb_done = true;
            rb_cancel(menu);
        } else if (menu->actions.settings || remain <= 0) {   /* START or timeout -> boot */
            rb_done = true;
            rb_commit(menu);
        }
        /* All other inputs are ignored during the countdown. */
    }

    draw(menu, display, secs);
}
