/**
 * @file link_disc.c
 * @brief Link picker -- two modes that share one cover-art grid.
 * @ingroup menu
 *
 * DISC ("Link disc") mode: opened from Files -> R -> File management -> "Link disc" on a
 *   selected ROM or .ndd disc. Lists the complementary-type files in the SAME directory
 *   (discs if the source is a ROM, ROMs if the source is a disc); picking one creates a
 *   combined favourite (primary = disc, secondary = ROM, type DISK) -- the shape load_disk.c
 *   boots combined. (The old settings.disc_folder model is retired.)
 *
 * CART mode: entered from the Games Grid when launching an UNLINKED expansion disk (an
 *   E-prefix 64DD code, e.g. EFZJ = F-Zero X Expansion Kit) that needs a base cartridge.
 *   Lists the user's favourite cartridges of the same game family (EFZ -> *FZ* = F-Zero X),
 *   picking one sets that disk favourite's secondary_path to the cart and boots combined.
 * See memory project-64dd-disc-linking.
 */

#include <libdragon.h>
#include <string.h>
#include <strings.h>

#include "../bookkeeping.h"
#include "../disclink.h"
#include "../disk_info.h"
#include "../game_metadata.h"
#include "../path.h"
#include "../rom_info.h"
#include "../settings.h"
#include "../ui_components.h"
#include "../ui_components/constants.h"
#include "views.h"

#define LD_MAX        64
#define LD_COLS       5
#define LD_VIS_ROWS   3
#define LD_CELL_W     108
#define LD_CELL_H     100
#define LD_GRID_X0    50
#define LD_GRID_Y0    104
#define LD_ART_MEM    (300 * 1024)

typedef struct {
    path_t             *path;
    char                name[64];
    component_boxart_t *art;
} item_t;

static item_t   items[LD_MAX];
static int      item_count = 0;
static int      sel = 0;
static int      scroll_row = 0;
static bool     linked = false;             /* DISC mode: show the confirmation box */

/* "Link disc" mode (Files -> File management -> Link disc): the SELECTED file is the source;
   the picker lists the complementary-type files in the SAME directory (discs if the source is a
   ROM, ROMs if the source is a disc). The combined favourite is ALWAYS primary = disc (.ndd),
   secondary = ROM, type DISK -- the shape load_disk.c boots combined. (Replaces the old
   disc_folder model, which is retired.) */
static path_t  *src_path    = NULL;   /* the selected file */
static char     src_disp[64];         /* its display name */
static bool     src_is_disc = false;  /* true if the selected file is the .ndd disc */

/* CART mode: the expansion disk that needs a cartridge. */
static bool     cart_mode = false;
static int      src_fav   = -1;             /* favourite index of the expansion disk */
static char     src_name[64];               /* its display name */
static char     src_code[5];                /* its 64DD code (e.g. "EFZJ") */

/* ---- helpers duplicated from games_grid.c -- KEEP IN SYNC (project-64dd-disc-linking) ---- */
static bool remap_64dd_code (path_t *p, char code[5]) {
    if (!code || memcmp(code, "NDDJ", 4) != 0) return false;
    if (!p || !path_has_value(p)) return false;
    const char *fn = path_last_get(p);
    if (!fn) return false;
    char low[160]; int n = 0;
    for (const char *s = fn; *s && n < (int)sizeof(low) - 1; s++)
        low[n++] = (*s >= 'A' && *s <= 'Z') ? (char)(*s + 32) : *s;
    low[n] = '\0';
    static const struct { const char *kw; const char *code; } m[] = {
        { "tinkling", "DKKJ" }, { "liberation", "DKKJ" }, { "kaihou", "DKKJ" },
        { "doshin", "DKDJ" },
        { "paint", "DMPJ" }, { "polygon", "DMGJ" }, { "talent", "DMTJ" },
        { "communication", "DMBJ" },
        { "expansion kit", "EFZJ" },
        { "japan pro golf", "DPGJ" }, { "pro golf", "DPGJ" },
        { "dezaemon", "DEZA" }, { "randnet", "DRDJ" },
        { "simcity", "DSCJ" }, { "sim city", "DSCJ" },
    };
    for (int i = 0; i < (int)(sizeof(m) / sizeof(m[0])); i++)
        if (strstr(low, m[i].kw)) { memcpy(code, m[i].code, 4); code[4] = '\0'; return true; }
    return false;
}

