#include <stdio.h>
#include <string.h>

#include "views.h"
#include "../sound.h"
#include "../ui_components/constants.h"

#ifndef MENU_VERSION
#define MENU_VERSION "Unknown"
#endif

#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP "Unknown"
#endif

#ifndef LIBDRAGON_VERSION
#define LIBDRAGON_VERSION ""
#endif

/* Upstream N64FlashcartMenu version this fork is based on (nearest git tag), baked by
   build-rom.sh. Fallback to the last known base if a build doesn't pass it. */
#ifndef MENU_BASE_VERSION
#define MENU_BASE_VERSION "V0.3.2"
#endif

/* One scrollable list inside the system black/rainbow popup, over the live screensaver. */
#define CR_BOX_W        340       /* narrower popup; still clears the 242px logo + padding */
#define CR_BOX_H        352
#define CR_PAD          18
#define CR_GAP          10        /* logo -> text gap */
#define CR_SCROLL_STEP  26

static sys_version_t sdk_version = {0};
static sprite_t *cr_logo = NULL;
static int       cr_scroll = 0;

static void process (menu_t *menu) {
    if (menu->actions.back) {
        view_grid_screensaver_end(menu);          /* stop the background marquee (frees its RAM) */
        cr_scroll = 0;
        sound_play_effect(SFX_EXIT);
        menu->next_mode = menu->load.load_return_mode ? menu->load.load_return_mode : MENU_MODE_BROWSER;
        menu->load.load_return_mode = 0;
        return;
    }
    /* Scroll the one list (clamped in draw, where the content height is known). */
    if (menu->actions.go_down) { cr_scroll += CR_SCROLL_STEP; sound_play_effect(SFX_CURSOR); }
    if (menu->actions.go_up)   { cr_scroll -= CR_SCROLL_STEP; sound_play_effect(SFX_CURSOR); }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    /* Clear to black FIRST: the screensaver only paints tiles (no full clear), so without this
       the previous frame's grid content -- covers AND its action bar -- bleeds through every gap
       the marquee doesn't cover. */
    ui_components_background_draw();

    /* Animated background: the grid's cover-art screensaver marquee. */
    view_grid_screensaver_frame(menu);

    /* System black popup + animated rainbow rim, centred. */
    ui_components_dialog_draw(CR_BOX_W, CR_BOX_H);
    int bx0 = DISPLAY_CENTER_X - CR_BOX_W / 2;
    int by0 = DISPLAY_CENTER_Y - CR_BOX_H / 2;
    int cx0 = bx0 + CR_PAD,            cy0 = by0 + CR_PAD;
    int cx1 = bx0 + CR_BOX_W - CR_PAD, cy1 = by0 + CR_BOX_H - CR_PAD;
    int cw  = cx1 - cx0,               ch  = cy1 - cy0;

    /* libdragon version: build-time value, else the rompak section, else unknown. */
    char libdragon_str[80];
    if (LIBDRAGON_VERSION[0] != '\0') {
        snprintf(libdragon_str, sizeof(libdragon_str), "%s", LIBDRAGON_VERSION);
    } else if (sdk_version.branch[0] || sdk_version.hash[0]) {
        snprintf(libdragon_str, sizeof(libdragon_str), "%s%s (%s, %.7s)",
                 sdk_version.branch, sdk_version.dirty ? "*" : "",
                 sdk_version.commit_date, sdk_version.hash);
    } else {
        snprintf(libdragon_str, sizeof(libdragon_str), "unknown");
    }

    /* The whole list as one block (text edits come later; OSS libraries are no longer a
       separate L/Z screen -- they're just further down this list). */
    char body[1400];
    snprintf(body, sizeof(body),
        "N64ever fork by Bjerreman and contributors,\n"
        "based on N64FlashcartMenu\n"
        "by Robin Jones (NetworkFusion) and Mateusz\n"
        "Faderewski (Polprzewodnikowy), and all\n"
        "contributors.\n"
        "\n"
        "N64ever pre-release:\n"
        "https://github.com/bjerreman\n"
        "N64FlashcartMenu-N64ever - steal my code,\n"
        "this is my only planned release.\n"
        "\n"
        "Original project:\n"
        "https://github.com/Polprzewodnikowy/\n"
        "N64FlashcartMenu\n"
        "\n"
        "Licensed under the GNU AGPL-3.0 License.\n"
        "\n"
        "OSS: libdragon, libspng, minimp3, miniz,\n"
        "PixelMplus & Firple fonts.\n"
        "\n"
        "N64ever build:  %s\n"
        "Based on N64FlashcartMenu:  %s\n"
        "Build timestamp:  %s\n"
        "libdragon SDK:\n"
        "%s\n",
        MENU_VERSION, MENU_BASE_VERSION, BUILD_TIMESTAMP, libdragon_str);

    int nbytes = (int)strlen(body);
    rdpq_paragraph_t *p = rdpq_paragraph_build(&(rdpq_textparms_t) {
        .width        = cw,
        .height       = 4000,         /* unconstrained: measure the full block */
        .align        = ALIGN_LEFT,
        .valign       = VALIGN_TOP,
        .wrap         = WRAP_WORD,
        .line_spacing = TEXT_LINE_SPACING_ADJUST,
    }, FNT_DEFAULT, body, &nbytes);
    int text_h = p->bbox.y1 - p->bbox.y0;

    int logo_w = cr_logo ? cr_logo->width  : 0;
    int logo_h = cr_logo ? cr_logo->height : 0;

    /* Clamp scroll now that the content height is known. */
    int content_h = logo_h + CR_GAP + text_h;
    int maxscroll = content_h > ch ? content_h - ch : 0;
    if (cr_scroll < 0)         cr_scroll = 0;
    if (cr_scroll > maxscroll) cr_scroll = maxscroll;

    /* Clip the list to the box interior; scroll by offsetting y. */
    rdpq_set_scissor(cx0, cy0, cx1, cy1);
    int y = cy0 - cr_scroll;
    if (cr_logo) {
        rdpq_mode_push();
            rdpq_set_mode_standard();
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_sprite_blit(cr_logo, DISPLAY_CENTER_X - logo_w / 2, y, NULL);
        rdpq_mode_pop();
    }
    rdpq_paragraph_render(p, cx0, y + logo_h + CR_GAP);
    rdpq_paragraph_free(p);
    rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    rdpq_detach_show();
}

void view_credits_init (menu_t *menu) {
    sys_get_version(&sdk_version);
    if (!cr_logo) cr_logo = sprite_load("rom:/credits_logo.sprite");
    cr_scroll = 0;
    view_grid_screensaver_begin(menu);            /* start the animated background */
}

void view_credits_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}
