#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "../bookkeeping.h"
#include "../fonts.h"
#include "../rom_info.h"
#include "../ui_components/constants.h"
#include "../sound.h"
#include "views.h"


#define HISTORY_VISIBLE   9
#define HISTORY_ROW_H     (19 * 2)
#define HISTORY_ROW_PAD   5

/* Thumbnail dimensions for the art strip on the left of each row */
#define HIST_ART_W        48
#define HIST_ART_H        (HISTORY_ROW_H - 2)
#define HIST_ART_PAD      4

/* Custom build branding shown in the action bar. Increment with each release. */
#define BJERREMAN_BUILD   "21"


static bookkeeping_item_t *item_list;

static int hist_idx[HISTORY_COUNT];
static int hist_count = 0;
static int selected   = 0;
static int scroll     = 0;

/* Per-slot boxart cache, indexed by bookkeeping item index (0..HISTORY_COUNT-1) */
static char               hist_game_codes[HISTORY_COUNT][5];
static char               hist_titles[HISTORY_COUNT][21];
static bool               hist_info_loaded[HISTORY_COUNT];
static component_boxart_t *hist_boxarts[HISTORY_COUNT];
static bool               hist_boxart_attempted[HISTORY_COUNT];


static void rebuild_list(void) {
    hist_count = 0;
    for (int i = 0; i < HISTORY_COUNT; i++) {
        if (item_list[i].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY
            && path_has_value(item_list[i].primary_path)) {
            hist_idx[hist_count++] = i;
        }
    }
    if (selected >= hist_count) selected = hist_count - 1;
    if (selected < 0)           selected = 0;

    int max_scroll = hist_count - HISTORY_VISIBLE;
    if (max_scroll < 0)      max_scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;
    if (scroll < 0)          scroll = 0;
}

static void ensure_visible(void) {
    if (selected < scroll) {
        scroll = selected;
    } else if (selected >= scroll + HISTORY_VISIBLE) {
        scroll = selected - HISTORY_VISIBLE + 1;
    }
}

/* Returns the index within favorite_items[] if the history slot is favorited, else -1. */
static int find_favorite_index(menu_t *menu, int bk) {
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        if (menu->bookkeeping.favorite_items[i].bookkeeping_type == BOOKKEEPING_TYPE_EMPTY) continue;
        if (path_are_match(item_list[bk].primary_path,
                           menu->bookkeeping.favorite_items[i].primary_path)) return i;
    }
    return -1;
}

static void process(menu_t *menu) {
    if (hist_count == 0) {
        if (menu->actions.lz_context) {
            menu->next_mode = MENU_MODE_GAMES_GRID;  sound_play_effect(SFX_CURSOR);
        } else if (menu->actions.options) {
            menu->next_mode = MENU_MODE_BROWSER;     sound_play_effect(SFX_CURSOR);
        }
        return;
    }

    if (menu->actions.go_fast && (menu->actions.go_down || menu->actions.go_up)) {
        int dir = menu->actions.go_down ? 1 : -1;
        int before = selected;
        selected += dir * HISTORY_VISIBLE;
        if (selected < 0)               selected = 0;
        if (selected > hist_count - 1)  selected = hist_count - 1;
        ensure_visible();
        if (selected != before) sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_down && hist_count > 0) {
        selected = (selected + 1) % hist_count;                 /* wrap bottom -> top */
        ensure_visible(); sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_up && hist_count > 0) {
        selected = (selected + hist_count - 1) % hist_count;    /* wrap top -> bottom */
        ensure_visible(); sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.enter) {
        int bk = hist_idx[selected];
        menu->load.load_history_id  = bk;
        menu->load.load_favorite_id = -1;

        if (item_list[bk].bookkeeping_type == BOOKKEEPING_TYPE_DISK) {
            menu->next_mode = MENU_MODE_LOAD_DISK;
            sound_play_effect(SFX_ENTER);
        } else if (item_list[bk].bookkeeping_type == BOOKKEEPING_TYPE_ROM) {
            menu->next_mode = MENU_MODE_LOAD_ROM;
            sound_play_effect(SFX_ENTER);
        }
    } else if (menu->actions.back) {
        /* B: toggle favorite for the selected entry */
        if (hist_count > 0) {
            int bk = hist_idx[selected];
            if (item_list[bk].bookkeeping_type == BOOKKEEPING_TYPE_ROM ||
                item_list[bk].bookkeeping_type == BOOKKEEPING_TYPE_DISK) {
                int fi = find_favorite_index(menu, bk);
                if (fi >= 0) {
                    bookkeeping_favorite_remove(&menu->bookkeeping, fi);
                } else {
                    bookkeeping_favorite_add(&menu->bookkeeping,
                        item_list[bk].primary_path,
                        item_list[bk].secondary_path,
                        item_list[bk].bookkeeping_type);
                }
                sound_play_effect(SFX_ENTER);
            }
        }
    } else if (menu->actions.lz_context) {
        menu->next_mode = MENU_MODE_GAMES_GRID;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.options) {
        menu->next_mode = MENU_MODE_BROWSER;
        sound_play_effect(SFX_CURSOR);
    }
}