static void name_from_filename (path_t *p, char *out, size_t outsz) {
    out[0] = '\0';
    if (!p || !path_has_value(p)) return;
    const char *fn = path_last_get(p);
    if (!fn) return;
    char buf[96];
    strncpy(buf, fn, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *dot = strrchr(buf, '.'); if (dot) *dot = '\0';
    char *par = strstr(buf, " (");  if (par) *par = '\0';
    char *brk = strstr(buf, " [");  if (brk) *brk = '\0';
    int len = (int)strlen(buf);
    while (len > 0 && buf[len - 1] == ' ') buf[--len] = '\0';
    strncpy(out, buf, outsz - 1); out[outsz - 1] = '\0';
}

/* DB title for a code, else the filename. */
static void best_name (const char *code, path_t *p, char *out, size_t outsz) {
    game_meta_t m;
    if (code && code[0] && game_metadata_db_lookup(code, &m) && m.title && m.title[0]) {
        strncpy(out, m.title, outsz - 1); out[outsz - 1] = '\0';
        return;
    }
    name_from_filename(p, out, outsz);
}

static bool disc_code (path_t *p, char code[5]) {
    code[0] = '\0';
    const char *fn = path_last_get(p);
    size_t l = fn ? strlen(fn) : 0;
    if (l >= 4 && strcasecmp(fn + l - 4, ".ndd") == 0) {
        disk_info_t di;
        if (disk_info_load(p, &di) == DISK_OK) { memcpy(code, di.id, 4); code[4] = '\0'; return true; }
        return false;
    }
    rom_info_t ri;
    if (rom_info_load_quick(p, &ri) == ROM_OK) {
        memcpy(code, ri.game_code, 4); code[4] = '\0';
        remap_64dd_code(p, code);
        return true;
    }
    return false;
}

static bool is_ndd_file (const char *fn) {
    size_t l = fn ? strlen(fn) : 0;
    return l >= 4 && strcasecmp(fn + l - 4, ".ndd") == 0;
}

static bool is_rom_file (const char *fn) {
    size_t l = fn ? strlen(fn) : 0;
    if (l < 4) return false;
    const char *e = fn + l - 4;
    return strcasecmp(e, ".z64") == 0 || strcasecmp(e, ".n64") == 0 || strcasecmp(e, ".v64") == 0;
}

static void free_items (void) {
    for (int i = 0; i < item_count; i++) {
        if (items[i].art)  { ui_components_boxart_free(items[i].art); items[i].art = NULL; }
        if (items[i].path) { path_free(items[i].path); items[i].path = NULL; }
    }
    item_count = 0;
}

/* "Link disc" mode: list the complementary-type files in the CURRENT browser directory --
   discs (.ndd) if the source is a ROM, ROMs (.z64/.n64/.v64) if the source is a disc. The
   source file itself is excluded. (Keep the disc and its cartridge in the same folder.) */
static void scan_link_targets (menu_t *menu) {
    item_count = 0;
    path_t *dir = menu->browser.directory;
    if (!dir || !path_has_value(dir)) return;
    const char *src_fn = src_path ? path_last_get(src_path) : NULL;

    dir_t info;
    int r = dir_findfirst(path_get(dir), &info);
    while (r == 0 && item_count < LD_MAX) {
        if (info.d_type != DT_DIR && info.d_name[0] != '.') {
            bool want = src_is_disc ? is_rom_file(info.d_name) : is_ndd_file(info.d_name);
            bool same = src_fn && strcmp(info.d_name, src_fn) == 0;
            if (want && !same) {
                item_t *e = &items[item_count];
                e->path = path_clone_push(dir, info.d_name);
                e->art  = NULL;
                char code[5] = "";
                disc_code(e->path, code);
                best_name(code, e->path, e->name, sizeof(e->name));
                heap_stats_t hs; sys_get_heap_stats(&hs);
                if (code[0] && (hs.total - hs.used) > LD_ART_MEM) {
                    component_boxart_t *b = ui_components_boxart_init(
                        menu->storage_prefix, code, NULL, NULL, IMAGE_BOXART_FRONT, false);
                    if (b && !b->loading && b->image) e->art = b;
                    else if (b) ui_components_boxart_free(b);
                }
                item_count++;
            }
        }
        r = dir_findnext(path_get(dir), &info);
    }
}

/* Add one favourite cartridge to the picker list (art only if its code is cached). */
static void add_fav_cart (menu_t *menu, bookkeeping_item_t *bk) {
    if (item_count >= LD_MAX) return;
    if (!bk->primary_path || !path_has_value(bk->primary_path)) return;
    const char *gc = bk->game_code;
    item_t *e = &items[item_count];
    e->path = path_clone(bk->primary_path);
    e->art  = NULL;
    best_name(gc, e->path, e->name, sizeof(e->name));
    heap_stats_t hs; sys_get_heap_stats(&hs);
    if (gc[0] && (hs.total - hs.used) > LD_ART_MEM) {
        component_boxart_t *b = ui_components_boxart_init(
            menu->storage_prefix, gc, NULL, NULL, IMAGE_BOXART_FRONT, false);
        if (b && !b->loading && b->image) e->art = b;
        else if (b) ui_components_boxart_free(b);
    }
    item_count++;
}

/* CART mode: list favourite cartridges to pair with the expansion disk.
   Pass 1 -- same game family (code chars [1..2] match, leading letter N/C), e.g. EFZ* -> *FZ*
   = F-Zero X, using each favourite's CACHED game_code (no header reads).
   Pass 2 (fallback) -- if NOTHING matched the family, list ALL favourite ROMs so the user can
   still pick the base manually. This is the F-Zero X English-port case: the converted base ROM
   carries a NON-F-Zero code (e.g. NTAC), so it never matches the family but IS the right cart.
   (See the golden-goose EFZE disc -- info.txt: "works with the official US version of F-Zero X".) */
static void scan_carts (menu_t *menu) {
    item_count = 0;
    for (int i = 0; i < FAVORITES_COUNT && item_count < LD_MAX; i++) {
        bookkeeping_item_t *bk = &menu->bookkeeping.favorite_items[i];
        if (bk->bookkeeping_type != BOOKKEEPING_TYPE_ROM) continue;
        const char *gc = bk->game_code;
        if (!gc[0]) continue;
        if ((gc[0] == 'N' || gc[0] == 'C') && gc[1] == src_code[1] && gc[2] == src_code[2])
            add_fav_cart(menu, bk);
    }
    if (item_count == 0) {   /* fallback: no family match -> any favourite ROM (e.g. NTAC base) */
        for (int i = 0; i < FAVORITES_COUNT && item_count < LD_MAX; i++) {
            bookkeeping_item_t *bk = &menu->bookkeeping.favorite_items[i];
            if (bk->bookkeeping_type == BOOKKEEPING_TYPE_ROM) add_fav_cart(menu, bk);
        }
    }
}

static void ensure_visible (void) {
    int row = sel / LD_COLS;
    if (row < scroll_row) scroll_row = row;
    if (row >= scroll_row + LD_VIS_ROWS) scroll_row = row - LD_VIS_ROWS + 1;
}

static void process (menu_t *menu) {
    if (linked) {   /* DISC-mode confirmation box */
        if (menu->actions.enter || menu->actions.back) menu->next_mode = MENU_MODE_BROWSER;
        return;
    }
    if (menu->actions.back) {
        menu->next_mode = cart_mode ? MENU_MODE_GAMES_GRID : MENU_MODE_BROWSER;
        return;
    }
    if (item_count == 0) return;

    if (menu->actions.go_right && sel < item_count - 1)        sel++;
    if (menu->actions.go_left  && sel > 0)                     sel--;
    if (menu->actions.go_down  && sel + LD_COLS < item_count)  sel += LD_COLS;
    if (menu->actions.go_up    && sel - LD_COLS >= 0)          sel -= LD_COLS;
    ensure_visible();

    if (menu->actions.enter && items[sel].path) {
        if (cart_mode) {
            /* Pair the chosen cartridge into the expansion-disk favourite and boot combined.
               Updating secondary_path in place (no duplicate favourite) -- persists, so next
               launch goes straight to the combined boot without this picker. */
            if (src_fav >= 0 && src_fav < FAVORITES_COUNT) {
                bookkeeping_item_t *bk = &menu->bookkeeping.favorite_items[src_fav];
                /* Cache the pairing in the per-region disclink override (keyed by the disc's
                   game code) instead of the favorite's secondary_path -- so the English (EFZE)
                   and Japanese (EFZJ) versions link independently and never overwrite each other.
                   load_disk resolves the base from disclink (it wins over secondary_path). */
                disclink_store(menu->storage_prefix, bk->game_code, path_get(items[sel].path));
                bk->bookkeeping_type = BOOKKEEPING_TYPE_DISK;
                bookkeeping_save(&menu->bookkeeping);
                menu->load.load_history_id  = -1;
                menu->load.load_favorite_id = src_fav;
                menu->next_mode = MENU_MODE_LOAD_DISK;   /* boots disk + cart combined (base via disclink) */
            } else {
                menu->next_mode = MENU_MODE_GAMES_GRID;
            }
        } else {
            /* Always primary = disc, secondary = ROM (the shape load_disk.c boots combined),
               regardless of which one the user selected as the source. */
            path_t *disc = src_is_disc ? src_path       : items[sel].path;
            path_t *rom  = src_is_disc ? items[sel].path : src_path;
            bookkeeping_favorite_add(&menu->bookkeeping, disc, rom, BOOKKEEPING_TYPE_DISK);
            linked = true;
        }
    }
}

static void blit_art (component_boxart_t *b, int cellx, int celly, int cw, int ch) {
    if (!b || b->loading || !b->image || !b->image->width || !b->image->height) return;
    float sx = (float)cw / b->image->width, sy = (float)ch / b->image->height;
    float scale = (sx < sy) ? sx : sy;
    int dw = (int)(b->image->width * scale), dh = (int)(b->image->height * scale);
    int dx = cellx + (cw - dw) / 2, dy = celly + (ch - dh) / 2;
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_alphacompare(ui_components_boxart_alpha_threshold(b));   /* higher for N64 3D carts */
        rdpq_tex_blit(b->image, dx, dy, &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
    rdpq_mode_pop();
}

static void draw (menu_t *menu, surface_t *d) {
    (void)menu;
    rdpq_attach(d, NULL);
    ui_components_background_draw();
    ui_components_layout_draw();

    if (item_count == 0) {
        if (cart_mode) {
            ui_components_main_text_draw(STL_DEFAULT, ALIGN_CENTER, VALIGN_TOP,
                "%s\nis an expansion disc -- it needs its base cartridge.\n\n\n"
                "No cartridges in your favourites yet.\n"
                "Favourite the base ROM (for this disc, the US\n"
                "version of F-Zero X), then launch this disc again.",
                src_name);
        } else {
            ui_components_main_text_draw(STL_DEFAULT, ALIGN_CENTER, VALIGN_TOP,
                "Link: %s\n\n\n\n"
                "No %s found in this folder.\n\n"
                "Keep the disc and its cartridge in the\n"
                "same folder, then open Link disc again.",
                src_disp, src_is_disc ? "cartridge" : "disc");
        }
        ui_components_actions_bar_buttons_draw(NULL, "B: Back", NULL, NULL, NULL);
        rdpq_detach_show();
        return;
    }

    if (cart_mode)
        ui_components_main_text_draw(STL_DEFAULT, ALIGN_CENTER, VALIGN_TOP,
            "Pick the cartridge for: %s\nSelected: %s", src_name, items[sel].name);
    else
        ui_components_main_text_draw(STL_DEFAULT, ALIGN_CENTER, VALIGN_TOP,
            "Link %s\nto a %s -- selected: %s",
            src_disp, src_is_disc ? "cartridge" : "disc", items[sel].name);

    for (int i = scroll_row * LD_COLS;
         i < item_count && i < (scroll_row + LD_VIS_ROWS) * LD_COLS; i++) {
        int col = i % LD_COLS;
        int row = i / LD_COLS - scroll_row;
        int cellx = LD_GRID_X0 + col * LD_CELL_W;
        int celly = LD_GRID_Y0 + row * LD_CELL_H;
        int cw = LD_CELL_W - 10, ch = LD_CELL_H - 16;
        int ax = cellx + 5, ay = celly + 5;
        if (items[i].art) blit_art(items[i].art, ax, ay, cw, ch);
        else              ui_components_box_draw(ax, ay, ax + cw, ay + ch, RGBA32(40, 40, 40, 255));
        if (i == sel)     ui_components_border_draw(ax - 2, ay - 2, ax + cw + 2, ay + ch + 2);
    }

    ui_components_actions_bar_buttons_draw(cart_mode ? "A: Link & play" : "A: Link",
                                           "B: Cancel", NULL, NULL, NULL);

    if (linked) {
        ui_components_messagebox_draw(
            "Linked!\n\n%s\n+ %s\n\n"
            "Added to your Games Grid favourites.\nPress A or B.",
            src_is_disc ? src_disp : items[sel].name,
            src_is_disc ? items[sel].name : src_disp);
    }

    rdpq_detach_show();
}

/* Entered from the Games Grid when an unlinked expansion disk is launched. Caller then
   sets menu->next_mode = MENU_MODE_LINK_DISC. */
void view_link_disc_pick_cart (menu_t *menu, int fav_i) {
    cart_mode = true;
    src_fav   = fav_i;
    src_code[0] = '\0';
    src_name[0] = '\0';
    if (fav_i >= 0 && fav_i < FAVORITES_COUNT) {
        bookkeeping_item_t *bk = &menu->bookkeeping.favorite_items[fav_i];
        memcpy(src_code, bk->game_code, 4); src_code[4] = '\0';
        best_name(src_code, bk->primary_path, src_name, sizeof(src_name));
    }
}

void view_link_disc_init (menu_t *menu) {
    linked = false;
    sel = 0;
    scroll_row = 0;
    if (src_path) { path_free(src_path); src_path = NULL; }
    src_disp[0] = '\0';
    src_is_disc = false;

    if (cart_mode) {
        scan_carts(menu);
    } else {
        /* Source = the selected file (ROM or .ndd disc). */
        if (menu->browser.entry &&
            (menu->browser.entry->type == ENTRY_TYPE_ROM ||
             menu->browser.entry->type == ENTRY_TYPE_DISK)) {
            src_path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
            src_is_disc = (menu->browser.entry->type == ENTRY_TYPE_DISK);
            name_from_filename(src_path, src_disp, sizeof(src_disp));
        }
        scan_link_targets(menu);
    }
}

static void deinit (void) {
    free_items();
    if (src_path) { path_free(src_path); src_path = NULL; }
    cart_mode = false;
    src_fav   = -1;
}

void view_link_disc_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
    if (menu->next_mode != MENU_MODE_LINK_DISC) {
        deinit();
    }
}
