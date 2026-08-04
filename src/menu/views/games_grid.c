#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bookkeeping.h"
#include "../disclink.h"
#include "../disk_info.h"
#include "../fonts.h"
#include "../game_metadata.h"
#include "../game_special.h"
#include "../library.h"
#include "../path.h"
#include "../rom_custom.h"
#include "../rom_info.h"
#include "../settings.h"
#include "../sound.h"
#include "../ui_components/constants.h"
#include "utils/fs.h"
#include "views.h"

/* ---------- Grid area ----------
   The tab bar is always shown now: Favorites plus either the pinned libraries, or (when none
   are pinned) a single placeholder tab explaining how to pin one. There are always >= 2 tabs,
   so L/R always has somewhere real to go -- see grid_tab_count(). */
#define GRID_X0             (VISIBLE_AREA_X0)
#define GRID_Y0             (grid_y0())
#define GRID_X1             (VISIBLE_AREA_X1)
#define GRID_Y1             LAYOUT_ACTIONS_SEPARATOR_Y

/* Tab set: 0 = Favorites, 1..library_count() = a pinned library. When library_count() == 0,
   grid_tab == 1 is a placeholder (not a real gallery) rather than a stale index. The file
   browser is reached via the grid's Z-menu ("File Browser" row), not a tab. */
static int grid_tab_count(void) {
    int n = library_count();
    return n > 0 ? n + 1 : 2;   /* Favorites + libraries, or Favorites + placeholder */
}
static int grid_y0(void) {
    return VISIBLE_AREA_Y0 + TAB_HEIGHT;
}

/* ---------- Flow (masonry) layout ----------
   Landscape covers are wide; Japanese covers are vertical, so a row of JP games
   packs more across.  Orientation comes from the ROM region byte (4th game-code
   character), so it is known synchronously — no need to wait for the PNG to
   decode, and the layout never reflows once a cover loads. */
#define G_PADX              8
#define G_PADY              8
#define G_GAP               6
#define LAND_W              104     /* landscape cover cell (≈158:112) */
#define LAND_H              74
#define PORT_W              80      /* Japanese vertical cover cell (taller, narrower) */
#define PORT_H              112
/* Filename caption strip ADDED to the bottom of a tile when captions are on (library tabs
   always; Favorites opt-in) -- disambiguates ROM hacks that share their base game's art.
   Its height is measured from the live font at runtime (see caption_box_h), never
   hardcoded: rdpq silently discards a text line that doesn't fit its box. */
#define CAPTION_PAD         2       /* breathing room between the art edge and the glyphs */
#define TEXT_LINES_MAX      4       /* upper bound for the measured line-height cache */
/* Full-name strip under the grid, above the action bar. The action-bar labels are
   VALIGN_BOTTOM in a box at y=411..451, so they start around y=432 (Classic) / y=437
   (Pixel), and nothing draws a separator line -- the band below GRID_Y1 is dead space,
   which is why this strip costs the grid no height and can never drop a tile row. */
#define FOOTER_GAP          2       /* px between the tile area and the full-name strip */

/* ---------- Inspect popup ---------- */
/* 2+2 inspect layout: smaller frame; top band = art | description (with a
   vertical divider between them); a full-width horizontal divider; then a bottom metadata
   strip split into two columns (Name/Developer under the art, Date/Region under the
   description). Dimensions are deliberately easy to fine-tune -- they only feed draw_inspect. */
#define INSPECT_W           316
#define INSPECT_H           237    /* 4:3 popup (316:237 = 4:3), shrunk ~1/3 diagonally from
                                      472x354 per request. Still centred in the 576x432 area. */
#define INSPECT_PAD         10
#define INSPECT_CENTER_X    ((VISIBLE_AREA_X0 + VISIBLE_AREA_X1) / 2)
#define INSPECT_CENTER_Y    ((VISIBLE_AREA_Y0 + VISIBLE_AREA_Y1) / 2)
#define INSPECT_LEFT_W      120          /* left column width (art + left metadata); scaled for the smaller 4:3 popup */
#define INSPECT_BOXART_W    INSPECT_LEFT_W
#define INSPECT_BOXART_H    160          /* top band = art height = description band height */
/* Rainbow glow layers (px outside dialog edge) */
#define GLOW_OUTER          4
#define GLOW_INNER          2
/* Description scroll step (px per button press) */
#define DESC_SCROLL_STEP    14
/* Gradient segments per border side (more = smoother rainbow) */
#define BORDER_SEGS         10
/* Hover: selected cell enlarges and gets a rainbow glow rim */
#define CELL_HOVER_ENLARGE  8
#define CELL_GLOW_OUTER     3
#define CELL_GLOW_INNER     2
/* Hold B for this many frames to enter move mode */
#define MOVE_HOLD_FRAMES    20
/* Hold R (in move mode) / B (in inspect) this many frames to toggle favorite */
#define UNFAV_HOLD_FRAMES   20
/* First pixel row available for tiles */
#define TILE_AREA_Y0        (GRID_Y0 + G_PADY)
/* Remove-favorite confirmation popup (centered over the inspect dialog) */
#define CONFIRM_W           324
#define CONFIRM_H           80

/* ---------- Per-slot data ---------- */
typedef struct {
    int fav_index;
    bool info_loaded;
    char game_code[5];
    char title[21];         /* ROM header title (fallback display) */
    char meta_name[64];     /* metadata name (nicer display name) shown on no-art tiles */
    /* author / date / description are NOT cached per-entry: inspect reads them from
       the shared inspect_meta / inspect_custom for the one selected game, so 2048
       entries don't each reserve ~590 bytes of mostly-empty text. */
    rom_save_type_t save_type;
    rom_expansion_pak_t expansion_pak;
    char destination_code;
    bool is_disk;           /* 64DD disk favorite -> no-art fallback uses the disc placeholder */
    int8_t special;         /* game_special.h table index (filename-keyed), or -1 = none */
} grid_entry_t;

/* Persistent caches across row changes — indexed by fav_index */
static grid_entry_t        fav_entry_cache[FAVORITES_COUNT];
static component_boxart_t *boxart_cache[FAVORITES_COUNT];
static bool                boxart_cache_attempted[FAVORITES_COUNT];

/* The active tab's item array: menu->bookkeeping.favorite_items[] on the Favorites tab,
   or a pinned library's item array on a library tab. Every "favorite index" (fav_i,
   fav_indices[...], near_i, gi, fa/fb) below indexes INTO THIS, not necessarily into the
   real favorites list -- see gm_target_fav_index() for the (separate) real-favorites search
   used by the handful of call sites that must stay bound to favorites.ini regardless of tab. */
static bookkeeping_item_t *grid_items     = NULL;
static int                 grid_items_cap = 0;

/* 0 = Favorites, 1..library_count() = a pinned library (see grid_tab_count() above). */
static int  grid_tab = 0;

/* "Working..." box shown for one frame before a first-visit library scan runs (the scan
   itself is a fast in-memory directory walk -- no per-ROM header reads -- but staging it
   behind a frame avoids an instant freeze on a folder with many entries). */
static bool lib_scan_working = false;
static int  lib_scan_tab     = -1;

static int  fav_indices[FAVORITES_COUNT];
static int  fav_count = 0;

/* ---- Flow layout, recomputed each frame from the favorites order ---- */
static int  row_of[FAVORITES_COUNT];        /* flow row of each favorite */
static int  xof[FAVORITES_COUNT];           /* tile x position */
static int  wof[FAVORITES_COUNT];           /* tile width  */
static int  hof[FAVORITES_COUNT];           /* tile height */
static int  row_h[FAVORITES_COUNT + 1];     /* height of each flow row */
static int  row_first[FAVORITES_COUNT + 1]; /* first favorite index of each row */
static int  total_rows = 0;

static void gm_invalidate_one_art(int fav_i, bool keep_info);   /* fwd decl (defined later) */

static int  scroll_row = 0;     /* first visible flow row */
static int  sel_fav    = 0;     /* selected favorite (index into fav order) */
static int  prev_sel_fav = -1;  /* drives the selection-grow animation */

static bool    show_inspect  = false;
static bool    show_confirm_remove = false; /* Z: remove-favorite confirmation popup */
static bool    show_confirm_sort   = false; /* Favorites submenu: confirm Sort A-Z */
static bool    show_confirm_clear  = false; /* Favorites submenu: confirm Clear all */
static bool    show_confirm_autosort = false; /* Favorites submenu: confirm enabling Always sort */
static bool    fav_working         = false; /* drives the "Working..." box during a sort */
static int     fav_working_phase   = 0;     /* 0 = show the box this frame, 1 = run */
static bool    reopen_inspect = false;      /* Z→Info page: reopen popup on return to grid */
static int     desc_scroll   = 0;

static uint8_t inspect_hue   = 0;    /* rainbow colour rotation */
static uint8_t inspect_pulse = 0;    /* brightness breathing */

static bool         grid_sq             = false;
static bool         grid_large          = false;  /* Tile size: false=Small (default), true=Large */
static bool         grid_caption_favs   = false;  /* Favorites-tab caption opt-in (library tabs always show it) */
static int          inspect_fav_i_cached = -1;
static rom_custom_t inspect_custom;
static bool         inspect_has_custom  = false;
static game_meta_t  inspect_meta;
static bool         inspect_has_meta    = false;
static int          inspect_platform    = GAME_PLATFORM_N64; /* Aleck64 / iQue warning flag */
static int          inspect_build        = GAME_BUILD_RETAIL; /* Demo / Prototype / Beta flag */
static sprite_t    *inspect_logo        = NULL; /* logo shown when a game has no description */
static int          inspect_logo_fav    = -1;
static bool         splash_active       = false;
static bool         splash_shown        = false; /* true after first display; never shows again this boot */

/* ---- Screensaver: random cover-art marquee ----
   A full-screen wall of RANDOM library cover-art. Each row scrolls horizontally,
   alternate rows in opposite directions, recycling tiles with fresh random art as
   they leave the screen (infinite). Engages after the Screensaver Timer idle period
   (ss_idle_timeout_ms) of no input on the plain grid, OR by holding L+R for SS_LR_MS.
   Any input wakes it. The real
   cursor/scroll are saved on entry and restored on wake. The favorites cover cache
   is freed on entry to make room (it reloads lazily on wake). Timers are WALL-CLOCK
   (get_ticks_ms): the grid framerate varies, so a frame counter under-counted and the
   L+R hold never completed. Tiles load from the baked DFS art (rom:), one per frame,
   with staggered row phases so the decode load never spikes. */
static bool     ss_active       = false;
static uint32_t ss_idle_last_ms = 0;   /* wall-clock ms of last input (0 = uninitialized) */
static int      ss_saved_sel    = -1;  /* cursor saved on entry (restored on wake) */
static int      ss_saved_scroll = 0;
static uint32_t ss_lr_start_ms  = 0;   /* wall-clock ms the L+R hold began (0 = not holding) */
static int      ss_wake_lock    = 0;   /* frames to ignore wake input right after entry */
static uint32_t ss_rng         = 0x4D5A0001u;

/* Idle time before the screensaver auto-engages — user-configurable (Screensaver Timer
   setting), clamped to the allowed 30 s .. 3600 s range. */
static uint32_t ss_idle_timeout_ms(menu_t *menu) {
    int s = menu->settings.screensaver_timeout_sec;
    if (s < 30)   s = 30;
    if (s > 3600) s = 3600;
    return (uint32_t)s * 1000u;
}
#define SS_LR_MS         2500  /* hold L+R ~2.5 s to engage manually (wall-clock, NOT frames --
                                  the grid framerate varies, so a frame count was unreliable) */
#define SS_CELL_W         152  /* marquee tile cell pitch -- BIG covers, FEW rows. Covers load at full
                                  source size regardless of cell size, so tile COUNT is the only RAM
                                  lever: 3 rows of large tiles (~21) fill reliably where 5-6 small rows
                                  (55+) starved and blanked out. ~152x160 -> ceil(640/152)=5 cols + 2
                                  buffer = 7 per row. */
#define SS_CELL_H         160
#define SS_MAX_ROWS         3   /* 3 big rows fill the 480 height (3*160 = 480), well within RAM */
#define SS_MAX_TILES       24   /* hard cap on concurrent marquee covers (3 rows * 7 = 21 + slack). Far
                                   below the old 55 -- the art_can_fit() probe makes any overflow a blank
                                   tile not a crash, but at this count it simply fills. */
#define SS_SCROLL_PX     0.3f  /* per-frame row scroll speed (px); halved for the 60fps render */
#define SS_INTRO_START    640  /* px each row starts off-screen (slides in on entry) */
#define SS_INTRO_STAGGER  200  /* extra off-screen px per row -> rows cascade in one by one */
#define SS_INTRO_SPEED   20.0f /* per-frame slide-in speed (px) */

static int   ss_rows = 0;                  /* marquee rows (computed at entry) */
static int   ss_per_row = 0;               /* tiles stored per row (visible cols + 2) */
static float ss_off[SS_MAX_ROWS];          /* per-row scroll offset, kept in [0, SS_CELL_W) */
static float ss_intro[SS_MAX_ROWS];        /* per-row slide-in offset (start screen black, rows
                                              enter from their side; animates to 0 then scrolls) */
static component_boxart_t *ss_tile[SS_MAX_TILES];  /* row-major: row*ss_per_row + slot */

static uint8_t grid_hue      = 0;    /* selected-cell rainbow hue */
static bool    move_mode     = false;
static int     b_held_frames = 0;
static uint8_t move_tick     = 0;
static int     move_r_held   = 0;    /* R-hold frames while in move mode (unfavorite) */

static float   sel_enter_t   = 1.0f; /* 0→1: newly-selected cell growing */

/* ====================================================================
   Grid universal "More" menu — overlaid directly on the grid/inspect
   without navigating to browser.c at all.
   ==================================================================== */

/* State */
static bool    grid_more_active      = false;  /* More menu is open */
static bool    grid_more_from_inspect= false;  /* opened via Z in inspect view */
static path_t *grid_more_target      = NULL;   /* path of the ROM this menu targets */
static bool    grid_more_is_fav      = false;  /* is grid_more_target currently a fav? */
static bool    show_metadata         = false;  /* "Game Metadata" art-register panel open */

/* Reopen-after-action flags (same pattern as browser.c grid_settings_action_fired) */
static bool    gm_fav_fired          = false;
static bool    gm_gs_fired           = false;
static int     gm_gs_row             = 0;
static bool    gm_pa_fired           = false;  /* Presents-As action → reopen submenu */
static int     gm_pa_row             = 0;
/* Return-from-Game-settings: reopen the More menu instead of dumping to the grid. */
static bool    gm_reopen_pending     = false;
static path_t *gm_reopen_path        = NULL;
static int     gm_reopen_row         = 0;     /* row to restore when the menu reopens */
static component_context_menu_t *gm_reopen_submenu = NULL;  /* submenu to re-enter (e.g. Hardware) */
static int     gm_reopen_subrow      = 0;

/* Dynamic label strings */
static char    gm_fav_lbl[24]       = "Unfavorite 0/128";
static char    gm_gs_grid_lbl[28]   = "Grid: Front";
static char    gm_gs_insp_lbl[28]   = "Inspect: Front";
static char    gm_gs_load_lbl[28]   = "Load: Front";
static char    gm_gs_sq_lbl[28]     = "Tile: Square";
static char    gm_gs_tile_lbl[28]   = "Tile size: Small";
static char    gm_gs_caption_lbl[32] = "Favorites captions: Off";
/* Per-game "Presents As" labels (override for THIS game) */
static char    gm_pa_grid_lbl[28]   = "Grid: Default";
static char    gm_pa_insp_lbl[28]   = "Inspect: Default";
static char    gm_pa_load_lbl[28]   = "Load: Default";
static char    gm_autosort_lbl[28]  = "Always sort A-Z: No";
/* These three blank out (hidden -- see ui_components/context_menu.c's empty-text-is-a-
   separator convention) on a library tab: sort/clear/always-sort only ever touch the
   real favorites list, which a library tab isn't. */
static char    gm_sort_lbl[24]      = "Run Once: Sort A-Z";
static char    gm_clear_lbl[28]     = "Clear all favorites";
static char    gm_rescan_lbl[24]    = "";   /* "Rescan library" -- shown only on a library tab */

