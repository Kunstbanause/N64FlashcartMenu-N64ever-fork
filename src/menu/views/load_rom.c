#include "../bookkeeping.h"
#include "../cart_load.h"
#include "../datel_codes.h"
#include "../rom_info.h"
#include "../sound.h"
#include "../ui_components/constants.h"
#include "boot/boot.h"
#include "utils/fs.h"
#include "views.h"
#include <string.h>

static void draw_load_content(float progress); /* forward decl */

static bool show_extra_info_message = false;
static bool show_advanced_info_message = false;
static char *rom_filename = NULL;
static component_boxart_t *load_boxart = NULL;
static menu_t *load_menu_ptr = NULL; /* used by draw_progress to access from_grid flag */

static char *convert_error_message (rom_err_t err) {
    switch (err) {
        case ROM_ERR_LOAD_IO: return "I/O error during loading ROM information and/or options";
        case ROM_ERR_SAVE_IO: return "I/O error during storing ROM options";
        case ROM_ERR_NO_FILE: return "Couldn't open ROM file";
        default: return "Unknown ROM info load error";
    }
}

static const char *format_rom_endianness (rom_endianness_t endianness) {
    switch (endianness) {
        case ENDIANNESS_BIG: return "Big (default)";
        case ENDIANNESS_LITTLE: return "Little (unsupported)";
        case ENDIANNESS_BYTE_SWAP: return "Byte swapped";
        default: return "Unknown";
    }
}

static const char *format_rom_media_type (rom_category_type_t media_type) {
    switch (media_type) {
        case N64_CART: return "Cartridge";
        case N64_DISK: return "Disk";
        case N64_CART_EXPANDABLE: return "Cartridge (Expandable)";
        case N64_DISK_EXPANDABLE: return "Disk (Expandable)";
        case N64_ALECK64: return "Aleck64";
        default: return "Unknown";
    }
}

static const char *format_rom_destination_market (rom_destination_type_t market_type) {
    // TODO: These are all assumptions and should be corrected if required.
    // From http://n64devkit.square7.ch/info/submission/pal/01-01.html
    switch (market_type) {
        case MARKET_JAPANESE_MULTI: return "Japanese & English"; // 1080 Snowboarding JPN
        case MARKET_BRAZILIAN: return "Brazilian (Portuguese)";
        case MARKET_CHINESE: return "Chinese";
        case MARKET_GERMAN: return "German";
        case MARKET_NORTH_AMERICA: return "American English";
        case MARKET_FRENCH: return "French";
        case MARKET_DUTCH: return "Dutch";
        case MARKET_ITALIAN: return "Italian";
        case MARKET_JAPANESE: return "Japanese";
        case MARKET_KOREAN: return "Korean";
        case MARKET_CANADIAN: return "Canadaian (English & French)";
        case MARKET_SPANISH: return "Spanish";
        case MARKET_AUSTRALIAN: return "Australian (English)";
        case MARKET_SCANDINAVIAN: return "Scandinavian";
        case MARKET_GATEWAY64_NTSC: return "LodgeNet/Gateway (NTSC)";
        case MARKET_GATEWAY64_PAL: return "LodgeNet/Gateway (PAL)";
        case MARKET_EUROPEAN_BASIC: return "PAL (includes English)"; // Mostly EU but is used on some Australian ROMs
        case MARKET_OTHER_X: return "Regional (non specific)"; // FIXME: AUS HSV Racing ROM's and Asia Top Gear Rally use this so not only EUR
        case MARKET_OTHER_Y: return "European (non specific)";
        case MARKET_OTHER_Z: return "Regional (unknown)";
        default: return "Unknown";
    }
}

