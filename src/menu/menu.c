/**
 * @file menu.c
 * @brief Menu system implementation
 * @ingroup menu
 */

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <libdragon.h>
#include <fatfs/ff.h>   /* f_rename -- one-time legacy-data migration */

#include "actions.h"
#include "boot/boot.h"
#include "flashcart/flashcart.h"
#include "fonts.h"
#include "hdmi.h"
#include "library.h"
#include "menu_state.h"
#include "menu.h"
#include "mp3_player.h"
#include "png_decoder.h"
#include "settings.h"
#include "sound.h"
#include "usb_comm.h"
#include "utils/fs.h"
#include "views/views.h"

#define MENU_DIRECTORY              "/menu"
#define MENU_N64EVER_DIRECTORY      "n64ever"   /**< User-data subfolder under /menu */
#define MENU_SETTINGS_FILE          "config.ini"
#define MENU_CUSTOM_FONT_FILE       "custom.font64"
#define MENU_FAVORITES_FILE         "favorites.ini"
#define MENU_HISTORY_FILE           "history.ini"
#define MENU_LIBRARIES_FILE         "libraries.ini"

#define MENU_CACHE_DIRECTORY        "cache"
#define BACKGROUND_CACHE_FILE       "background.data"

#define FPS_LIMIT                   (60.0f)

static menu_t *menu;

static bool interlaced = true;

/* Data/migration schema version. BUMP this whenever a release should force a one-time
   favorites/history wipe on the next boot (so migrating users start on a clean grid + the
   greeting, and a stale carried-over list can't reference changed art codes or drag out boot).
   It is DELIBERATELY independent of MENU_VERSION -- the user-facing build number can stay the
   same across rebuilds while this still drives the migrate reset, gated by a per-version marker
   file (menu/n64ever/.migrated.vN) so each bump fires the reset exactly once per user. */
#define MENU_DATA_VERSION 1

/**
 * @brief One-time migration of pre-n64ever data into the new /menu layout.
 *
 * Run once at boot (before /menu/n64ever is created). f_rename is a same-volume
 * metadata move of the whole subtree; each move is guarded on the destination NOT
 * already existing, so it never clobbers current data and is a harmless no-op once
 * migrated or on a fresh install.
 *   <sd>/menu/custom -> <sd>/menu/n64ever    (gameconfigs, custom splash/bg, font)
 *   <sd>/cpak_saves  -> <sd>/menu/cpak_saves (Controller Pak backups + notes)
 * Game saves are NOT here -- they live in a "saves" folder next to each ROM.
 * Favorites/history migrate separately from the legacy /menu/history.ini (bookkeeping).
 */
static void migrate_legacy_layout (const char *prefix) {
    char old_p[256], new_p[256];

    snprintf(old_p, sizeof(old_p), "%smenu/custom",  prefix);
    snprintf(new_p, sizeof(new_p), "%smenu/n64ever", prefix);
    if (directory_exists(old_p) && !directory_exists(new_p)) {
        FRESULT r = f_rename(strip_fs_prefix(old_p), strip_fs_prefix(new_p));
        debugf("[MIGRATE] menu/custom -> menu/n64ever: %d\n", r);
    }

    snprintf(old_p, sizeof(old_p), "%scpak_saves",      prefix);
    snprintf(new_p, sizeof(new_p), "%smenu/cpak_saves", prefix);
    if (directory_exists(old_p) && !directory_exists(new_p)) {
        FRESULT r = f_rename(strip_fs_prefix(old_p), strip_fs_prefix(new_p));
        debugf("[MIGRATE] cpak_saves -> menu/cpak_saves: %d\n", r);
    }
}

/**
 * @brief Initialize the menu system.
 *
 * @param boot_params Pointer to the boot parameters structure.
 */