/* Load ROM info + boxart in the background, one step per display call. */
static void maybe_background_load(menu_t *menu) {
    /* Don't start a new PNG decode while one is in flight */
    for (int i = 0; i < hist_count; i++) {
        int bk = hist_idx[i];
        if (hist_boxarts[bk] && hist_boxarts[bk]->loading) return;
    }

    /* Priority 1: ROM header read for any uncached history slot */
    for (int i = 0; i < hist_count; i++) {
        int bk = hist_idx[i];
        if (!hist_info_loaded[bk]) {
            if (item_list[bk].bookkeeping_type == BOOKKEEPING_TYPE_ROM) {
                rom_info_t info;
                if (rom_config_load(item_list[bk].primary_path, &info) == ROM_OK) {
                    memcpy(hist_game_codes[bk], info.game_code, 4);
                    hist_game_codes[bk][4] = '\0';
                    memcpy(hist_titles[bk], info.title, 20);
                    hist_titles[bk][20] = '\0';
                    rom_info_free_meta(&info);
                }
            }
            hist_info_loaded[bk] = true;
            return;
        }
    }

    /* Priority 2: Boxart init for any slot with a game code not yet attempted */
    for (int i = 0; i < hist_count; i++) {
        int bk = hist_idx[i];
        if (!hist_boxart_attempted[bk] && hist_game_codes[bk][0]) {
            hist_boxart_attempted[bk] = true;
            component_boxart_t *b = ui_components_boxart_init(
                menu->storage_prefix,
                hist_game_codes[bk],
                hist_titles[bk][0] ? hist_titles[bk] : NULL,
                path_get(item_list[bk].primary_path),
                IMAGE_BOXART_FRONT, menu->settings.use_custom_files
            );
            hist_boxarts[bk] = b;
            if (b && b->loading) return;
        }
    }
}