static const char *format_rom_save_type (rom_save_type_t save_type, bool supports_cpak) {
    switch (save_type) {
        case SAVE_TYPE_NONE: return supports_cpak ? "Controller PAK" : "None";
        case SAVE_TYPE_EEPROM_4KBIT: return supports_cpak ?   "EEPROM 4kbit | Controller PAK" : "EEPROM 4kbit";
        case SAVE_TYPE_EEPROM_16KBIT: return supports_cpak ?  "EEPROM 16kbit | Controller PAK" : "EEPROM 16kbit";
        case SAVE_TYPE_SRAM_256KBIT: return supports_cpak ?   "SRAM 256kbit | Controller PAK" : "SRAM 256kbit";
        case SAVE_TYPE_SRAM_BANKED: return supports_cpak ?    "SRAM 768kbit / 3 banks | Controller PAK" : "SRAM 768kbit / 3 banks";
        case SAVE_TYPE_SRAM_1MBIT: return supports_cpak ?     "SRAM 1Mbit | Controller PAK" : "SRAM 1Mbit";
        case SAVE_TYPE_FLASHRAM_1MBIT: return supports_cpak ? "FlashRAM 1Mbit | Controller PAK" : "FlashRAM 1Mbit";
        case SAVE_TYPE_FLASHRAM_PKST2: return supports_cpak ? "FlashRAM (Pokemon Stadium 2) | Controller PAK" : "FlashRAM (Pokemon Stadium 2)";
        default: return "Unknown";
    }
}

static const char *format_rom_tv_type (rom_tv_type_t tv_type) {
    switch (tv_type) {
        case ROM_TV_TYPE_PAL: return "PAL";
        case ROM_TV_TYPE_NTSC: return "NTSC";
        case ROM_TV_TYPE_MPAL: return "MPAL";
        default: return "Unknown";
    }
}

static const char *format_rom_expansion_pak_info (rom_expansion_pak_t expansion_pak_info) {
    switch (expansion_pak_info) {
        case EXPANSION_PAK_REQUIRED: return "Required";
        case EXPANSION_PAK_RECOMMENDED: return "Recommended";
        case EXPANSION_PAK_SUGGESTED: return "Suggested";
        case EXPANSION_PAK_FAULTY: return "May require ROM patch";
        default: return "Not required";
    }
}

static const char *format_rom_pak_feature_info (bool pak_feature_info) {
    if (pak_feature_info) {
        return "Supported";
    } else {
        return "Not used";
    }
}

static const char *format_cic_type (rom_cic_type_t cic_type) {
    switch (cic_type) {
        case ROM_CIC_TYPE_5101: return "5101";
        case ROM_CIC_TYPE_5167: return "5167";
        case ROM_CIC_TYPE_6101: return "6101";
        case ROM_CIC_TYPE_7102: return "7102";
        case ROM_CIC_TYPE_x102: return "6102 / 7101";
        case ROM_CIC_TYPE_x103: return "6103 / 7103";
        case ROM_CIC_TYPE_x105: return "6105 / 7105";
        case ROM_CIC_TYPE_x106: return "6106 / 7106";
        case ROM_CIC_TYPE_8301: return "8301";
        case ROM_CIC_TYPE_8302: return "8302";
        case ROM_CIC_TYPE_8303: return "8303";
        case ROM_CIC_TYPE_8401: return "8401";
        case ROM_CIC_TYPE_8501: return "8501";
        default: return "Unknown";
    }
}

static const char *format_age_rating (uint32_t age_rating) {
    if (age_rating >= 18) {
        return "Adults Only";
    }
    else if (age_rating >= 17) {
        return "Mature";
    }
    else if (age_rating >= 13) {
        return "Teen";
    }
    else if (age_rating >= 10) {
        return "Everyone 10+";
    }
    else if (age_rating > 0) {
        return "Everyone";
    }
    else if (age_rating == 0) {
        return "None";
    }
    else {
        return "Unknown";
    }
}

static inline const char *format_boolean_type (bool bool_value) {
    return bool_value ? "On" : "Off";
}

static void set_cic_type (menu_t *menu, void *arg) {
    rom_cic_type_t cic_type = (rom_cic_type_t) (arg);
    rom_err_t err = rom_config_override_cic_type(menu->load.rom_path, &menu->load.rom_info, cic_type);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
    }
    menu->browser.reload = true;
}

static void set_save_type (menu_t *menu, void *arg) {
    rom_save_type_t save_type = (rom_save_type_t) (arg);
    rom_err_t err = rom_config_override_save_type(menu->load.rom_path, &menu->load.rom_info, save_type);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
    }
    menu->browser.reload = true;
}