static void menu_init (boot_params_t *boot_params) {
    menu = calloc(1, sizeof(menu_t));
    assert(menu != NULL);

    /* Boot timing: get_ticks_ms() counts from CPU-reset-release, so these phase
       deltas cover menu_init only -- they deliberately EXCLUDE the SC64's pre-CPU
       SD->SDRAM load of the ROM image. Isolate that load with: (wall-clock from
       power-on to grid) - (menu_init total below) - (first-cover settle time).
       Visible over USB (sc64deployer/UNFLoader), a no-op without a debug host. */
    uint64_t bt_start = get_ticks_ms();
    uint64_t bt_flash = bt_start, bt_pre = bt_start, bt_disp = bt_start,
             bt_font = bt_start, bt_splash = bt_start, bt_book = bt_start;

    menu->boot_params = boot_params;

    menu->mode = MENU_MODE_NONE;
    menu->next_mode = MENU_MODE_STARTUP;

    menu->flashcart_err = flashcart_init(&menu->storage_prefix);
    if (menu->flashcart_err != FLASHCART_OK) {
        menu->next_mode = MENU_MODE_FAULT;
    }
    bt_flash = get_ticks_ms();

    joypad_init();
    timer_init();
    rtc_init();
    rspq_init();
    rdpq_init();
    dfs_init(DFS_DEFAULT_LOCATION);

    /* Boxart sprites are baked at compression level 3, whose decompressor is NOT
       auto-initialised (levels 1-2 are). Register it now, before any sprite loads,
       or the first cover load asserts "compression level 3 not initialized". */
    asset_init_compression(3);

    actions_init();
    sound_init_default();
    sound_init_sfx();

    hdmi_clear_game_id();

    path_t *path = path_init(menu->storage_prefix, MENU_DIRECTORY);

    directory_create(path_get(path));

    /* Migrate pre-n64ever data + old Controller Pak backups into /menu/... now, before
       /menu/n64ever is created below (so a fresh upgrade renames straight across). */
    migrate_legacy_layout(menu->storage_prefix);

    path_push(path, MENU_SETTINGS_FILE);
    settings_init(path_get(path));
    settings_load(&menu->settings);
    path_pop(path);
    bt_pre = get_ticks_ms();

    /* Favorites + history are split into separate files under /menu/n64ever.
       The pre-split combined file (/menu/history.ini) is used to migrate. */
    path_push(path, MENU_HISTORY_FILE);                 /* <prefix>/menu/history.ini (legacy) */
    char *legacy_bk_path = strdup(path_get(path));
    path_pop(path);

    path_push(path, MENU_N64EVER_DIRECTORY);            /* <prefix>/menu/n64ever */
    directory_create(path_get(path));
    path_push(path, MENU_FAVORITES_FILE);
    char *favorites_bk_path = strdup(path_get(path));
    path_pop(path);
    path_push(path, MENU_HISTORY_FILE);
    char *history_bk_path = strdup(path_get(path));
    path_pop(path);                                     /* back to <prefix>/menu/n64ever */
    path_pop(path);                                     /* back to <prefix>/menu */

    /* bookkeeping_init/load is deferred until after display + background init
       (below) so the boot splash is on screen during the favorites/history load
       instead of a black screen. The *_bk_path strings stay alive until then. */

    // Force interlacing off in VI settings for TVs and other devices that struggle with interlaced video input.
    interlaced = !menu->settings.force_progressive_scan;

    resolution_t resolution = {
        .width = 640,
        .height = 480,
        .interlaced = interlaced ? INTERLACE_HALF : INTERLACE_OFF,
    };

    /* NOTE: a 32-bit framebuffer at 640x480 BLACK-SCREENS on real N64 hardware (the VI can't
       sustain 640x480x32bpp), so stay 16-bit. The cart-body banding that 32bpp would have fixed
       has to be solved a 16bpp-compatible way (dither tuning), not by deepening the framebuffer
       at this resolution. */
    display_init(resolution, DEPTH_16_BPP, 2, GAMMA_NONE, interlaced ? FILTERS_DISABLED : FILTERS_RESAMPLE);
    
    if (menu->settings.pal60_enabled) { // it is not given that hardware VI mods understand the output
        tv_type_t tv_type = get_tv_type();
        if (tv_type == TV_PAL) {
            // Set VI timing so it will use 60Hz signal.
            vi_set_timing_preset(&VI_TIMING_PAL60);

            // FIXME: timeout and restore to PAL 50Hz if not shown, 
            // this should be added as a button confirm, or reset combo, rather than re-setting via manual edit of the INI?.
            //vi_set_timing_preset(&VI_TIMING_PAL);
        }
    }
    
    display_set_fps_limit(FPS_LIMIT);
    bt_disp = get_ticks_ms();

    path_push(path, MENU_CUSTOM_FONT_FILE);
    fonts_init(path_get(path), menu->settings.use_legacy_font);
    path_pop(path);
    bt_font = get_ticks_ms();

    path_push(path, MENU_CACHE_DIRECTORY);
    directory_create(path_get(path));

    path_push(path, BACKGROUND_CACHE_FILE);
    ui_components_background_init(path_get(path), menu->settings.custom_splash_enabled);

    /* Display + splash image are ready: paint the boot splash now, then run the
       (potentially slow) favorites/history load behind it -- previously this load
       happened on a black screen before the display was even initialised. */
    surface_t *splash_surface = display_get();
    rdpq_attach(splash_surface, NULL);
    if (menu->settings.splash_enabled) {
        ui_components_background_draw_splash(menu->settings.custom_splash_enabled);
    } else {
        ui_components_background_draw();   /* Boot Splash off -> plain background, no brief flash */
    }
    rdpq_detach_show();
    bt_splash = get_ticks_ms();

    /* Migrate reset: wipe favorites + history once so migrating users land on the empty-grid
       greeting (which carries the how-to info), and a stale carried-over list can't drag out
       boot or point at changed art codes. Gated by a DATA-VERSION marker file (.migrated.vN),
       NOT the build number, so it fires exactly once per MENU_DATA_VERSION bump -- a user who
       migrates from stock/an older build (or whose marker predates this version) gets the reset
       on first boot, then never again. When it fires we also skip the legacy /menu/history.ini
       migration so the lists stay empty. */
    bool nuke_bk = false;
    {
        char marker[256];
        snprintf(marker, sizeof(marker), "%smenu/n64ever/.migrated.v%d",
                 menu->storage_prefix, MENU_DATA_VERSION);
        /* CONFIRMED absent only. file_exists() reports false for an unreadable card too, and
           acting on that unlinked the user's favorites.ini on a momentary SD read error -- the
           marker is invisible for one boot and this reset fires as if they'd never migrated.
           If we can't tell, do nothing: a migrating user keeping a stale list is recoverable,
           a wiped favorites.ini is not. */
        switch (file_presence(marker)) {
            case FILE_PRESENCE_ABSENT: {
                f_unlink(strip_fs_prefix(favorites_bk_path));
                f_unlink(strip_fs_prefix(history_bk_path));
                FILE *mk = fopen(marker, "wb");
                if (mk) fclose(mk);
                nuke_bk = true;
                debugf("[MIGRATE] favorites/history reset for data v%d -> greeting\n", MENU_DATA_VERSION);
                break;
            }
            case FILE_PRESENCE_UNKNOWN:
                /* Don't write the marker either -- that would suppress a genuine reset later. */
                debugf("[MIGRATE] marker unreadable; skipping reset to protect favorites/history\n");
                break;
            case FILE_PRESENCE_PRESENT:
                break;
        }
    }
    menu->fresh_reset = nuke_bk;

    bookkeeping_init(favorites_bk_path, history_bk_path, nuke_bk ? NULL : legacy_bk_path);
    bookkeeping_load(&menu->bookkeeping);
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    free(legacy_bk_path);
    free(favorites_bk_path);
    free(history_bk_path);

    /* Independent path_t, NOT the shared `path` above -- by this point `path` already has two
       unpaired pushes on it (MENU_CACHE_DIRECTORY / BACKGROUND_CACHE_FILE, above) that are never
       popped because nothing else used to read `path` again before it's freed. Pushing onto that
       already-fouled path silently built the wrong libraries.ini location. */
    {
        path_t *lib_path = path_init(menu->storage_prefix, MENU_DIRECTORY);
        path_push(lib_path, MENU_N64EVER_DIRECTORY);
        path_push(lib_path, MENU_LIBRARIES_FILE);
        library_init(path_get(lib_path));
        library_load();
        path_free(lib_path);
    }

    bt_book = get_ticks_ms();

    debugf("[BOOT] menu_init phases (ms): flashcart=%llu coreinit=%llu display=%llu "
           "fonts=%llu bg+splash=%llu bookkeeping=%llu | total=%llu\n",
           bt_flash - bt_start, bt_pre - bt_flash, bt_disp - bt_pre,
           bt_font - bt_disp, bt_splash - bt_font, bt_book - bt_splash,
           bt_book - bt_start);

    /* Mirror the phase deltas into the menu struct so the on-screen boot readout (in
       the games grid) can show WHERE the time goes without a USB debug host. */
    menu->boot_ms[0] = (uint32_t)(bt_flash  - bt_start);   /* flashcart   */
    menu->boot_ms[1] = (uint32_t)(bt_pre    - bt_flash);   /* core init   */
    menu->boot_ms[2] = (uint32_t)(bt_disp   - bt_pre);     /* display     */
    menu->boot_ms[3] = (uint32_t)(bt_font   - bt_disp);    /* fonts       */
    menu->boot_ms[4] = (uint32_t)(bt_splash - bt_font);    /* bg + splash */
    menu->boot_ms[5] = (uint32_t)(bt_book   - bt_splash);  /* bookkeeping */
    menu->boot_ms[6] = (uint32_t)(bt_book   - bt_start);   /* menu_init total */

    path_free(path);

    sound_use_sfx(menu->settings.soundfx_enabled);
    sound_init_grid_sfx(menu->storage_prefix);
    sound_set_grid_sfx_enabled(menu->settings.bgm_enabled);

    /* path_init's path_push prepends its own '/', so strip any leading slash from
       default_directory — "sd:///" has root "///" != "/" making path_is_root fail. */
    const char *dd = menu->settings.default_directory;
    while (dd[0] == '/') dd++;
    menu->browser.directory = path_init(menu->storage_prefix, (char *)dd);
    if (!directory_exists(path_get(menu->browser.directory))) {
        path_free(menu->browser.directory);
        menu->browser.directory = path_init(menu->storage_prefix, "");
    }

    debugf("N64FlashcartMenu debugging...\n");
}