static void draw_list(menu_t *menu) {
    int last = scroll + HISTORY_VISIBLE;
    if (last > hist_count) last = hist_count;

    float list_x  = VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL;
    float base_y  = VISIBLE_AREA_Y0 + TAB_HEIGHT + BORDER_THICKNESS + TEXT_MARGIN_VERTICAL;
    float name_x  = list_x + HIST_ART_W + HIST_ART_PAD;
    /* Leave room on the right for the star glyph */
    float row_w   = FILE_LIST_HIGHLIGHT_WIDTH - TEXT_MARGIN_HORIZONTAL - HIST_ART_W - HIST_ART_PAD - 14;

    for (int r = scroll; r < last; r++) {
        int   bk    = hist_idx[r];
        float row_y = base_y + (r - scroll) * HISTORY_ROW_H;

        /* Selection highlight */
        if (r == selected) {
            ui_components_box_draw(
                VISIBLE_AREA_X0,
                row_y,
                VISIBLE_AREA_X0 + FILE_LIST_HIGHLIGHT_WIDTH + LIST_SCROLLBAR_WIDTH,
                row_y + HISTORY_ROW_H,
                FILE_LIST_HIGHLIGHT_COLOR
            );
        }

        /* Art thumbnail on the left */
        int art_y = (int)row_y + 1;
        ui_components_box_draw((int)list_x, art_y, (int)list_x + HIST_ART_W, art_y + HIST_ART_H,
                               BOXART_LOADING_COLOR);
        if (hist_boxarts[bk] && !hist_boxarts[bk]->loading && hist_boxarts[bk]->image) {
            surface_t *img = hist_boxarts[bk]->image;
            float sx    = (float)HIST_ART_W / img->width;
            float sy    = (float)HIST_ART_H / img->height;
            float scale = sx < sy ? sx : sy;
            int draw_w  = (int)(img->width  * scale);
            int draw_h  = (int)(img->height * scale);
            int off_x   = (HIST_ART_W - draw_w) / 2;
            int off_y   = (HIST_ART_H - draw_h) / 2;
            rdpq_mode_push();
            rdpq_set_mode_copy(false);
            rdpq_tex_blit(img, (int)list_x + off_x, art_y + off_y,
                          &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
            rdpq_mode_pop();
        }

        /* Game name */
        const char *name = path_last_get(item_list[bk].primary_path);
        rdpq_text_printn(
            &(rdpq_textparms_t){
                .width  = row_w,
                .align  = ALIGN_LEFT,
                .valign = VALIGN_TOP,
                .wrap   = WRAP_ELLIPSES,
            },
            FNT_DEFAULT,
            name_x, row_y + HISTORY_ROW_PAD,
            name, strlen(name)
        );

        /* Secondary path for disk games */
        if (path_has_value(item_list[bk].secondary_path)) {
            const char *sec = path_last_get(item_list[bk].secondary_path);
            rdpq_text_printn(
                &(rdpq_textparms_t){
                    .width  = row_w,
                    .align  = ALIGN_LEFT,
                    .valign = VALIGN_TOP,
                    .wrap   = WRAP_ELLIPSES,
                },
                FNT_DEFAULT,
                name_x, row_y + HISTORY_ROW_PAD + 14,
                sec, strlen(sec)
            );
        }

        /* Star on the right if this entry is already in Favorites */
        if (find_favorite_index(menu, bk) >= 0) {
            rdpq_text_printf(
                &(rdpq_textparms_t){
                    .width = FILE_LIST_HIGHLIGHT_WIDTH - TEXT_MARGIN_HORIZONTAL,
                    .align = ALIGN_RIGHT,
                },
                FNT_DEFAULT,
                list_x, row_y + HISTORY_ROW_PAD,
                "^%02X*^00", STL_YELLOW
            );
        }
    }

    /* Scroll indicators */
    int ind_x = VISIBLE_AREA_X1 - 16;
    if (scroll > 0) {
        rdpq_text_printf(
            &(rdpq_textparms_t){ .width = 14, .align = ALIGN_CENTER },
            FNT_DEFAULT,
            ind_x,
            base_y + HISTORY_ROW_H / 2 - 8,
            "▲"
        );
    }
    if (scroll + HISTORY_VISIBLE < hist_count) {
        rdpq_text_printf(
            &(rdpq_textparms_t){ .width = 14, .align = ALIGN_CENTER },
            FNT_DEFAULT,
            ind_x,
            LAYOUT_ACTIONS_SEPARATOR_Y - 22,
            "▼"
        );
    }
}

static void draw(menu_t *menu, surface_t *display) {
    rdpq_attach(display, NULL);

    ui_components_background_draw();
    ui_components_tabs_common_draw(1);
    ui_components_layout_draw_tabbed();

    if (hist_count > 0) {
        draw_list(menu);
        ui_components_actions_bar_text_draw(
            STL_DEFAULT, ALIGN_LEFT, VALIGN_TOP,
            "A: Load Game\n"
            "B: Favorite"
        );
    } else {
        ui_components_main_text_draw(
            STL_DEFAULT, ALIGN_CENTER, VALIGN_CENTER,
            "No history yet."
        );
    }

    ui_components_actions_bar_text_draw(
        STL_DEFAULT, ALIGN_CENTER, VALIGN_TOP,
        "C▲▼: Scroll\n"
        "Tabs: ◀ L/Z   R ▶"
    );

    if (menu->current_time >= 0) {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT, ALIGN_RIGHT, VALIGN_TOP,
            "Bjerreman Build " BJERREMAN_BUILD "\n"
            "%s",
            ctime(&menu->current_time)
        );
    } else {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT, ALIGN_RIGHT, VALIGN_TOP,
            "Bjerreman Build " BJERREMAN_BUILD "\n"
            "\n"
        );
    }

    rdpq_detach_show();
}

void view_history_init(menu_t *menu) {
    item_list = menu->bookkeeping.history_items;
    selected  = 0;
    scroll    = 0;
    rebuild_list();
    /* Boxart cache arrays persist across tab switches — no reset needed */
}

void view_history_display(menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
    maybe_background_load(menu);
}