/* History popup — forward-declared; built by gm_rebuild_history() */
#define GM_HIST_MAX 20
static char                 gm_hist_lbuf[GM_HIST_MAX][64];
static component_context_menu_t gm_hist_cm = {
    .row_selected = -1,
    .left_align   = true,
    .list = {
        {0},{0},{0},{0},{0},{0},{0},{0},{0},{0},
        {0},{0},{0},{0},{0},{0},{0},{0},{0},{0},
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

/* History modal (Files-style popup): names in gm_hist_lbuf, bookkeeping indices in
   hist_idx, count in hist_n. */
static bool show_history  = false;
static int  hist_sel      = 0;
static int  hist_scroll   = 0;
static int  hist_idx[GM_HIST_MAX];
static int  hist_n        = 0;
static int  hist_b_hold   = 0;
static bool gm_fav_dirty  = false;  /* a History fav/unfav changed bookkeeping; the grid re-flow is
                                       deferred until the overlays close (no live churn behind them) */
#define GM_HIST_VIS 12

/* Forward declarations for action functions referenced in context menus */
static void gm_launch(menu_t *menu, void *arg);
static void gm_fav_toggle(menu_t *menu, void *arg);
static void gm_game_settings(menu_t *menu, void *arg);
static void gm_file_browser(menu_t *menu, void *arg);
static void gm_go_to_mode(menu_t *menu, void *arg);
static void gm_gs_cycle_grid(menu_t *menu, void *arg);
static void gm_gs_cycle_inspect(menu_t *menu, void *arg);
static void gm_gs_cycle_load(menu_t *menu, void *arg);
static void gm_gs_toggle_square(menu_t *menu, void *arg);
static void gm_gs_toggle_tile(menu_t *menu, void *arg);
static void gm_gs_toggle_caption(menu_t *menu, void *arg);
static void gm_gs_set_region(menu_t *menu, void *arg);
static void gm_hist_launch(menu_t *menu, void *arg);
static void gm_open_history(menu_t *menu, void *arg);
static void ss_activate(menu_t *menu);
static void gm_show_metadata(menu_t *menu, void *arg);
static void gm_set_region(menu_t *menu, void *arg);
static void gm_pa_cycle_grid(menu_t *menu, void *arg);
static void gm_pa_cycle_inspect(menu_t *menu, void *arg);
static void gm_pa_cycle_load(menu_t *menu, void *arg);
static void gm_pa_reset(menu_t *menu, void *arg);
static void gm_fav_sort(menu_t *menu, void *arg);
static void gm_fav_clear(menu_t *menu, void *arg);
static void gm_fav_toggle_autosort(menu_t *menu, void *arg);
static void gm_rescan_library(menu_t *menu, void *arg);

/* Hardware submenu */
static component_context_menu_t gm_hw_cm = { .list = {
    { .text = "Controller Pak manager", .action = gm_go_to_mode, .arg = (void*)(intptr_t)(MENU_MODE_CONTROLLER_PAKFS) },
    { .text = "Time (RTC) settings",    .action = gm_go_to_mode, .arg = (void*)(intptr_t)(MENU_MODE_RTC) },
    { .text = "Flashcart information",  .action = gm_go_to_mode, .arg = (void*)(intptr_t)(MENU_MODE_FLASHCART) },
    { .text = "N64 information",        .action = gm_go_to_mode, .arg = (void*)(intptr_t)(MENU_MODE_SYSTEM_INFO) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* "Extra" submenu — menu-wide settings/info, hardware, and per-game details. */
static component_context_menu_t gm_extra_cm = { .list = {
    { .text = "Menu Settings",    .action = gm_go_to_mode, .arg = (void*)(intptr_t)(MENU_MODE_SETTINGS_EDITOR) },
    { .text = "Menu Information", .action = gm_go_to_mode, .arg = (void*)(intptr_t)(MENU_MODE_CREDITS) },
    { .text = "Hardware",         .submenu = &gm_hw_cm },
    { .text = "Game details",     .action = gm_game_settings },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* Global default region for box art — applies to every game without a per-game override
   (mirrors the per-game "Region art" submenu, but stored in settings). */
static component_context_menu_t gm_gs_region_cm = { .list = {
    { .text = "Auto",            .action = gm_gs_set_region, .arg = (void*)(uintptr_t)ROM_PRESENTS_AS_AUTO },
    { .text = "NTSC (American)", .action = gm_gs_set_region, .arg = (void*)(uintptr_t)ROM_PRESENTS_AS_NTSC },
    { .text = "PAL (European)",  .action = gm_gs_set_region, .arg = (void*)(uintptr_t)ROM_PRESENTS_AS_PAL },
    { .text = "NTSC-J (Japan)",  .action = gm_gs_set_region, .arg = (void*)(uintptr_t)ROM_PRESENTS_AS_NTSC_J },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* Grid settings submenu — image-view toggles, the global Game art region default, plus the
 * favorites tools that used to live in a separate "Grid Favorites" submenu. The action .arg
 * values and GM_GS_ROW_* below MUST track each item's row (used to reopen at the right row). */
static component_context_menu_t gm_gs_cm = { .list = {
    { .text = gm_gs_grid_lbl, .action = gm_gs_cycle_grid,    .arg = (void*)(intptr_t)0 },  /* row 0 */
    { .text = gm_gs_insp_lbl, .action = gm_gs_cycle_inspect, .arg = (void*)(intptr_t)1 },  /* row 1 */
    { .text = gm_gs_load_lbl, .action = gm_gs_cycle_load,    .arg = (void*)(intptr_t)2 },  /* row 2 */
    { .text = "" },                                              /* row 3 */
    { .text = "Game art",     .submenu = &gm_gs_region_cm },     /* row 4 (global region default) */
    { .text = "" },                                              /* row 5 */
    { .text = gm_gs_sq_lbl,   .action = gm_gs_toggle_square, .arg = (void*)(intptr_t)6 },  /* row 6 */
    { .text = gm_gs_tile_lbl, .action = gm_gs_toggle_tile,   .arg = (void*)(intptr_t)7 },  /* row 7 (tile size) */
    { .text = "" },                                              /* row 8 */
    { .text = gm_sort_lbl,           .action = gm_fav_sort },    /* row 9 */
    { .text = gm_autosort_lbl,       .action = gm_fav_toggle_autosort }, /* row 10 */
    { .text = "" },                                              /* row 11 */
    { .text = gm_clear_lbl,          .action = gm_fav_clear },   /* row 12 */
    { .text = "" },                                              /* row 13 */
    { .text = gm_gs_caption_lbl,     .action = gm_gs_toggle_caption, .arg = (void*)(intptr_t)14 },  /* row 14 */
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* Region-art toggle (which region's box art this game uses) */
static component_context_menu_t gm_pa_region_cm = { .list = {
    { .text = "Auto",            .action = gm_set_region, .arg = (void*)(uintptr_t)ROM_PRESENTS_AS_AUTO },
    { .text = "NTSC (American)", .action = gm_set_region, .arg = (void*)(uintptr_t)ROM_PRESENTS_AS_NTSC },
    { .text = "PAL (European)",  .action = gm_set_region, .arg = (void*)(uintptr_t)ROM_PRESENTS_AS_PAL },
    { .text = "NTSC-J (Japan)",  .action = gm_set_region, .arg = (void*)(uintptr_t)ROM_PRESENTS_AS_NTSC_J },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* Per-game "Presents As": per-game image-view overrides for THIS game, then Region art
   below Load (with a blank-line separator), matching the global Grid settings layout. */
static component_context_menu_t gm_pa_cm = { .list = {
    { .text = gm_pa_grid_lbl, .action = gm_pa_cycle_grid,    .arg = (void*)(intptr_t)0 },  /* row 0 */
    { .text = gm_pa_insp_lbl, .action = gm_pa_cycle_inspect, .arg = (void*)(intptr_t)1 },  /* row 1 */
    { .text = gm_pa_load_lbl, .action = gm_pa_cycle_load,    .arg = (void*)(intptr_t)2 },  /* row 2 */
    { .text = "" },                                                                        /* row 3 */
    { .text = "Region art",   .submenu = &gm_pa_region_cm },                               /* row 4 */
    { .text = "" },                                                                        /* row 5 */
    { .text = "Game Metadata",     .action = gm_show_metadata },                           /* row 6 */
    { .text = "" },                                                                        /* row 7 */
    { .text = "Reset to defaults", .action = gm_pa_reset },                                /* row 8 */
    COMPONENT_CONTEXT_MENU_LIST_END,
}};


/* ROM-specific universal More menu */
static component_context_menu_t gm_more_cm = { .list = {
    { .text = "Launch",           .action = gm_launch },
    { .text = gm_fav_lbl,         .action = gm_fav_toggle },
    { .text = gm_rescan_lbl,      .action = gm_rescan_library },   /* blank/hidden off a library tab */
    { .text = "" },
    { .text = "Game Look",        .submenu = &gm_pa_cm },
    { .text = "Grid settings",    .submenu = &gm_gs_cm },   /* + Sort / Clear / Always-sort */
    { .text = "" },
    { .text = "Extra",            .submenu = &gm_extra_cm },
    { .text = "" },
    { .text = "History",          .action = gm_open_history },
    { .text = "File Browser",     .action = gm_file_browser },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* ------------------------------------------------------------------ */
/* Release market label for the ROM's destination code. */
static const char *region_label(char dest) {
    switch (dest) {
        case 'J': return "NTSC-J";                          /* Japan */
        case 'E': return "NTSC";                            /* USA */
        case 'P': case 'A': case 'U':                       /* Europe, Australia */
        case 'D': case 'F': case 'I': case 'S':             /* DE, FR, IT, ES */
        case 'X': case 'Y':
            return "PAL";
        default:  return "Unknown";
    }
}

/* hue 0-255, brightness 0-255 */
static color_t rainbow_color(uint8_t hue, uint8_t brightness) {
    uint8_t sector = hue / 43;
    uint8_t frac   = (uint8_t)((hue % 43) * 6);
    uint8_t inv    = 255 - frac;
    uint8_t r, g, b;
    switch (sector) {
        case 0:  r=255; g=frac; b=0;    break;
        case 1:  r=inv; g=255;  b=0;    break;
        case 2:  r=0;   g=255;  b=frac; break;
        case 3:  r=0;   g=inv;  b=255;  break;
        case 4:  r=frac;g=0;    b=255;  break;
        default: r=255; g=0;    b=inv;  break;
    }
    r = (uint8_t)((uint16_t)r * brightness / 255);
    g = (uint8_t)((uint16_t)g * brightness / 255);
    b = (uint8_t)((uint16_t)b * brightness / 255);
    return RGBA32(r, g, b, 255);
}

/* Clean a raw ROM-header title for display on a no-art tile: keep only printable
   ASCII, drop the rdpq '^' style-escape, and trim trailing spaces. Removes the
   high/control-byte garbage some headers carry (e.g. "40 Winks <junk>") so the
   fallback name reads cleanly. (Note: headers that are junky in pure ASCII, like
   some JP titles, can only be fixed by a DB entry -- this just kills the worst.) */
static void sanitize_display_name(char *s) {
    int w = 0;
    for (int r = 0; s[r]; r++) {
        unsigned char c = (unsigned char)s[r];
        /* Drop BOTH rdpq_text inline-escape introducers: '^' (style) and '$' (font). A name
           with either followed by non-hex (e.g. "...$,4") makes rdpq_paragraph assert
           "invalid font/style id". */
        if (c >= 0x20 && c <= 0x7E && c != '^' && c != '$') s[w++] = (char)c;
    }
    while (w > 0 && s[w - 1] == ' ') w--;
    s[w] = '\0';
}

/* Neutralise rdpq_text inline-escape introducers ('$'=font, '^'=style) in place, replacing
   each with a space. Unlike sanitize_display_name this KEEPS '\n' and other chars, so it's
   safe for multi-line composed strings (inspect info block) and descriptions. */
static void neutralize_rdpq_escapes(char *s) {
    for (; *s; s++) if (*s == '$' || *s == '^') *s = ' ';
}

/* Draw one layer of the glow border as a continuous clockwise rainbow gradient. */
static void draw_glow_layer(int x0, int y0, int x1, int y1,
                             int pad, uint8_t base_hue, uint8_t brightness) {
    int lx0 = x0 - pad, ly0 = y0 - pad;
    int lx1 = x1 + pad, ly1 = y1 + pad;
    int W = lx1 - lx0;
    int H = y1  - y0;
    int N = BORDER_SEGS;

    for (int i = 0; i < N; i++) {
        int sx0 = lx0 + W * i / N;
        int sx1 = lx0 + W * (i + 1) / N;
        ui_components_box_draw(sx0, ly0, sx1, y0,
            rainbow_color((uint8_t)(base_hue + 64 * i / N), brightness));
    }
    for (int i = 0; i < N; i++) {
        int sy0 = y0 + H * i / N;
        int sy1 = y0 + H * (i + 1) / N;
        ui_components_box_draw(x1, sy0, lx1, sy1,
            rainbow_color((uint8_t)(base_hue + 64 + 64 * i / N), brightness));
    }
    for (int i = 0; i < N; i++) {
        int sx0 = lx1 - W * (i + 1) / N;
        int sx1 = lx1 - W * i / N;
        ui_components_box_draw(sx0, y1, sx1, ly1,
            rainbow_color((uint8_t)(base_hue + 128 + 64 * i / N), brightness));
    }
    for (int i = 0; i < N; i++) {
        int sy0 = y1 - H * (i + 1) / N;
        int sy1 = y1 - H * i / N;
        ui_components_box_draw(lx0, sy0, x0, sy1,
            rainbow_color((uint8_t)(base_hue + 192 + 64 * i / N), brightness));
    }
}

/* ------------------------------------------------------------------ */
/* True when the current grid image view is a *box* type — only box art is
   portrait (and only for JP). Cart / 3D-cart / logo art is landscape for every
   region, so those views must not force JP tiles vertical. Updated whenever the
   grid view changes (init + the cycle action). */
static bool gv_is_box = true;
static bool is_box_view(grid_image_view_t v) {
    return v == GRID_IMAGE_BOX_FRONT || v == GRID_IMAGE_BOX_BACK || v == GRID_IMAGE_BOX_3D;
}

/* Japanese *box* covers are vertical; everything else is landscape. Region is the
   4th game-code char (ROM destination byte), read straight from the header. */
static bool fav_is_portrait(int gi) {
    return gv_is_box && fav_entry_cache[fav_indices[gi]].destination_code == MARKET_JAPANESE;
}

/* Library tabs always caption their tiles (a pinned folder is typically full of ROM hacks
   that share one base game's art); the Favorites tab is opt-in via the setting. */
static bool grid_captions_on(void) {
    return (grid_tab != 0) || grid_caption_favs;
}

/* Measured box heights, indexed by line count. 0 = not measured yet.
   Reset in init() -- Settings can swap the font live. */
static int caption_h_cached[TEXT_LINES_MAX + 1] = { 0 };

/* Height of an N-line filename caption strip, measured from whatever font is loaded.
   This CANNOT be a hardcoded constant: rdpq DISCARDS a text line outright when the box is
   shorter than the font's line height (rdpq_paragraph.c: `ascent - descent >= height`
   -> skip_current_line), so an undersized strip renders NOTHING rather than clipping --
   which is exactly how the original 14px constant failed silently, the bundled 12px font
   needing 15. Measuring keeps a legacy or SD custom.font64 from resurrecting the bug.

   Ask the layout engine directly -- lay N lines out at each height and take the first that
   actually emits every glyph -- rather than deriving it from the reported bbox: a laid-out
   line's bbox is (ascent - descent + line_gap + 1), and mkfont gives BOTH bundled fonts a
   line_gap of -1, so the bbox reads one pixel UNDER the height the skip test demands.
   Probe with the wrap mode we actually render with, and with explicit newlines: rdpq
   re-evaluates the skip test per line, so a too-short box drops the LAST lines, not all. */
static int caption_box_h(int lines) {
    if (lines < 1) lines = 1;
    if (lines > TEXT_LINES_MAX) lines = TEXT_LINES_MAX;
    if (caption_h_cached[lines]) return caption_h_cached[lines];

    /* "Ag" per line, newline-separated. Newlines aren't glyphs, so a fully laid-out
       probe reports exactly 2 chars per line; anything less means a line was skipped. */
    char probe[TEXT_LINES_MAX * 3];
    int  plen = 0;
    for (int i = 0; i < lines; i++) {
        if (i) probe[plen++] = '\n';
        probe[plen++] = 'A';
        probe[plen++] = 'g';
    }
    probe[plen] = '\0';

    for (int h = 8; h <= 64 * TEXT_LINES_MAX; h++) {
        int nbytes = plen;
        rdpq_paragraph_t *p = rdpq_paragraph_build(
            &(rdpq_textparms_t){ .width = 64, .height = h, .wrap = WRAP_WORD },
            FNT_DEFAULT, probe, &nbytes);
        bool fits = p && p->nchars >= 2 * lines;
        if (p) rdpq_paragraph_free(p);
        if (fits) { caption_h_cached[lines] = h + CAPTION_PAD; return caption_h_cached[lines]; }
    }

    /* Unreachable in practice; keep the strip usable. */
    caption_h_cached[lines] = 15 * lines + CAPTION_PAD;
    return caption_h_cached[lines];
}

/* How many lines of the loaded font actually survive a box `h` px tall. Anything laid out
   past this is INVISIBLE, not clipped -- rdpq drops the overflowing lines outright. */
static int text_lines_that_fit(int h) {
    for (int n = TEXT_LINES_MAX; n > 1; n--) {
        if (caption_box_h(n) - CAPTION_PAD <= h) return n;
    }
    return 1;
}

/* Rows of `tile_h`-tall tiles that fit the tile area, mirroring visible_last_row(). */
static int grid_rows_that_fit(int tile_h) {
    int avail = (GRID_Y1 - G_PADY) - TILE_AREA_Y0;
    int n = (avail + G_GAP) / (tile_h + G_GAP);
    return n > 0 ? n : 1;
}

/* Square tiles are 104x104 against Box's 104x74, so they have the headroom for a second
   caption line; Box tiles don't and stay single-line.

   But only take that second line if it's actually free. Strip height is measured from the
   live font, and the Classic font's is nearly double the Pixel font's -- on Large square
   tiles a 2-line strip pushes 135+40 past the point where two rows fit, halving the grid.
   Checking rather than assuming keeps this correct for an SD custom.font64 too. */
static int caption_lines(void) {
    if (!grid_sq) return 1;
    int base = grid_large ? (LAND_W * 13 / 10) : LAND_W;
    return (grid_rows_that_fit(base + caption_box_h(2))
            >= grid_rows_that_fit(base + caption_box_h(1))) ? 2 : 1;
}

/* Strip height for the active tile shape. */
static int caption_strip_h(void) { return caption_box_h(caption_lines()); }

/* Flow the favorites (in order) into rows of variable width: landscape covers are
   wide, Japanese covers are narrow, so JP-heavy rows pack more across and any row
   that holds a vertical cover grows taller. */
static void compute_flow(void) {
    int left  = GRID_X0 + G_PADX;
    int right = GRID_X1 - G_PADX;
    int avail = right - left;
    int x = left, row = 0, rh = 0;
    /* The caption strip is ADDED to the cell rather than carved out of it, so cover art
       keeps its designed size when captions are on (at the cost of ~one row per screen). */
    int cap = grid_captions_on() ? caption_strip_h() : 0;

    /* Pass 1: assign tiles to rows, left-aligned */
    for (int gi = 0; gi < fav_count; gi++) {
        bool port = fav_is_portrait(gi);
        int w = grid_sq ? LAND_W : (port ? PORT_W : LAND_W);
        int h = grid_sq ? LAND_W : (port ? PORT_H : LAND_H);
        if (grid_large) { w = w * 13 / 10; h = h * 13 / 10; }   /* Large tiles: ~30% bigger -> ~one less row */
        h += cap;   /* after the Large scaling: text doesn't grow with the tile */
        if (x + w > right && x > left) { row++; x = left; rh = 0; }
        if (x == left) row_first[row] = gi;
        row_of[gi] = row; xof[gi] = x; wof[gi] = w; hof[gi] = h;
        x += w + G_GAP;
        if (h > rh) rh = h;
        row_h[row] = rh;
    }
    total_rows = (fav_count > 0) ? row + 1 : 0;

    /* Pass 2: center each row — partial rows (JP-only, mixed, last row) get
       equal left/right margins instead of being left-aligned. */
    for (int r = 0; r < total_rows; r++) {
        int last = row_first[r];
        for (int gi = row_first[r]; gi < fav_count && row_of[gi] == r; gi++) last = gi;
        int row_used = xof[last] + wof[last] - left;
        int offset   = (avail - row_used) / 2;
        if (offset > 0) {
            for (int gi = row_first[r]; gi < fav_count && row_of[gi] == r; gi++) {
                xof[gi] += offset;
            }
        }
    }
}

/* Last flow row that fits fully in the tile area. */
static int visible_last_row(void) {
    int top    = TILE_AREA_Y0;
    int bottom = GRID_Y1 - G_PADY;
    int y = top, r = scroll_row;
    if (r >= total_rows) return total_rows - 1;
    while (r < total_rows) {
        if (y + row_h[r] > bottom) break;
        y += row_h[r] + G_GAP;
        r++;
    }
    if (r == scroll_row) return scroll_row;   /* always show at least the top row */
    return r - 1;
}

/* Tile in target_row whose horizontal centre is nearest center_x. */
static int nearest_in_row(int target_row, int center_x) {
    if (target_row < 0 || target_row >= total_rows) return -1;
    int best = -1, bestd = 1 << 30;
    for (int gi = row_first[target_row]; gi < fav_count && row_of[gi] == target_row; gi++) {
        int c = xof[gi] + wof[gi] / 2;
        int d = c > center_x ? c - center_x : center_x - c;
        if (d < bestd) { bestd = d; best = gi; }
    }
    return best;
}

static void ensure_visible(void) {
    if (fav_count == 0) { scroll_row = 0; return; }
    if (sel_fav < 0) sel_fav = 0;
    if (sel_fav > fav_count - 1) sel_fav = fav_count - 1;
    int sr = row_of[sel_fav];
    if (sr < scroll_row) scroll_row = sr;
    else while (visible_last_row() < sr && scroll_row < total_rows - 1) scroll_row++;
    if (scroll_row > total_rows - 1) scroll_row = total_rows - 1;
    if (scroll_row < 0) scroll_row = 0;
}

/* ------------------------------------------------------------------ */
static void rebuild_fav_list(menu_t *menu) {
    (void)menu;
    fav_count = 0;
    for (int i = 0; i < grid_items_cap; i++) {
        /* Include ROM *and* DISK favorites (64DD .ndd standalone discs and Link-disc combined
           pairs are type DISK) -- only skip empty slots. The grid pipeline handles disks
           (load_rom_info_into DISK branch, JP orientation, launch routing). */
        if (grid_items[i].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            fav_indices[fav_count++] = i;
        }
    }
}

static bool fav_meta_dirty = false;   /* favorites.ini has newly cached game_code(s) to persist */

/* Apply a per-game "Presents As" region override to the tile's effective region
   (drives orientation + box-art region). AUTO keeps the ROM's own region. */
/* Metadata lookup honoring the "Use custom files" setting: DB-only (instant) when
   off, full per-game-override probe when on. */
/* Fill metadata from a filename-keyed special edition (game_special.h). */
static bool gm_special_fill(int special, game_meta_t *out) {
    if (special < 0) return false;
    const game_special_t *s = game_special_get(special);
    if (!s) return false;
    memset(out, 0, sizeof(*out));
    out->title       = s->title;
    out->developer   = s->developer;
    out->release_jp  = s->release_jp;
    out->release_us  = s->release_us;
    out->release_eu  = s->release_eu;
    out->description = s->description;
    return true;
}

static bool gm_meta_lookup(menu_t *menu, const char *game_code, int special, game_meta_t *out) {
    if (gm_special_fill(special, out)) return true;   /* special edition beats the code-keyed DB */
    return menu->settings.use_custom_files
        ? game_metadata_get(menu->storage_prefix, game_code, out)
        : game_metadata_db_lookup(game_code, out);
}

static void apply_presents_as(grid_entry_t *e, int presents_as) {
    switch (presents_as) {
        case ROM_PRESENTS_AS_NTSC_J: e->destination_code = (char)MARKET_JAPANESE;       break;
        case ROM_PRESENTS_AS_NTSC:   e->destination_code = (char)MARKET_NORTH_AMERICA;  break;
        case ROM_PRESENTS_AS_PAL:    e->destination_code = (char)MARKET_EUROPEAN_BASIC; break;
        default: break;
    }
}

/* Effective region override for a tile: an explicit per-game choice always wins; otherwise
   the global "Game art" default (Grid settings) applies. Disks are 64DD (Japan-only) art, so
   the global default never repaints them to a US/PAL region. AUTO everywhere -> ROM's region. */
static int effective_presents_as(menu_t *menu, int per_game, bool is_disk) {
    if (per_game) return per_game;
    if (is_disk)  return ROM_PRESENTS_AS_AUTO;
    return menu->settings.image_region_default;
}

/* 64DD ROM conversions all carry the generic header game-code "NDDJ" (the conversion
   format stamps a placeholder, not each disk's real ID), so they can't be told apart by
   code. When we see NDDJ, recover the real 64DD code by matching keywords in the (user's
   descriptive) filename. Returns true if `code` was remapped. Codes match the baked art
   dirs so both metadata (3-char base) and art resolve. */
/* Keyword-match a descriptive 64DD filename to a known DB code, IGNORING any header/disk
   id. Used for (a) ROM conversions whose header is the generic NDDJ and (b) real .ndd disks
   whose system-area id isn't one of our DB codes (the DB was built from the conversion
   codes, so most disk ids miss it). Returns true + fills code[5] on a keyword hit. */
#define GM_ISAL(c)  (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))
#define GM_ISALN(c) (GM_ISAL(c) || ((c) >= '0' && (c) <= '9'))

static bool match_64dd_filename(path_t *p, char code[5]) {
    if (!p || !path_has_value(p)) return false;
    const char *fn = path_last_get(p);
    if (!fn) return false;

    /* (a) An explicit 4-char NUS/NUD product code after a "NUD-"/"NUS-" tag is the most
       reliable signal and carries the REAL region byte (the keyword table below can't):
       e.g. "NUD-DPGJ-JPN.ndd" -> DPGJ, "NUD-DRDJ01-JPN" -> DRDJ. We never assume it's
       present -- descriptive dumps fall through to the keyword table. (Only the unambiguous
       NUD-/NUS- tag is trusted; a bare "(XXXX)" would false-match "(Demo)" etc.) */
    for (const char *s = fn; *s; s++) {
        char a = s[0], b = (a ? s[1] : 0), d = (b ? s[2] : 0);
        bool tag = ((s == fn || !GM_ISALN((unsigned char)s[-1])) &&
                    (a == 'N' || a == 'n') && (b == 'U' || b == 'u') &&
                    (d == 'D' || d == 'd' || d == 'S' || d == 's') && s[3] == '-');
        if (!tag) continue;
        const char *c = s + 4;                          /* code right after "NUD-"/"NUS-" */
        if (GM_ISAL((unsigned char)c[0]) && GM_ISALN((unsigned char)c[1]) &&
            GM_ISALN((unsigned char)c[2]) && GM_ISAL((unsigned char)c[3])) {
            for (int i = 0; i < 4; i++)
                code[i] = (c[i] >= 'a' && c[i] <= 'z') ? (char)(c[i] - 32) : c[i];
            code[4] = '\0';
            return true;
        }
    }

    /* (b) Keyword table for descriptive filenames. Codes are the JP releases (64DD is
       Japan-only); the 3-char base drives the DB + art (try_dfs_sprite region-fallback
       finds E-region baked art), the region byte [3]='J' gives correct orientation. */
    char low[160]; int n = 0;
    for (const char *s = fn; *s && n < (int)sizeof(low) - 1; s++)
        low[n++] = (*s >= 'A' && *s <= 'Z') ? (char)(*s + 32) : *s;
    low[n] = '\0';
    static const struct { const char *kw; const char *code; } m[] = {
        { "tinkling",      "DKKJ" }, { "liberation", "DKKJ" }, { "kaihou", "DKKJ" },
        { "doshin",        "DKDJ" },
        { "paint",         "DMPJ" }, { "polygon", "DMGJ" }, { "talent", "DMTJ" },
        { "communication", "DMBJ" },
        { "expansion kit", "EFZJ" },
        { "japan pro golf","DPGJ" }, { "pro golf", "DPGJ" },
        { "dezaemon",      "DEZA" }, { "randnet", "DRDJ" },
        { "simcity",       "DSCJ" }, { "sim city", "DSCJ" },
    };
    for (int i = 0; i < (int)(sizeof(m) / sizeof(m[0])); i++) {
        if (strstr(low, m[i].kw)) { memcpy(code, m[i].code, 4); code[4] = '\0'; return true; }
    }
    return false;
}

/* A 64DD ROM conversion carries the generic header code "NDDJ"; recover its real code from
   the (descriptive) filename. (Disks go through match_64dd_filename directly, gated on a DB
   miss -- see load_rom_info_into / gm_fav_sort_key.) */
static bool remap_64dd_code(path_t *p, char code[5]) {
    if (!code || memcmp(code, "NDDJ", 4) != 0) return false;
    return match_64dd_filename(p, code);
}

/* Universal last-resort name: derive a readable title from the FILENAME (basename, drop the
   extension and any " (region)"/" [tag]" suffix). No assumptions about specific games -- it
   just shows whatever the user named the file, so an unidentified ROM is never "Unknown". */
static void fav_name_from_filename(path_t *p, char *out, size_t outsz) {
    out[0] = '\0';
    if (!p || !path_has_value(p)) return;
    const char *fn = path_last_get(p);
    if (!fn) return;
    char buf[96];
    strncpy(buf, fn, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *dot = strrchr(buf, '.');     if (dot) *dot = '\0';   /* drop extension */
    char *par = strstr(buf, " (");     if (par) *par = '\0';   /* drop " (region/version)" */
    char *brk = strstr(buf, " [");     if (brk) *brk = '\0';
    int len = (int)strlen(buf);
    while (len > 0 && buf[len - 1] == ' ') buf[--len] = '\0';
    strncpy(out, buf, outsz - 1); out[outsz - 1] = '\0';
}

/* The item's filename with the extension stripped -- the ONLY string that tells ROM hacks and
   homebrew apart. They carry their base game's game code, so the metadata DB (which keys on
   the first three code chars) hands them the base game's title, and the boxart loader hands
   them its cover. Everything user-facing in the grid names the file instead.

   Unlike fav_name_from_filename() this keeps " (region)"/" [tag]" suffixes: they're often the
   only difference between two builds of the same hack. */
static void grid_file_label(int fav_i, char *out, size_t outsz) {
    out[0] = '\0';
    if (fav_i < 0 || fav_i >= grid_items_cap) return;
    path_t *p = grid_items[fav_i].primary_path;
    if (!p || !path_has_value(p)) return;
    const char *fn = path_last_get(p);
    if (!fn) return;
    strncpy(out, fn, outsz - 1);
    out[outsz - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    sanitize_display_name(out);
}

static void load_rom_info_into(grid_entry_t *e, int fav_i, menu_t *menu) {
    bookkeeping_item_t *bk = &grid_items[fav_i];
    e->is_disk = (bk->bookkeeping_type == BOOKKEEPING_TYPE_DISK);
    e->special = -1;
    path_t *p = bk->primary_path;
    if (!p || !path_has_value(p)) return;

    e->fav_index = fav_i;

    /* Filename-keyed special-edition match (e.g. OoT Master Quest under the OoT code).
       Computed once here while the path is in hand; cached as a 1-byte index so the
       metadata sites don't need the (large) filename. */
    e->special = (int8_t)game_special_match(path_last_get(p));

    /* Reject a cached NON-ALNUM code (an old build may have stored a disk's garbage system
       id). Clearing it forces re-resolution below (DISK branch / header read) so the fast
       path can't keep serving junk like "- ,4". */
    if (bk->game_code[0]) {
        for (int k = 0; k < 4 && bk->game_code[k]; k++) {
            char c = bk->game_code[k];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
                bk->game_code[0] = '\0'; fav_meta_dirty = true; break;
            }
        }
    }

    /* Fast path: the ROM code is cached in favorites.ini, so we read NO ROM header.
       Region (orientation + art) comes from the code, the display name from the DB
       (fetched only for no-art tiles), and full text metadata loads on inspect.
       This is what makes a large grid boot near-instant. */
    if (bk->game_code[0]) {
        memcpy(e->game_code, bk->game_code, 4); e->game_code[4] = '\0';
        if (remap_64dd_code(p, e->game_code)) {       /* fix a cached generic NDDJ */
            memcpy(bk->game_code, e->game_code, 5);   /* persist the real code */
            fav_meta_dirty = true;
        }
        e->title[0]         = '\0';
        /* Disks are 64DD = Japan-only -> always JP orientation; ROMs use their region byte. */
        e->destination_code = (bk->bookkeeping_type == BOOKKEEPING_TYPE_DISK)
                              ? (char)MARKET_JAPANESE : e->game_code[3];
        apply_presents_as(e, effective_presents_as(menu, bk->presents_as, e->is_disk));
        e->info_loaded = true;
        return;
    }

    /* 64DD disk favorites: the code + region come from the disk's system area (id[4]),
       NOT a cartridge header. rom_info_load_quick fails on a .ndd, which is why these
       tiles showed the generic "64DD" art and "Unknown" metadata. Read the disk id and
       cache it like a ROM code (the DB lookup keys on the 3-char base, region from id[3]). */
    if (bk->bookkeeping_type == BOOKKEEPING_TYPE_DISK) {
        /* Identify the disk. The FILENAME is the most reliable source -- an explicit
           NUD-XXXX product code (real region byte) or a keyword -- so try it first; only
           if the name yields nothing do we fall back to the disk's internal system-area id
           (which often isn't a DB code). Name/art resolve downstream from the 3-char base,
           or from the filename if nothing matches (never a blank "Unknown"). */
        e->game_code[0] = '\0';
        if (!match_64dd_filename(p, e->game_code)) {
            disk_info_t di;
            if (disk_info_load(p, &di) != DISK_OK) return;
            memcpy(e->game_code, di.id, 4); e->game_code[4] = '\0';
            /* Some .ndd dumps carry a non-code system id (garbage bytes). A non-alnum code
               yields no DB hit + bogus art; clear it so the tile falls back to the filename
               name and skips art rather than showing "(.480FL(4<"-style junk. */
            for (int k = 0; k < 4; k++) {
                char c = e->game_code[k];
                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
                    e->game_code[0] = '\0'; break;
                }
            }
        }
        e->title[0]         = '\0';
        e->destination_code = (char)MARKET_JAPANESE;   /* 64DD is Japan-only -> JP orientation,
                                                          regardless of the code's region byte
                                                          (art still uses game_code, e.g. DEZA). */
        apply_presents_as(e, effective_presents_as(menu, bk->presents_as, true));
        e->info_loaded = true;
        memcpy(bk->game_code, e->game_code, 5);   /* cache the resolved code for next boot */
        fav_meta_dirty = true;
        return;
    }

    /* Slow path (first time we see this favorite): read the header once, then cache
       the code back to favorites.ini so future boots take the fast path. */
    rom_info_t info;
    if (rom_info_load_quick(p, &info) != ROM_OK) return;
    memcpy(e->game_code, info.game_code, 4); e->game_code[4] = '\0';
    memcpy(e->title, info.title, 20);        e->title[20]    = '\0';
    e->save_type        = info.save_type;
    e->expansion_pak    = info.features.expansion_pak;
    e->destination_code = info.destination_code;
    if (remap_64dd_code(p, e->game_code)) {       /* 64DD conversion (NDDJ) -> real code */
        e->destination_code = e->game_code[3];
        e->title[0] = '\0';                       /* generic header title; use the DB name */
    }
    apply_presents_as(e, effective_presents_as(menu, bk->presents_as, false));
    rom_info_free_meta(&info);
    e->info_loaded = true;

    memcpy(bk->game_code, e->game_code, 5);   /* cache for next boot */
    fav_meta_dirty = true;
}

/* Kick off the boxart load for one favorite (one decode at a time).
   Returns true if a PNG decode was started (caller should yield this frame). */
static bool gm_load_boxart(menu_t *menu, int fav_i) {
    boxart_cache_attempted[fav_i] = true;
    /* Per-game override (gameconfigs .ini) takes precedence over the global. */
    int gv_ovr = rom_config_get_image_view(grid_items[fav_i].primary_path, 0);
    int gv = (gv_ovr >= 0 && gv_ovr < GRID_IMAGE_COUNT) ? gv_ovr : menu->settings.image_view_grid;
    file_image_type_t img_type = ui_components_boxart_view_to_type(gv);
    /* The presents_as region override only affects box front/back art; cart, 3D and
       logo art aren't region-specific, so use the base region for them. */
    char eff_gc[5];
    memcpy(eff_gc, fav_entry_cache[fav_i].game_code, 4);
    if (gv == GRID_IMAGE_BOX_FRONT || gv == GRID_IMAGE_BOX_BACK) {
        eff_gc[3] = fav_entry_cache[fav_i].destination_code;
    }
    eff_gc[4] = '\0';
    component_boxart_t *b = ui_components_boxart_init(
        menu->storage_prefix, eff_gc,
        fav_entry_cache[fav_i].title,
        path_get(grid_items[fav_i].primary_path),
        img_type, menu->settings.use_custom_files);
    boxart_cache[fav_i] = b;
    return (b && b->loading);
}

/* Cover-load memory safety. libdragon's asset_load HARD-ASSERTS (crashes) on a failed
   malloc, and the real failure mode here is FRAGMENTATION, not exhaustion: churning
   80K RGBA32 cart3d covers against 35K RGBA16 fronts shatters the free pool, so an 80K
   surface alloc can fail with 2-3 MB still free. A total-free threshold (the old
   ART_MEM_LOAD/EVICT) is blind to that. Instead we PROBE for a real contiguous block
   the size of the biggest cover (+ decode slack): if malloc can hand one back, asset_load
   will succeed; the probe frees it immediately so the block is still there for the load
   (single-threaded, nothing allocates in between). Eviction and the load gate both use
   the probe, so the working set self-limits to what actually fits and a failed fit
   degrades to a placeholder this frame instead of crashing. */
#define ART_FIT_PROBE  (128 * 1024)   /* biggest cover surface (RGBA32 cart3d ~80K) + decode headroom */
static bool art_can_fit(void) {
    void *p = malloc(ART_FIT_PROBE);
    if (p) { free(p); return true; }
    return false;
}

static void maybe_background_load(menu_t *menu) {
    /* While the screensaver runs it owns the cover memory (it freed the favorites
       cache on entry to make room for its marquee). Reloading favorites covers here
       would compete for that memory and risk OOM, so don't. They reload on wake. */
    if (ss_active) return;

    /* A PNG cover may be decoding asynchronously (SD art). That must gate only ART
       loads (one decode at a time) — never the cheap ROM-info load, otherwise text
       metadata stalls behind every cover and games linger as "Unknown". */
    bool png_busy = false;
    for (int i = 0; i < fav_count; i++) {
        int fav_i = fav_indices[i];
        if (boxart_cache[fav_i] && boxart_cache[fav_i]->loading) { png_busy = true; break; }
    }

    int vis_first_row = scroll_row - 1; if (vis_first_row < 0) vis_first_row = 0;
    int vis_last_row  = visible_last_row() + 1;

    int sfi = (fav_count > 0 && sel_fav >= 0 && sel_fav < fav_count) ? fav_indices[sel_fav] : -1;

    /* Bound loaded covers so RAM can't be exhausted. CRITICAL: libdragon's
       asset_load HARD-ASSERTS ("Out of memory") on a failed malloc — it never
       returns NULL — so a cover load that runs the heap dry crashes the menu.
       Each cover is an uncompressed RGBA16 surface (~35-50KB), but the splash and
       background sprites, font atlas and framebuffers also live in RAM, so the
       safe count is modest even with an Expansion Pak. Two guards: a conservative
       per-RAM count cap, AND a live free-heap floor (evict here / gate loads
       below) that protects against the assert regardless of the count estimate. */
    static int art_cap = 0;
    if (art_cap == 0) {
        art_cap = (get_memory_size() >= (8 << 20)) ? 80 : 8;   /* deep preload buffer. The art_can_fit() probe -- not this cap -- prevents OOM, so the cap is generous: caches as many as actually fit (probe-bounded). 80 deepens the ahead-of-cursor buffer but stays short of the memory edge on a front-heavy grid (where filling right to the edge would churn evict/reload on every scroll). cart3d-heavy grids are probe-limited below this regardless. */
    }
    int sel_row = (sel_fav >= 0 && sel_fav < fav_count) ? row_of[sel_fav] : scroll_row;

    /* Evict the farthest covers while over the count cap OR while free heap is
       below the safety floor (but keep a few so we never blank everything). */
    for (;;) {
        int loaded = 0;
        for (int i = 0; i < fav_count; i++) if (boxart_cache[fav_indices[i]]) loaded++;
        bool over_cap = (loaded > art_cap);
        /* Fragmentation guard: if no contiguous cover-sized block is available, evict the
           farthest cover to coalesce free space (keep a few so we never blank everything). */
        bool low_mem  = !art_can_fit() && (loaded > 3);
        if (!over_cap && !low_mem) break;
        int worst_i = -1, worst_dist = -1;
        for (int i = 0; i < fav_count; i++) {
            int fav_i = fav_indices[i];
            /* Never evict the selected cover, an empty slot, or a cover that is
               mid-decode — freeing the latter aborts the active PNG decode and
               can use-after-free the decoder callback's data. */
            if (fav_i == sfi || !boxart_cache[fav_i] || boxart_cache[fav_i]->loading) continue;
            int d = row_of[i] - sel_row; if (d < 0) d = -d;
            /* The grid wraps vertically, so the top and bottom rows are neighbours:
               use circular distance so covers just past the wrap count as near. */
            if (total_rows > 0 && (total_rows - d) < d) d = total_rows - d;
            if (d > worst_dist) { worst_dist = d; worst_i = i; }
        }
        if (worst_i < 0) break;
        int fav_i = fav_indices[worst_i];
        ui_components_boxart_free(boxart_cache[fav_i]);
        boxart_cache[fav_i] = NULL;
        boxart_cache_attempted[fav_i] = false;
    }

    /* ---- ROM info: selected + visible only ----
       Loading info reads metadata from SD (several file-opens per ROM), so doing
       it for every favorite up front made boot take ~a minute on a 128-game grid.
       Instead, load only the selected + visible tiles here; the rest load lazily,
       paired with their art (below). The masonry layout defaults un-loaded tiles
       to landscape and reflows as info arrives, so partial info is fine. */
    if (sfi >= 0 && !fav_entry_cache[sfi].info_loaded) {
        load_rom_info_into(&fav_entry_cache[sfi], sfi, menu);
        fav_entry_cache[sfi].info_loaded = true;
        return;
    }
    for (int i = 0; i < fav_count; i++) {
        if (row_of[i] < vis_first_row || row_of[i] > vis_last_row) continue;
        int fav_i = fav_indices[i];
        if (!fav_entry_cache[fav_i].info_loaded) {
            load_rom_info_into(&fav_entry_cache[fav_i], fav_i, menu);
            fav_entry_cache[fav_i].info_loaded = true;
            return;
        }
    }

    /* ---- Boxart (+ lazy info) for tiles near the cursor ----
       Work outward from the cursor by circular row distance (first page, then the
       wrap-adjacent rows, then outward), bounded by art_cap. A tile's info is read
       only when it's about to get art, so we never bulk-read all 128. When the
       cache is full, swap the farthest loaded cover for a nearer one so the working
       set follows the cursor. (png_busy is false here, so no loaded cover is
       mid-decode and freeing one cannot abort an active decode.) */
    if (png_busy) return;
    /* Hard OOM guard: never START a cover load unless a real contiguous block exists --
       asset_load asserts (crashes) on a failed malloc, and total-free is blind to
       fragmentation. The eviction above already freed far covers to coalesce one if it
       could; if even that can't make room, skip to a placeholder this frame (the tile
       retries on a later frame once eviction opens a block). */
    if (!art_can_fit()) return;
    if (sfi >= 0 && !boxart_cache_attempted[sfi] && fav_entry_cache[sfi].game_code[0]) {
        if (gm_load_boxart(menu, sfi)) return;
    }
    {
        int near_i = -1, near_d = 1 << 30;   /* nearest tile still needing art */
        int far_i  = -1, far_d  = -1;        /* farthest cover currently loaded */
        int loaded2 = 0;
        for (int i = 0; i < fav_count; i++) {
            int fav_i = fav_indices[i];
            int d = row_of[i] - sel_row; if (d < 0) d = -d;
            if (total_rows > 0 && (total_rows - d) < d) d = total_rows - d;
            if (boxart_cache[fav_i]) {
                loaded2++;
                if (fav_i != sfi && d > far_d) { far_d = d; far_i = fav_i; }
            } else if (!boxart_cache_attempted[fav_i] && d < near_d) {
                near_d = d; near_i = fav_i;
            }
        }
        if (near_i >= 0) {
            bool in_budget = (loaded2 < art_cap) || (far_i >= 0 && near_d < far_d);
            if (in_budget) {
                if (!fav_entry_cache[near_i].info_loaded) {
                    /* Need the game code before we can choose art. */
                    load_rom_info_into(&fav_entry_cache[near_i], near_i, menu);
                    fav_entry_cache[near_i].info_loaded = true;
                    return;
                }
                if (!fav_entry_cache[near_i].game_code[0]) {
                    boxart_cache_attempted[near_i] = true;   /* no code → no art possible */
                } else {
                    if (loaded2 >= art_cap) {                /* at cap, but nearer → swap */
                        ui_components_boxart_free(boxart_cache[far_i]);
                        boxart_cache[far_i] = NULL;
                        boxart_cache_attempted[far_i] = false;
                    }
                    bool started = gm_load_boxart(menu, near_i);
                    /* Only NOW, if no cover resolved, resolve a display name for the
                       text fallback (we don't read text metadata for tiles with art).
                       Prefer the in-memory DB; if the game isn't in the DB, read the
                       ROM header once for its title so a no-art tile is never a
                       nameless "unknown" dummy. */
                    grid_entry_t *ne = &fav_entry_cache[near_i];
                    if (!started && !boxart_cache[near_i] && !ne->meta_name[0]) {
                        game_meta_t m;
                        if (gm_meta_lookup(menu, ne->game_code, ne->special, &m) &&
                            m.title && m.title[0]) {
                            strncpy(ne->meta_name, m.title, sizeof(ne->meta_name) - 1);
                            ne->meta_name[sizeof(ne->meta_name) - 1] = '\0';
                        } else if (!ne->title[0] &&
                                   grid_items[near_i].bookkeeping_type
                                       != BOOKKEEPING_TYPE_DISK) {
                            /* ROMs only -- reading a .ndd disk through the cartridge header
                               path yields a garbage "title" (e.g. "(.480FL(4<"). Disks fall
                               straight through to the filename fallback below. */
                            rom_info_t hi;
                            path_t *pp = grid_items[near_i].primary_path;
                            if (pp && path_has_value(pp) && rom_info_load_quick(pp, &hi) == ROM_OK) {
                                char t[21]; memcpy(t, hi.title, 20); t[20] = '\0';
                                for (int k = 19; k >= 0 && t[k] == ' '; k--) t[k] = '\0';
                                sanitize_display_name(t);
                                if (t[0]) {
                                    strncpy(ne->meta_name, t, sizeof(ne->meta_name) - 1);
                                    ne->meta_name[sizeof(ne->meta_name) - 1] = '\0';
                                }
                                rom_info_free_meta(&hi);
                            }
                        }
                        /* Last resort (no DB entry, no header title -- e.g. a 64DD ROM
                           conversion): show the user's own filename rather than "Unknown". */
                        if (!ne->meta_name[0]) {
                            fav_name_from_filename(
                                grid_items[near_i].primary_path,
                                ne->meta_name, sizeof(ne->meta_name));
                        }
                    }
                    return;
                }
            }
        }
    }

    /* Reaching here means nothing was loaded this frame (the window is settled).
       If we cached any game codes during this pass, persist favorites.ini once now
       so the next boot can take the no-header-read fast path. */
    if (fav_meta_dirty) {
        bookkeeping_save_favorites(&menu->bookkeeping);
        fav_meta_dirty = false;
    }
}

/* Swap two entries by their global index in fav_indices[].
   Updates bookkeeping + all caches so the order is always consistent. */
static void swap_global_favs(menu_t *menu, int gi_a, int gi_b) {
    /* Belt-and-braces: move mode is Favorites-only (hidden on a library tab -- see
       process()'s move_mode gating), but never write through grid_items unless it's
       really the real favorites array. */
    if (grid_items != menu->bookkeeping.favorite_items) return;
    int fa = fav_indices[gi_a];
    int fb = fav_indices[gi_b];

    bookkeeping_item_t tmp_bk = grid_items[fa];
    grid_items[fa] = grid_items[fb];
    grid_items[fb] = tmp_bk;

    grid_entry_t tmp_e = fav_entry_cache[fa];
    fav_entry_cache[fa] = fav_entry_cache[fb];
    fav_entry_cache[fb] = tmp_e;
    fav_entry_cache[fa].fav_index = fa;
    fav_entry_cache[fb].fav_index = fb;

    component_boxart_t *tmp_b = boxart_cache[fa];
    boxart_cache[fa] = boxart_cache[fb];
    boxart_cache[fb] = tmp_b;

    bool tmp_at = boxart_cache_attempted[fa];
    boxart_cache_attempted[fa] = boxart_cache_attempted[fb];
    boxart_cache_attempted[fb] = tmp_at;
}

/* Reorder the selected favorite to position `to` via sequential adjacent swaps,
   so the games in between shift one step (iOS-home-screen style). */
static void move_to(menu_t *menu, int to) {
    if (grid_items != menu->bookkeeping.favorite_items) return;
    int from = sel_fav;
    int step = (to > from) ? 1 : -1;
    for (int j = from; j != to; j += step) swap_global_favs(menu, j, j + step);
    sel_fav = to;
}

/* After bookkeeping_favorite_remove(fav_i) the bookkeeping array is compacted
   (items shift down to fill the gap). Mirror that same shift on our caches so
   fav_entry_cache[i] and boxart_cache[i] stay in sync with bookkeeping[i]. */
static void cache_compact_after_remove(int fav_i) {
    ui_components_boxart_free(boxart_cache[fav_i]);
    for (int i = fav_i; i < FAVORITES_COUNT - 1; i++) {
        fav_entry_cache[i]           = fav_entry_cache[i + 1];
        fav_entry_cache[i].fav_index = i;
        boxart_cache[i]              = boxart_cache[i + 1];
        boxart_cache_attempted[i]    = boxart_cache_attempted[i + 1];
    }
    memset(&fav_entry_cache[FAVORITES_COUNT - 1], 0, sizeof(grid_entry_t));
    fav_entry_cache[FAVORITES_COUNT - 1].special = -1;   /* 0 would false-match the first special edition */
    boxart_cache[FAVORITES_COUNT - 1]           = NULL;
    boxart_cache_attempted[FAVORITES_COUNT - 1] = false;
}

/* Rebuild the favorites list and clamp selection/scroll to it. */
/* Per-favorite initial letter for A-Z section jumps (C-left/right). Captured from the SORT
   KEY -- the same name source the A-Z sort uses -- so EVERY tile, including off-screen and
   non-DB games, has a correct letter without a per-jump SD read. Indexed by (sorted)
   bookkeeping slot; valid only while the list stays sorted (set by gm_run_sort_az, cleared
   on any list change). Without this, az_initial fell back to "Unknown"->'U' for unloaded
   non-DB games, so the letter jump found no letter boundary and only nudged one tile. */
static char az_letter[FAVORITES_COUNT];
static bool az_letter_ready = false;
static int  az_letter_count = -1;   /* fav_count when az_letter was built; mismatch = stale */

static char first_initial(const char *nm) {
    for (const char *p = nm; p && *p; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') return (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }
    return '#';
}

/* A-Z section order: letters A..Z = 0..25, numbers/symbols after Z = 26 (matches gm_sort_cmp). */
static int az_order(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    return 26;
}

/* True when the favorites need a (re)sort: any has no cached initial (freshly added) or the
   cached initials aren't in non-decreasing A-Z order. Uses ONLY the persisted initials, so
   it's cheap (no ROM-header reads) -- this is what lets a boot skip the sort when nothing
   changed, instead of re-deriving every name (and re-reading non-DB headers) each launch. */
static bool fav_needs_resort(menu_t *menu) {
    int prev = -1;
    for (int gi = 0; gi < fav_count; gi++) {
        char si = grid_items[fav_indices[gi]].sort_initial;
        if (!si) return true;                 /* a favorite with no cached initial -> just added */
        int o = az_order(si);
        if (o < prev) return true;            /* out of order */
        prev = o;
    }
    return false;
}

static void refresh_grid(menu_t *menu);   /* fwd decl (defined below; grid_switch_tab needs it) */

/* Point grid_items/grid_items_cap at whichever tab is active. Clamps grid_tab back to
   Favorites if it's gone stale (e.g. the active library was just unpinned) -- but the
   placeholder (no libraries pinned, grid_tab == 1) is a permanent, intentional state, not a
   stale one, so it must not fall through to Favorites. */
static void grid_repoint_items(menu_t *menu) {
    if (grid_tab > 0) {
        if (library_count() == 0 && grid_tab == 1) {
            grid_items     = NULL;   /* placeholder tab: no gallery, cap = 0 keeps it unreachable */
            grid_items_cap = 0;
            return;
        }
        library_t *lib = library_get(grid_tab - 1);
        if (lib) {
            /* lib->items is NULL until the first scan; grid_items_cap = 0 in that case
               keeps every grid_items[...] access unreachable until it's populated. */
            grid_items     = lib->items;
            grid_items_cap = lib->items ? lib->count : 0;
            return;
        }
        grid_tab = 0;   /* genuinely stale (e.g. mid-list unpin) -- fall through to Favorites */
    }
    grid_items     = menu->bookkeeping.favorite_items;
    grid_items_cap = FAVORITES_COUNT;
}

/* Free every tile's cached info/art. The caches are keyed by position in whichever array
   is active, so switching tabs must flush them -- a library's tile N would otherwise show
   the previous tab's tile N stale art. */
static void grid_flush_caches(void) {
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        ui_components_boxart_free(boxart_cache[i]);
        boxart_cache[i] = NULL;
        fav_entry_cache[i].info_loaded = false;
    }
    memset(boxart_cache_attempted, 0, sizeof(boxart_cache_attempted));
}

/* Switch tabs (dir = +1 for R/tab_next, -1 for L/tab_prev), wrapping around Favorites and the
   pinned libraries (or the placeholder, when none are pinned). The file browser is no longer
   part of this cycle -- it's reached via the grid's Z-menu "File Browser" row instead. */
static void grid_switch_tab(menu_t *menu, int dir) {
    int lib_n = library_count();
    int count = lib_n > 0 ? lib_n + 1 : 2;
    grid_tab  = ((grid_tab + dir) % count + count) % count;

    if (grid_tab > 0 && lib_n > 0) {
        library_t *lib = library_get(grid_tab - 1);
        if (lib && !lib->scanned) {
            /* Draw the "Working..." box this frame; the scan itself runs next frame
               (see the lib_scan_working check near the top of process()). */
            lib_scan_working = true;
            lib_scan_tab     = grid_tab;
            return;
        }
    }

    grid_flush_caches();
    grid_repoint_items(menu);
    refresh_grid(menu);
    sel_fav    = 0;
    scroll_row = 0;
}

static void refresh_grid(menu_t *menu) {
    grid_repoint_items(menu);
    rebuild_fav_list(menu);
    if (sel_fav > fav_count - 1) sel_fav = fav_count - 1;
    if (sel_fav < 0) sel_fav = 0;
    compute_flow();
    ensure_visible();
}

/* ------------------------------------------------------------------ */
/* Draw the ROM's filename (extension stripped) into the caption strip at the bottom of a
   tile. This is the only thing that tells ROM hacks apart: they share their base game's
   game code, so they resolve to the same cover art AND the same metadata DB name. */
static void draw_tile_caption(int fav_i, int x0, int y, int w, int h, int lines) {
    char cap[64];
    grid_file_label(fav_i, cap, sizeof(cap));
    if (!cap[0]) return;

    /* WRAP_ELLIPSES cannot wrap: rdpq draws the "..." and then abandons the rest of the
       paragraph (rdpq_paragraph.c, WRAP_ELLIPSES -> skip_current_line -> return). So a
       multi-line caption has to be WRAP_WORD, which truncates SILENTLY instead. That's
       tolerable only because the full name is always on screen in the footer strip. */
    rdpq_text_printn(
        &(rdpq_textparms_t){
            .width   = w,
            .height  = h,
            .align   = ALIGN_CENTER,
            .valign  = VALIGN_CENTER,
            .wrap    = (lines > 1) ? WRAP_WORD : WRAP_ELLIPSES,
        },
        FNT_DEFAULT,
        x0, y,
        cap, strlen(cap)
    );
}

/* anim_f 0.0=not selected, 1.0=fully selected — scales size and glow.
   The cell already matches the cover's orientation, so the rainbow rim hugs
   the art instead of a loose square. */
static void draw_tile(int gi, int cx, int cy, int cw, int ch, float anim_f, bool is_move_sel) {
    int fav_i = fav_indices[gi];
    component_boxart_t *b = boxart_cache[fav_i];

    int enlarge = (int)(CELL_HOVER_ENLARGE * anim_f);
    int x0 = cx - enlarge;
    int y0 = cy - enlarge;
    int x1 = cx + cw + enlarge;
    int y1 = cy + ch + enlarge;

    if (anim_f > 0.01f) {
        if (is_move_sel) {
            int bob = (move_tick & 8) ? -1 : 1;
            y0 += bob; y1 += bob;
            uint8_t mb_i = (move_tick & 4) ? (uint8_t)(200 * anim_f) : (uint8_t)(60 * anim_f);
            uint8_t mb_o = (move_tick & 4) ? (uint8_t)(80  * anim_f) : (uint8_t)(20 * anim_f);
            draw_glow_layer(x0, y0, x1, y1, CELL_GLOW_OUTER, (uint8_t)(grid_hue * 3), mb_o);
            draw_glow_layer(x0, y0, x1, y1, CELL_GLOW_INNER, (uint8_t)(grid_hue * 3), mb_i);
        } else {
            draw_glow_layer(x0, y0, x1, y1, CELL_GLOW_OUTER, grid_hue, (uint8_t)(55  * anim_f));
            draw_glow_layer(x0, y0, x1, y1, CELL_GLOW_INNER, grid_hue, (uint8_t)(160 * anim_f));
        }
    }

    ui_components_box_draw(x0, y0, x1, y1, RGBA32(0x00, 0x00, 0x00, 0xFF));

    int inner_w = x1 - x0;
    int inner_h = y1 - y0;

    /* Duplicate-art disambiguation: ROM hacks correctly match their base game's art (same
       game code), so several hacks can render as identical tiles -- a filename caption tells
       them apart. compute_flow() already grew the cell by cap_h, so the art band below is
       the tile's designed size. Note the hover enlarge inflates inner_h but not cap_h, so
       the selection's extra pixels go to the art and the strip stays a constant height. */
    bool show_caption = grid_captions_on();
    int  cap_lines    = caption_lines();
    int  cap_h = show_caption ? caption_strip_h() : 0;
    int  art_h = inner_h - cap_h;
    if (art_h < 1) art_h = 1;

    if (b && !b->loading && b->image) {
        float sx = (float)inner_w / b->image->width;
        float sy = (float)art_h / b->image->height;
        float scale = (sx < sy) ? sx : sy;
        int draw_w = (int)(b->image->width  * scale);
        int draw_h = (int)(b->image->height * scale);
        int off_x  = (inner_w - draw_w) / 2;
        int off_y  = (art_h - draw_h) / 2;

        rdpq_mode_push();
            rdpq_set_mode_standard();
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_mode_alphacompare(ui_components_boxart_alpha_threshold(b));   /* higher for N64 3D carts */
            rdpq_tex_blit(b->image, x0 + off_x, y0 + off_y,
                          &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
        rdpq_mode_pop();

        if (show_caption) {
            draw_tile_caption(fav_i, x0, y0 + art_h, inner_w, cap_h, cap_lines);
        }
    } else {
        /* No art: grey cartridge (or 64DD disc) placeholder plus the filename.

           This used to centre fav_entry_cache[].meta_name in the tile instead, and only fall
           back to the placeholder when that name was empty. But meta_name comes from the
           metadata DB, which matches on the first THREE chars of the game code -- so hacks
           and homebrew carrying a retail base code confidently rendered as the base game
           ("Super Mario 64" over a Mario hack), and because a resolved name always beat the
           placeholder, the artwork-less look was a wrong title rather than a neutral cart.
           The filename is the only string that's actually right for these, so it's all we
           show. */
        int cap_h_local   = cap_h;
        int cap_lines_local = cap_lines;
        if (!show_caption) {
            /* Captions are off (Favorites opt-out) but an unlabelled grey cart is
               unidentifiable, so force one. compute_flow() reserved no room for it, so carve
               it out of this tile's own art band -- everything stays inside the tile's rect,
               leaving the grid layout byte-identical for the tiles that do have art. One
               line even in Square mode: two would eat too much of a 104px placeholder. */
            cap_lines_local = 1;
            cap_h_local     = caption_box_h(1);
        }
        int art_h_local = inner_h - cap_h_local;
        if (art_h_local < 1) art_h_local = 1;

        if (fav_entry_cache[fav_i].is_disk) {
            ui_components_disc_placeholder_draw(x0, y0, x1, y0 + art_h_local);
        } else {
            ui_components_cart_placeholder_draw(x0, y0, x1, y0 + art_h_local);
        }

        draw_tile_caption(fav_i, x0, y0 + art_h_local, inner_w, cap_h_local, cap_lines_local);
    }
}

/* Full filename of the highlighted game, in the dead strip between the tile area and the
   action bar. A tile caption is only ~13 characters wide, so this is the surface that
   actually answers "what am I looking at" -- at 556px it fits ~70 characters, i.e. it never
   truncates in practice, which is what makes the caption's silent truncation acceptable. */
static void draw_selected_name_footer(void) {
    if (fav_count <= 0 || sel_fav < 0 || sel_fav >= fav_count) return;

    char name[128];
    grid_file_label(fav_indices[sel_fav], name, sizeof(name));
    if (!name[0]) return;

    rdpq_text_printn(
        &(rdpq_textparms_t){
            .style_id = STL_DEFAULT,
            .width    = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height   = caption_box_h(1),
            .align    = ALIGN_CENTER,
            .valign   = VALIGN_CENTER,
            .wrap     = WRAP_ELLIPSES,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL, GRID_Y1 + FOOTER_GAP,
        name, strlen(name)
    );
}

/* Pick the display name (metadata.ini name preferred, ROM header title fallback)
   and strip any trailing " (...)" region/revision tag into `out`. */
static void game_display_name(const grid_entry_t *e, char *out, size_t out_size) {
    char title[21];
    const char *src;
    if (e->meta_name[0] != '\0') {
        src = e->meta_name;
    } else {
        memcpy(title, e->title, 20);
        title[20] = '\0';
        for (int k = 19; k >= 0 && title[k] == ' '; k--) title[k] = '\0';
        src = (title[0] != '\0') ? title : "Unknown";
    }
    strncpy(out, src, out_size - 1);
    out[out_size - 1] = '\0';
    sanitize_display_name(out);
    char *paren = strstr(out, " (");
    if (paren) *paren = '\0';
}

/* The inspect popup shows its OWN cover, resolved from the Inspect view setting
   (per-game override, else settings.image_view_inspect) — NOT the grid tile's cover.
   One slot, loaded on open and freed on close. */
static component_boxart_t *insp_boxart     = NULL;
static int                 insp_boxart_fav = -1;

static void gm_free_inspect_boxart(void) {
    if (insp_boxart) { ui_components_boxart_free(insp_boxart); insp_boxart = NULL; }
    insp_boxart_fav = -1;
}

static void gm_load_inspect_boxart(menu_t *menu, int fav_i) {
    if (insp_boxart_fav == fav_i && insp_boxart) return;   /* already current */
    gm_free_inspect_boxart();
    insp_boxart_fav = fav_i;
    if (!art_can_fit()) return;   /* leave NULL -> placeholder; fragmentation-safe */
    int iv_ovr = rom_config_get_image_view(grid_items[fav_i].primary_path, 1);
    int iv = (iv_ovr >= 0 && iv_ovr < GRID_IMAGE_COUNT) ? iv_ovr : menu->settings.image_view_inspect;
    file_image_type_t img_type = ui_components_boxart_view_to_type(iv);
    /* presents_as region only matters for box front/back; other types aren't region-specific. */
    char eff_gc[5];
    memcpy(eff_gc, fav_entry_cache[fav_i].game_code, 4);
    if (iv == GRID_IMAGE_BOX_FRONT || iv == GRID_IMAGE_BOX_BACK) {
        eff_gc[3] = fav_entry_cache[fav_i].destination_code;
    }
    eff_gc[4] = '\0';
    insp_boxart = ui_components_boxart_init(
        menu->storage_prefix, eff_gc,
        fav_entry_cache[fav_i].title,
        path_get(grid_items[fav_i].primary_path),
        img_type, menu->settings.use_custom_files);
}

static void draw_inspect(menu_t *menu) {
    if (fav_count == 0 || sel_fav < 0 || sel_fav >= fav_count) return;
    int fav_i = fav_indices[sel_fav];

    /* Make sure this game's ROM info is loaded before we read it — otherwise the
       inspect would show "Unknown" if opened before the background load reached it. */
    if (!fav_entry_cache[fav_i].info_loaded) {
        load_rom_info_into(&fav_entry_cache[fav_i], fav_i, menu);
        fav_entry_cache[fav_i].info_loaded = true;
        inspect_fav_i_cached = -1;   /* force the metadata lookup below to re-run */
    }

    if (fav_i != inspect_fav_i_cached) {
        inspect_fav_i_cached = fav_i;
        bookkeeping_item_t *item = &grid_items[fav_i];
        if (item->primary_path && path_has_value(item->primary_path)) {
            inspect_has_custom = rom_custom_load(
                menu->storage_prefix,
                path_get(item->primary_path),
                &inspect_custom
            );
            /* Inspect is a deliberate action, so verify the cached game code against
               the actual ROM here — self-heals a stale cache (e.g. a ROM file swapped
               at the same path) by refreshing the code, tile and art. ROMs ONLY: a .ndd
               disk read through the cartridge header path returns garbage that differs from
               the real disk code (e.g. EFZJ), which would clobber it and break the metadata
               lookup -> "Unknown". Disk codes come from disk_info.id / the filename, not here. */
            rom_info_t hi;
            if (item->bookkeeping_type != BOOKKEEPING_TYPE_DISK &&
                rom_info_load_quick(item->primary_path, &hi) == ROM_OK) {
                /* Apply the 64DD-conversion remap to the freshly-read code BEFORE comparing,
                   or a conversion (header NDDJ) always looks "changed" vs the remapped cache
                   (e.g. DKDJ) -- which clobbered the remap and broke the metadata lookup. */
                char hc[5]; memcpy(hc, hi.game_code, 4); hc[4] = '\0';
                remap_64dd_code(item->primary_path, hc);
                if (memcmp(hc, item->game_code, 4) != 0) {
                    memcpy(item->game_code, hc, 4); item->game_code[4] = '\0';
                    memcpy(fav_entry_cache[fav_i].game_code, hc, 4);
                    fav_entry_cache[fav_i].game_code[4] = '\0';
                    fav_entry_cache[fav_i].meta_name[0] = '\0';
                    fav_entry_cache[fav_i].info_loaded = false;
                    gm_invalidate_one_art(fav_i, false);
                    bookkeeping_save_favorites(&menu->bookkeeping);
                }
                rom_info_free_meta(&hi);
            }
        } else {
            inspect_has_custom = false;
        }
        grid_entry_t *ec = &fav_entry_cache[fav_i];
        inspect_has_meta = gm_meta_lookup(menu, ec->game_code, ec->special, &inspect_meta);
        /* No DB/special metadata and no cached name yet -> derive a clean display name from the
           filename right now, so Inspect ALWAYS shows a human-readable name (never a bare
           "Unknown") for blank-header ROMs: iQue ports, protos, homebrew, Aleck64 carts, etc.
           (game_display_name strips region/version tags + sanitizes on render.) */
        if (!inspect_has_meta && !ec->meta_name[0])
            fav_name_from_filename(grid_items[fav_i].primary_path,
                                   ec->meta_name, sizeof(ec->meta_name));
        /* Flag non-N64 hardware variants (may not run on a stock N64 / SC64). Filename first;
           a 'Z'-media game code (Seta Aleck64) is a reliable secondary signal. */
        inspect_platform = game_platform_classify(
            path_last_get(grid_items[fav_i].primary_path));
        if (inspect_platform == GAME_PLATFORM_N64 && ec->game_code[0] == 'Z')
            inspect_platform = GAME_PLATFORM_ALECK64;
        /* Demo / proto / beta builds: flagged from the filename so a missing cover
           reads as expected (these rarely have their own art). */
        inspect_build = game_build_classify(
            path_last_get(grid_items[fav_i].primary_path));
    }
    grid_entry_t *e = &fav_entry_cache[fav_i];

    /* Dialog coords, centered in the grid area */
    int dlg_x0 = INSPECT_CENTER_X - INSPECT_W / 2;
    int dlg_y0 = INSPECT_CENTER_Y - INSPECT_H / 2;
    int dlg_x1 = dlg_x0 + INSPECT_W;
    int dlg_y1 = dlg_y0 + INSPECT_H;

    /* Breathing brightness: triangle wave drives a subtle pulse */
    uint8_t bv = inspect_pulse < 128 ? inspect_pulse : (uint8_t)(255 - inspect_pulse);
    uint8_t bright_outer = (uint8_t)(22 + bv / 8);   /* 22–38 */
    uint8_t bright_inner = (uint8_t)(95 + bv / 2);   /* 95–159 */

    draw_glow_layer(dlg_x0, dlg_y0, dlg_x1, dlg_y1, GLOW_OUTER, inspect_hue, bright_outer);
    draw_glow_layer(dlg_x0, dlg_y0, dlg_x1, dlg_y1, GLOW_INNER, inspect_hue, bright_inner);

    /* Dialog fill */
    ui_components_box_draw(dlg_x0, dlg_y0, dlg_x1, dlg_y1,
                           RGBA32(0x0C, 0x0C, 0x14, 0xFF));

    /* ---- Left column: boxart (Inspect view, not the grid cover) ---- */
    int art_x = dlg_x0 + INSPECT_PAD;
    int art_y = dlg_y0 + INSPECT_PAD;

    gm_load_inspect_boxart(menu, fav_i);
    component_boxart_t *b = insp_boxart;
    if (b && !b->loading && b->image) {
        float sx = (float)INSPECT_BOXART_W / b->image->width;
        float sy = (float)INSPECT_BOXART_H / b->image->height;
        float scale = (sx < sy) ? sx : sy;
        int draw_w = (int)(b->image->width  * scale);
        int draw_h = (int)(b->image->height * scale);
        int off_x  = (INSPECT_BOXART_W - draw_w) / 2;
        int off_y  = (INSPECT_BOXART_H - draw_h) / 2;
        rdpq_mode_push();
            rdpq_set_mode_standard();
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_mode_alphacompare(ui_components_boxart_alpha_threshold(b));   /* higher for N64 3D carts */
            rdpq_tex_blit(b->image, art_x + off_x, art_y + off_y,
                          &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
        rdpq_mode_pop();
    } else if (b && b->loading) {
        ui_components_box_draw(art_x, art_y,
                               art_x + INSPECT_BOXART_W, art_y + INSPECT_BOXART_H,
                               BOXART_LOADING_COLOR);
    } else if (fav_i >= 0 && fav_i < FAVORITES_COUNT && fav_entry_cache[fav_i].is_disk) {
        /* No art, 64DD disk -> grey 64DD-disc placeholder. */
        ui_components_disc_placeholder_draw(art_x, art_y,
                               art_x + INSPECT_BOXART_W, art_y + INSPECT_BOXART_H);
    } else {
        /* No art for this game: show the grey N64 cartridge placeholder. */
        ui_components_cart_placeholder_draw(art_x, art_y,
                               art_x + INSPECT_BOXART_W, art_y + INSPECT_BOXART_H);
    }

    /* ---- Layout regions ---- */
    /* Computed before the metadata strings below, which size themselves to meta_h. */
    int top_h   = INSPECT_BOXART_H;                              /* top band = art height */
    int div_x   = art_x + INSPECT_LEFT_W + INSPECT_PAD / 2;      /* vertical divider x */
    int right_x = div_x + INSPECT_PAD / 2 + 2;
    int right_w = dlg_x1 - INSPECT_PAD - right_x;
    int right_y = art_y;
    int right_h = top_h;                                         /* description = top band height */
    int hdiv_y  = art_y + top_h + INSPECT_PAD / 2;               /* horizontal divider y */
    int meta_y  = hdiv_y + 1 + INSPECT_PAD / 2;
    int meta_h  = dlg_y1 - INSPECT_PAD - meta_y;

    /* Dividers removed per request — div_x (column split) and hdiv_y (metadata-strip baseline)
       are still used for layout below, just no longer drawn as lines. */

    /* ---- Metadata for the bottom 2+2 strip ---- */
    char left_info[256];
    char right_info[256];
    if (inspect_has_custom && inspect_custom.field_count > 0) {
        /* User custom fields ("up to 6"): distribute 2+2 -- first half under the art,
           the rest under the description. */
        int n = inspect_custom.field_count;
        int half = (n + 1) / 2;
        int off = 0; left_info[0] = '\0';
        for (int fi = 0; fi < half && off < (int)sizeof(left_info) - 2; fi++)
            off += snprintf(left_info + off, sizeof(left_info) - off, "%s: %s\n",
                            inspect_custom.fields[fi].label, inspect_custom.fields[fi].value);
        off = 0; right_info[0] = '\0';
        for (int fi = half; fi < n && off < (int)sizeof(right_info) - 2; fi++)
            off += snprintf(right_info + off, sizeof(right_info) - off, "%s: %s\n",
                            inspect_custom.fields[fi].label, inspect_custom.fields[fi].value);
    } else {
        char name_buf[64];
        game_display_name(e, name_buf, sizeof(name_buf));

        /* Title: DB meta > rom header */
        const char *display_name = (inspect_has_meta && inspect_meta.title)
                                   ? inspect_meta.title : name_buf;
        /* Developer: DB meta, else fallback */
        const char *author = (inspect_has_meta && inspect_meta.developer)
                             ? inspect_meta.developer : "Developer unknown";
        const char *standard = e->destination_code ? region_label(e->destination_code) : "Unknown";
        /* Hardware-variant carts read better as their platform than a region byte. */
        if (inspect_platform == GAME_PLATFORM_ALECK64) standard = "Aleck64 (arcade)";
        else if (inspect_platform == GAME_PLATFORM_IQUE) standard = "iQue (China)";

        /* Release date: regional date from DB, else fallback */
        const char *date = NULL;
        if (inspect_has_meta) {
            char region = e->game_code[3];
            if (region == 'J')      date = inspect_meta.release_jp;   /* Japan */
            else if (region == 'E') date = inspect_meta.release_us;   /* North America */
            else                    date = inspect_meta.release_eu;   /* P/D/F/I/S/U/X/Y/Z -- PAL */
        }
        if (!date || !date[0]) date = "Date unknown";

        /* Narrow LEFT column (under the art) holds the SHORT fields: Date + Region.
           Wide RIGHT column (under the description) holds the LONG ones: Name + Developer
           -- titles/developers can be arbitrarily long, so they get the room to wrap. */
        /* Demo / Prototype / Beta marker sits directly under the release date. */
        const char *build_lbl = game_build_label((game_build_t)inspect_build);
        if (build_lbl)
            snprintf(left_info, sizeof(left_info), "%s\n%s\n%s", date, build_lbl, standard);
        else
            snprintf(left_info, sizeof(left_info), "%s\n%s", date, standard);

        /* Filename FIRST: display_name comes from the metadata DB, which keys on the first
           three game-code chars, so a hack inherits its base game's title and this popup
           used to name the wrong game. The DB title stays as a subtitle -- it's still the
           nicer label for a legit ROM with an ugly filename -- but drop it when there's no
           DB hit, when it just repeats the filename, or when the strip can't hold three
           lines. That last case is real: the Classic font's line height only leaves room for
           two here, and rdpq would silently drop the developer rather than the subtitle. */
        char file_label[64];
        grid_file_label(fav_i, file_label, sizeof(file_label));
        if (!file_label[0]) {
            strncpy(file_label, display_name, sizeof(file_label) - 1);
            file_label[sizeof(file_label) - 1] = '\0';
        }
        bool show_db_title = display_name[0]
                             && strcmp(display_name, file_label) != 0
                             && text_lines_that_fit(meta_h) >= 3;
        if (show_db_title)
            snprintf(right_info, sizeof(right_info), "%.60s\n%.40s\n%.40s",
                     file_label, display_name, author);
        else
            snprintf(right_info, sizeof(right_info), "%.60s\n%.40s", file_label, author);
    }
    neutralize_rdpq_escapes(left_info);    /* any field may carry a '$'/'^' (title, code) */
    neutralize_rdpq_escapes(right_info);

    /* ---- Bottom 2+2 metadata strip (both columns vertically centred in the strip) ---- */
    rdpq_text_printn(
        &(rdpq_textparms_t){ .width = INSPECT_LEFT_W, .height = meta_h,
                             .align = ALIGN_LEFT, .valign = VALIGN_CENTER, .wrap = WRAP_WORD },
        FNT_DEFAULT, art_x, meta_y, left_info, strlen(left_info));
    /* WRAP_ELLIPSES, not WRAP_WORD: this column can now carry three entries (filename, DB
       title, developer) and a filename is long enough to wrap to two lines on its own, which
       would push the developer out of the box entirely. rdpq re-evaluates its fits-in-box
       test at every explicit newline, so ellipsising each entry to one line keeps them all
       visible. meta_h (45px) holds 3 lines of the Pixel font but only 2 of the Classic one,
       which is why the DB-title line is gated on text_lines_that_fit() above. */
    rdpq_text_printn(
        &(rdpq_textparms_t){ .width = right_w, .height = meta_h,
                             .align = ALIGN_LEFT, .valign = VALIGN_CENTER, .wrap = WRAP_ELLIPSES },
        FNT_DEFAULT, right_x, meta_y, right_info, strlen(right_info));

    /* ---- Right column: scrollable description (top band) ---- */

    const char *desc =
          (inspect_has_custom && inspect_custom.has_description) ? inspect_custom.description
        : (inspect_has_meta && inspect_meta.description && inspect_meta.description[0]) ? inspect_meta.description
        : NULL;

    /* No description -> use the description area to warn about non-N64 hardware variants. */
    if (!desc && inspect_platform == GAME_PLATFORM_ALECK64)
        desc = "Aleck64 arcade-board title (Seta's N64-based arcade hardware). "
               "It may not run on a standard Nintendo 64 or the SC64 flashcart.";
    else if (!desc && inspect_platform == GAME_PLATFORM_IQUE)
        desc = "iQue Player release (China) - a China-only N64 variant with encrypted, "
               "integrated cartridges. It may not run on a standard Nintendo 64 or the SC64 flashcart.";

    if (!desc) {
        /* No description at all: show the game's logo centred in the box if one is
           baked in, otherwise a short notice. Cached per inspected game. */
        if (inspect_logo_fav != fav_i) {
            if (inspect_logo) { sprite_free(inspect_logo); inspect_logo = NULL; }
            char lp[64];
            snprintf(lp, sizeof(lp), "rom:/boxart/%c%c%c%c/logo.sprite",
                     e->game_code[0], e->game_code[1], e->game_code[2], e->game_code[3]);
            FILE *lf = fopen(lp, "rb");
            if (lf) { fclose(lf); inspect_logo = sprite_load(lp); }
            inspect_logo_fav = fav_i;
        }
        if (inspect_logo) {
            surface_t s = sprite_get_pixels(inspect_logo);
            float sx = (float)right_w / s.width, sy = (float)right_h / s.height;
            float scale = sx < sy ? sx : sy;
            if (scale > 1.0f) scale = 1.0f;
            int dw = (int)(s.width * scale), dh = (int)(s.height * scale);
            int lx = right_x + (right_w - dw) / 2, ly = right_y + (right_h - dh) / 2;
            rdpq_mode_push();
                rdpq_set_mode_standard();
                rdpq_mode_filter(FILTER_BILINEAR);
                rdpq_mode_alphacompare(128);   /* logos: crop the soft AA fringe (cleaner edge) */
                rdpq_tex_blit(&s, lx, ly, &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
            rdpq_mode_pop();
        } else {
            rdpq_text_print(
                &(rdpq_textparms_t){ .width = right_w, .align = ALIGN_CENTER },
                FNT_DEFAULT, right_x, right_y + right_h / 2, "No description available."
            );
        }
    } else {
        /* Custom .meta.ini descriptions may carry rdpq escapes ('$'/'^'); neutralise a copy
           and use that everywhere below. (DB descriptions are clean, but be safe.) */
        static char descbuf[2048];
        strncpy(descbuf, desc, sizeof(descbuf) - 1); descbuf[sizeof(descbuf) - 1] = '\0';
        neutralize_rdpq_escapes(descbuf);
        desc = descbuf;
        int desc_len = strlen(desc);

        /* Estimate rendered height to decide whether the description overflows the pane
           (and may therefore scroll). The Classic font is larger than the Pixel font, so
           it wraps to more/taller lines -- account for that or it gets mis-cut. */
        bool classic = menu->settings.use_legacy_font;
        const int EST_CHAR_W = classic ? 9 : 8;
        const int EST_LINE_H = classic ? 18 : 14;
        int cpl = right_w / EST_CHAR_W; if (cpl < 1) cpl = 1;
        int lines = 1, col = 0;
        for (const char *p = desc; *p; p++) {
            if (*p == '\n') { lines++; col = 0; }
            else if (++col >= cpl) { lines++; col = 0; }
        }
        int est_h = lines * EST_LINE_H;

        /* When the description FITS the pane, vertically centre it (matches the metadata).
           When it overflows, fall back to top-aligned + scrollable. */
        int max_scroll = (est_h > right_h) ? (est_h - right_h) : 0;
        if (desc_scroll > max_scroll) desc_scroll = max_scroll;
        bool desc_fits = (max_scroll == 0);

        rdpq_set_scissor(right_x, right_y, right_x + right_w, right_y + right_h);
        rdpq_text_printn(
            &(rdpq_textparms_t){
                .width  = right_w,
                .height = desc_fits ? right_h : (right_h + desc_scroll + 200),
                .align  = ALIGN_LEFT,
                .valign = desc_fits ? VALIGN_CENTER : VALIGN_TOP,
                .wrap   = WRAP_WORD,
            },
            FNT_DEFAULT, right_x, right_y - (desc_fits ? 0 : desc_scroll),
            desc, desc_len
        );
        rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }

    if (desc_scroll > 0) {
        rdpq_text_print(
            &(rdpq_textparms_t){ .align = ALIGN_RIGHT, .width = right_w },
            FNT_DEFAULT, right_x, right_y,
            "▲"
        );
    }

    /* ---- Action bar (replaces grid action bar while inspect is open) — standardized
       fixed slots so C/Z line up with the grid bar behind it. ---- */
    ui_components_actions_bar_buttons_draw("A: Launch", "B: Back", NULL, "C: Scroll", "Z: Menu");
}

/* Destructive-action confirmation drawn on top of the inspect dialog.
   START confirms the removal, B cancels (see process()). */
static void draw_confirm_remove(menu_t *menu) {
    (void)menu;
    char name_buf[128];
    strcpy(name_buf, "this game");
    if (fav_count > 0 && sel_fav >= 0 && sel_fav < fav_count) {
        /* Prefer custom JSON name field if available */
        bool used_custom = false;
        if (inspect_has_custom && inspect_fav_i_cached == fav_indices[sel_fav]) {
            for (int fi = 0; fi < inspect_custom.field_count; fi++) {
                const char *lbl = inspect_custom.fields[fi].label;
                if (lbl[0] == 'N' || lbl[0] == 'n' || lbl[0] == 'T' || lbl[0] == 't') {
                    /* label starts with N/n (Name) or T/t (Title) */
                    strncpy(name_buf, inspect_custom.fields[fi].value, sizeof(name_buf) - 1);
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    used_custom = true;
                    break;
                }
            }
        }
        if (!used_custom) {
            /* Name the FILE, not the metadata-DB title -- confirming a destructive action
               against "Super Mario 64" when you're actually removing a hack of it is exactly
               the wrong time to show the base game's name. */
            grid_file_label(fav_indices[sel_fav], name_buf, sizeof(name_buf));
            if (!name_buf[0]) strcpy(name_buf, "this game");
        }
    }
    const char *name = name_buf;

    int x0 = INSPECT_CENTER_X - CONFIRM_W / 2;
    int y0 = INSPECT_CENTER_Y - CONFIRM_H / 2;
    int x1 = x0 + CONFIRM_W;
    int y1 = y0 + CONFIRM_H;

    /* Animated rainbow rim (matches the inspect dialog), brighter to draw the eye */
    uint8_t bv = inspect_pulse < 128 ? inspect_pulse : (uint8_t)(255 - inspect_pulse);
    draw_glow_layer(x0, y0, x1, y1, GLOW_OUTER, inspect_hue, (uint8_t)(40 + bv / 4));
    draw_glow_layer(x0, y0, x1, y1, GLOW_INNER, inspect_hue, (uint8_t)(150 + bv / 3));

    /* Panel: dark with a red tint to signal a destructive action */
    ui_components_box_draw(x0, y0, x1, y1, RGBA32(0x1C, 0x0E, 0x0E, 0xFF));

    /* Render all three lines as one block with VALIGN_CENTER so rdpq accounts for
       the font's actual ascender/descender when centering within the box. */
    rdpq_text_printf(
        &(rdpq_textparms_t){
            .width  = CONFIRM_W - 16,
            .height = CONFIRM_H - 16,
            .align  = ALIGN_CENTER,
            .valign = VALIGN_CENTER,
            .wrap   = WRAP_ELLIPSES,
        },
        FNT_DEFAULT, x0 + 8, y0 + 8,
        "Remove from Favorites?\n%s\nSTART: Remove     B: Cancel",
        name
    );
}

static void launch_favorite(menu_t *menu, int fav_i, bool direct) {
    /* On a library tab, fav_i indexes the library's item array, not favorites -- load_rom.c
       and load_disk.c both resolve load_favorite_id against menu->bookkeeping.favorite_items,
       so that index would be meaningless (or point at an unrelated favorite) there. Instead
       hand the loader the path directly via rom_path/library_disk_path and leave
       load_favorite_id at -1, with load_return_mode/from_grid standing in for what
       load_favorite_id normally tells leave_view()/launch_origin_mode about where to return. */
    bool from_library = (grid_tab != 0);
    bookkeeping_item_t *bk = &grid_items[fav_i];

    menu->load.load_history_id  = -1;
    menu->load.load_favorite_id = from_library ? -1 : fav_i;
    if (from_library) {
        menu->load.load_return_mode = MENU_MODE_GAMES_GRID;
        menu->load.from_grid        = true;
    }

    /* 64DD disk favorites go to the DISK loader, which reads the disk and its linked base
       ROM (the favorite's secondary path) via load_favorite_id -- the ROM loader can't boot
       a .ndd. From there A launches the disk (Z = combined disk+ROM for games like the
       F-Zero X Expansion Kit). */
    if (fav_i >= 0 && fav_i < grid_items_cap && bk->bookkeeping_type == BOOKKEEPING_TYPE_DISK) {
        /* An E-prefix 64DD code is an EXPANSION disk (e.g. EFZJ = F-Zero X Expansion Kit):
           it needs its base cartridge. If this favorite isn't linked to a ROM yet, divert to
           the cart picker instead of booting standalone (which would just black-screen). */
        bool expansion = (bk->game_code[0] == 'E');
        /* A disclink override (manual /menu/n64ever/disclink_{jp,us}.ini, keyed by game code)
           counts as linked -- load_disk resolves the actual base from it, so don't divert to
           the picker. */
        bool linked    = (bk->secondary_path && path_has_value(bk->secondary_path))
                      || disclink_has(menu->storage_prefix, bk->game_code);
        if (expansion && !linked) {
            /* Auto-link: derive the base cart family (expansion 'E' -> cart 'N') and scan
               favorites for it, region-aware. A unique hit links silently via disclink and boots;
               otherwise hand off to the file browser in link-pick mode to choose the base ROM. */
            char base0 = 'N', base1 = bk->game_code[1], base2 = bk->game_code[2];
            char region = bk->game_code[3];
            int hit = -1, exact = -1, fam = 0;
            for (int k = 0; k < FAVORITES_COUNT; k++) {
                bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
                if (f->bookkeeping_type != BOOKKEEPING_TYPE_ROM) continue;
                if (!f->primary_path || !path_has_value(f->primary_path)) continue;
                if (f->game_code[0] == base0 && f->game_code[1] == base1 && f->game_code[2] == base2) {
                    fam++;
                    if (hit < 0) hit = k;
                    if (f->game_code[3] == region) { exact = k; break; }   /* exact-region wins */
                }
            }
            int base = (exact >= 0) ? exact : (fam == 1 ? hit : -1);
            if (base >= 0) {
                disclink_store(menu->storage_prefix, bk->game_code,
                               path_get(menu->bookkeeping.favorite_items[base].primary_path));
                if (from_library) {
                    if (menu->load.library_disk_path) path_free(menu->load.library_disk_path);
                    menu->load.library_disk_path = path_clone(bk->primary_path);
                }
                menu->next_mode = MENU_MODE_LOAD_DISK;   /* now linked -> boots combined */
                return;
            }
            /* No clear favorite base -> pick it in the file browser (link-pick mode). Its
               resume path (finish_link_pick) is keyed by a real favorites index, and always
               leaves the disc as a tracked favorite once linked anyway -- so from a library
               tab, find-or-add this disk to favorites first rather than teach link-pick a
               second, library-flavored resume path. */
            if (from_library) {
                int real_fi = -1;
                for (int k = 0; k < FAVORITES_COUNT; k++) {
                    bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
                    if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && f->primary_path &&
                        path_are_match(bk->primary_path, f->primary_path)) { real_fi = k; break; }
                }
                if (real_fi < 0) {
                    bookkeeping_favorite_add(&menu->bookkeeping, bk->primary_path, bk->secondary_path,
                                              BOOKKEEPING_TYPE_DISK);
                    real_fi = 0;   /* insert_top places the new entry at index 0 */
                }
                fav_i = real_fi;
                bk    = &menu->bookkeeping.favorite_items[fav_i];
            }
            char dname[64];
            fav_name_from_filename(bk->primary_path, dname, sizeof dname);
            view_browser_request_link_pick(dname, bk->game_code, fav_i);
            menu->next_mode = MENU_MODE_BROWSER;
            return;
        }
        if (from_library) {
            if (menu->load.library_disk_path) path_free(menu->load.library_disk_path);
            menu->load.library_disk_path = path_clone(bk->primary_path);
        }
        menu->next_mode = MENU_MODE_LOAD_DISK;
        return;
    }

    if (from_library) {
        /* NOT rom_path directly: menu->browser.entry is never reset to NULL on leaving the
           browser (and pinning a folder requires having visited it at least once), so
           view_load_rom_init's browser.entry branch would silently overwrite rom_path with a
           stale browser path before this one was ever read. library_rom_path is checked (and
           consumed) there BEFORE the browser.entry fallback -- see menu_state.h. */
        if (menu->load.library_rom_path) path_free(menu->load.library_rom_path);
        menu->load.library_rom_path = path_clone(bk->primary_path);
    }
    if (direct) {
        menu->load_pending.rom_file = true;
    }
    menu->next_mode = MENU_MODE_LOAD_ROM;
}

/* ====================================================================
   Grid More menu helpers and action implementations
   ==================================================================== */

static const char *gm_view_name(grid_image_view_t v) {
    static const char *names[] = { "Front", "Back", "3D Box", "Cart", "3D Cart", "Logo" };
    return (v >= 0 && v < GRID_IMAGE_COUNT) ? names[v] : "?";
}

static void gm_gs_update_labels(menu_t *menu) {
    snprintf(gm_gs_grid_lbl, sizeof(gm_gs_grid_lbl), "Grid: %s",    gm_view_name(menu->settings.image_view_grid));
    snprintf(gm_gs_insp_lbl, sizeof(gm_gs_insp_lbl), "Inspect: %s", gm_view_name(menu->settings.image_view_inspect));
    snprintf(gm_gs_load_lbl, sizeof(gm_gs_load_lbl), "Load: %s",    gm_view_name(menu->settings.image_view_load));
    snprintf(gm_gs_sq_lbl,   sizeof(gm_gs_sq_lbl),
             "Tile: %s", menu->settings.grid_square_tiles ? "Square" : "Box");
    snprintf(gm_gs_tile_lbl, sizeof(gm_gs_tile_lbl), "Tile size: %s",
             menu->settings.grid_large_tiles ? "Large" : "Small");
    snprintf(gm_gs_caption_lbl, sizeof(gm_gs_caption_lbl), "Favorites captions: %s",
             menu->settings.grid_show_captions_favorites ? "On" : "Off");
}

static void gm_rebuild_history(menu_t *menu) {
    int n = 0;
    for (int i = 0; i < HISTORY_COUNT && n < GM_HIST_MAX; i++) {
        bookkeeping_item_t *item = &menu->bookkeeping.history_items[i];
        if (item->bookkeeping_type == BOOKKEEPING_TYPE_EMPTY || !item->primary_path) continue;
        const char *name = path_last_get(item->primary_path);
        if (!name || !name[0]) continue;
        int len = (int)strlen(name);
        if (len > 4 && name[len - 4] == '.') {
            snprintf(gm_hist_lbuf[n], sizeof(gm_hist_lbuf[n]), "%.*s", len - 4, name);
        } else {
            strncpy(gm_hist_lbuf[n], name, sizeof(gm_hist_lbuf[n]) - 1);
            gm_hist_lbuf[n][sizeof(gm_hist_lbuf[n]) - 1] = '\0';
        }
        gm_hist_cm.list[n].text   = gm_hist_lbuf[n];
        gm_hist_cm.list[n].action = gm_hist_launch;
        gm_hist_cm.list[n].arg    = (void *)(intptr_t)i;
        gm_hist_cm.list[n].submenu= NULL;
        hist_idx[n] = i;   /* bookkeeping index for the modal popup */
        n++;
    }
    hist_n = n;   /* real history count for the modal (0 = empty) */
    if (n == 0) {
        strncpy(gm_hist_lbuf[0], "No history", sizeof(gm_hist_lbuf[0]) - 1);
        gm_hist_cm.list[0].text   = gm_hist_lbuf[0];
        gm_hist_cm.list[0].action = NULL;
        gm_hist_cm.list[0].arg    = NULL;
        n = 1;
    }
    gm_hist_cm.list[n].text = NULL;
    ui_components_context_menu_init(&gm_hist_cm);
}

/* Find the favorites-array index of grid_more_target (or -1 if not a favorite). Always
   searches the REAL favorites, regardless of which tab is active -- used only for the
   favorites.ini presents_as cache, which has no equivalent on a (read-only) library tab. */
static int gm_target_fav_index(menu_t *menu) {
    if (!grid_more_target) return -1;
    for (int k = 0; k < FAVORITES_COUNT; k++) {
        bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
        if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && f->primary_path &&
            path_are_match(grid_more_target, f->primary_path)) {
            return k;
        }
    }
    return -1;
}

/* Find grid_more_target's index in the ACTIVE tab's items (grid_items), or -1. Unlike
   gm_target_fav_index(), this is the right index for invalidating the tile caches
   (fav_entry_cache/boxart_cache are keyed by position in whichever array is active) --
   using the favorites-index there instead would flush the wrong tile's cache whenever the
   targeted ROM is a favorite but the active tab is a library. */
static int gm_target_grid_index(menu_t *menu) {
    (void)menu;
    if (!grid_more_target) return -1;
    for (int k = 0; k < grid_items_cap; k++) {
        if (grid_items[k].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && grid_items[k].primary_path &&
            path_are_match(grid_more_target, grid_items[k].primary_path)) {
            return k;
        }
    }
    return -1;
}

/* Drop one game's cached info+cover so it re-fetches at once (region/per-game view
   changes affect both the destination_code orientation and the art). */
static void gm_invalidate_one_art(int fav_i, bool keep_info) {
    if (fav_i < 0 || fav_i >= FAVORITES_COUNT) return;
    ui_components_boxart_free(boxart_cache[fav_i]);
    boxart_cache[fav_i] = NULL;
    boxart_cache_attempted[fav_i] = false;
    /* keep_info: a Game-Look view-TYPE change doesn't alter the game code/metadata, so don't force
       a full reload -- that re-reads the disk (disk_info_load, slow SD I/O) for DD games on every
       cycle. Callers that DO change the code/region clear info_loaded themselves. */
    if (!keep_info) fav_entry_cache[fav_i].info_loaded = false;
}

/* Per-game "Presents As" labels reflect this game's overrides (Default = none). */
static void gm_pa_update_labels(void) {
    int g = -1, i = -1, l = -1;
    if (grid_more_target) {
        g = rom_config_get_image_view(grid_more_target, 0);
        i = rom_config_get_image_view(grid_more_target, 1);
        l = rom_config_get_image_view(grid_more_target, 2);
    }
    snprintf(gm_pa_grid_lbl, sizeof(gm_pa_grid_lbl), "Grid: %s",    (g >= 0 && g < GRID_IMAGE_COUNT) ? gm_view_name(g) : "Default");
    snprintf(gm_pa_insp_lbl, sizeof(gm_pa_insp_lbl), "Inspect: %s", (i >= 0 && i < GRID_IMAGE_COUNT) ? gm_view_name(i) : "Default");
    snprintf(gm_pa_load_lbl, sizeof(gm_pa_load_lbl), "Load: %s",    (l >= 0 && l < GRID_IMAGE_COUNT) ? gm_view_name(l) : "Default");
}

static void gm_update_dynamic_labels(menu_t *menu) {
    gm_gs_update_labels(menu);
    gm_pa_update_labels();
    gm_rebuild_history(menu);
    if (grid_tab != 0) {
        /* Library tab: a one-way "Add to Favorites" (see gm_fav_toggle), not the
           Favorite/Unfavorite toggle -- this tab isn't the favorites list itself. */
        bool already = (gm_target_fav_index(menu) >= 0);
        snprintf(gm_fav_lbl, sizeof(gm_fav_lbl), "%s", already ? "Already a Favorite" : "Add to Favorites");
    } else {
        /* The toggle is deferred (committed on menu dismiss), so show the count the list WILL
           have once the pending change applies: +1 for a pending add, -1 for a pending remove,
           else the current count. */
        bool cur_fav = (gm_target_fav_index(menu) >= 0);
        int shown = fav_count + ((grid_more_is_fav && !cur_fav) ? 1 : 0)
                              - ((!grid_more_is_fav && cur_fav) ? 1 : 0);
        snprintf(gm_fav_lbl, sizeof(gm_fav_lbl), "%s %d/%d",
                 grid_more_is_fav ? "Unfavorite" : "Favorite", shown, FAVORITES_COUNT);
    }
    if (grid_tab != 0) {
        /* Favorites-only tools: blank text = the row is skipped/hidden (see
           ui_components/context_menu.c's empty-text-is-a-separator convention). A
           library tab is a read-only gallery, not the favorites list. */
        gm_sort_lbl[0]     = '\0';
        gm_clear_lbl[0]    = '\0';
        gm_autosort_lbl[0] = '\0';
        strncpy(gm_rescan_lbl, "Rescan library", sizeof(gm_rescan_lbl) - 1);
        gm_rescan_lbl[sizeof(gm_rescan_lbl) - 1] = '\0';
    } else {
        strncpy(gm_sort_lbl, "Run Once: Sort A-Z", sizeof(gm_sort_lbl) - 1);
        gm_sort_lbl[sizeof(gm_sort_lbl) - 1] = '\0';
        strncpy(gm_clear_lbl, "Clear all favorites", sizeof(gm_clear_lbl) - 1);
        gm_clear_lbl[sizeof(gm_clear_lbl) - 1] = '\0';
        snprintf(gm_autosort_lbl, sizeof(gm_autosort_lbl), "Always sort A-Z: %s",
                 menu->settings.always_sort_az ? "Yes" : "No");
        gm_rescan_lbl[0] = '\0';
    }
}

/* Close the More menu and clean up the target path */
static void gm_close(void) {
    grid_more_active       = false;
    grid_more_from_inspect = false;
    if (grid_more_target) { path_free(grid_more_target); grid_more_target = NULL; }
}

/* Open the More menu for the currently selected favorite */
static void gm_open(menu_t *menu, bool from_inspect) {
    if (fav_count == 0 || sel_fav < 0 || sel_fav >= fav_count) return;
    int fav_i = fav_indices[sel_fav];
    bookkeeping_item_t *bi = &grid_items[fav_i];
    if (!bi->primary_path) return;

    if (grid_more_target) path_free(grid_more_target);
    grid_more_target       = path_clone(bi->primary_path);
    /* On Favorites this is always true (the tile IS a favorite); on a library tab the
       tile may or may not also be a real favorite, so check rather than assume. */
    grid_more_is_fav       = (gm_target_fav_index(menu) >= 0);
    grid_more_from_inspect = from_inspect;
    grid_more_active       = true;

    gm_update_dynamic_labels(menu);   /* builds "Unfavorite N/128" */
    ui_components_context_menu_init(&gm_more_cm);
    ui_components_context_menu_show(&gm_more_cm);
}

/* ---- Action functions ---- */

static void gm_launch(menu_t *menu, void *arg) {
    if (!grid_more_target) return;
    /* Find fav by path or re-add if unfavorited */
    int fi = -1;
    for (int k = 0; k < FAVORITES_COUNT; k++) {
        bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
        if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && f->primary_path &&
            path_are_match(grid_more_target, f->primary_path)) {
            fi = k; break;
        }
    }
    if (fi < 0) {
        /* Was unfavorited — re-add temporarily, then launch */
        bookkeeping_favorite_add(&menu->bookkeeping, grid_more_target, NULL, BOOKKEEPING_TYPE_ROM);
        refresh_grid(menu);
        /* Find the new fav_i */
        for (int k = 0; k < FAVORITES_COUNT; k++) {
            bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
            if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && f->primary_path &&
                path_are_match(grid_more_target, f->primary_path)) {
                fi = k; break;
            }
        }
    }
    if (fi < 0) { menu_show_error(menu, "ROM not found."); return; }
    gm_close();
    show_inspect = false;
    sound_play_effect(SFX_LAUNCH);
    launch_favorite(menu, fi, true);
}

/* DEFERRED favorite toggle: flip the desired-state flag only; the real add/remove is
   committed by gm_apply_pending_fav() when the menu is dismissed (B). The game must stay
   put on the grid -- and remain THIS menu's target -- while the menu is open, so an
   accidental Unfavorite can be undone by pressing Favorite again. (Committing here
   reflowed the grid and slid the target onto the next game, so the re-press favorited the
   WRONG game -> the duplicate-favorite bug.) The label shows the resulting count. */
static void gm_fav_toggle(menu_t *menu, void *arg) {
    (void)arg;
    if (!grid_more_target) return;
    /* Library tab: one-way "Add to Favorites" -- this row never removes an existing
       favorite from a read-only library tab (see gm_update_dynamic_labels). */
    grid_more_is_fav = (grid_tab != 0) ? true : !grid_more_is_fav;
    gm_update_dynamic_labels(menu);   /* label + count reflect the pending state */
    gm_fav_fired = true;              /* reopen the menu so the new label shows */
    sound_play_effect(SFX_SETTING);
}

/* Commit the deferred favorite state to bookkeeping (called when the More menu is
   dismissed). Adds or removes only if it actually differs from the current state. */
static void gm_apply_pending_fav(menu_t *menu) {
    if (!grid_more_target) return;
    int fi = gm_target_fav_index(menu);
    bool currently_fav = (fi >= 0);
    if (grid_more_is_fav == currently_fav) return;   /* no net change */
    if (grid_more_is_fav) {
        bookkeeping_favorite_add(&menu->bookkeeping, grid_more_target, NULL, BOOKKEEPING_TYPE_ROM);
    } else {
        cache_compact_after_remove(fi);
        bookkeeping_favorite_remove(&menu->bookkeeping, fi);
    }
    bookkeeping_save(&menu->bookkeeping);
    refresh_grid(menu);
    compute_flow();
    if (sel_fav >= fav_count) sel_fav = (fav_count > 0) ? fav_count - 1 : 0;
    ensure_visible();
}

static void gm_game_settings(menu_t *menu, void *arg) {
    if (!grid_more_target) return;
    int fi = -1;
    for (int k = 0; k < FAVORITES_COUNT; k++) {
        bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
        if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && f->primary_path &&
            path_are_match(grid_more_target, f->primary_path)) {
            fi = k; break;
        }
    }
    if (fi < 0) {
        /* Not currently a fav — add it so load_rom can access the ROM */
        bookkeeping_favorite_add(&menu->bookkeeping, grid_more_target, NULL, BOOKKEEPING_TYPE_ROM);
        refresh_grid(menu);
        for (int k = 0; k < FAVORITES_COUNT; k++) {
            bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
            if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && f->primary_path &&
                path_are_match(grid_more_target, f->primary_path)) {
                fi = k; break;
            }
        }
    }
    if (fi < 0) { menu_show_error(menu, "ROM not found."); return; }
    /* Remember to reopen this game's More menu (at the same row) when it closes. */
    gm_reopen_pending = true;
    gm_reopen_row     = gm_more_cm.row_selected;
    gm_reopen_submenu = gm_more_cm.submenu;
    gm_reopen_subrow  = gm_more_cm.submenu ? gm_more_cm.submenu->row_selected : 0;
    if (gm_reopen_path) path_free(gm_reopen_path);
    gm_reopen_path = path_clone(grid_more_target);
    gm_close();
    show_inspect = false;
    menu->load.load_favorite_id = fi;
    menu->load.load_history_id  = -1;
    menu->load.load_return_mode = MENU_MODE_GAMES_GRID;
    menu->load.from_grid        = true;
    sound_play_effect(SFX_SETTING);
    menu->next_mode = MENU_MODE_LOAD_ROM;
}

static void gm_file_browser(menu_t *menu, void *arg) {
    /* Remember to reopen this game's More menu when the browser backs out at root. */
    if (grid_more_target) {
        gm_reopen_pending = true;
        gm_reopen_row     = gm_more_cm.row_selected;
        gm_reopen_submenu = gm_more_cm.submenu;
        gm_reopen_subrow  = gm_more_cm.submenu ? gm_more_cm.submenu->row_selected : 0;
        if (gm_reopen_path) path_free(gm_reopen_path);
        gm_reopen_path = path_clone(grid_more_target);
    }
    gm_close();
    show_inspect = false;
    view_browser_open_popup(menu);
    sound_play_effect(SFX_ENTER);
}

static void gm_go_to_mode(menu_t *menu, void *arg) {
    menu_mode_t m = (menu_mode_t)(intptr_t)arg;
    /* These sub-views (settings, menu info, hardware) honour load_return_mode and
       draw over the grid; on exit we return to the grid and reopen this game's More
       menu so the user lands back in the universal menu, not the file browser. */
    if (grid_more_target) {
        gm_reopen_pending = true;
        gm_reopen_row     = gm_more_cm.row_selected;
        gm_reopen_submenu = gm_more_cm.submenu;
        gm_reopen_subrow  = gm_more_cm.submenu ? gm_more_cm.submenu->row_selected : 0;
        if (gm_reopen_path) path_free(gm_reopen_path);
        gm_reopen_path = path_clone(grid_more_target);
    }
    menu->load.load_return_mode = MENU_MODE_GAMES_GRID;
    gm_close();
    show_inspect = false;
    sound_play_effect(SFX_SETTING);
    menu->next_mode = m;
}

static void gm_hist_launch(menu_t *menu, void *arg) {
    int i = (int)(intptr_t)arg;
    if (i < 0 || i >= HISTORY_COUNT) return;
    bookkeeping_item_t *item = &menu->bookkeeping.history_items[i];
    if (!item->primary_path) return;
    gm_close();
    show_inspect = false;
    menu->load.load_history_id  = i;
    menu->load.load_favorite_id = -1;
    menu->load.load_return_mode = MENU_MODE_GAMES_GRID;
    menu->load.from_grid        = true;
    menu->load_pending.launch_rom = true;
    sound_play_effect(SFX_LAUNCH);
    menu->next_mode = MENU_MODE_LOAD_ROM;
}

/* Open the History modal (Files-style popup). Closes the menu list but keeps the
   target so B reopens the More menu. */
static void gm_open_history(menu_t *menu, void *arg) {
    (void)arg;
    gm_rebuild_history(menu);
    hist_sel = 0; hist_scroll = 0;
    show_history = true;
    grid_more_active = false;
    grid_more_from_inspect = false;
    sound_play_effect(SFX_SETTING);
}

/* Is the history item's ROM currently in the favorites list? */
static bool gm_hist_is_fav(menu_t *menu, int display_row) {
    if (display_row < 0 || display_row >= hist_n) return false;
    path_t *p = menu->bookkeeping.history_items[hist_idx[display_row]].primary_path;
    if (!p) return false;
    for (int k = 0; k < FAVORITES_COUNT; k++) {
        bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
        if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && f->primary_path &&
            path_are_match(p, f->primary_path)) return true;
    }
    return false;
}

/* Drop all cached covers so maybe_background_load re-fetches them with the current
   grid image-view setting — the grid updates live, before the submenu even closes. */
static void gm_invalidate_all_art(void) {
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        ui_components_boxart_free(boxart_cache[i]);
        boxart_cache[i] = NULL;
        boxart_cache_attempted[i] = false;
    }
}

/* Drop every per-favorite cache (art + info) so the grid fully re-derives from the
   current bookkeeping order. Used after a sort or a clear, where indices shift. */
static void gm_reset_all_caches(menu_t *menu) {
    gm_invalidate_all_art();
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        fav_entry_cache[i].info_loaded = false;
        fav_entry_cache[i].meta_name[0] = '\0';
        fav_entry_cache[i].game_code[0] = '\0';
    }
    sel_fav = 0;
    scroll_row = 0;
    refresh_grid(menu);
}

/* ---- Favorites submenu: Sort A-Z + Clear all ---- */

static char gm_lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int gm_name_casecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = gm_lc(*a), cb = gm_lc(*b);
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++; b++;
    }
    return (unsigned char)gm_lc(*a) - (unsigned char)gm_lc(*b);
}

typedef struct { int idx; int empty; char key[48]; } gm_sort_ent_t;
static gm_sort_ent_t      gm_sort_ents[FAVORITES_COUNT];
static bookkeeping_item_t gm_sort_tmp[FAVORITES_COUNT];

static int gm_sort_cmp(const void *a, const void *b) {
    const gm_sort_ent_t *x = a, *y = b;
    if (x->empty != y->empty) return x->empty - y->empty;   /* empty slots sort last */
    /* Normal "0A-Z" order: a numeric initial (0-9) sorts BEFORE letters; symbols last.
       (first_initial returns an uppercased letter, a digit, or '#'.) */
    char cx = first_initial(x->key), cy = first_initial(y->key);
    int gx = (cx >= '0' && cx <= '9') ? 0 : ((cx >= 'A' && cx <= 'Z') ? 1 : 2);
    int gy = (cy >= '0' && cy <= '9') ? 0 : ((cy >= 'A' && cy <= 'Z') ? 1 : 2);
    if (gx != gy) return gx - gy;
    return gm_name_casecmp(x->key, y->key);
}

/* Resolve a favorite's display name for sorting: DB name (by cached code, caching
   the code if missing), else the ROM header title. */
static void gm_fav_sort_key(menu_t *menu, bookkeeping_item_t *it, char *out, size_t outsz) {
    out[0] = '\0';
    if (!it->primary_path || !path_has_value(it->primary_path)) return;
    /* Cached name for a non-DB game -> no ROM-header re-read on this (or any later) sort. */
    if (it->sort_name[0]) {
        strncpy(out, it->sort_name, outsz - 1); out[outsz - 1] = '\0';
        return;
    }
    if (!it->game_code[0]) {
        if (it->bookkeeping_type == BOOKKEEPING_TYPE_DISK) {
            /* Filename first (explicit code / keyword), else the internal disk id. */
            if (!match_64dd_filename(it->primary_path, it->game_code)) {
                disk_info_t di;
                if (disk_info_load(it->primary_path, &di) == DISK_OK) {
                    memcpy(it->game_code, di.id, 4); it->game_code[4] = '\0';
                }
            }
        } else {
            rom_info_t hi;
            if (rom_info_load_quick(it->primary_path, &hi) == ROM_OK) {
                memcpy(it->game_code, hi.game_code, 4); it->game_code[4] = '\0';
                rom_info_free_meta(&hi);
            }
        }
    }
    remap_64dd_code(it->primary_path, it->game_code);   /* 64DD conversion NDDJ -> real code */
    if (it->game_code[0]) {
        game_meta_t m;
        int sp = game_special_match(path_last_get(it->primary_path));
        if (gm_meta_lookup(menu, it->game_code, sp, &m) && m.title && m.title[0]) {
            strncpy(out, m.title, outsz - 1); out[outsz - 1] = '\0';
            return;
        }
    }
    rom_info_t hi;
    if (rom_info_load_quick(it->primary_path, &hi) == ROM_OK) {
        char t[21]; memcpy(t, hi.title, 20); t[20] = '\0';
        for (int k = 19; k >= 0 && t[k] == ' '; k--) t[k] = '\0';
        strncpy(out, t, outsz - 1); out[outsz - 1] = '\0';
        rom_info_free_meta(&hi);
    }
    if (!out[0]) {                       /* no DB entry + no header title -> use the filename */
        fav_name_from_filename(it->primary_path, out, outsz);
    }
    if (out[0]) {                        /* cache so future re-sorts skip the SD read */
        strncpy(it->sort_name, out, sizeof(it->sort_name) - 1);
        it->sort_name[sizeof(it->sort_name) - 1] = '\0';
    }
}

/* Sort the favorites list alphabetically by display name (case-insensitive),
   empty slots last, then persist. Blocking — run behind the "Working..." box. */
static void gm_run_sort_az(menu_t *menu) {
    /* Belt-and-braces: this always sorts the REAL favorites, never a library tab's
       items, regardless of what UI gating did or didn't prevent reaching here. */
    if (grid_items != menu->bookkeeping.favorite_items) return;
    bookkeeping_t *bk = &menu->bookkeeping;
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        /* Abort: B held during this (blocking, SD-read-heavy) key derivation bails BEFORE the
           reorder below, so the favorites list is left exactly as it was -- a clean cancel. */
        if ((i & 15) == 0) {
            joypad_poll();
            if (joypad_get_buttons_held(JOYPAD_PORT_1).b) return;
        }
        bookkeeping_item_t *it = &bk->favorite_items[i];
        gm_sort_ents[i].idx   = i;
        gm_sort_ents[i].empty = (it->bookkeeping_type == BOOKKEEPING_TYPE_EMPTY) ? 1 : 0;
        gm_sort_ents[i].key[0] = '\0';
        if (!gm_sort_ents[i].empty) {
            gm_fav_sort_key(menu, it, gm_sort_ents[i].key, sizeof(gm_sort_ents[i].key));
        }
    }
    qsort(gm_sort_ents, FAVORITES_COUNT, sizeof(gm_sort_ent_t), gm_sort_cmp);
    /* Permute the list to the sorted order (shallow move — each item's path
       pointers travel exactly once). */
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        gm_sort_tmp[i] = bk->favorite_items[gm_sort_ents[i].idx];
        /* Capture the initial from the sort key, aligned with the new sorted order, so
           A-Z C-jumps match exactly what the eye sees. */
        az_letter[i]   = gm_sort_ents[i].empty ? '#' : first_initial(gm_sort_ents[i].key);
    }
    memcpy(bk->favorite_items, gm_sort_tmp, sizeof(gm_sort_tmp));
    /* Persist each favorite's initial (so later boots skip the re-sort + re-reads) and count
       the real entries for the in-session cache-validity check. */
    az_letter_count = 0;
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        if (bk->favorite_items[i].bookkeeping_type == BOOKKEEPING_TYPE_EMPTY) break;
        bk->favorite_items[i].sort_initial = az_letter[i];
        az_letter_count++;
    }
    az_letter_ready = true;
    bookkeeping_save_favorites(bk);
}

/* These three now live in the Grid settings submenu (gm_gs_cm). The rows below
 * are their indices there, used so a cancelled confirm reopens at the right row. */
#define GM_GS_ROW_SORT     9
#define GM_GS_ROW_AUTOSORT 10
#define GM_GS_ROW_CLEAR    12

static void gm_fav_sort(menu_t *menu, void *arg) {
    (void)menu; (void)arg;
    gm_gs_row = GM_GS_ROW_SORT;
    show_confirm_sort = true;   /* the context menu closes; confirm takes over */
}

static void gm_fav_clear(menu_t *menu, void *arg) {
    (void)arg;
    /* Belt-and-braces: never arm the confirm on a library tab (unreachable via the menu
       anyway -- see gm_clear_lbl -- but this is a destructive op, so check again here). */
    if (grid_items != menu->bookkeeping.favorite_items) return;
    gm_gs_row = GM_GS_ROW_CLEAR;
    show_confirm_clear = true;
}

static void gm_fav_toggle_autosort(menu_t *menu, void *arg) {
    (void)arg;
    gm_gs_row = GM_GS_ROW_AUTOSORT;
    if (menu->settings.always_sort_az) {
        /* Turning OFF needs no confirmation. */
        menu->settings.always_sort_az = false;
        settings_save(&menu->settings);
        gm_update_dynamic_labels(menu);
        gm_gs_fired = true;       /* reopen Grid settings with the updated label */
        sound_play_effect(SFX_SETTING);
    } else {
        /* Turning ON: confirm + recommend disabling custom files. */
        show_confirm_autosort = true;  /* the context menu closes; confirm takes over */
    }
}

/* Re-scan the active library's folder from scratch (the escape hatch for the
   scan-once-per-boot cache -- e.g. after adding/removing ROMs on the SD card). No-op
   (and hidden -- see gm_rescan_lbl) off a library tab. */
static void gm_rescan_library(menu_t *menu, void *arg) {
    (void)arg;
    if (grid_tab == 0) return;
    library_t *lib = library_get(grid_tab - 1);
    if (!lib) return;
    library_free_items(lib);
    library_scan(lib);
    grid_flush_caches();
    grid_repoint_items(menu);
    refresh_grid(menu);
    sel_fav    = 0;
    scroll_row = 0;
    gm_close();
    sound_play_effect(SFX_SETTING);
}

static void gm_gs_cycle_grid(menu_t *menu, void *arg) {
    gm_gs_row = (int)(intptr_t)arg;
    gm_gs_fired = true;
    menu->settings.image_view_grid =
        (grid_image_view_t)((menu->settings.image_view_grid + 1) % GRID_IMAGE_COUNT);
    settings_save(&menu->settings);
    gm_gs_update_labels(menu);
    gv_is_box = is_box_view(menu->settings.image_view_grid);  /* JP portrait only for box views */
    gm_invalidate_all_art();   /* reload every visible cover with the new view, live */
}
static void gm_gs_cycle_inspect(menu_t *menu, void *arg) {
    gm_gs_row = (int)(intptr_t)arg;
    gm_gs_fired = true;
    menu->settings.image_view_inspect =
        (grid_image_view_t)((menu->settings.image_view_inspect + 1) % GRID_IMAGE_COUNT);
    settings_save(&menu->settings);
    insp_boxart_fav = -1;   /* inspect cover must reload with the new view */
    gm_gs_update_labels(menu);
}
static void gm_gs_cycle_load(menu_t *menu, void *arg) {
    gm_gs_row = (int)(intptr_t)arg;
    gm_gs_fired = true;
    menu->settings.image_view_load =
        (grid_image_view_t)((menu->settings.image_view_load + 1) % GRID_IMAGE_COUNT);
    settings_save(&menu->settings);
    gm_gs_update_labels(menu);
}
static void gm_gs_toggle_square(menu_t *menu, void *arg) {
    gm_gs_row = (int)(intptr_t)arg;
    gm_gs_fired = true;
    menu->settings.grid_square_tiles = !menu->settings.grid_square_tiles;
    settings_save(&menu->settings);
    grid_sq = menu->settings.grid_square_tiles;
    gm_gs_update_labels(menu);
}
/* Tile size Small<->Large. compute_flow reads grid_large every frame, so the grid behind
   the settings menu re-flows live (no cover reload -- covers are source-size, just rescaled). */
static void gm_gs_toggle_tile(menu_t *menu, void *arg) {
    gm_gs_row = (int)(intptr_t)arg;
    gm_gs_fired = true;
    menu->settings.grid_large_tiles = !menu->settings.grid_large_tiles;
    settings_save(&menu->settings);
    grid_large = menu->settings.grid_large_tiles;
    gm_gs_update_labels(menu);
}
/* Favorites-tab caption opt-in (library tabs always show captions regardless of this). */
static void gm_gs_toggle_caption(menu_t *menu, void *arg) {
    gm_gs_row = (int)(intptr_t)arg;
    gm_gs_fired = true;
    menu->settings.grid_show_captions_favorites = !menu->settings.grid_show_captions_favorites;
    settings_save(&menu->settings);
    grid_caption_favs = menu->settings.grid_show_captions_favorites;
    gm_gs_update_labels(menu);
}
/* Global "Game art" region default. Reopens Grid settings at the Game art row, then
   re-resolves every tile (box art + portrait/landscape orientation can change). */
static void gm_gs_set_region(menu_t *menu, void *arg) {
    gm_gs_row   = 4;            /* the "Game art" row of gm_gs_cm */
    gm_gs_fired = true;
    menu->settings.image_region_default = (int)(uintptr_t)arg;
    settings_save(&menu->settings);
    for (int i = 0; i < FAVORITES_COUNT; i++) fav_entry_cache[i].info_loaded = false;
    insp_boxart_fav = -1;      /* inspect cover re-resolves with the new global region */
    gm_invalidate_all_art();   /* drop covers so they reload with the new region */
    compute_flow();            /* orientation may flip JP portrait <-> landscape */
    sound_play_effect(SFX_SETTING);
}

/* ---- Per-game "Presents As" actions (operate on grid_more_target) ---- */

/* After a per-game override changes, reload just this game's info+art so the grid
   reflects it at once, and re-flow (region can change portrait/landscape). */
/* affects_grid: only then do we reload the grid cover (and reflow). Inspect/Load
   per-game changes don't touch the grid tile, so they must not flash it. */
static void gm_pa_refresh_target(menu_t *menu, bool affects_grid) {
    if (affects_grid) {
        int fav_i = gm_target_grid_index(menu);
        if (fav_i >= 0) gm_invalidate_one_art(fav_i, true);   /* view-type change: keep info, no disk re-read */
        compute_flow();
    }
    gm_pa_update_labels();
    gm_pa_fired = true;   /* reopen the Presents-As submenu after the action */
}

static void gm_set_region(menu_t *menu, void *arg) {
    gm_pa_row = 4;   /* "Region art" is row 4 of gm_pa_cm (below Load) */
    if (!grid_more_target) return;
    rom_presents_as_t presents = (rom_presents_as_t)(uintptr_t)arg;
    rom_info_t dummy; memset(&dummy, 0, sizeof(dummy));
    rom_config_override_presents_as(grid_more_target, &dummy, presents);   /* gameconfigs (launch/inspect) */
    /* Also cache it in favorites.ini so the grid reflects it without reading config,
       and force the tile to re-apply the override. */
    int fav_i = gm_target_fav_index(menu);
    if (fav_i >= 0) {
        menu->bookkeeping.favorite_items[fav_i].presents_as = (int)presents;
        bookkeeping_save_favorites(&menu->bookkeeping);
    }
    int gi = gm_target_grid_index(menu);
    if (gi >= 0) fav_entry_cache[gi].info_loaded = false;
    insp_boxart_fav = -1;               /* region changes box front/back -> reload inspect cover */
    gm_pa_refresh_target(menu, true);   /* region affects the grid (box art + orientation) */
    sound_play_effect(SFX_SETTING);
}

static void gm_pa_cycle(menu_t *menu, int context, int row) {
    gm_pa_row = row;
    if (!grid_more_target) return;
    int cur  = rom_config_get_image_view(grid_more_target, context);
    int next = cur + 1;
    if (next > GRID_IMAGE_COUNT - 1) next = -1;   /* wrap back to Default */
    rom_config_override_image_view(grid_more_target, context, next);
    if (context == 1) insp_boxart_fav = -1;     /* inspect cover must reload with the new view */
    gm_pa_refresh_target(menu, context == 0);   /* only the Grid context touches the tile */
    sound_play_effect(SFX_SETTING);
}
static void gm_pa_cycle_grid(menu_t *menu, void *arg)    { gm_pa_cycle(menu, 0, (int)(intptr_t)arg); }
static void gm_pa_cycle_inspect(menu_t *menu, void *arg) { gm_pa_cycle(menu, 1, (int)(intptr_t)arg); }
static void gm_pa_cycle_load(menu_t *menu, void *arg)    { gm_pa_cycle(menu, 2, (int)(intptr_t)arg); }

/* Reset all per-game overrides for the selected game back to defaults. */
static void gm_pa_reset(menu_t *menu, void *arg) {
    gm_pa_row = 8;   /* the Reset row (Grid/Insp/Load, blank, Region, blank, Meta, blank, Reset) */
    if (!grid_more_target) return;
    rom_info_t dummy; memset(&dummy, 0, sizeof(dummy));
    rom_config_override_presents_as(grid_more_target, &dummy, ROM_PRESENTS_AS_AUTO);
    rom_config_override_image_view(grid_more_target, 0, -1);
    rom_config_override_image_view(grid_more_target, 1, -1);
    rom_config_override_image_view(grid_more_target, 2, -1);
    int fav_i = gm_target_fav_index(menu);
    if (fav_i >= 0) {
        menu->bookkeeping.favorite_items[fav_i].presents_as = 0;   /* AUTO */
        bookkeeping_save_favorites(&menu->bookkeeping);
    }
    int gi = gm_target_grid_index(menu);
    if (gi >= 0) fav_entry_cache[gi].info_loaded = false;
    insp_boxart_fav = -1;   /* reset to defaults -> reload inspect cover too */
    gm_pa_refresh_target(menu, true);
    sound_play_effect(SFX_SETTING);
}


/* ====================================================================
   "Game Metadata" register — interrogates, for the selected game, which art
   files actually resolve and from where (baked ROM DFS / SD / custom). Doubles
   as the on-device diagnostic for missing art.
   ==================================================================== */

/* DFS sprite basenames, matched to boxart.c dfs_type_name(). */
static const char *gm_md_type_label[6] = { "Box front", "Box back", "3D box", "Cart", "3D cart", "Logo" };
static const char *gm_md_dfs_name[6]   = { "front", "back", "box3d", "cart", "cart3d", "logo" };

/* Does rom:/boxart/<CODE>/<name>.sprite exist? (This is the exact check
   try_dfs_sprite gates on — so the panel reveals whether baked art is reachable.) */
static bool gm_md_rom_exists(const char *code, const char *name) {
    /* Mirror try_dfs_sprite's region fallback: exact region first, then alternates. */
    static const char regions[] = { 'E', 'P', 'J', 'U', 'A', 'X', 'F', 'D', 'I', 'S' };
    char p[64];
    char order[1 + sizeof(regions)]; int no = 0;
    order[no++] = code[3];
    for (size_t i = 0; i < sizeof(regions); i++) if (regions[i] != code[3]) order[no++] = regions[i];
    for (int i = 0; i < no; i++) {
        snprintf(p, sizeof(p), "rom:/boxart/%c%c%c%c/%s.sprite", code[0], code[1], code[2], order[i], name);
        FILE *f = fopen(p, "rb");
        if (f) { fclose(f); return true; }
    }
    return false;
}

/* Does a per-game custom override PNG exist on SD? */
static bool gm_md_custom_exists(menu_t *menu, int type) {
    if (!grid_more_target) return false;
    static const char *suf[6] = { "-front", "-back", "-3dbox", "-cart", "-3dcart", "-logo" };
    const char *full = path_get(grid_more_target);
    const char *colon = strchr(full, ':');
    char prefix[16] = "sd:";
    if (colon) { size_t n = (size_t)(colon - full + 1); if (n < sizeof(prefix)) { memcpy(prefix, full, n); prefix[n] = '\0'; } }
    const char *slash = strrchr(full, '/');
    char stem[128]; strncpy(stem, slash ? slash + 1 : full, sizeof(stem) - 1); stem[sizeof(stem) - 1] = '\0';
    char *dot = strrchr(stem, '.'); if (dot) *dot = '\0';
    char cp[256];
    snprintf(cp, sizeof(cp), "%s/menu/n64ever/gameconfigs/%s%s.png", prefix, stem, suf[type]);
    FILE *f = fopen(cp, "rb");
    if (f) { fclose(f); return true; }
    /* Legacy location */
    snprintf(cp, sizeof(cp), "%s/menu/custom/gameconfigs/%s%s.png", prefix, stem, suf[type]);
    f = fopen(cp, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

/* Text-metadata presence, probed once when the panel opens (avoids per-frame I/O). */
static bool md_txt_rom = false;   /* embedded / external .meta text on the ROM */
static bool md_txt_db  = false;   /* SD metadata database entry */
static bool md_txt_cus = false;   /* custom JSON / gameconfigs text */

static bool gm_str_present(const char *s) {
    return s && s[0] && strcmp(s, "Not specified") != 0;
}

static void gm_show_metadata(menu_t *menu, void *arg) {
    (void)arg;
    /* Keep grid_more_target alive for the panel's custom-art probe, but close the
       menu list itself. (gm_close frees the target, so don't call it here.) */
    grid_more_active       = false;
    grid_more_from_inspect = false;
    show_metadata          = true;

    /* Probe text-metadata sources once, now. */
    md_txt_rom = md_txt_db = md_txt_cus = false;
    if (sel_fav >= 0 && sel_fav < fav_count) {
        grid_entry_t *e = &fav_entry_cache[fav_indices[sel_fav]];
        md_txt_rom = gm_str_present(e->meta_name);
        game_meta_t m;
        md_txt_db = (e->special >= 0) || game_metadata_get(menu->storage_prefix, e->game_code, &m);
        if (grid_more_target) {
            rom_custom_t c;
            md_txt_cus = rom_custom_load(menu->storage_prefix, path_get(grid_more_target), &c)
                      && (c.field_count > 0 || c.has_description);
        }
    }
    sound_play_effect(SFX_SETTING);
}

static void draw_metadata_panel(menu_t *menu) {
    if (fav_count == 0 || sel_fav < 0 || sel_fav >= fav_count) { show_metadata = false; return; }
    grid_entry_t *e = &fav_entry_cache[fav_indices[sel_fav]];

    int W = 460, H = 366;
    ui_components_dialog_draw(W, H);
    int x0 = DISPLAY_CENTER_X - W / 2;
    int y0 = DISPLAY_CENTER_Y - H / 2;
    int pad = 16;

    char name_buf[64];
    game_display_name(e, name_buf, sizeof(name_buf));

    rdpq_text_printf(
        &(rdpq_textparms_t){ .style_id = STL_DEFAULT, .width = W - pad * 2, .align = ALIGN_LEFT, .wrap = WRAP_ELLIPSES },
        FNT_DEFAULT, x0 + pad, y0 + pad + 4,
        "%.40s", name_buf);
    rdpq_text_printf(
        &(rdpq_textparms_t){ .style_id = STL_GRAY, .width = W - pad * 2, .align = ALIGN_LEFT },
        FNT_DEFAULT, x0 + pad, y0 + pad + 26,
        "code %c%c%c%c   region %s",
        e->game_code[0], e->game_code[1], e->game_code[2], e->game_code[3],
        e->destination_code ? region_label(e->destination_code) : "?");

    /* ---- Art (per type: baked ROM / custom override) ---- */
    int row_y = y0 + pad + 48;
    for (int t = 0; t < 6; t++) {
        bool rom = gm_md_rom_exists(e->game_code, gm_md_dfs_name[t]);
        bool cus = gm_md_custom_exists(menu, t);
        rdpq_text_printf(
            &(rdpq_textparms_t){ .style_id = STL_DEFAULT, .width = 160, .align = ALIGN_LEFT },
            FNT_DEFAULT, x0 + pad, row_y, "%s", gm_md_type_label[t]);
        rdpq_text_printf(
            &(rdpq_textparms_t){ .style_id = rom ? STL_GREEN : STL_GRAY, .width = 110, .align = ALIGN_LEFT },
            FNT_DEFAULT, x0 + pad + 170, row_y, rom ? "ROM: yes" : "ROM: --");
        rdpq_text_printf(
            &(rdpq_textparms_t){ .style_id = cus ? STL_GREEN : STL_GRAY, .width = 110, .align = ALIGN_LEFT },
            FNT_DEFAULT, x0 + pad + 290, row_y, cus ? "Custom: yes" : "Custom: --");
        row_y += 26;
    }

    /* ---- Text metadata sources ---- */
    row_y += 6;
    ui_components_box_draw(x0 + pad, row_y, x0 + W - pad, row_y + 1, RGBA32(0x44, 0x44, 0x44, 0xFF));
    row_y += 22;   /* clear gap so the separator doesn't run through the first row */
    struct { const char *label; bool present; } txt[3] = {
        { "ROM text (embedded)", md_txt_rom },
        { "Database (SD)",       md_txt_db  },
        { "Custom text",         md_txt_cus },
    };
    for (int t = 0; t < 3; t++) {
        rdpq_text_printf(
            &(rdpq_textparms_t){ .style_id = STL_DEFAULT, .width = 230, .align = ALIGN_LEFT },
            FNT_DEFAULT, x0 + pad, row_y, "%s", txt[t].label);
        rdpq_text_print(
            &(rdpq_textparms_t){ .style_id = txt[t].present ? STL_GREEN : STL_GRAY, .width = 110, .align = ALIGN_LEFT },
            FNT_DEFAULT, x0 + pad + 250, row_y, txt[t].present ? "yes" : "--");
        row_y += 26;
    }

    rdpq_text_print(
        &(rdpq_textparms_t){ .style_id = STL_GRAY, .width = W - pad * 2, .align = ALIGN_CENTER },
        FNT_DEFAULT, x0 + pad, y0 + H - pad, "B: Back");
}

/* History modal: a left-weighted list popup (like Files) with launch + fav star. */
#define HISTP_W   (DISPLAY_WIDTH * 2 / 3)
#define HISTP_LH  22
#define HISTP_PAD 14
static void draw_history_panel(menu_t *menu) {
    if (hist_sel < 0) hist_sel = 0;
    if (hist_n > 0 && hist_sel >= hist_n) hist_sel = hist_n - 1;
    if (hist_scroll > hist_sel) hist_scroll = hist_sel;
    if (hist_scroll < hist_sel - GM_HIST_VIS + 1) hist_scroll = hist_sel - GM_HIST_VIS + 1;
    if (hist_scroll < 0) hist_scroll = 0;

    int box_h = GM_HIST_VIS * HISTP_LH + HISTP_PAD * 2;
    ui_components_dialog_draw(HISTP_W, box_h);
    int x0 = DISPLAY_CENTER_X - HISTP_W / 2;
    int y0 = DISPLAY_CENTER_Y - box_h / 2;
    int rx = x0 + HISTP_PAD, rw = HISTP_W - HISTP_PAD * 2;

    if (hist_n <= 0) {
        rdpq_text_print(&(rdpq_textparms_t){ .style_id = STL_GRAY, .width = rw, .align = ALIGN_LEFT },
            FNT_DEFAULT, rx, y0 + HISTP_PAD + HISTP_LH - 4, "No history");
    } else {
        int sel_row = hist_sel - hist_scroll;
        if (sel_row >= 0 && sel_row < GM_HIST_VIS) {
            int hl_y = y0 + HISTP_PAD + sel_row * HISTP_LH;
            ui_components_box_draw(x0 + 1, hl_y, x0 + HISTP_W - 1, hl_y + HISTP_LH, CONTEXT_MENU_HIGHLIGHT_COLOR);
        }
        for (int i = 0; i < GM_HIST_VIS; i++) {
            int ri = hist_scroll + i;
            if (ri >= hist_n) break;
            int row_top = y0 + HISTP_PAD + i * HISTP_LH;
            rdpq_text_printf(
                &(rdpq_textparms_t){ .style_id = STL_DEFAULT, .width = rw - 16, .height = HISTP_LH,
                                     .align = ALIGN_LEFT, .valign = VALIGN_CENTER, .wrap = WRAP_ELLIPSES },
                FNT_DEFAULT, rx, row_top, "%s", gm_hist_lbuf[ri]);
            if (gm_hist_is_fav(menu, ri)) {
                rdpq_text_print(
                    &(rdpq_textparms_t){ .style_id = STL_YELLOW, .width = rw, .height = HISTP_LH,
                                         .align = ALIGN_RIGHT, .valign = VALIGN_CENTER },
                    FNT_DEFAULT, rx, row_top, "*");
            }
        }
    }
    rdpq_text_print(
        &(rdpq_textparms_t){ .style_id = STL_DEFAULT, .width = HISTP_W, .align = ALIGN_CENTER },
        FNT_DEFAULT, x0, y0 + box_h + 16, "A: Launch   B: Fav (hold)");
}

/* ------------------------------------------------------------------ */
/* ---------------- Screensaver helpers ---------------- */

static bool ss_any_input(menu_t *menu) {
    return menu->actions.enter    || menu->actions.back    ||
           menu->actions.settings || menu->actions.options ||
           menu->actions.go_up    || menu->actions.go_down ||
           menu->actions.go_left  || menu->actions.go_right ||
           menu->actions.go_fast  || menu->actions.lz_context;
}

/* Whether the screensaver may engage. Engages anywhere in the games-grid view when
   idle — including over its popups/menus (so "AFK in a menu" still kicks it in) — and
   on an empty grid (the marquee uses random LIBRARY art, not favorites). Excludes only
   the boot splash and the transient "Working…" sort. Allowing popups also makes the
   L+R trigger robust: even if R opened the More menu a frame before L, the 5 s hold
   still completes and engages (you return to whatever was open on wake). */
static bool ss_plain_grid(void) {
    return !splash_active && !fav_working;
}

/* Real (tab-independent) favorites count/lookup, for the screensaver's "favorites only"
   mode -- deliberately NOT fav_count/grid_items, which reflect whichever tab is active. */
static int real_favorites_count(menu_t *menu) {
    int n = 0;
    for (int k = 0; k < FAVORITES_COUNT; k++) {
        if (menu->bookkeeping.favorite_items[k].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) n++;
    }
    return n;
}
static bookkeeping_item_t *real_favorite_at(menu_t *menu, int idx) {
    for (int k = 0; k < FAVORITES_COUNT; k++) {
        bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
        if (f->bookkeeping_type == BOOKKEEPING_TYPE_EMPTY) continue;
        if (idx-- == 0) return f;
    }
    return NULL;
}

/* Load a random baked library cover (front art). Returns NULL on OOM or if the
   randomly-chosen game has no baked art (caller leaves the slot empty and retries). */
static component_boxart_t *ss_load_random(menu_t *menu) {
    /* "Favorite Screensaver" pulls only from the user's REAL favorited games (when they
       have any) -- deliberately tab-independent (real_favorites_count/_at, not
       fav_count/grid_items), so it means the same thing while browsing a library tab as
       it does on Favorites; otherwise (and on an empty grid) it pulls from the whole
       baked library. */
    int rfc = real_favorites_count(menu);
    bool fav_only = menu->settings.screensaver_favorites_only && rfc > 0;
    int pool = fav_only ? rfc : game_metadata_db_count();
    if (pool <= 0) return NULL;
    for (int tries = 0; tries < 10; tries++) {
        if (!art_can_fit()) return NULL;   /* fragmentation-safe guard: a contiguous cover block must exist (total-free is blind to fragmentation) */
        ss_rng = ss_rng * 1103515245u + 12345u;
        int idx = (int)((ss_rng >> 9) % (uint32_t)pool);
        char code[5];
        if (fav_only) {
            bookkeeping_item_t *rf = real_favorite_at(menu, idx);
            const char *gc = rf ? rf->game_code : NULL;
            if (!gc || !gc[0]) continue;          /* unknown code -> retry another favorite */
            /* Use this favorite's base code with a RANDOM region byte so the marquee shows ALL
               regional cover variants (US/JP/EU) of the favorited titles. try_dfs_sprite tries
               this region first then falls back, so an unbaked region still yields a cover. */
            static const char ss_regions[] = { 'E', 'J', 'P' };
            ss_rng = ss_rng * 1103515245u + 12345u;
            code[0] = gc[0]; code[1] = gc[1]; code[2] = gc[2];
            code[3] = ss_regions[(ss_rng >> 13) % 3];
            code[4] = '\0';
        } else {
            const char *base = game_metadata_db_base(idx);
            if (!base || !base[0]) continue;
            /* base is the 3-char code; try_dfs_sprite fills the region byte from its own
               E/P/J/... fallback list, so the 'E' here just seeds that search. */
            code[0] = base[0]; code[1] = base[1]; code[2] = base[2]; code[3] = 'E'; code[4] = '\0';
        }
        /* ~50% front, ~50% a mix of the other art types (each falls back to front when a
           game lacks that type), so the marquee shows boxes, 3D box renders, carts and logos. */
        /* GAMEPAK_3D (cart-3D) is included in the marquee. NOTE the standing OOM risk -- cart-3D
           is heavy art, and the marquee churns 70+ full-size tiles continuously, so the varying-size
           allocs can fragment the heap on very long idle runs. The art_can_fit() probe above makes a
           load that won't fit degrade to a blank tile instead of crashing; if rows look sparse on
           long AFK, the lever is SS_MAX_TILES (lower it) or splitting the cache by size. */
        static const file_image_type_t mix[] = {
            IMAGE_BOXART_BACK, IMAGE_BOXART_3D, IMAGE_GAMEPAK_FRONT, IMAGE_GAMEPAK_3D, IMAGE_LOGO };
        ss_rng = ss_rng * 1103515245u + 12345u;
        file_image_type_t type = (ss_rng & 0x10000u)
                                 ? IMAGE_BOXART_FRONT
                                 : mix[(ss_rng >> 17) % 5];
        component_boxart_t *b = ui_components_boxart_init(
            menu->storage_prefix, code, NULL, NULL, type, false);
        if (b && !b->loading && b->image) return b;
        if (b) ui_components_boxart_free(b);   /* no baked art for this code; retry */
    }
    return NULL;
}

static void ss_free_tiles(void) {
    for (int i = 0; i < SS_MAX_TILES; i++) {
        if (ss_tile[i]) { ui_components_boxart_free(ss_tile[i]); ss_tile[i] = NULL; }
    }
}

static void ss_activate(menu_t *menu) {
    ss_saved_sel    = sel_fav;
    ss_saved_scroll = scroll_row;
    ss_active       = true;
    ss_wake_lock    = 12;                       /* ~0.2 s so the trigger buttons don't wake it */
    ss_lr_start_ms  = 0;
    ss_rng         ^= ((uint32_t)get_ticks() | 1u);   /* seed */

    /* Reclaim memory for the marquee, but KEEP the covers on the page you were looking at
       so returning from the screensaver is instant -- only off-screen covers are freed, and
       they reload lazily on wake as you scroll. The art_can_fit() probe lets the marquee fill
       whatever memory is left around the retained page (fewer rows if the page is heavy, but
       no crash). Never free one mid-decode (that would use-after-free the PNG decoder cb). */
    int ss_vfirst = scroll_row;
    int ss_vlast  = visible_last_row();
    for (int pos = 0; pos < fav_count; pos++) {
        int fav_i = fav_indices[pos];
        if (!boxart_cache[fav_i] || boxart_cache[fav_i]->loading) continue;
        if (row_of[pos] >= ss_vfirst && row_of[pos] <= ss_vlast) continue;   /* keep on-screen page */
        ui_components_boxart_free(boxart_cache[fav_i]);
        boxart_cache[fav_i] = NULL;
        boxart_cache_attempted[fav_i] = false;
    }

    /* Lay out the marquee: columns to span the width (+2 buffer tiles for slide
       in/out), rows to span the height, capped by the tile budget. */
    int cols = (DISPLAY_WIDTH + SS_CELL_W - 1) / SS_CELL_W;
    ss_per_row = cols + 2;
    if (ss_per_row > 12) ss_per_row = 12;
    int rows = (DISPLAY_HEIGHT + SS_CELL_H - 1) / SS_CELL_H;
    if (rows > SS_MAX_ROWS) rows = SS_MAX_ROWS;
    while (rows > 1 && rows * ss_per_row > SS_MAX_TILES) rows--;
    ss_rows = rows;

    for (int i = 0; i < SS_MAX_TILES; i++) ss_tile[i] = NULL;
    /* Stagger each row's starting scroll phase so they don't all cross the recycle
       threshold on the same frame -- that synchronized reload was the "slam". Combined
       with the 1-tile-per-frame fill in ss_step(), loads stay spread out. */
    for (int r = 0; r < ss_rows; r++) {
        ss_off[r]   = (float)r * SS_CELL_W / (ss_rows > 0 ? ss_rows : 1);
        /* No slide-in entrance: rows sit in place from the start and just fill as covers
           load (one/frame), then drift-scroll. The cascade-from-the-side intro isn't needed
           at 3 big rows. (ss_step/draw treat ss_intro==0 as a no-op.) */
        ss_intro[r] = 0.0f;
    }
    /* Tiles fill in gradually in ss_step() (one per frame) to avoid a decode spike. */
}

static void ss_deactivate(void) {
    ss_free_tiles();
    if (ss_saved_sel >= 0 && ss_saved_sel < fav_count) sel_fav = ss_saved_sel;
    scroll_row = ss_saved_scroll;
    if (total_rows > 0 && scroll_row > total_rows - 1) scroll_row = total_rows - 1;
    if (scroll_row < 0) scroll_row = 0;
    ss_active       = false;
    ss_idle_last_ms = (uint32_t)get_ticks_ms();   /* restart the idle clock from the wake */
    ss_lr_start_ms  = 0;
    ensure_visible();
}

/* Advance the marquee one frame: fill a few empty slots, then scroll each row and
   recycle any tile that has fully left the screen with fresh random art (infinite). */
static void ss_step(menu_t *menu) {
    if (ss_wake_lock > 0) ss_wake_lock--;

    /* Fill at most ONE empty tile per frame so the initial populate (and any catch-up)
       never decodes a batch at once -- the cause of the entry "slam". */
    int fill_budget = 1;
    for (int t = 0; t < ss_rows * ss_per_row && fill_budget > 0; t++) {
        if (!ss_tile[t]) { ss_tile[t] = ss_load_random(menu); fill_budget--; }
    }

    for (int r = 0; r < ss_rows; r++) {
        if (ss_intro[r] > 0.0f) {               /* row still sliding in -> hold its scroll */
            ss_intro[r] -= SS_INTRO_SPEED;
            if (ss_intro[r] < 0.0f) ss_intro[r] = 0.0f;
            continue;
        }
        int dir = (r & 1) ? +1 : -1;            /* alternate row directions */
        ss_off[r] += SS_SCROLL_PX;
        if (ss_off[r] >= SS_CELL_W) {
            ss_off[r] -= SS_CELL_W;
            component_boxart_t **row = &ss_tile[r * ss_per_row];
            if (dir < 0) {                        /* content moves left: leftmost exits */
                if (row[0]) ui_components_boxart_free(row[0]);
                for (int k = 0; k < ss_per_row - 1; k++) row[k] = row[k + 1];
                row[ss_per_row - 1] = ss_load_random(menu);
            } else {                              /* content moves right: rightmost exits */
                if (row[ss_per_row - 1]) ui_components_boxart_free(row[ss_per_row - 1]);
                for (int k = ss_per_row - 1; k > 0; k--) row[k] = row[k - 1];
                row[0] = ss_load_random(menu);
            }
        }
    }
}

/* First alphanumeric character (uppercased) of a favorite's sorted name — used for
   the A-Z C-pad letter jumps. MUST match the sort key, which is the DB title keyed by
   the favorite's cached game_code (the grid's lazily-loaded display name is empty for
   off-screen favorites, so reading that gave wrong jumps). DB lookup is in-memory. */
static char az_initial(menu_t *menu, int gi) {
    int fav_i = fav_indices[gi];
    /* Fast + reliable path: the letter captured from the sort key (covers off-screen and
       non-DB games, which the lookups below can't resolve without an SD read). Valid only
       while the favorite count matches the sorted set (an add/remove desyncs it). */
    if (az_letter_ready && fav_count == az_letter_count &&
        fav_i >= 0 && fav_i < grid_items_cap && az_letter[fav_i]) {
        return az_letter[fav_i];
    }
    /* Persisted initial from the last sort (survives boots that skipped the re-sort). */
    if (fav_i >= 0 && fav_i < grid_items_cap &&
        grid_items[fav_i].sort_initial) {
        return grid_items[fav_i].sort_initial;
    }
    char buf[64];
    const char *nm = NULL;
    game_meta_t m;
    const char *code = grid_items[fav_i].game_code;
    if (code[0] && game_metadata_db_lookup(code, &m) && m.title && m.title[0]) {
        nm = m.title;                                        /* same source as the sort */
    } else {
        game_display_name(&fav_entry_cache[fav_i], buf, sizeof(buf));  /* fallback */
        nm = buf;
    }
    return first_initial(nm);
}

/* In A-Z mode, jump to the start of the next (dir>0) or previous (dir<0) letter group
   (iOS section-scroll style). Returns the target favorite index. */
static int az_jump(menu_t *menu, int dir) {
    if (fav_count <= 0) return sel_fav;
    char ci = az_initial(menu, sel_fav);
    if (dir > 0) {
        int j = sel_fav + 1;
        while (j < fav_count && az_initial(menu, j) == ci) j++;   /* end of current group */
        return (j < fav_count) ? j : 0;                           /* next group, or wrap to the first */
    }
    int s = sel_fav;
    while (s > 0 && az_initial(menu, s - 1) == ci) s--;           /* start of current group */
    if (s < sel_fav) return s;                                    /* mid-group: jump to its start */
    if (s > 0) {                                                  /* at a group start: go to previous group */
        int p = s - 1; char pc = az_initial(menu, p);
        while (p > 0 && az_initial(menu, p - 1) == pc) p--;
        return p;
    }
    /* already at the very first group's start: wrap to the start of the LAST group */
    char lc = az_initial(menu, fav_count - 1);
    int q = fav_count - 1;
    while (q > 0 && az_initial(menu, q - 1) == lc) q--;
    return q;
}

/* Greeting opt-in /Favorites import — state shared by process()/draw(); the scan/step/
   finish/popup helpers are defined further down (after draw()). Replaces the old
   every-boot auto-scan: enumeration only runs when the user presses R on the greeting. */
static bool     gs_can_scan   = false;  /* importable folder present -> greeting offers S/Z: Scan */
static char     gs_folder[16] = "";     /* "Favorites" / "Favourites" (label) */
static bool     gs_pending    = false;  /* user pressed R -> run */
static bool     gs_working    = false;  /* popup is up */
static bool     gs_scanned    = false;  /* enumeration done */
static path_t **gs_roms       = NULL;   /* enumerated ROM paths */
static int      gs_total      = 0;
static int      gs_done       = 0;
static void gs_scan(menu_t *menu);
static void gs_step(menu_t *menu, int chunk);
static void gs_finish(menu_t *menu);
static void gs_popup_draw(void);

static void process(menu_t *menu) {
    compute_flow();

    /* Inspect closed since last frame -> release its dedicated cover (reclaim memory). */
    if (!show_inspect && insp_boxart) {
        gm_free_inspect_boxart();
    }

    /* Deferred History fav/unfav: re-flow the grid ONCE, only after all overlays are closed,
       so toggling favorites in the History panel doesn't churn the grid live behind it. */
    if (gm_fav_dirty && !grid_more_active && !show_history && !show_inspect) {
        refresh_grid(menu);
        gm_fav_dirty = false;
    }

    if (splash_active) {
        /* Any button skips the splash — never let the user get stuck on it. */
        if (menu->actions.enter || menu->actions.back ||
            menu->actions.settings || menu->actions.options) {
            splash_active = false;
            splash_shown  = true;
        }
        return;
    }

    /* -------- Screensaver: random cover-art marquee -------- */
    {
        bool input = ss_any_input(menu);

        /* Hidden manual trigger: hold L+R together for ~5 s on the plain grid. While
           both are held, swallow their normal actions (L=lz_context/tab_prev,
           R=tab_next) and don't count the hold as activity, so the gesture completes
           cleanly. Press them together (if one registers first it fires a tab switch
           before the gesture accumulates). */
        uint32_t now = (uint32_t)get_ticks_ms();
        if (ss_idle_last_ms == 0) ss_idle_last_ms = now;   /* init on first frame */

        bool lr_held = false;
        JOYPAD_PORT_FOREACH (p) {
            joypad_buttons_t h = joypad_get_buttons_held(p);
            if (h.l && h.r) { lr_held = true; break; }
        }
        if (lr_held && !ss_active && menu->settings.screensaver_mode != SCREENSAVER_OFF &&
            ss_plain_grid()) {
            menu->actions.options    = false;
            menu->actions.lz_context = false;
            menu->actions.tab_next   = false;
            menu->actions.tab_prev   = false;
            input = false;
            if (ss_lr_start_ms == 0) ss_lr_start_ms = now;          /* hold began */
            else if (now - ss_lr_start_ms >= SS_LR_MS) { ss_activate(menu); return; }
        } else {
            ss_lr_start_ms = 0;            /* released (or noise) -> restart the hold timer */
        }

        if (input) ss_idle_last_ms = now;

        if (ss_active) {
            bool wake = input && ss_wake_lock == 0;
            if (wake || menu->settings.screensaver_mode == SCREENSAVER_OFF) {
                ss_deactivate();            /* wake: restore the saved cursor/scroll */
                if (wake) {                 /* swallow the waking press so it doesn't act */
                    sound_play_effect(SFX_GRID_BACK);
                    return;
                }
            } else {
                ss_step(menu);
                return;                     /* hold all input while the screensaver runs */
            }
        } else if (menu->settings.screensaver_mode != SCREENSAVER_OFF &&
                   (now - ss_idle_last_ms) >= ss_idle_timeout_ms(menu) && ss_plain_grid()) {
            ss_activate(menu);
            return;
        }
    }

    /* -------- GAME METADATA register panel (modal) -------- */
    if (show_metadata) {
        if (menu->actions.back || menu->actions.enter) {
            show_metadata = false;
            /* Game Metadata now lives in the Presents As submenu — return there
               (the gm_pa_fired handler reopens that submenu at gm_pa_row). */
            if (grid_more_target) {
                int md_row = 0;
                for (int i = 0; gm_pa_cm.list[i].text != NULL; i++) {
                    if (gm_pa_cm.list[i].action == gm_show_metadata) { md_row = i; break; }
                }
                grid_more_active = true;
                gm_update_dynamic_labels(menu);
                gm_pa_row   = md_row;
                gm_pa_fired = true;
            }
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    /* -------- HISTORY modal (Files-style popup) -------- */
    if (show_history) {
        if (hist_n > 0) {
            if (menu->actions.go_up)        { hist_sel = (hist_sel <= 0) ? hist_n - 1 : hist_sel - 1; sound_play_effect(SFX_GRID_MOVE); }
            else if (menu->actions.go_down) { hist_sel = (hist_sel >= hist_n - 1) ? 0 : hist_sel + 1; sound_play_effect(SFX_GRID_MOVE); }
            if (menu->actions.enter) {
                show_history = false; hist_b_hold = 0;
                gm_hist_launch(menu, (void *)(intptr_t)hist_idx[hist_sel]);
                return;
            }
            joypad_buttons_t bh = {0};
            JOYPAD_PORT_FOREACH(port) { bh = joypad_get_buttons_held(port); if (bh.raw) break; }
            if (bh.b) {
                hist_b_hold++;
                if (hist_b_hold == 20) {   /* hold B: toggle favorite of this ROM */
                    path_t *p = menu->bookkeeping.history_items[hist_idx[hist_sel]].primary_path;
                    if (p) {
                        int fi = -1;
                        for (int k = 0; k < FAVORITES_COUNT; k++) {
                            bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
                            if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && f->primary_path && path_are_match(p, f->primary_path)) { fi = k; break; }
                        }
                        if (fi >= 0) { cache_compact_after_remove(fi); bookkeeping_favorite_remove(&menu->bookkeeping, fi); }
                        else         { bookkeeping_favorite_add(&menu->bookkeeping, p, NULL, BOOKKEEPING_TYPE_ROM); }
                        /* add/remove already persist; just DEFER the grid re-flow (don't churn the
                           grid live behind the panel) -- it settles once when the overlays close. */
                        gm_fav_dirty = true;
                        sound_play_effect(SFX_SETTING);
                    }
                    hist_b_hold = 9999;
                }
                return;
            }
            if (hist_b_hold > 0 && hist_b_hold < 20) {   /* short B: back to the menu */
                hist_b_hold = 0; show_history = false;
                if (grid_more_target) {
                    int hr = 0;
                    for (int i = 0; gm_more_cm.list[i].text != NULL; i++)
                        if (gm_more_cm.list[i].action == gm_open_history) { hr = i; break; }
                    grid_more_active = true;
                    gm_update_dynamic_labels(menu);
                    ui_components_context_menu_init(&gm_more_cm);
                    ui_components_context_menu_show(&gm_more_cm);
                    gm_more_cm.row_selected = hr;
                }
                sound_play_effect(SFX_EXIT);
                return;
            }
            hist_b_hold = 0;
        } else if (menu->actions.back) {
            show_history = false;
            if (grid_more_target) {
                grid_more_active = true;
                gm_update_dynamic_labels(menu);
                ui_components_context_menu_init(&gm_more_cm);
                ui_components_context_menu_show(&gm_more_cm);
            }
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    /* -------- GRID MORE MENU (highest priority, overlays inspect/grid) -------- */
    if (grid_more_active) {
        static bool gm_cm_was_active = false;
        bool gm_cm_active = (gm_more_cm.row_selected >= 0);

        /* Favorites submenu: Sort/Clear confirmations + the blocking sort run take
           over input while active (the More menu is frozen behind them). */
        if (show_confirm_sort) {
            if (menu->actions.enter)     { show_confirm_sort = false; fav_working = true; fav_working_phase = 0; sound_play_effect(SFX_SETTING); }
            else if (menu->actions.back) { show_confirm_sort = false; gm_gs_fired = true; sound_play_effect(SFX_EXIT); }
            return;
        }
        if (fav_working) {
            if (fav_working_phase == 0) { fav_working_phase = 1; return; }   /* draw the box first */
            gm_run_sort_az(menu);
            gm_reset_all_caches(menu);
            fav_working = false;
            grid_more_active = false;                       /* return to the grid, now sorted */
            if (grid_more_target) { path_free(grid_more_target); grid_more_target = NULL; }
            gm_more_cm.row_selected = -1; gm_more_cm.submenu = NULL;
            sound_play_effect(SFX_ENTER);
            return;
        }
        if (show_confirm_clear) {
            if (menu->actions.enter) {
                show_confirm_clear = false;
                bookkeeping_favorite_clear_all(&menu->bookkeeping);
                gm_reset_all_caches(menu);
                grid_more_active = false;
                if (grid_more_target) { path_free(grid_more_target); grid_more_target = NULL; }
                gm_more_cm.row_selected = -1; gm_more_cm.submenu = NULL;
                sound_play_effect(SFX_SETTING);
            } else if (menu->actions.back) {
                show_confirm_clear = false; gm_gs_fired = true; sound_play_effect(SFX_EXIT);
            }
            return;
        }
        if (show_confirm_autosort) {
            if (menu->actions.enter) {
                show_confirm_autosort = false;
                menu->settings.always_sort_az = true;
                settings_save(&menu->settings);
                gm_update_dynamic_labels(menu);
                fav_working = true; fav_working_phase = 0;   /* sort now (and on every entry) */
                sound_play_effect(SFX_SETTING);
            } else if (menu->actions.back) {
                show_confirm_autosort = false; gm_gs_fired = true; sound_play_effect(SFX_EXIT);
            }
            return;
        }

        /* Reopen grid-settings submenu at saved row after a cycle action (or a
           cancelled favorites confirm — Sort / Clear / Always-sort live here now). */
        if (gm_gs_fired) {
            gm_gs_fired = false;
            int gs_parent_row = 0;
            for (int i = 0; gm_more_cm.list[i].text != NULL; i++) {
                if (gm_more_cm.list[i].submenu == &gm_gs_cm) { gs_parent_row = i; break; }
            }
            ui_components_context_menu_show(&gm_more_cm);
            gm_more_cm.row_selected = gs_parent_row;
            gm_more_cm.submenu = &gm_gs_cm;
            gm_gs_cm.row_selected = gm_gs_row;
            gm_gs_cm.parent = &gm_more_cm;
            gm_cm_active = true;
        }
        /* Reopen the Presents-As submenu at the saved row after an override change */
        if (gm_pa_fired) {
            gm_pa_fired = false;
            int pa_parent_row = 0;
            for (int i = 0; gm_more_cm.list[i].text != NULL; i++) {
                if (gm_more_cm.list[i].submenu == &gm_pa_cm) { pa_parent_row = i; break; }
            }
            ui_components_context_menu_show(&gm_more_cm);
            gm_more_cm.row_selected = pa_parent_row;
            gm_more_cm.submenu = &gm_pa_cm;
            gm_pa_cm.row_selected = gm_pa_row;
            gm_pa_cm.parent = &gm_more_cm;
            gm_cm_active = true;
        }
        /* Reopen More menu after fav toggle, keeping the cursor on the fav row. */
        if (gm_fav_fired) {
            gm_fav_fired = false;
            int fav_row = 0;
            for (int i = 0; gm_more_cm.list[i].text != NULL; i++) {
                if (gm_more_cm.list[i].action == gm_fav_toggle) { fav_row = i; break; }
            }
            ui_components_context_menu_show(&gm_more_cm);
            gm_more_cm.row_selected = fav_row;
            gm_cm_active = true;
        }

        if (!gm_cm_active && gm_cm_was_active) {
            /* More menu dismissed (B pressed without selecting) — commit any deferred
               favorite change now, then return to wherever we came from. */
            gm_apply_pending_fav(menu);
            grid_more_active = false;
            if (grid_more_target) { path_free(grid_more_target); grid_more_target = NULL; }
            /* show_inspect stays as it was (grid_more_from_inspect keeps inspect visible) */
        }
        gm_cm_was_active = gm_cm_active;

        if (gm_cm_active) {
            ui_components_context_menu_process(menu, &gm_more_cm);
        }
        return;
    }

    /* -------- Library scan staging: the "Working..." box was drawn last frame; run the
       (fast, in-memory, no-header-reads) scan now and land on the new tab. -------- */
    if (lib_scan_working) {
        library_t *lib = library_get(lib_scan_tab - 1);
        if (lib) library_scan(lib);
        lib_scan_working = false;
        grid_flush_caches();
        grid_repoint_items(menu);
        refresh_grid(menu);
        sel_fav    = 0;
        scroll_row = 0;
        return;
    }

    /* -------- Tab switching: Favorites <-> pinned libraries <-> Files --------
       Suppressed while inspect/move-mode/a confirm popup owns input (those are all
       gated behind show_inspect or move_mode below), and while the More menu or any
       modal above is open (already returned by this point). */
    if (!show_inspect && !move_mode && (menu->actions.tab_next || menu->actions.tab_prev)) {
        grid_switch_tab(menu, menu->actions.tab_next ? 1 : -1);
        sound_play_effect(SFX_CURSOR);
        return;
    }

    /* -------- INSPECT POPUP (modal) -------- */
    if (show_inspect) {
        if (fav_count == 0) { show_inspect = false; return; }

        /* B: close inspect popup */
        if (menu->actions.back) {
            show_inspect = false;
            sound_play_effect(SFX_GRID_BACK);
            return;
        }

        if (menu->actions.enter || menu->actions.settings) {
            int fav_i = fav_indices[sel_fav];
            path_t *rom_p = grid_items[fav_i].primary_path;
            if (!rom_p || !file_exists(path_get(rom_p))) {
                menu_show_error(menu, "ROM file not found on SD card.");
            } else {
                show_inspect = false;
                sound_play_effect(SFX_LAUNCH);
                launch_favorite(menu, fav_i, true);
            }
        } else if (menu->actions.options) {
            /* R in inspect: open More menu as overlay (keep inspect visible behind) */
            sound_play_effect(SFX_SETTING);
            gm_open(menu, true);
        } else if (menu->actions.go_fast) {
            if (menu->actions.go_down) {
                desc_scroll += DESC_SCROLL_STEP;
                sound_play_effect(SFX_CURSOR);
            } else if (menu->actions.go_up) {
                desc_scroll -= DESC_SCROLL_STEP;
                if (desc_scroll < 0) desc_scroll = 0;
                sound_play_effect(SFX_CURSOR);
            }
        } else {
            int before = sel_fav;
            int cx = xof[sel_fav] + wof[sel_fav] / 2;
            if (menu->actions.go_up)         { int r = row_of[sel_fav] - 1; if (r < 0) r = total_rows - 1; int t = nearest_in_row(r, cx); if (t >= 0) sel_fav = t; }
            else if (menu->actions.go_down)  { int r = row_of[sel_fav] + 1; if (r >= total_rows) r = 0; int t = nearest_in_row(r, cx); if (t >= 0) sel_fav = t; }
            else if (menu->actions.go_left)  { if (sel_fav > 0 && row_of[sel_fav - 1] == row_of[sel_fav]) sel_fav--; }
            else if (menu->actions.go_right) { if (sel_fav + 1 < fav_count && row_of[sel_fav + 1] == row_of[sel_fav]) sel_fav++; }
            if (sel_fav != before) { desc_scroll = 0; ensure_visible(); sound_play_effect(SFX_GRID_MOVE); }
        }
        return;
    }

    /* -------- EMPTY LIST -------- */
    if (fav_count == 0) {
        /* The /Favorites-folder greeting/import is Favorites-only -- a library tab has no
           equivalent "well-known folder" to offer, it's just empty until pinned ROMs show up. */
        if (grid_tab == 0) {
            /* Greeting "Scan SD:/Favorites" import — incremental so the popup animates.
               Frame 1: show the box. Frame 2: enumerate. Then a few ROMs per frame. */
            if (gs_pending) {
                if (!gs_working) { gs_working = true; return; }   /* draw the box first */
                if (!gs_scanned) { gs_scan(menu); return; }        /* draw 0% before mutating */
                gs_step(menu, 8);
                if (gs_done >= gs_total) gs_finish(menu);
                return;
            }
            /* When a Favorites folder exists, auto-import it a short moment AFTER the greeting
               first appears (so the user sees the greet, then the scan runs in the background
               while they read). S launches into the grid right away; Z still works too. */
            if (gs_can_scan && !gs_scanned) {
                /* Wait for the user: S (or Z) starts the import and launches into the grid.
                   No auto-start -- the greeting stays up until they choose. */
                if (menu->actions.settings || menu->actions.options) {
                    gs_pending = true;
                    sound_play_effect(SFX_SETTING);
                }
                return;
            }
        }
        if (menu->actions.options) {           /* Z: no folder -> open the file browser */
            view_browser_open_popup(menu);     /* the shrink-wrapped file popup */
            sound_play_effect(SFX_CURSOR);
        }
        return;
    }

    /* -------- MOVE MODE: D-pad reorders, A/B/START confirms -------- */
    if (move_mode) {
        /* Hold R to remove the selected favorite (smartphone-style delete). */
        joypad_buttons_t mh = {0};
        JOYPAD_PORT_FOREACH(port) { mh = joypad_get_buttons_held(port); if (mh.raw) break; }
        if (mh.r) {
            move_r_held++;
            if (move_r_held == UNFAV_HOLD_FRAMES) {
                int fav_i = fav_indices[sel_fav];
                cache_compact_after_remove(fav_i);
                bookkeeping_favorite_remove(&menu->bookkeeping, fav_i);
                refresh_grid(menu);
                compute_flow();
                if (fav_count == 0) {
                    bookkeeping_save(&menu->bookkeeping);
                    move_mode = false;
                } else {
                    if (sel_fav >= fav_count) sel_fav = fav_count - 1;
                    ensure_visible();
                }
                sound_play_effect(SFX_SETTING);
            }
            return;
        } else {
            move_r_held = 0;
        }

        int cx = xof[sel_fav] + wof[sel_fav] / 2;
        if (menu->actions.enter || menu->actions.back || menu->actions.settings) {
            bookkeeping_save(&menu->bookkeeping);
            move_mode = false;
            sound_play_effect(SFX_ENTER);
        } else if (menu->actions.go_right) {
            if (sel_fav + 1 < fav_count) { move_to(menu, sel_fav + 1); compute_flow(); ensure_visible(); sound_play_effect(SFX_GRID_MOVE); }
        } else if (menu->actions.go_left) {
            if (sel_fav > 0) { move_to(menu, sel_fav - 1); compute_flow(); ensure_visible(); sound_play_effect(SFX_GRID_MOVE); }
        } else if (menu->actions.go_down) {
            int t = nearest_in_row(row_of[sel_fav] + 1, cx);
            if (t >= 0) { move_to(menu, t); compute_flow(); ensure_visible(); sound_play_effect(SFX_GRID_MOVE); }
        } else if (menu->actions.go_up) {
            int t = nearest_in_row(row_of[sel_fav] - 1, cx);
            if (t >= 0) { move_to(menu, t); compute_flow(); ensure_visible(); sound_play_effect(SFX_GRID_MOVE); }
        }
        return;
    }

    /* -------- B hold detection: 30 frames → move mode --------
       Disabled while "Always sort A-Z" is on: manual ordering would just be
       undone on the next grid entry, so we hide the affordance entirely. Also disabled
       on a library tab -- a library is a read-only gallery, not a list you reorder. */
    joypad_buttons_t bh = {0};
    JOYPAD_PORT_FOREACH(port) {
        bh = joypad_get_buttons_held(port);
        if (bh.raw) break;
    }
    if (bh.b && !menu->settings.always_sort_az && grid_tab == 0) {
        b_held_frames++;
        if (b_held_frames == MOVE_HOLD_FRAMES) {
            move_mode     = true;
            move_tick     = 0;
            sel_enter_t   = 1.0f;
            prev_sel_fav  = sel_fav;
            sound_play_effect(SFX_SETTING);
        }
        return;
    } else {
        b_held_frames = 0;
    }

    /* -------- NORMAL NAVIGATION -------- */
    if (menu->actions.settings) {
        sound_play_effect(SFX_LAUNCH);
        launch_favorite(menu, fav_indices[sel_fav], true);
    } else if (menu->actions.enter) {
        show_inspect          = true;
        show_confirm_remove   = false;
        desc_scroll           = 0;
        sound_play_effect(SFX_GRID_ENTER);
    } else if (menu->actions.options) {
        /* Z: open the universal More menu as an overlay on the grid */
        if (fav_count > 0) {
            sound_play_effect(SFX_SETTING);
            gm_open(menu, false);
        }
    } else if (menu->actions.go_fast && (menu->actions.go_up || menu->actions.go_down)) {
        /* C-up/down: page scroll by a visible page -- SAME in A-Z and normal modes.
           Clamps to the edge row first; pressing again AT the edge wraps around the grid
           (top<->bottom), matching the d-pad up/down wrap. */
        int span = visible_last_row() - scroll_row + 1;
        if (span < 1) span = 1;
        int dir = menu->actions.go_down ? 1 : -1;
        int cur = row_of[sel_fav];
        int tr  = cur + dir * span;
        if (tr < 0)              tr = (cur == 0) ? total_rows - 1 : 0;
        if (tr > total_rows - 1) tr = (cur == total_rows - 1) ? 0 : total_rows - 1;
        int t = nearest_in_row(tr, xof[sel_fav] + wof[sel_fav] / 2);
        if (t >= 0 && t != sel_fav) { sel_fav = t; ensure_visible(); sound_play_effect(SFX_GRID_MOVE); }
    } else if (menu->actions.go_fast && (menu->actions.go_left || menu->actions.go_right)) {
        if (menu->settings.always_sort_az) {
            /* A-Z mode: C-right/left jump to the next/previous initial-letter group. */
            int t = az_jump(menu, menu->actions.go_right ? 1 : -1);
            if (t >= 0 && t != sel_fav) { sel_fav = t; ensure_visible(); sound_play_effect(SFX_GRID_MOVE); }
        } else {
            /* Normal mode: C-left/right jump to the first / last tile in the current row. */
            int r = row_of[sel_fav], t = sel_fav;
            if (menu->actions.go_left)  { while (t > 0           && row_of[t - 1] == r) t--; }
            else                        { while (t + 1 < fav_count && row_of[t + 1] == r) t++; }
            if (t != sel_fav) { sel_fav = t; ensure_visible(); sound_play_effect(SFX_GRID_MOVE); }
        }
    } else if (menu->actions.go_up) {
        int r = row_of[sel_fav] - 1; if (r < 0) r = total_rows - 1;   /* wrap to bottom */
        int t = nearest_in_row(r, xof[sel_fav] + wof[sel_fav] / 2);
        if (t >= 0) { sel_fav = t; ensure_visible(); sound_play_effect(SFX_GRID_MOVE); }
    } else if (menu->actions.go_down) {
        int r = row_of[sel_fav] + 1; if (r >= total_rows) r = 0;      /* wrap to top */
        int t = nearest_in_row(r, xof[sel_fav] + wof[sel_fav] / 2);
        if (t >= 0) { sel_fav = t; ensure_visible(); sound_play_effect(SFX_GRID_MOVE); }
    } else if (menu->actions.go_left) {
        /* Past the leftmost tile -> last tile of the row above (wraps first<->last overall). */
        sel_fav = (sel_fav > 0) ? sel_fav - 1 : fav_count - 1;
        ensure_visible(); sound_play_effect(SFX_GRID_MOVE);
    } else if (menu->actions.go_right) {
        /* Past the rightmost tile -> first tile of the row below (wraps last<->first overall). */
        sel_fav = (sel_fav + 1 < fav_count) ? sel_fav + 1 : 0;
        ensure_visible(); sound_play_effect(SFX_GRID_MOVE);
    }
}

/* Render the active screensaver: a full-screen marquee of random cover-art, rows
   scrolling in alternating directions. Kept fully separate from the normal grid draw
   so it can never perturb the regular path (which only runs while !ss_active). */
static void draw_screensaver(menu_t *menu) {
    (void)menu;
    int grid_h = ss_rows * SS_CELL_H;
    int y0 = (DISPLAY_HEIGHT - grid_h) / 2;            /* centre; may be slightly negative */

    /* Inset 4px top/bottom: with 3 rows the marquee is exactly 480px tall (no margin), so any
       edge sliver (top-row cover top, or a VI/overscan wrap of the bottom line) shows at the
       screen edge. Clipping the tiles to y=4..476 leaves a thin black band that masks it. */
    rdpq_set_scissor(0, 4, DISPLAY_WIDTH, DISPLAY_HEIGHT - 4);
    for (int r = 0; r < ss_rows; r++) {
        int dir = (r & 1) ? +1 : -1;
        float phase = (dir < 0) ? ss_off[r] : (SS_CELL_W - ss_off[r]);
        /* Slide-in: a row off-screen on its entry side (dir<0 = from the right, dir>0 = from
           the left) until ss_intro reaches 0. */
        int introx = (dir < 0) ? (int)ss_intro[r] : -(int)ss_intro[r];
        int ry = y0 + r * SS_CELL_H;
        component_boxart_t **row = &ss_tile[r * ss_per_row];
        for (int i = 0; i < ss_per_row; i++) {
            component_boxart_t *b = row[i];
            if (!b || b->loading || !b->image || !b->image->width || !b->image->height) continue;
            int cx = (int)(i * SS_CELL_W - phase) + introx;
            int cell_w = SS_CELL_W - 4, cell_h = SS_CELL_H - 4;
            int cellx = cx + 2, celly = ry + 2;
            float sx = (float)cell_w / b->image->width;
            float sy = (float)cell_h / b->image->height;
            /* Fit WITHIN the cell (aspect-correct, no crop). Cover-fit hugged the cell but
               mangled transparent/angled art (3D carts, logos); letterbox gaps on black are
               fine, and rdpq_tex_blit tolerates off-screen coords, so no per-cell scissor. */
            float scale = (sx < sy) ? sx : sy;
            int dw = (int)(b->image->width  * scale);
            int dh = (int)(b->image->height * scale);
            int dx = cellx + (cell_w - dw) / 2;
            int dy = celly + (cell_h - dh) / 2;
            rdpq_mode_push();
                rdpq_set_mode_standard();
                rdpq_mode_filter(FILTER_BILINEAR);
                /* Cutout at the MID alpha (128), not 1: a 3D cart's irregular silhouette
                   under bilinear leaves a fuzzy interpolated fringe (alpha 1..254) that
                   reads as a rough/eroded edge. Clipping at 128 keeps only the solid core
                   -> clean hard silhouette. (Boxes/labels are solid so they were unaffected;
                   user reported only the carts' edges as funky.) */
                rdpq_mode_alphacompare(128);
                rdpq_tex_blit(b->image, dx, dy,
                              &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
            rdpq_mode_pop();
        }
    }
}

/* ---- Screensaver as a reusable animated background for OTHER views (e.g. the credits popup).
   Drives the same marquee state: begin lays out tiles (and frees the fav cover cache to reclaim
   RAM -- covers reload lazily on return), frame steps + draws to the ATTACHED surface, end
   restores. The caller MUST call end on exit, or the grid would return showing the screensaver
   (it keys off ss_active). ---- */
/* Release ALL grid cover art so a large contiguous block is available -- e.g. before loading
   the 486KB Firple font, which a full+fragmented cover cache (cap 80) can't make room for, so
   asset_load asserts "Out of memory" (the font-toggle crash). Covers reload lazily when the grid
   is next shown. Skips any mid-decode cover (freeing it would use-after-free the PNG decode cb). */
void view_grid_release_boxart (void) {
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        if (boxart_cache[i] && !boxart_cache[i]->loading) {
            ui_components_boxart_free(boxart_cache[i]);
            boxart_cache[i] = NULL;
            boxart_cache_attempted[i] = false;
        }
    }
    if (insp_boxart && !insp_boxart->loading) {
        gm_free_inspect_boxart();
    }
}

void view_grid_screensaver_begin (menu_t *menu) { if (!ss_active) ss_activate(menu); }
void view_grid_screensaver_frame (menu_t *menu) { if (ss_active) { ss_step(menu); draw_screensaver(menu); } }
void view_grid_screensaver_end   (menu_t *menu) { if (ss_active) ss_deactivate(); }

static void draw(menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);
    ui_components_background_draw();

    compute_flow();
    ensure_visible();

    if (ss_active) {
        draw_screensaver(menu);
        rdpq_detach_show();
        return;
    }

    /* Tab bar is always shown: Favorites plus either the pinned libraries or a single
       placeholder tab explaining how to pin one. */
    int grid_tab_n = grid_tab_count();
    {
        const char *tab_labels[LIBRARIES_MAX + 1];
        int lib_n = library_count();
        tab_labels[0] = "Favorites";
        if (lib_n == 0) {
            tab_labels[1] = "+ Library";
        } else {
            for (int i = 0; i < lib_n; i++) {
                library_t *lib = library_get(i);
                tab_labels[1 + i] = (lib && lib->name[0]) ? lib->name : "Library";
            }
        }
        float tab_w = (float)VISIBLE_AREA_WIDTH / (float)grid_tab_n;
        ui_components_tabs_draw(tab_labels, grid_tab_n, grid_tab, tab_w);
    }

    if (lib_scan_working) {
        ui_components_messagebox_draw("Loading library...");
        rdpq_detach_show();
        return;
    }

    if (fav_count == 0 && grid_tab != 0) {
        library_t *lib = library_get(grid_tab - 1);
        if (lib) {
            /* An empty library tab (nothing under the pinned folder, or not yet rescanned
               after its contents changed) -- no Favorites-specific greeting here. */
            ui_components_main_text_draw(STL_DEFAULT, ALIGN_CENTER, VALIGN_CENTER,
                                     "\"%s\" has no ROMs.\n"
                                     "\n"
                                     "Z: File Browser",
                                     lib->name);
        } else {
            /* The placeholder tab (no libraries pinned yet). */
            ui_components_main_text_draw(STL_DEFAULT, ALIGN_CENTER, VALIGN_CENTER,
                                     "Pinned folders appear here as tabs.\n"
                                     "\n"
                                     "To pin one: press Z, choose \"File Browser\",\n"
                                     "browse to a folder, press Z for its menu,\n"
                                     "and choose \"Pin as Library\".\n"
                                     "\n"
                                     "Z: File Browser");
        }
    } else if (fav_count == 0) {
        /* The greeting signoff flows through the rainbow like the inspect-border glow:
           recolour the spare style slots (STL_RAINBOW_BASE..+6 = ^07..^0D) every
           frame, spread across the hue wheel and rotated by an animated offset. */
        /* A soft, slowly-shifting sheen rather than a marching rainbow. The 7 style
           slots sit CLOSE together on the hue wheel (small spread) so the per-column
           colouring reads as a faint shimmer, not bands; the whole set drifts slowly
           (~14 s per cycle) and a little dimmer, so the colour gently shifts in place
           instead of parading across the text. */
        static float greet_phase = 0.0f;
        greet_phase += 0.15f;   /* halved for the 60fps render (same shimmer speed as 30fps) */
        uint8_t greet_hue = (uint8_t)greet_phase;
        for (int i = 0; i < STL_RAINBOW_COUNT; i++) {
            fonts_set_style_color(STL_RAINBOW_BASE + i,
                                  rainbow_color((uint8_t)(greet_hue + i * 5), 205));
        }
        static const char signoff[] =
            "I really hope you enjoy N64ever, I put\n"
            "my heart and soul into making it the best I could.\n"
            "\n"
            "Thanks for using it,\n"
            "Bjerreman ★";
        char rainbow[1280];
        size_t o = 0;
        int col = 0;
        for (const char *p = signoff; *p && o + 5 < sizeof(rainbow); p++) {
            if (*p == '\n') { rainbow[o++] = '\n'; col = 0; continue; }
            if (*p != ' ') {   /* colour each visible glyph by its column */
                int nib = (STL_RAINBOW_BASE + (col % STL_RAINBOW_COUNT)) & 0xF; /* 0x7..0xD */
                rainbow[o++] = '^';
                rainbow[o++] = '0';
                rainbow[o++] = (nib < 10) ? ('0' + nib) : ('A' + nib - 10);
            }
            rainbow[o++] = *p;
            col++;
        }
        rainbow[o] = '\0';
        if (gs_can_scan) {
            ui_components_main_text_draw(STL_DEFAULT, ALIGN_CENTER, VALIGN_CENTER,
                                     "Welcome to N64ever!\n"
                                     "\n"
                                     "Your grid is empty, but I found a\n"
                                     "Favorites folder on your SD card.\n"
                                     "\n"
                                     "Press S to import it and launch.\n"
                                     "(You can rearrange or remove any later.)\n"
                                     "\n"
                                     "\n"
                                     "\n"
                                     "%s^00", rainbow);
        } else {
            ui_components_main_text_draw(STL_DEFAULT, ALIGN_CENTER, VALIGN_CENTER,
                                     "Welcome to N64ever!\n"
                                     "\n"
                                     "Your favorites grid is empty.\n"
                                     "\n"
                                     "Press Z for the File Browser, then\n"
                                     "hold B on a game to add it here.\n"
                                     "\n"
                                     "Or highlight a folder and press Z to\n"
                                     "favorite every game inside it at once.\n"
                                     "\n"
                                     "\n"
                                     "%s^00", rainbow);
        }
    } else {
        int lastr   = visible_last_row();
        int tile_x0 = GRID_X0;
        int tile_x1 = GRID_X1;

        /* Always centre the visible rows at the grid midpoint */
        int grid_mid  = (GRID_Y0 + GRID_Y1) / 2;
        int visible_h = 0;
        for (int r = scroll_row; r <= lastr; r++) visible_h += row_h[r] + G_GAP;
        if (visible_h > 0) visible_h -= G_GAP;
        int yy_start  = grid_mid - visible_h / 2;
        if (yy_start < TILE_AREA_Y0) yy_start = TILE_AREA_Y0;

        /* ---- Pass 1: all non-selected fully-visible tiles ---- */
        rdpq_set_scissor(tile_x0, GRID_Y0, tile_x1, GRID_Y1);
        int yy = yy_start;
        for (int r = scroll_row; r <= lastr; r++) {
            for (int gi = row_first[r]; gi < fav_count && row_of[gi] == r; gi++) {
                if (gi == sel_fav) continue;
                draw_tile(gi, xof[gi], yy + (row_h[r] - hof[gi]) / 2,
                          wof[gi], hof[gi], 0.0f, false);
            }
            yy += row_h[r] + G_GAP;
        }

        /* ---- Pass 2: selected tile last — glow renders on top ---- */
        rdpq_set_scissor(tile_x0, GRID_Y0, tile_x1, GRID_Y1);
        if (sel_fav >= 0 && row_of[sel_fav] >= scroll_row && row_of[sel_fav] <= lastr) {
            int r  = row_of[sel_fav];
            int ry = yy_start;
            for (int rr = scroll_row; rr < r; rr++) ry += row_h[rr] + G_GAP;
            draw_tile(sel_fav, xof[sel_fav], ry + (row_h[r] - hof[sel_fav]) / 2,
                      wof[sel_fav], hof[sel_fav], sel_enter_t, move_mode);
        }
        rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }

    /* Suppressed under the overlays -- they own the screen and draw their own action bars.
       Kept in move mode, where knowing what you're dragging is the whole point. (The
       screensaver and the library-scan messagebox already returned above.) */
    if (!show_history && !show_metadata && !grid_more_active && !show_inspect) {
        draw_selected_name_footer();
    }

    if (show_history) {
        draw_history_panel(menu);
    } else if (show_metadata) {
        draw_metadata_panel(menu);
    } else if (grid_more_active) {
        /* Draw inspect behind the More menu if it was open when More was triggered */
        if (grid_more_from_inspect && show_inspect) {
            draw_inspect(menu);
        }
        /* Draw the More menu context popup on top */
        ui_components_context_menu_draw(&gm_more_cm);
        /* Favorites submenu confirmations / progress on top of the menu */
        if (show_confirm_sort) {
            ui_components_messagebox_draw(
                "Sort favorites A-Z?\n"
                "This reorders all favorites\n"
                "and cannot be undone.\n\n"
                "A: Yes, B: Back"
            );
        } else if (show_confirm_clear) {
            ui_components_messagebox_draw(
                "Clear ALL favorites?\n"
                "This cannot be undone.\n\n"
                "A: Yes, B: Back"
            );
        } else if (show_confirm_autosort) {
            ui_components_messagebox_draw(
                "Always sort A-Z?\n"
                "\n"
                "On a large library the first launch after\n"
                "this -- and any launch after adding games --\n"
                "takes a few extra seconds (game names are\n"
                "read once, then cached). Later launches stay\n"
                "fast, and favoriting a folder is slower too.\n"
                "\n"
                "Tip: turn OFF 'Use Custom Files' first\n"
                "(custom files make sorting slower).\n"
                "\n"
                "A: Yes, B: Back"
            );
        } else if (fav_working) {
            ui_components_messagebox_draw("Sorting favorites...\n\nB: Cancel");
        }
    } else if (show_inspect) {
        draw_inspect(menu);
        if (show_confirm_remove) {
            draw_confirm_remove(menu);
        }
    } else if (move_mode) {
        ui_components_actions_bar_buttons_draw("A: Done", "B: Done", "S: Done", NULL, "R: Unfav (hold)");
    } else if (fav_count == 0) {
        /* Empty grid: Z either imports the /Favorites folder (if one was found) or opens
           the file browser (see process()). */
        if (gs_can_scan) {
            ui_components_actions_bar_buttons_draw(NULL, NULL, "S: Launch", NULL, NULL);
        } else {
            ui_components_actions_bar_buttons_draw(NULL, NULL, NULL, NULL, "Z: File Browser");
        }
    } else {
        /* Standardized fixed slots: A inspect, B move, S launch, C page, Z menu. In A-Z
           mode C does letter jumps ("C: A-Z") and manual Move is disabled (B hidden). */
        bool az = menu->settings.always_sort_az;
        ui_components_actions_bar_buttons_draw(
            "A: Inspect",
            az ? NULL : "B: Move (hold)",
            "S: Launch",
            az ? "C: A-Z" : "C: Page",
            "Z: Menu");
    }

    /* Splash overlay: shown until the first full page (all visible rows) of boxart
       has settled, with a hard failsafe timeout so a stuck/slow load can never trap
       the user here. */
    if (splash_active) {
        /* WALL-CLOCK failsafe (not a frame count): if per-frame SD I/O tanks the
           framerate, a frame-count cap can stretch to tens of seconds. Release after
           1.5 s of real time no matter what, so the grid is always reachable fast and
           any slow covers simply pop in afterward. */
        static uint32_t splash_start_ms = 0;
        if (splash_start_ms == 0) splash_start_ms = (uint32_t)get_ticks_ms();
        uint32_t splash_elapsed = (uint32_t)get_ticks_ms() - splash_start_ms;
        /* Show the splash for a MINIMUM of 3.5 s so a fast boot doesn't flash it by, then
           release as soon as the visible page of covers has settled. Hard failsafe at 7 s
           so a stuck/slow load can never trap the user. */
        bool done = splash_elapsed > 7000;            /* failsafe (max) */
        if (!done && splash_elapsed >= 3500) {        /* past the minimum: release when ready */
            done = (fav_count == 0);
            if (!done) {
                int vlast = visible_last_row();
                done = true;
                for (int gi = 0; gi < fav_count; gi++) {
                    int r = row_of[gi];
                    if (r < scroll_row || r > vlast) continue;   /* only the visible page */
                    int fav_i = fav_indices[gi];
                    if (!boxart_cache_attempted[fav_i] ||
                        (boxart_cache[fav_i] && boxart_cache[fav_i]->loading)) {
                        done = false; break;
                    }
                }
            }
        }
        if (!done) {
            ui_components_background_draw_splash(menu->settings.custom_splash_enabled);
        } else {
            splash_active = false;
            splash_shown  = true;
            splash_start_ms = 0;
            /* Grid is interactive. get_ticks_ms() counts from CPU-reset-release, so this
               absolute value is the CPU-start->grid time (USB log only; no on-screen readout). */
            debugf("[BOOT] grid interactive at %llu ms (since CPU reset)\n", get_ticks_ms());
        }
    }

    /* Greeting opt-in import: progress popup on top of the greeting (we stay on this
       screen during the scan rather than jumping to the browser). */
    if (gs_working) {
        gs_popup_draw();
    }

    rdpq_detach_show();
}

/* Public: draw grid tiles + background without overlays, for other views to use
   as their background (load_rom game settings, browser popup, etc.).
   Caller must have already called rdpq_attach(). */
void view_games_grid_draw_background(menu_t *menu, surface_t *display) {
    (void)display;
    compute_flow();
    ensure_visible();
    ui_components_background_draw();

    if (fav_count > 0) {
        int lastr   = visible_last_row();
        int tile_x0 = GRID_X0;
        int tile_x1 = GRID_X1;
        int grid_mid  = (GRID_Y0 + GRID_Y1) / 2;
        int visible_h = 0;
        for (int r = scroll_row; r <= lastr; r++) visible_h += row_h[r] + G_GAP;
        if (visible_h > 0) visible_h -= G_GAP;
        int yy_start = grid_mid - visible_h / 2;
        if (yy_start < TILE_AREA_Y0) yy_start = TILE_AREA_Y0;

        rdpq_set_scissor(tile_x0, GRID_Y0, tile_x1, GRID_Y1);
        int yy = yy_start;
        for (int r = scroll_row; r <= lastr; r++) {
            for (int gi = row_first[r]; gi < fav_count && row_of[gi] == r; gi++) {
                if (gi == sel_fav) continue;
                draw_tile(gi, xof[gi], yy + (row_h[r] - hof[gi]) / 2, wof[gi], hof[gi], 0.0f, false);
            }
            yy += row_h[r] + G_GAP;
        }
        rdpq_set_scissor(tile_x0, GRID_Y0, tile_x1, GRID_Y1);
        if (sel_fav >= 0 && row_of[sel_fav] >= scroll_row && row_of[sel_fav] <= lastr) {
            int r  = row_of[sel_fav];
            int ry = yy_start;
            for (int rr = scroll_row; rr < r; rr++) ry += row_h[rr] + G_GAP;
            draw_tile(sel_fav, xof[sel_fav], ry + (row_h[r] - hof[sel_fav]) / 2,
                      wof[sel_fav], hof[sel_fav], sel_enter_t, false);
        }
        rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }
}

/* ------------------------------------------------------------------ */
/* Scan /Favorites and /Favourites on the SD card and add any ROMs found there
   to the bookkeeping favorites list (if not already present).  This lets users
   drop ROMs into a folder instead of hold-B-ing each one. */
static const char *auto_fav_rom_ext[]    = { "z64", "n64", "v64", "rom", NULL };
static const char *auto_fav_folder_names[] = { "Favorites", "Favourites", NULL };

/* Greeting opt-in import helpers (state + prototypes declared above process()). */

/* Phase 1 (one frame): collect every visible ROM path under the import folders WITHOUT
   writing the SD -- writing favorites.ini mid-enumeration corrupts the FatFs iterator. */
static void gs_scan(menu_t *menu) {
    gs_roms = NULL; gs_total = 0; gs_done = 0;
    int cap = 0;
    for (int di = 0; auto_fav_folder_names[di]; di++) {
        path_t *dir = path_init(menu->storage_prefix, "");
        path_push(dir, (char *)auto_fav_folder_names[di]);
        dir_t info;
        int r = dir_findfirst(path_get(dir), &info);
        while (r == 0) {
            if (info.d_type != DT_DIR && info.d_name[0] != '.'   /* skip hidden + macOS ._ sidecars */
                && file_has_extensions(info.d_name, auto_fav_rom_ext)) {
                path_t *rom = path_clone_push(dir, info.d_name);
                if (gs_total >= cap) {
                    int nc = cap ? cap * 2 : 64;
                    path_t **g = realloc(gs_roms, nc * sizeof(path_t *));
                    if (g) { gs_roms = g; cap = nc; }
                }
                if (gs_total < cap) gs_roms[gs_total++] = rom; else path_free(rom);
            }
            r = dir_findnext(path_get(dir), &info);
        }
        path_free(dir);
    }
    gs_scanned = true;
    debugf("[GREETSCAN] found %d ROM(s)\n", gs_total);
}

/* Phase 2: favorite up to `chunk` enumerated ROMs (dedup; no per-item save -- gs_finish
   persists once, so this stays O(N) rather than rewriting favorites.ini each add). */
static void gs_step(menu_t *menu, int chunk) {
    int end = gs_done + chunk; if (end > gs_total) end = gs_total;
    for (; gs_done < end; gs_done++) {
        path_t *rom = gs_roms[gs_done];
        bool already = false;
        for (int k = 0; k < FAVORITES_COUNT; k++) {
            bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
            if (f->bookkeeping_type == BOOKKEEPING_TYPE_EMPTY) break;   /* compacted: end */
            if (f->primary_path && path_are_match(rom, f->primary_path)) { already = true; break; }
        }
        if (already) continue;

        /* Pre-cache the ROM code (behind the progress bar) ONLY when A-Z sorting is on --
           that's the mode whose boot reads every game name up front, so caching here avoids
           the bulk ~950-header read on the next boot. With A-Z off the grid resolves names
           lazily as tiles scroll into view, so skip the read and keep the scan fast. */
        char code[5] = "";
        if (menu->settings.always_sort_az) {
            rom_info_t qi;
            if (rom_info_load_quick(rom, &qi) == ROM_OK) {
                memcpy(code, qi.game_code, 4); code[4] = '\0';
                rom_info_free_meta(&qi);
            }
        }
        bookkeeping_favorite_add_nosave(&menu->bookkeeping, rom, NULL, BOOKKEEPING_TYPE_ROM);
        if (code[0]) {   /* insert_top placed the new entry at index 0 */
            memcpy(menu->bookkeeping.favorite_items[0].game_code, code, 4);
            menu->bookkeeping.favorite_items[0].game_code[4] = '\0';
        }
    }
}

/* Phase 3: free the list, persist once, rebuild the grid, and reset state. */
static void gs_finish(menu_t *menu) {
    for (int j = 0; j < gs_total; j++) path_free(gs_roms[j]);
    free(gs_roms); gs_roms = NULL;
    bookkeeping_save(&menu->bookkeeping);
    refresh_grid(menu);
    compute_flow();
    gs_total = gs_done = 0;
    gs_pending = gs_working = gs_scanned = false;
    gs_can_scan = false;   /* imported once; if still empty the greeting reverts to File Browser */
    debugf("[GREETSCAN] import complete\n");
}

/* The "Working" popup: title + a slim progress bar, like the Files folder-fav popup. */
static void gs_popup_draw(void) {
    const int box_w = 300, box_h = 74;
    const int box_top = DISPLAY_CENTER_Y - box_h / 2;
    char line[64];
    if (!gs_scanned || gs_total == 0)
        snprintf(line, sizeof(line), "Scanning SD:/%s/ ...", gs_folder);
    else
        snprintf(line, sizeof(line), "Importing favorites   %d / %d", gs_done, gs_total);

    ui_components_dialog_draw(box_w, box_h);
    rdpq_text_print(&(rdpq_textparms_t){ .width = box_w, .align = ALIGN_CENTER },
                    FNT_DEFAULT, DISPLAY_CENTER_X - box_w / 2, box_top + 26, line);

    int bw = box_w - 56;
    int x0 = DISPLAY_CENTER_X - bw / 2, x1 = DISPLAY_CENTER_X + bw / 2;
    int y0 = box_top + 44, y1 = y0 + 12;
    float prog = gs_total > 0 ? (float)gs_done / (float)gs_total : 0.0f;
    ui_components_border_draw(x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    ui_components_progressbar_draw_rainbow(x0, y0, x1, y1, prog);
}

/* ------------------------------------------------------------------ */
void view_games_grid_init(menu_t *menu) {
    /* Set before anything below reads it (e.g. the cache signature). Restores whichever
       gallery tab (Favorites or a library) was active before we left the grid. */
    grid_repoint_items(menu);

    /* Returning from the Z "Info" page reopens the popup we left from;
       any other entry (cold boot, tab switch) starts on the bare grid. */
    bool is_reopen       = reopen_inspect;
    show_inspect         = reopen_inspect;
    reopen_inspect       = false;
    show_confirm_remove  = false;
    show_confirm_sort    = false;
    show_confirm_clear   = false;
    show_confirm_autosort = false;
    fav_working          = false;
    fav_working_phase    = 0;
    desc_scroll          = 0;
    inspect_pulse        = 0;
    /* Reset More menu state on every grid entry */
    grid_more_active       = false;
    grid_more_from_inspect = false;
    show_metadata          = false;
    show_history           = false;
    hist_b_hold            = 0;
    gm_fav_dirty           = false;   /* grid re-inits fresh from bookkeeping here, so clear any pending defer */
    gm_fav_fired           = false;
    gm_gs_fired            = false;
    gm_pa_fired            = false;
    if (grid_more_target) { path_free(grid_more_target); grid_more_target = NULL; }
    gm_gs_update_labels(menu);

    move_mode            = false;
    b_held_frames        = 0;
    move_tick            = 0;
    move_r_held          = 0;
    grid_sq              = menu->settings.grid_square_tiles;
    grid_large           = menu->settings.grid_large_tiles;
    grid_caption_favs    = menu->settings.grid_show_captions_favorites;
    /* re-measure every strip height: the font may have been swapped in Settings */
    for (int i = 0; i <= TEXT_LINES_MAX; i++) caption_h_cached[i] = 0;
    gv_is_box            = is_box_view(menu->settings.image_view_grid);
    inspect_fav_i_cached = -1;
    if (inspect_logo) { sprite_free(inspect_logo); inspect_logo = NULL; }
    inspect_logo_fav     = -1;
    splash_active        = menu->settings.splash_enabled && !is_reopen && !splash_shown;

    /* Importing /Favorites used to run here on EVERY boot -- an O(files x list) scan that
       was the ~33 s grid-side boot cost. It's now opt-in from the empty greeting (press S/Z
       to scan, with a progress bar). Detection of an importable folder happens below,
       after refresh_grid, only when the grid is empty. */

    /* Only wipe the whole boxart cache when the favorites set or the grid image
       view actually changed (signature). Otherwise keep it — re-entering the grid
       from a sub-view (Game settings, file browser, etc.) must NOT re-decode every
       cover. The one game edited in Game settings is invalidated explicitly below. */
    static uint32_t last_grid_sig = 0;
    uint32_t sig = 2166136261u;  /* FNV-1a over fav paths + relevant settings */
    for (int k = 0; k < grid_items_cap; k++) {
        bookkeeping_item_t *f = &grid_items[k];
        sig ^= (uint32_t)f->bookkeeping_type; sig *= 16777619u;
        if (f->bookkeeping_type == BOOKKEEPING_TYPE_ROM && f->primary_path) {
            for (const char *s = path_get(f->primary_path); *s; s++) { sig ^= (uint8_t)*s; sig *= 16777619u; }
        }
    }
    sig ^= (uint32_t)menu->settings.image_view_grid;  sig *= 16777619u;
    sig ^= (uint32_t)menu->settings.grid_square_tiles; sig *= 16777619u;

    if (sig != last_grid_sig) {
        for (int i = 0; i < FAVORITES_COUNT; i++) {
            ui_components_boxart_free(boxart_cache[i]);
            boxart_cache[i] = NULL;
            fav_entry_cache[i].info_loaded = false;
        }
        memset(boxart_cache_attempted, 0, sizeof(boxart_cache_attempted));
        last_grid_sig = sig;
    } else if (menu->load.load_favorite_id >= 0 && menu->load.load_favorite_id < FAVORITES_COUNT) {
        /* Returning from Game settings: refresh just that game's art (presents-as
           etc. may have changed) without disturbing every other cover. */
        gm_invalidate_one_art(menu->load.load_favorite_id, false);
    }

    refresh_grid(menu);

    /* Greeting opt-in import: when the grid is empty, offer "S: Scan SD:/Favorites/"
       instead of "Z: File Browser" if an importable folder exists. This is a single dir
       probe (dir_findfirst == 0 means the folder exists and has at least one entry) --
       the actual enumeration only runs when the user presses S/Z, so boot stays instant. */
    gs_pending = gs_working = gs_scanned = false;
    gs_can_scan = false;
    if (grid_tab == 0 && fav_count == 0 && !is_reopen) {
        for (int di = 0; auto_fav_folder_names[di] && !gs_can_scan; di++) {
            path_t *d = path_init(menu->storage_prefix, "");
            path_push(d, (char *)auto_fav_folder_names[di]);
            dir_t info;
            if (dir_findfirst(path_get(d), &info) == 0) {
                gs_can_scan = true;
                strncpy(gs_folder, auto_fav_folder_names[di], sizeof(gs_folder) - 1);
                gs_folder[sizeof(gs_folder) - 1] = '\0';
            }
            path_free(d);
        }
    }

    /* "Always sort A-Z": keep the grid alphabetized. Only actually re-sort when something
       changed (a favorite added, or the saved order isn't sorted) -- otherwise the list was
       saved sorted last time, so we skip the sort (and its per-non-DB-game ROM-header reads,
       the few-seconds-every-boot cost) and just rebuild the A-Z letter cache from the saved
       initials. This is also what removes the boot black-gap: no blocking sort before the
       first frame on a normal launch. */
    if (grid_tab == 0 && menu->settings.always_sort_az && fav_count > 1) {
        if (fav_needs_resort(menu)) {
            gm_run_sort_az(menu);
            gm_reset_all_caches(menu);
        } else {
            for (int gi = 0; gi < fav_count; gi++)
                az_letter[fav_indices[gi]] = grid_items[fav_indices[gi]].sort_initial;
            az_letter_ready = true;
            az_letter_count = fav_count;
        }
    }

    /* Returning from Game settings — reopen this game's More menu instead of the
       bare grid. */
    if (gm_reopen_pending) {
        gm_reopen_pending = false;
        if (grid_more_target) path_free(grid_more_target);
        grid_more_target = gm_reopen_path; gm_reopen_path = NULL;
        if (grid_more_target) {
            grid_more_is_fav = (gm_target_fav_index(menu) >= 0);
            /* Point the grid cursor at this game so it sits behind the menu. */
            int gti = gm_target_grid_index(menu);
            for (int gi = 0; gi < fav_count; gi++) {
                if (fav_indices[gi] == gti) { sel_fav = gi; break; }
            }
            ensure_visible();
            strncpy(gm_fav_lbl, grid_more_is_fav ? "Unfavorite" : "Favorite", sizeof(gm_fav_lbl) - 1);
            gm_fav_lbl[sizeof(gm_fav_lbl) - 1] = '\0';
            grid_more_from_inspect = false;
            grid_more_active = true;
            gm_update_dynamic_labels(menu);
            ui_components_context_menu_init(&gm_more_cm);
            ui_components_context_menu_show(&gm_more_cm);
            /* Restore the row the user launched the sub-view from, and re-enter the
               submenu (e.g. Hardware) if it was open. */
            if (gm_reopen_row > 0 && gm_reopen_row < gm_more_cm.row_count) {
                gm_more_cm.row_selected = gm_reopen_row;
            }
            if (gm_reopen_submenu) {
                gm_more_cm.submenu = gm_reopen_submenu;
                gm_reopen_submenu->parent = &gm_more_cm;
                gm_reopen_submenu->row_selected = gm_reopen_subrow;
                gm_reopen_submenu = NULL;
            }
        }
    }

    sel_enter_t  = 1.0f;
    prev_sel_fav = sel_fav;

    /* Never start inside the screensaver; reset the idle clock on every grid entry. */
    ss_active       = false;
    ss_idle_last_ms = 0;   /* re-initialized to "now" on the next process() frame */
    ss_lr_start_ms  = 0;
    ss_saved_sel    = -1;
}

void view_games_grid_display(menu_t *menu, surface_t *display) {
    process(menu);
    /* Render runs at 60fps for smooth motion, but the integer hue counters can't be halved
       cleanly, so advance them only every OTHER frame -> same cycle speed as the old 30fps. */
    static bool anim_tick = false;
    anim_tick = !anim_tick;
    if (anim_tick) {
        grid_hue += 1;
        if (show_inspect) {
            inspect_hue   += 1;
            inspect_pulse += 3;
        }
    }
    if (move_mode) {
        move_tick += 1;
    }

    /* Selection change → kick off the grow transition */
    if (sel_fav != prev_sel_fav) {
        sel_enter_t  = 0.0f;
        prev_sel_fav = sel_fav;
    }
    sel_enter_t += 0.19f;   /* halved from 0.38 so the grow takes the same time at 60fps */
    if (sel_enter_t > 1.0f) sel_enter_t = 1.0f;

    draw(menu, display);
    maybe_background_load(menu);
}