/**
 * @brief Deinitialize the menu system.
 * 
 * @param menu Pointer to the menu structure.
 */
static void menu_deinit (menu_t *menu) {
    library_deinit();

    ui_components_background_free();
    rspq_wait();  // Execute deferred callbacks (e.g., display list freeing) before closing RSPQ

    hdmi_send_game_id(menu->boot_params);

    path_free(menu->load.disk_slots.primary.disk_path);
    path_free(menu->load.rom_path);
    path_free(menu->load.library_disk_path);
    path_free(menu->load.library_rom_path);
    for (int i = 0; i < menu->browser.entries; i++) {
        free(menu->browser.list[i].name);
    }
    free(menu->browser.list);
    path_free(menu->browser.directory);
    free(menu);

    display_close();

    sound_deinit();
    
    rspq_wait();  // Execute deferred callbacks before closing RSPQ
    rspq_close();
    rdpq_close();
    rtc_close();
    timer_close();
    joypad_close();

    flashcart_deinit();
}

/**
 * @brief View structure containing initialization and display functions.
 */
typedef const struct {
    menu_mode_t id; /**< View ID */
    void (*init) (menu_t *menu); /**< Initialization function */
    void (*show) (menu_t *menu, surface_t *display); /**< Display function */
} view_t;

static view_t menu_views[] = {
    { MENU_MODE_STARTUP, view_startup_init, view_startup_display },
    { MENU_MODE_BROWSER, view_browser_init, view_browser_display },
    { MENU_MODE_FILE_INFO, view_file_info_init, view_file_info_display },
    { MENU_MODE_SYSTEM_INFO, view_system_info_init, view_system_info_display },
    { MENU_MODE_IMAGE_VIEWER, view_image_viewer_init, view_image_viewer_display },
    { MENU_MODE_TEXT_VIEWER, view_text_viewer_init, view_text_viewer_display },
    { MENU_MODE_MUSIC_PLAYER, view_music_player_init, view_music_player_display },
    { MENU_MODE_CREDITS, view_credits_init, view_credits_display },
    { MENU_MODE_SETTINGS_EDITOR, view_settings_init, view_settings_display },
    { MENU_MODE_RTC, view_rtc_init, view_rtc_display },
    { MENU_MODE_CONTROLLER_PAKFS, view_controller_pakfs_init, view_controller_pakfs_display },
    { MENU_MODE_CONTROLLER_PAK_DUMP_INFO, view_controller_pak_dump_info_init, view_controller_pak_dump_info_display },
    { MENU_MODE_CONTROLLER_PAK_DUMP_NOTE_INFO, view_controller_pak_note_dump_info_init, view_controller_pak_note_dump_info_display },
    { MENU_MODE_FLASHCART, view_flashcart_info_init, view_flashcart_info_display },
    { MENU_MODE_LOAD_ROM, view_load_rom_init, view_load_rom_display },
    { MENU_MODE_LOAD_DISK, view_load_disk_init, view_load_disk_display },
    { MENU_MODE_LOAD_EMULATOR, view_load_emulator_init, view_load_emulator_display },
    { MENU_MODE_ERROR, view_error_init, view_error_display },
    { MENU_MODE_FAULT, view_fault_init, view_fault_display },
    { MENU_MODE_HISTORY, view_history_init, view_history_display },
    { MENU_MODE_DATEL_CODE_EDITOR, view_datel_code_editor_init, view_datel_code_editor_display },
    { MENU_MODE_EXTRACT_FILE, view_extract_file_init, view_extract_file_display },
    { MENU_MODE_GAMES_GRID, view_games_grid_init, view_games_grid_display },
    { MENU_MODE_LINK_DISC, view_link_disc_init, view_link_disc_display },
    { MENU_MODE_ROM_BOOT, view_rom_boot_init, view_rom_boot_display }
};