static void set_tv_type (menu_t *menu, void *arg) {
    rom_tv_type_t tv_type = (rom_tv_type_t) (arg);
    rom_err_t err = rom_config_override_tv_type(menu->load.rom_path, &menu->load.rom_info, tv_type);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
    }
    menu->browser.reload = true;
}
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static void set_autoload_type (menu_t *menu, void *arg) {
    free(menu->settings.rom_autoload_path);
    menu->settings.rom_autoload_path = strdup(strip_fs_prefix(path_get(menu->browser.directory)));
    free(menu->settings.rom_autoload_filename);
    menu->settings.rom_autoload_filename = strdup(menu->browser.entry->name);
    // FIXME: add a confirmation box here! (press start on reboot)
    menu->settings.rom_autoload_enabled = true;
    settings_save(&menu->settings);
    menu->browser.reload = true;
}
#endif

static void set_cheat_option(menu_t *menu, void *arg) {
    debugf("Load Rom: setting cheat option to %d\n", (int)arg);
    if (!is_memory_expanded()) {
        // If the Expansion pak is not installed, we cannot use cheats, and force it to off (just incase).
        rom_config_setting_set_cheats(menu->load.rom_path, &menu->load.rom_info, false);
        menu->browser.reload = true;
    }
    else {
        bool enabled = (bool)arg;
        rom_config_setting_set_cheats(menu->load.rom_path, &menu->load.rom_info, enabled);
        menu->browser.reload = true;
    }
}

#ifdef FEATURE_PATCHER_GUI_ENABLED
static void set_patcher_option(menu_t *menu, void *arg) {
    bool enabled = (bool)arg;
    rom_config_setting_set_patches(menu->load.rom_path, &menu->load.rom_info, enabled);
    menu->browser.reload = true;
}
#endif