/**
 * @brief Get the view structure for the specified menu mode.
 * 
 * @param id The menu mode ID.
 * @return view_t* Pointer to the view structure.
 */
static view_t *menu_get_view (menu_mode_t id) {
    for (int i = 0; i < sizeof(menu_views) / sizeof(view_t); i++) {
        if (menu_views[i].id == id) {
            return &menu_views[i];
        }
    }
    return NULL;
}

/**
 * @brief Run the menu system.
 * 
 * @param boot_params Pointer to the boot parameters structure.
 */
void menu_run (boot_params_t *boot_params) {
    menu_init(boot_params);

    while (true) {
        surface_t *display = display_try_get();

        if (display != NULL) {
            actions_update(menu);

            view_t *view = menu_get_view(menu->mode);
            if (view && view->show) {
                view->show(menu, display);
            } else {
                rdpq_attach_clear(display, NULL);
                rdpq_detach_wait();
                display_show(display);
            }

            if (menu->mode == MENU_MODE_BOOT) {
                break;
            }

            while (menu->mode != menu->next_mode) {
                menu->mode = menu->next_mode;

                view_t *next_view = menu_get_view(menu->next_mode);
                if (next_view && next_view->init) {
                    next_view->init(menu);
                }
            }

            time(&menu->current_time);
        }

        sound_poll();

        png_decoder_poll();

        usb_comm_poll(menu);
    }

    menu_deinit(menu);

    while (exception_reset_time() > 0) {
        // Do nothing if reset button was pressed
    }
}