static component_context_menu_t set_cic_type_context_menu = { .list = {
    {.text = "Automatic", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_AUTOMATIC) },
    {.text = "CIC-6101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_6101) },
    {.text = "CIC-7102", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_7102) },
    {.text = "CIC-6102 / CIC-7101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x102) },
    {.text = "CIC-6103 / CIC-7103", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x103) },
    {.text = "CIC-6105 / CIC-7105", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x105) },
    {.text = "CIC-6106 / CIC-7106", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x106) },
    {.text = "Aleck64 CIC-5101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_5101) },
    {.text = "64DD ROM conversion CIC-5167", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_5167) },
    {.text = "NDDJ0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8301) },
    {.text = "NDDJ1 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8302) },
    {.text = "NDDJ2 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8303) },
    {.text = "NDXJ0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8401) },
    {.text = "NDDE0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8501) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_save_type_context_menu = { .list = {
    { .text = "Automatic", .action = set_save_type, .arg = (void *) (SAVE_TYPE_AUTOMATIC) },
    { .text = "None", .action = set_save_type, .arg = (void *) (SAVE_TYPE_NONE) },
    { .text = "EEPROM 4kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_EEPROM_4KBIT) },
    { .text = "EEPROM 16kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_EEPROM_16KBIT) },
    { .text = "SRAM 256kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_256KBIT) },
    { .text = "SRAM 768kbit / 3 banks", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_BANKED) },
    { .text = "SRAM 1Mbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_1MBIT) },
    { .text = "FlashRAM 1Mbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_FLASHRAM_1MBIT) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_tv_type_context_menu = { .list = {
    { .text = "Automatic", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_AUTOMATIC) },
    { .text = "PAL", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_PAL) },
    { .text = "NTSC", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_NTSC) },
    { .text = "MPAL", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_MPAL) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_cheat_options_menu = { .list = {
    { .text = "Enable", .action = set_cheat_option, .arg = (void *) (true)},
    { .text = "Disable", .action = set_cheat_option, .arg = (void *) (false)},
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

#ifdef FEATURE_PATCHER_GUI_ENABLED
static component_context_menu_t set_patcher_options_menu = { .list = {
    { .text = "Enable", .action = set_patcher_option, .arg = (void *) (true)},
    { .text = "Disable", .action = set_patcher_option, .arg = (void *) (false)},
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

static void set_menu_next_mode (menu_t *menu, void *arg) {
    menu_mode_t next_mode = (menu_mode_t) (arg);
    menu->next_mode = next_mode;
}

/* "Advanced options" — the legacy ROM option popup, now a submenu of Game settings. */
static component_context_menu_t options_context_menu = { .list = {
    { .text = "Set CIC Type", .submenu = &set_cic_type_context_menu },
    { .text = "Set Save Type", .submenu = &set_save_type_context_menu },
    { .text = "Set TV Type", .submenu = &set_tv_type_context_menu },
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    { .text = "Set ROM to autoload", .action = set_autoload_type },
#endif
    { .text = "Use Cheats", .submenu = &set_cheat_options_menu },
    { .text = "Datel Code Editor", .action = set_menu_next_mode, .arg = (void *) (MENU_MODE_DATEL_CODE_EDITOR) },
#ifdef FEATURE_PATCHER_GUI_ENABLED
    { .text = "Use Patches", .submenu = &set_patcher_options_menu },
#endif
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* ---- Game settings menu (replaces the legacy detailed ROM view) ---- */
static void gs_show_advanced_info (menu_t *menu, void *arg);
static void gs_show_extra_info (menu_t *menu, void *arg);

/* Row labels carry the current value; the buffers are updated in place, so the
   menu's .text pointers (set to these buffers) never need reassigning. */
static char gs_save_label[64]     = "Save Type";
static char gs_tv_label[48]       = "TV region";
static char gs_exp_label[48]      = "Expansion PAK";
static char gs_rumble_label[40]   = "Rumble PAK";
static char gs_transfer_label[40] = "Transfer PAK";
static char gs_cheats_label[40]   = "Datel Cheats";

static component_context_menu_t game_settings_context_menu = { .list = {
    { .text = gs_save_label,      .submenu = &set_save_type_context_menu },
    { .text = gs_tv_label,        .submenu = &set_tv_type_context_menu },
    { .text = gs_exp_label },                                  /* read-only info */
    { .text = gs_rumble_label },                               /* read-only info */
    { .text = gs_transfer_label },                             /* read-only info */
    { .text = gs_cheats_label,    .submenu = &set_cheat_options_menu },
    { .text = "" },                                            /* separator */
    { .text = "Advanced options", .submenu = &options_context_menu },
    { .text = "Advanced Info",    .action = gs_show_advanced_info },
    { .text = "Extra ROM Info",   .action = gs_show_extra_info },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* Remembered top-level row so re-showing the menu (after a leaf action or an
   info box) lands back where the user was. */
static int gs_saved_row = 0;

static void gs_show_advanced_info (menu_t *menu, void *arg) {
    gs_saved_row = game_settings_context_menu.row_selected;
    show_advanced_info_message = true;
}

static void gs_show_extra_info (menu_t *menu, void *arg) {
    gs_saved_row = game_settings_context_menu.row_selected;
    show_extra_info_message = true;
}

static void refresh_game_settings_labels (menu_t *menu) {
    snprintf(gs_save_label, sizeof(gs_save_label), "Save Type: %s",
        format_rom_save_type(rom_info_get_save_type(&menu->load.rom_info), menu->load.rom_info.features.controller_pak));
    snprintf(gs_tv_label, sizeof(gs_tv_label), "TV region: %s",
        format_rom_tv_type(rom_info_get_tv_type(&menu->load.rom_info)));
    snprintf(gs_exp_label, sizeof(gs_exp_label), "Expansion PAK: %s",
        format_rom_expansion_pak_info(menu->load.rom_info.features.expansion_pak));
    snprintf(gs_rumble_label, sizeof(gs_rumble_label), "Rumble PAK: %s",
        format_rom_pak_feature_info(menu->load.rom_info.features.rumble_pak));
    snprintf(gs_transfer_label, sizeof(gs_transfer_label), "Transfer PAK: %s",
        format_rom_pak_feature_info(menu->load.rom_info.features.transfer_pak));
    snprintf(gs_cheats_label, sizeof(gs_cheats_label), "Datel Cheats: %s",
        format_boolean_type(menu->load.rom_info.settings.cheats_enabled));
}

/* Where a launch came from — used so a load error returns there, not always Files. */
static menu_mode_t launch_origin_mode (menu_t *menu) {
    if (menu->load.from_grid)             return MENU_MODE_GAMES_GRID;
    if (menu->load.load_favorite_id >= 0) return MENU_MODE_GAMES_GRID;
    if (menu->load.load_history_id >= 0)  return MENU_MODE_HISTORY;
    return MENU_MODE_BROWSER;
}

static void leave_view (menu_t *menu) {
    sound_play_effect(SFX_EXIT);
    menu->load.from_grid = false;
    if (menu->load.load_return_mode != 0) {
        menu->next_mode = menu->load.load_return_mode;
        menu->load.load_return_mode = 0;
    } else if (menu->load.load_favorite_id >= 0) {
        menu->next_mode = MENU_MODE_GAMES_GRID;
    } else if (menu->load.load_history_id >= 0) {
        menu->next_mode = MENU_MODE_HISTORY;
    } else {
        menu->next_mode = MENU_MODE_BROWSER;
    }
}

static void process (menu_t *menu) {
    /* Info overlays are modal: any of these buttons dismisses and reopens the menu. */
    if (show_extra_info_message || show_advanced_info_message) {
        if (menu->actions.back || menu->actions.enter || menu->actions.settings || menu->actions.lz_context) {
            show_extra_info_message = false;
            show_advanced_info_message = false;
            ui_components_context_menu_show(&game_settings_context_menu);
            game_settings_context_menu.row_selected = gs_saved_row;
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    /* A leaf action (setting change) closes the menu — reopen it so this view
       persists, refreshing the value labels. Launch is the one exception. */
    if (game_settings_context_menu.row_selected < 0) {
        if (menu->load_pending.rom_file) {
            return;
        }
        refresh_game_settings_labels(menu);
        ui_components_context_menu_show(&game_settings_context_menu);
        game_settings_context_menu.row_selected = gs_saved_row;
        return;
    }

    /* B at the top level leaves the view; deeper, let it pop submenus normally. */
    if (menu->actions.back && game_settings_context_menu.submenu == NULL) {
        leave_view(menu);
        return;
    }

    /* Remember the top-level row before navigating, for post-action re-show. */
    gs_saved_row = game_settings_context_menu.row_selected;

    ui_components_context_menu_process(menu, &game_settings_context_menu);
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    /* Use the grid backdrop when opened from the grid — and also on the exit frame
       (next_mode set to the grid), so closing doesn't flash a black background. */
    if (menu->load.from_grid || menu->next_mode == MENU_MODE_GAMES_GRID) {
        view_games_grid_draw_background(menu, d);
    } else {
        ui_components_background_draw();
    }

    if (menu->load_pending.rom_file) {
        draw_load_content(0.0f);
        rdpq_detach_show();
        return;
    }

    ui_components_context_menu_draw(&game_settings_context_menu);

    if (show_extra_info_message) {
        /* Format into a buffer, then neutralise rdpq inline-escape introducers ('$'=font,
           '^'=style) — a ROM title / author / website / license carrying one crashes
           rdpq_paragraph_build ("invalid font id"). Drawn as a literal so no field re-escapes. */
        char info[1024];
        snprintf(info, sizeof info,
            "EXTRA ROM INFO\n"
            "\n"
            "Title: %.20s\n"
            "Age Rating: %s\n"
            "Release Date: %s\n"
            "Author: %s\n"
            "Website: %s\n"
            "License: %s\n"
            "Game code: %c%c%c%c\n"
            "Media type: %s\n"
            "Variant: %s\n"
            "Version: %hhu\n"
            "CIC: %s\n\n\n"
            "Press B to return.\n",
            menu->load.rom_info.title,
            format_age_rating(menu->load.rom_info.meta.age_rating),
            menu->load.rom_info.meta.release_date,
            menu->load.rom_info.meta.author,
            menu->load.rom_info.meta.website,
            menu->load.rom_info.meta.osi_license,
            menu->load.rom_info.game_code[0], menu->load.rom_info.game_code[1], menu->load.rom_info.game_code[2], menu->load.rom_info.game_code[3],
            format_rom_media_type(menu->load.rom_info.category_code),
            format_rom_destination_market(menu->load.rom_info.destination_code),
            menu->load.rom_info.version,
            format_cic_type(rom_info_get_cic_type(&menu->load.rom_info))
        );
        for (char *p = info; *p; p++) if (*p == '$' || *p == '^') *p = ' ';
        ui_components_messagebox_draw("%s", info);
    }

    if (show_advanced_info_message) {
        ui_components_messagebox_draw(
            "ADVANCED ROM INFO\n"
            "\n"
            "Boot address: 0x%08lX\n"
            "SDK version: %.1f%c\n"
            "Clock Rate: %.2fMHz\n"
            "Check code: 0x%016llX\n"
            "Endianness: %s\n\n\n"
            "Press B to return.\n",
            menu->load.rom_info.boot_address,
            (menu->load.rom_info.libultra.version / 10.0f), menu->load.rom_info.libultra.revision,
            menu->load.rom_info.clock_rate,
            menu->load.rom_info.check_code,
            format_rom_endianness(menu->load.rom_info.endianness)
        );
    }

    rdpq_detach_show();
}

static void draw_progress (float progress) {
    surface_t *d = (progress >= 1.0f) ? display_get() : display_try_get();

    if (d) {
        rdpq_attach(d, NULL);
        if (load_menu_ptr && load_menu_ptr->load.from_grid) {
            view_games_grid_draw_background(load_menu_ptr, d);
        } else {
            ui_components_background_draw();
        }
        draw_load_content(progress);
        rdpq_detach_show();
    }
}

static void load (menu_t *menu) {
    debugf("Load ROM: load function called\n");
    cart_load_err_t err;
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (!menu->settings.loading_progress_bar_enabled) {
        err = cart_load_n64_rom_and_save(menu, NULL);
    } else  {
        err = cart_load_n64_rom_and_save(menu, draw_progress);
    }
#else
    err = cart_load_n64_rom_and_save(menu, draw_progress);
#endif

    if (err != CART_LOAD_OK) {
        /* Return to wherever the launch came from (grid/history), not always Files. */
        menu->error_return_mode = launch_origin_mode(menu);
        menu_show_error(menu, cart_load_convert_error_message(err));
        return;
    }

    bookkeeping_history_add(&menu->bookkeeping, menu->load.rom_path, NULL, BOOKKEEPING_TYPE_ROM);

    menu->next_mode = MENU_MODE_BOOT;

    menu->boot_params->device_type = BOOT_DEVICE_TYPE_ROM;
    menu->boot_params->detect_cic_seed = rom_info_get_cic_seed(&menu->load.rom_info, &menu->boot_params->cic_seed);
    switch (rom_info_get_tv_type(&menu->load.rom_info)) {
        case ROM_TV_TYPE_PAL: menu->boot_params->tv_type = BOOT_TV_TYPE_PAL; break;
        case ROM_TV_TYPE_NTSC: menu->boot_params->tv_type = BOOT_TV_TYPE_NTSC; break;
        case ROM_TV_TYPE_MPAL: menu->boot_params->tv_type = BOOT_TV_TYPE_MPAL; break;
        default: menu->boot_params->tv_type = BOOT_TV_TYPE_PASSTHROUGH; break;
    }

    // Handle cheat codes only if Expansion Pak is present and cheats are enabled
    if (is_memory_expanded() && menu->load.rom_info.settings.cheats_enabled) {
        uint32_t tmp_cheats[MAX_CHEAT_CODE_ARRAYLIST_SIZE];
        size_t cheat_item_count = generate_enabled_cheats_array(get_cheat_codes(), tmp_cheats);

        if (cheat_item_count > 2) { // account for at least one valid cheat code (address and value), excluding the last two 0s
            // Allocate memory for the cheats array
            uint32_t *cheats = malloc(cheat_item_count * sizeof(uint32_t));
            if (cheats) {
                memcpy(cheats, tmp_cheats, cheat_item_count * sizeof(uint32_t));
                for (size_t i = 0; i + 1 < cheat_item_count; i += 2) {
                    debugf("Cheat %u: Address: 0x%08lX, Value: 0x%08lX\n", i / 2, cheats[i], cheats[i + 1]);
                }
                debugf("Cheats enabled, %u cheats found\n", cheat_item_count / 2);
                menu->boot_params->cheat_list = cheats;
            } else {
                debugf("Failed to allocate memory for cheat list\n");
                menu->boot_params->cheat_list = NULL;
            }
        } else {
            debugf("Cheats enabled, but no cheats found\n");
            menu->boot_params->cheat_list = NULL;
        }
    } else {
        debugf("Cheats disabled or Expansion Pak not present\n");
        menu->boot_params->cheat_list = NULL;
    }
}

static void draw_load_content (float progress) {
    /* When launched from the grid, show a compact rainbow popup box;
       otherwise use the full-screen layout. */
    bool compact = load_menu_ptr && load_menu_ptr->load.from_grid;

    int img_w = compact ? 120 : 160;
    int img_h = compact ? 90  : 120;
    int bar_w = img_w;
    int bar_h = compact ? 14  : 18;
    int gap   = 24;   /* breathing room between the cover and the progress bar */
    int block_h = img_h + gap + bar_h;

    int img_x0 = DISPLAY_CENTER_X - img_w / 2;
    int img_y0 = DISPLAY_CENTER_Y - block_h / 2;
    int img_x1 = img_x0 + img_w;
    int img_y1 = img_y0 + img_h;

    int bar_x0 = DISPLAY_CENTER_X - bar_w / 2;
    int bar_y0 = img_y1 + gap;
    int bar_x1 = bar_x0 + bar_w;
    int bar_y1 = bar_y0 + bar_h;

    if (compact) {
        /* Rainbow popup box — drawn before the content so cart+bar sit on top */
        int box_w = bar_w + MESSAGEBOX_MARGIN;
        int box_h = block_h + MESSAGEBOX_MARGIN;
        ui_components_dialog_draw(box_w, box_h);
    }

    /* Image area: blit the real boxart when we have a sane surface; otherwise show
       the cart placeholder (same as the grid/inspect). The dimension guard rejects a
       corrupt surface that would otherwise assert in TMEM ("rectangle too big").
       No opaque backdrop is drawn, so transparent art (cart/logo) shows the screen/
       dialog behind it rather than a grey block. */
    surface_t *img = (load_boxart && !load_boxart->loading) ? load_boxart->image : NULL;
    bool img_ok = img && img->width > 0 && img->height > 0
                      && img->width <= 1024 && img->height <= 1024;
    if (img_ok) {
        float sx = (float)img_w / img->width;
        float sy = (float)img_h / img->height;
        float scale = sx < sy ? sx : sy;
        int dw = (int)(img->width  * scale);
        int dh = (int)(img->height * scale);
        rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        /* Alpha-BLEND (not alpha-compare): partial-alpha (anti-aliased) logo/art edges rendered
           opaque under alphacompare(1) -> coloured halo. Blending composites them smoothly;
           fully transparent texels still contribute nothing (no green). Matches the placeholder. */
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_tex_blit(img,
            img_x0 + (img_w - dw) / 2,
            img_y0 + (img_h - dh) / 2,
            &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
        rdpq_mode_pop();
    } else {
        ui_components_cart_placeholder_draw(img_x0, img_y0, img_x1, img_y1);
    }

    /* Progress bar: dark background then rainbow fill */
    rdpq_mode_push();
    rdpq_set_mode_fill(RGBA32(0x08, 0x08, 0x08, 0xFF));
    rdpq_fill_rectangle(bar_x0, bar_y0, bar_x1, bar_y1);
    rdpq_mode_pop();

    int W = bar_w;
    int filled_px = (int)(progress * W);
    int N = W / 4;
    if (N > 96) N = 96;
    if (N < 1)  N = 1;
    for (int i = 0; i < N; i++) {
        int sx0 = bar_x0 + W * i / N;
        int sx1 = bar_x0 + W * (i + 1) / N;
        int sc  = (sx0 + sx1) / 2 - bar_x0;
        uint8_t hue = (uint8_t)(255 * i / N);
        uint8_t bri = (sc < filled_px) ? 220 : 28;
        color_t c = ui_components_rainbow_color(hue, bri);
        rdpq_mode_push();
        rdpq_set_mode_fill(c);
        rdpq_fill_rectangle(sx0, bar_y0, sx1, bar_y1);
        rdpq_mode_pop();
    }

    /* Small rim around the bar, matching the folder-fav / favorite-import progress bars. */
    ui_components_border_draw(bar_x0 - 1, bar_y0 - 1, bar_x1 + 1, bar_y1 + 1);
}

static void deinit (void) {
    ui_components_boxart_free(load_boxart);
    load_boxart = NULL;
}


void view_load_rom_init (menu_t *menu) {
    load_menu_ptr = menu;

    /* Resolve the ROM path from whichever source the caller set. History / favorite / browser
       launches each replace any previous rom_path. A caller that pre-set rom_path with NONE of
       those sources (the ROM-boot countdown) keeps its path unchanged -- the old code's
       unconditional `browser.entry->name` deref would crash in that case. */
    if (menu->load.load_history_id != -1) {
        if (menu->load.rom_path) { rom_info_free_meta(&menu->load.rom_info); path_free(menu->load.rom_path); }
        menu->load.rom_path = path_clone(menu->bookkeeping.history_items[menu->load.load_history_id].primary_path);
    } else if (menu->load.load_favorite_id != -1) {
        if (menu->load.rom_path) { rom_info_free_meta(&menu->load.rom_info); path_free(menu->load.rom_path); }
        menu->load.rom_path = path_clone(menu->bookkeeping.favorite_items[menu->load.load_favorite_id].primary_path);
    } else if (menu->browser.entry) {
        if (menu->load.rom_path) { rom_info_free_meta(&menu->load.rom_info); path_free(menu->load.rom_path); }
        menu->load.rom_path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    }
    /* else: rom_path was pre-set by the caller (ROM-boot preview) -- use it as-is. */

    rom_filename = path_last_get(menu->load.rom_path);

    if (show_extra_info_message) {
        show_extra_info_message = false;
    }
    if (show_advanced_info_message) {
        show_advanced_info_message = false;
    }

    /* Start boxart load for the load screen placeholder */
    ui_components_boxart_free(load_boxart);
    load_boxart = NULL;

    debugf("Load ROM: loading ROM info from %s\n", path_get(menu->load.rom_path));
    rom_err_t err = rom_config_load(menu->load.rom_path, &menu->load.rom_info);
    if (err != ROM_OK) {
        rom_info_free_meta(&menu->load.rom_info);
        path_free(menu->load.rom_path);
        menu->load.rom_path = NULL;
        /* Capture the origin before clearing the favorite/history ids below. */
        menu->error_return_mode = launch_origin_mode(menu);
        //disable the attempt at loading the favorite / history
        menu->load.load_history_id = -1;
        menu->load.load_favorite_id = -1;
        // FIXME: use bookkeeping_favorite_remove() here instead of just showing an error and leaving the broken favorite / history item in place
        menu_show_error(menu, convert_error_message(err));
        return;
    }
    {
        char gc[5];
        memcpy(gc, menu->load.rom_info.game_code, 4);
        gc[4] = '\0';
        int lv_ovr = rom_config_get_image_view(menu->load.rom_path, 2);
        int lv = (lv_ovr >= 0 && lv_ovr < GRID_IMAGE_COUNT) ? lv_ovr : menu->settings.image_view_load;
        file_image_type_t img_type = ui_components_boxart_view_to_type(lv);
        load_boxart = ui_components_boxart_init(
            menu->storage_prefix, gc,
            menu->load.rom_info.title,
            path_get(menu->load.rom_path),
            img_type, menu->settings.use_custom_files
        );
    }

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (!menu->settings.rom_autoload_enabled) {
#endif
        ui_components_context_menu_init(&options_context_menu);
        refresh_game_settings_labels(menu);
        ui_components_context_menu_init(&game_settings_context_menu);
        /* Don't open the menu when we're launching straight through. */
        if (!menu->load_pending.launch_rom) {
            ui_components_context_menu_show(&game_settings_context_menu);
            gs_saved_row = 0;
        }
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    }
#endif

}

void view_load_rom_display (menu_t *menu, surface_t *display) {
    /* "Launch Game" shortcut: boot immediately once ROM info has loaded, skipping
       the detail view entirely. Guards on rom_path so a failed init can't load NULL. */
    if (menu->load_pending.launch_rom) {
        menu->load_pending.launch_rom = false;
        if (menu->load.rom_path) {
            menu->load_pending.rom_file = true;
        }
    }

    process(menu);

    draw(menu, display);

    if (menu->load_pending.rom_file) {
        menu->load_pending.rom_file = false;
        load(menu);
    }

    if (menu->next_mode != MENU_MODE_LOAD_ROM && menu->next_mode != MENU_MODE_DATEL_CODE_EDITOR) {
        menu->load.load_history_id = -1;
        menu->load.load_favorite_id = -1;
        deinit();
    }
}
